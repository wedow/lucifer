/* Calls and returns (eval.h). A call whose target is a Python function
   the state knows goes on in the callee: its parameters are bound from
   the call's operands as values (a rest tuple is a tuple that does not
   exist), its frame is one more frame of the state, and its returns join
   after the call in the caller. Nothing is pushed and nothing is undone:
   the callee was checked before (lu_ev_supported), and what cannot be
   compiled inside it is an exit that makes the frames. A primitive of
   the kernel is answered from the state where its transfer function can
   (eval_prims.c). Any other call is the interpreter's, made by a helper. */
#include "jit/eval.h"
#include "interp/gen.h"

#include <string.h>

#define MAX_ARGS 30

/* The primitive a callable is, by name, or NULL. */
static const char *
primitive_name(PyObject *o)
{
    if (o == NULL || !PyCFunction_Check(o) || lu_kernel_def_of(((PyCFunctionObject *)o)->m_ml) != NULL) {
        return NULL;
    }
    PyObject *m = ((PyCFunctionObject *)o)->m_self;
    if (m == NULL || !PyModule_Check(m)) {
        return NULL;
    }
    const char *mod = PyModule_GetName(m);
    if (mod == NULL) {
        PyErr_Clear();
        return NULL;
    }
    return strcmp(mod, "_lucifer") == 0 ? ((PyCFunctionObject *)o)->m_ml->ml_name : NULL;
}

/* The Python function a constant callable runs, and the self it binds
   (NULL: none), or NULL. */
static PyFunctionObject *
target_of(PyObject *o, PyObject **bound)
{
    *bound = NULL;
    if (o == NULL) {
        return NULL;
    }
    if (PyFunction_Check(o)) {
        return (PyFunctionObject *)o;
    }
    if (PyMethod_Check(o) && PyFunction_Check(((PyMethodObject *)o)->im_func)) {
        *bound = ((PyMethodObject *)o)->im_self;
        return (PyFunctionObject *)((PyMethodObject *)o)->im_func;
    }
    PyObject *fn = lu_kernel_function_of(o, bound);
    return fn != NULL && PyFunction_Check(fn) ? (PyFunctionObject *)fn : NULL;
}

/* A reference of compiled code's own to the object in a's word at `offset`
   (a method's self): the method may go before the call it was read for
   ends. */
static lu_v
held_field(struct ctx *c, lu_v a, size_t offset)
{
    lu_v v = lu_ev_emit(c, OP_LOAD, REP_OBJ, a, LU_NOV, LU_NOV, (int64_t)offset);
    lu_ev_emit(c, OP_INCREF, REP_NONE, v, LU_NOV, LU_NOV, 0);
    return lu_ev_own(c, v);
}

/* A function that stands, in all they share, for every function of the
   code and globals: what a call of one of them is built from, while the
   function called goes with its frame as a value (struct ctx funcv) and
   gives its own cells. It has no defaults, so a call that needs one is
   not bound from it. The value is guarded to be such a function; one the
   unit made itself (`made`) is one unless its code was changed since.
   The unit keeps it; NULL when it cannot be made. */
static PyObject *
function_of_code(struct ctx *c, lu_v callable, PyObject *code, PyObject *globals, int made)
{
    PyObject *one = lu_function_new(code, globals);
    if (one == NULL) {
        PyErr_Clear();
        return NULL;
    }
    static const size_t shared[] = { offsetof(PyFunctionObject, func_code), offsetof(PyFunctionObject, func_globals),
                                     offsetof(PyFunctionObject, func_builtins) };
    PyObject *same[] = { code, globals, ((PyFunctionObject *)one)->func_builtins };
    if (!made) {
        lu_ev_guard_type(c, callable, &PyFunction_Type);
    }
    for (int i = 0; i < (made ? 1 : 3); i++) {
        lu_v g = lu_ev_guard(c, OP_GUARD_IS, callable, (int64_t)(intptr_t)same[i]);
        I(c, g)->imm2 = GI_FIELD | (int64_t)shared[i];
        lu_ev_konst(c, same[i]);
    }
    lu_ev_konst(c, one);
    Py_DECREF(one);             /* the unit holds its constants */
    return one;
}

/* A callable that is not a constant, at a site that met one function
   only (the site's callee table, jit.h): guarded to be it, and a constant
   from here. The function is met by itself, with a self beside it where
   the stack holds one; as a bound method object, a Python function's or a
   builtin method's of a kernel type (f = d.append), whose self is then
   read from the object into *self. NULL when the site says no such
   thing. */
static PyObject *
speculate_callee(struct ctx *c, lu_v callable, lu_v *self, lu_v *funcv)
{
    if (callable == LU_NOV || LU_IS_VIRT(callable) || nospec(c, c->pc)) {
        return NULL;
    }
    struct lu_callees *t = lu_jit_callees(c->code, c->pc);
    if (t == NULL || __atomic_load_n(&t->mega, __ATOMIC_ACQUIRE) || __atomic_load_n(&t->n, __ATOMIC_ACQUIRE) != 1) {
        return NULL;
    }
    struct lu_callee e = t->e[0];
    if (e.kind & LU_CALLEE_CODE) {
        /* Functions of one code and globals, each made anew (a closure, a
           lambda): the value called is guarded to be one of them. */
        if (!PyCode_Check(e.obj) || e.globals == NULL || !PyDict_CheckExact(e.globals)
            || ((e.kind & LU_CALLEE_BOUND) != 0) != (*self != LU_NOV)) {
            return NULL;
        }
        PyObject *one = function_of_code(c, callable, e.obj, e.globals, 0);
        *funcv = one != NULL ? callable : LU_NOV;
        return one;
    }
    if (!PyFunction_Check(e.obj)) {
        return NULL;
    }
    if (e.kind & LU_CALLEE_CMETH) {
        /* A builtin method bound to its self: the kernel function its
           definition names, called with that self. */
        if (*self != LU_NOV) {
            return NULL;
        }
        lu_ev_guard_type(c, callable, &PyCFunction_Type);
        lu_v g = lu_ev_guard(c, OP_GUARD_IS, callable, (int64_t)(intptr_t)e.globals);
        I(c, g)->imm2 = GI_FIELD | (int64_t)offsetof(PyCFunctionObject, m_ml);
        *self = held_field(c, callable, offsetof(PyCFunctionObject, m_self));
    }
    else if ((e.kind & LU_CALLEE_BOUND) && *self == LU_NOV) {
        /* A bound method object: its function, called with its self. */
        lu_ev_guard_type(c, callable, &PyMethod_Type);
        lu_v g = lu_ev_guard(c, OP_GUARD_IS, callable, (int64_t)(intptr_t)e.obj);
        I(c, g)->imm2 = GI_FIELD | (int64_t)offsetof(PyMethodObject, im_func);
        *self = held_field(c, callable, offsetof(PyMethodObject, im_self));
    }
    else if (((e.kind & LU_CALLEE_BOUND) != 0) == (*self != LU_NOV)) {
        /* Named by itself, and passed a self only where the stack holds one:
           then the value called is the function. */
        lu_ev_guard(c, OP_GUARD_IS, callable, (int64_t)(intptr_t)e.obj);
    }
    else {
        return NULL;
    }
    lu_ev_konst(c, e.obj);      /* the unit keeps what it compares with and calls */
    return e.obj;
}

static void
note_key(uint64_t **keys, int *n, int *cap, uint64_t key)
{
    if (*n == *cap) {
        *cap = *cap ? 2 * *cap : 16;
        *keys = realloc(*keys, sizeof(uint64_t) * (size_t)*cap);
    }
    (*keys)[(*n)++] = key;
}

int
lu_ev_room(struct ctx *c, int again)
{
    struct lu_func *f = c->g->f;
    c->g->hot_room = 0;
    /* An inlined frame's entry and return each cost an instruction once
       (the recursion count, kept now only across calls): the budget, tuned
       with them, still charges them. */
    if (f->nins + 2 * f->ninlined <= c->g->budget) {
        return 1;
    }
    uint64_t key = lu_jit_site_key(c->code, c->pc, c->parent != NULL ? c->parent->code : NULL,
                                   c->parent != NULL ? c->parent->next : 0);
    /* A hot site goes in line past the budget once: the build's other
       paths to it, and copies of the code around it, keep to the budget. */
    int taken = 0;
    for (int i = 0; i < f->nhots && !taken; i++) {
        taken = f->hots[i] == key;
    }
    if (!taken && again == 0 && c->g->budget >= 0 && lu_jit_hot_site(f->code, key)) {
        c->g->hot_room = key;
        return 1;
    }
    f->refused++;
    note_key(&f->outs, &f->nouts, &f->capouts, key);
    return 0;
}

int
lu_ev_room_begin(struct ctx *c)
{
    struct gctx *g = c->g;
    uint64_t key = g->hot_room;
    g->hot_room = 0;
    if (key == 0) {
        return 0;
    }
    note_key(&g->f->hots, &g->f->nhots, &g->f->caphots, key);
    int room = g->f->nins + 2 * g->f->ninlined + g->f->budget - g->budget;
    g->budget += room;
    return room;
}

void
lu_ev_room_end(struct ctx *c, int room)
{
    c->g->budget -= room;
}

/* Why fn cannot be evaluated in line for a call here, or NULL. */
static const char *
not_inlinable(struct ctx *c, PyFunctionObject *fn, lu_v funcv)
{
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    int kernel = (co->co_flags & LU_CO_KERNEL) != 0;
    if (!PyCode_Check(co) || !(co->co_flags & CO_OPTIMIZED)
        || ((co->co_flags & (LU_CO_GENERATORS | CO_ITERABLE_COROUTINE)) && !lu_ev_gen_takes(co))) {
        return "not plain code";
    }
    if (co->_co_monitoring != NULL || (!kernel && funcv == LU_NOV && fn->func_version < 2)) {
        return "watched, or unversioned";
    }
    if (lu_jit_noinline(co)) {
        return "its inlined copies kept leaving";
    }
    if (c->depth >= 24) {
        return "too deep";
    }
    /* A function already being evaluated goes in line once more: a
       dispatcher reached through what it dispatched to is a different call. */
    int again = 0;
    for (struct ctx *k = c; k != NULL; k = k->parent) {
        again += k->code == co;
    }
    if (again > 1) {
        return "recursive";
    }
    if (!lu_ev_room(c, again)) {
        return "over budget";
    }
    if (!lu_ev_supported(c->g, co)) {
        return "an instruction the evaluator lacks";
    }
    return NULL;
}

/* Whether a parameter of co takes the keyword `name`. */
static int
takes(PyCodeObject *co, PyObject *name)
{
    PyObject **names = &PyTuple_GET_ITEM(co->co_localsplusnames, 0);
    for (int p = co->co_posonlyargcount; p < co->co_argcount + co->co_kwonlyargcount; p++) {
        if (names[p] == name || (PyUnicode_CheckExact(name) && PyUnicode_Compare(names[p], name) == 0)) {
            return 1;
        }
    }
    return 0;
}

/* fn's parameters bound to the call's operands, as initialize_locals
   binds them, into `locals` (the callee's slots, unset): args[0..npos)
   positional, then one value per name of kwnames (NULL: none). 0 where
   the binding would raise or needs what the state cannot hold (keywords
   for **kwargs), and the call is made out of line. `dry` only answers
   that, emitting nothing. */
static int
bind(struct ctx *c, PyFunctionObject *fn, const lu_v *args, int npos, PyObject *kwnames, lu_v *locals, int dry)
{
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    int argc = co->co_argcount, total = argc + co->co_kwonlyargcount;
    int star = (co->co_flags & CO_VARARGS) != 0, nkw = kwnames != NULL ? (int)PyTuple_GET_SIZE(kwnames) : 0;
    if (npos > argc && !star) {
        return 0;
    }
    for (int i = 0; i < npos && i < argc; i++) {
        locals[i] = dry ? 0 : args[i];
    }
    PyObject **names = &PyTuple_GET_ITEM(co->co_localsplusnames, 0);
    for (int k = 0; k < nkw; k++) {
        PyObject *name = PyTuple_GET_ITEM(kwnames, k);
        int j = total;
        for (int p = co->co_posonlyargcount; p < total && j == total; p++) {
            if (names[p] == name || (PyUnicode_CheckExact(name) && PyUnicode_Compare(names[p], name) == 0)) {
                j = p;
            }
        }
        if (j == total && (co->co_flags & CO_VARKEYWORDS) && PyUnicode_CheckExact(name)) {
            continue;           /* one for **kwargs, below */
        }
        if (j == total || locals[j] != LU_NOV) {
            return 0;           /* a name it does not take, or given twice */
        }
        locals[j] = dry ? 0 : args[npos + k];
    }
    PyObject *defaults = fn->func_defaults, *kwdefaults = fn->func_kwdefaults;
    int first = argc - (defaults != NULL ? (int)PyTuple_GET_SIZE(defaults) : 0);
    for (int i = 0; i < argc; i++) {
        if (locals[i] == LU_NOV) {
            if (i < first) {
                return 0;
            }
            locals[i] = dry ? 0 : lu_ev_konst(c, PyTuple_GET_ITEM(defaults, i - first));
        }
    }
    for (int j = argc; j < total; j++) {
        if (locals[j] == LU_NOV) {
            PyObject *d = kwdefaults != NULL && PyDict_CheckExact(kwdefaults) ? lu_ns_get(kwdefaults, names[j]) : NULL;
            if (d == NULL) {
                PyErr_Clear();
                return 0;
            }
            locals[j] = dry ? 0 : lu_ev_konst(c, d);
            Py_DECREF(d);       /* the unit holds its constants */
        }
    }
    if (dry) {
        return 1;
    }
    if (star && npos > argc) {
        struct lu_vobj *o;
        locals[total] = lu_ev_virt_new(c, VK_TUPLE, &PyTuple_Type, npos - argc, &o);
        for (int i = argc; i < npos; i++) {
            o->f[i - argc] = args[i];
        }
    }
    else if (star) {
        locals[total] = lu_ev_konst(c, (PyObject *)&_PyRuntime.singletons.empty_tuple);
    }
    if (co->co_flags & CO_VARKEYWORDS) {
        /* The keywords no parameter took, in the call's order: a dict
           that does not exist. */
        int extra = 0;
        for (int k = 0; k < nkw; k++) {
            extra += !takes(co, PyTuple_GET_ITEM(kwnames, k));
        }
        struct lu_vobj *o;
        locals[total + star] = lu_ev_virt_new(c, VK_DICT, &PyDict_Type, 2 * extra, &o);
        for (int k = 0, at = 0; k < nkw; k++) {
            if (!takes(co, PyTuple_GET_ITEM(kwnames, k))) {
                o->f[at++] = lu_ev_konst(c, PyTuple_GET_ITEM(kwnames, k));
                o->f[at++] = args[npos + k];
            }
        }
    }
    return 1;
}

/* Whether a call of class tp with nargs positional arguments is one
   type.__call__ makes by object.__new__ and then a Python __init__
   (*init), or object's own given none (*init NULL), for an instance of an
   ordinary layout: as of tp's version *version, which the call guards,
   and its metaclass's (*meta: 0 for type itself), a class of type's whose
   __call__ is type's. */
static int
class_init(struct ctx *c, PyTypeObject *tp, int nargs, PyFunctionObject **init, uint32_t *version, uint32_t *meta)
{
    PyTypeObject *mt = Py_TYPE(tp);
    *meta = 0;
    if (mt != &PyType_Type) {
        *meta = lu_type_assign_version(mt) ? __atomic_load_n(&mt->tp_version_tag, __ATOMIC_ACQUIRE) : 0;
        if (*meta == 0 || mt->tp_call != PyType_Type.tp_call
            || _PyType_Lookup(mt, LU_ID(__call__)) != _PyType_Lookup(&PyType_Type, LU_ID(__call__))
            || __atomic_load_n(&mt->tp_version_tag, __ATOMIC_ACQUIRE) != *meta) {
            return 0;
        }
    }
    if (!PyType_IsSubtype(mt, &PyType_Type) || !(tp->tp_flags & Py_TPFLAGS_HEAPTYPE) || !(tp->tp_flags & Py_TPFLAGS_HAVE_GC)
        || (tp->tp_flags & Py_TPFLAGS_IS_ABSTRACT) || tp->tp_alloc != PyType_GenericAlloc || tp->tp_itemsize != 0
        || tp->tp_new == NULL || nospec(c, c->pc) || !lu_type_assign_version(tp)) {
        return 0;
    }
    PyTypeObject *solid = tp;
    while (solid->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        solid = solid->tp_base;
    }
    if (solid != &PyBaseObject_Type) {
        return 0;               /* object.__new__ refuses it */
    }
    *version = __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE);
    PyObject *new = _PyType_Lookup(tp, LU_ID(__new__)), *in = _PyType_Lookup(tp, LU_ID(__init__));
    if (new != _PyType_Lookup(&PyBaseObject_Type, LU_ID(__new__)) || in == NULL
        || __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) != *version) {
        return 0;
    }
    *init = Py_IS_TYPE(in, &PyFunction_Type) ? (PyFunctionObject *)in : NULL;
    return *init != NULL || (in == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__init__)) && nargs == 0);
}

/* The call goes on in fn: `nops` operands leave the stack, the callee's
   frame joins the state with its parameters bound, and its returns join
   after the call. 0 when the call is to be made out of line instead
   (nothing was changed); -1 when the build failed. */
static int
inline_call(struct ctx *c, PyFunctionObject *fn, lu_v funcv, lu_v *args, int npos, PyObject *kwnames, int nops,
            PyTypeObject *cls, int ret)
{
    struct gctx *g = c->g;
    if (kwnames == NULL && cls == NULL && (((PyCodeObject *)fn->func_code)->co_flags & LU_CO_KERNEL)) {
        /* The kernel's own version of fn for this many arguments. */
        PyFunctionObject *twin = lu_kernel_twin(fn, npos);
        fn = twin != NULL ? twin : fn;
    }
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    const char *why = not_inlinable(c, fn, funcv);
    if (why != NULL) {
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: %s not inlined at %s@%d: %s\n", PyUnicode_AsUTF8(co->co_qualname),
                    PyUnicode_AsUTF8(c->code->co_qualname), c->pc, why);
        }
        return 0;
    }
    int nslots = co->co_nlocalsplus + co->co_stacksize + 2;
    lu_v *locals = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)nslots);
    for (int i = 0; i < nslots; i++) {
        locals[i] = LU_NOV;
    }
    for (int i = cls != NULL; i < npos + (kwnames != NULL ? (int)PyTuple_GET_SIZE(kwnames) : 0); i++) {
        if (args[i] == LU_NOV) {
            return 0;
        }
    }
    if (!bind(c, fn, args, npos, kwnames, locals, 1)) {
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: %s not inlined at %s@%d: its arguments do not bind\n",
                    PyUnicode_AsUTF8(co->co_qualname), PyUnicode_AsUTF8(c->code->co_qualname), c->pc);
        }
        return 0;
    }
    for (int i = 0; i < nslots; i++) {
        locals[i] = LU_NOV;
    }
    if (cls != NULL) {
        /* A class called: the instance is made as object.__new__ makes
           it, and is __init__'s first argument. */
        args[0] = lu_ev_new_instance(c, cls);
    }
    bind(c, fn, args, npos, kwnames, locals, 0);
    if (!(co->co_flags & LU_CO_KERNEL) && funcv == LU_NOV) {
        /* Its code and defaults are what the unit was built from. */
        lu_v gv = lu_ev_guard(c, OP_GUARD_VERSION, lu_ev_konst(c, (PyObject *)fn), fn->func_version);
        I(c, gv)->imm2 = GV_FUNC;
    }
    lu_ev_konst(c, (PyObject *)fn);     /* the unit keeps the function its frame states name */
    for (int i = 0; i < nops; i++) {
        lu_ev_pop(c);
    }
    if (cls != NULL) {
        lu_ev_push(c, args[0]);         /* the call's value, on the stack while __init__ runs */
    }
    struct ctx *k = lu_ir_alloc(g->f, sizeof(struct ctx));
    k->ret = ret;
    k->g = g;
    k->parent = c;
    k->depth = c->depth + 1;
    k->code = co;
    k->func = fn;
    k->funcv = funcv;
    k->globals = fn->func_globals;
    k->builtins = fn->func_builtins;
    k->kernel = (co->co_flags & LU_CO_KERNEL) != 0;
    k->ncode = (int)Py_SIZE(co);
    k->nlocals = co->co_nlocalsplus;
    k->caller_fs = lu_ev_snapshot(c, c->next);
    lu_ev_state_push(g, k->nlocals, nslots);
    memcpy(slots(k), locals, sizeof(lu_v) * (size_t)k->nlocals);
    if (k->depth > g->maxdepth) {
        g->maxdepth = k->depth;
    }
    g->f->ninlined++;
    /* The call's own releases (a callable nothing names now) come before
       the callee's first instruction, where its frame is whole. */
    k->pc = 0;
    lu_ev_settle(k, 0);
    /* A hot site's callee has room for its own calls: what its own unit
       would take in line comes with it. */
    int room = lu_ev_room_begin(c);
    int built = lu_ev_build(k, 0);
    lu_ev_room_end(c, room);
    if (built < 0) {
        return -1;
    }
    if (g->restart) {
        g->cur = -1;
        return 1;
    }
    if (k->nrets == 0) {
        g->cur = -1;            /* every path of the callee left */
        return 1;
    }
    lu_ev_merge(c, k->rets, k->nrets, c->next);
    g->ret_missing = k->missing;
    return 1;
}

/* Whether frame c's caller returns what c returns: its call is followed
   by RETURN_VALUE, in a frame that is itself in line. Each of c's returns
   then goes on through the caller's, one by one, instead of joining the
   others first: what one of them knows of its value (a constant that
   ends a loop, a tuple its consumer unpacks) is still known where the
   caller's caller takes it. */
static int
returns_through(struct ctx *c)
{
    struct ctx *p = c->parent;
    int base, oparg, op;
    if (c->ret != LU_RETURN_PUSH || p->parent == NULL || p->next >= p->ncode) {
        return 0;
    }
    lu_ev_decode(p, p->next, &base, &oparg, &op);
    return base == RETURN_VALUE;
}

/* The RETURN_VALUE after p's current instruction, a call, evaluated on
   the path one of the callee's returns came by. The call is p's
   instruction again after it: the callee's other paths are still to be
   built. */
static int
return_through(struct ctx *p)
{
    struct ctx call = *p;
    struct undo *undo = lu_ir_alloc(p->g->f, sizeof(struct undo) * (size_t)(p->nundo ? p->nundo : 1));
    memcpy(undo, p->undo, sizeof(struct undo) * (size_t)p->nundo);
    p->pc = call.next;
    p->next = lu_ev_decode(p, p->pc, &p->base, &p->oparg, &p->opcode);
    p->cache = lu_code_start(p->code) + p->next - lu_opcode_caches[p->base];
    lu_ev_begin(p, p->pc);
    int r = lu_ev_return(p);
    p->pc = call.pc;
    p->next = call.next;
    p->base = call.base;
    p->oparg = call.oparg;
    p->opcode = call.opcode;
    p->cache = call.cache;
    p->snap = call.snap;
    p->snap_pc = call.snap_pc;
    p->snap_sp = call.snap_sp;
    p->snap_live = call.snap_live;
    p->nundo = call.nundo;
    memcpy(p->undo, undo, sizeof(struct undo) * (size_t)call.nundo);
    return r;
}

int
lu_ev_return(struct ctx *c)
{
    struct gctx *g = c->g;
    lu_v v = lu_ev_peek(c, 1);
    if (c->parent == NULL) {
        /* A frame object another holds takes the locals when the frame
           stops: the interpreter returns from such a frame. */
        lu_ev_guard(c, OP_GUARD_FRAMES, LU_NOV, 1);
        lu_v x = lu_ev_val(c, v);
        if (I(c, x)->flags & INS_OWNED) {
            g->keep = x;        /* its reference is the caller's now */
        }
        else {
            lu_ev_retain(c, x);
        }
        /* GH-99729: a finalizer the releases run sees the caller on top. */
        lu_ev_emit(c, OP_UNLINK, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        while (*depth_of(c) > 0) {
            lu_ev_pop(c);
        }
        for (int i = 0; i < c->nlocals; i++) {
            lu_ev_set_local(c, i, LU_NOV);
        }
        g->leaving = 1;
        lu_ev_settle(c, c->pc);
        g->leaving = 0;
        g->keep = LU_NOV;
        lu_ev_emit(c, OP_RETURN, REP_NONE, x, LU_NOV, LU_NOV, 0);
        g->cur = -1;
        return 0;
    }
    /* The frame is gone before its references are; the value is the
       caller's stack entry, as whatever it is (a tuple returned to a
       caller that unpacks it never exists). */
    if (lu_ev_in_gen(c)) {
        /* A generator resumed in line returns: it is finished, and its
           consumer's loop ends with no value. */
        lu_ev_gen_finish(c);
        lu_ev_pop(c);
        v = lu_ev_konst(c, LU_MISSING);
        lu_ev_push(c, v);
    }
    if (c->ret == LU_RETURN_INIT && lu_ev_const(c, v) != Py_None) {
        /* An __init__ that returns anything but None: the interpreter raises. */
        lu_ev_guard(c, OP_GUARD_BIT, lu_ev_emit(c, OP_IS, REP_BIT, lu_ev_val(c, v), lu_ev_konst(c, Py_None), LU_NOV, 0), 1);
    }
    for (int i = 0; i < c->nlocals + *depth_of(c); i++) {
        lu_ev_drop(c, slots(c)[i]);
    }
    lu_ev_drop(c, c->funcv);
    lu_ev_state_pop(g);
    return lu_ev_returned(c, v);
}

int
lu_ev_returned(struct ctx *c, lu_v v)
{
    struct gctx *g = c->g;
    struct ctx *p = c->parent;
    if (c->ret == LU_RETURN_FOR_ITER) {
        if (lu_ev_const(c, v) == LU_MISSING) {
            /* The iterator is exhausted on this path: the caller's loop ends. */
            int exit_pc = lu_ev_jump_target(FOR_ITER, p->oparg, p->next);
            lu_ev_settle(p, exit_pc);
            if (g->cur >= 0) {
                lu_ev_jump(p, exit_pc);
            }
            return 0;
        }
        PyTypeObject *t = lu_ev_type(c, v);
        c->missing |= !LU_IS_VIRT(v) && lu_ev_const(c, v) == NULL && (t == NULL || t == Py_TYPE(LU_MISSING));
    }
    if (c->ret != LU_RETURN_INIT) {
        lu_ev_push(p, v);
    }
    lu_ev_settle(p, p->next);
    if (g->cur < 0) {
        return 0;
    }
    if (returns_through(c)) {
        return return_through(p);
    }
    if (c->nrets == c->caprets) {
        int cap = c->caprets ? 2 * c->caprets : 4;
        struct edge *e = lu_ir_alloc(g->f, sizeof(struct edge) * (size_t)cap);
        memcpy(e, c->rets, sizeof(struct edge) * (size_t)c->nrets);
        c->rets = e;
        c->caprets = cap;
    }
    c->rets[c->nrets].block = g->cur;
    lu_ev_state_copy(g, &c->rets[c->nrets].st, &g->st);
    c->nrets++;
    g->cur = -1;
    return 0;
}

/* A call of `callable` (the self beside it, or LU_NOV) with n positional
   arguments in a[], then one value per name of kwnames (NULL: none). The
   instruction's `nops` operands are on the stack still and are popped
   here; the result is pushed. The call is answered from the state,
   continued in its callee, or made as the interpreter makes it. */
static int
call_values(struct ctx *c, lu_v callable, lu_v self, lu_v *a, int n, PyObject *kwnames, int nops, int site)
{
    int nkw = kwnames != NULL ? (int)PyTuple_GET_SIZE(kwnames) : 0;
    lu_v made;
    struct lu_vobj *mo = LU_IS_VIRT(callable) && self == LU_NOV ? lu_ev_virt(c->g, &c->g->st, callable, &made) : NULL;
    if (mo != NULL && made == LU_NOV && mo->kind == VK_METHOD) {
        /* A bound method that does not exist: its function, called with its self. */
        callable = mo->f[0];
        self = mo->f[1];
    }
    PyObject *kc = lu_ev_const(c, callable);
    lu_v r = LU_EV_NONE, written = self;        /* the self the instruction has, for a call made as written */
    lu_v funcv = LU_NOV;
    if (kc == NULL && self == LU_NOV && callable >= 0 && I(c, callable)->op == OP_HELPER
        && I(c, callable)->imm == H_make_function && !nospec(c, c->pc)) {
        /* A function the unit made (a lambda passed down to where it is called). */
        PyObject *code = lu_ev_const(c, I(c, callable)->args[0]), *globals = lu_ev_const(c, I(c, callable)->args[1]);
        kc = code != NULL && globals != NULL ? function_of_code(c, callable, code, globals, 1) : NULL;
        funcv = kc != NULL ? callable : LU_NOV;
    }
    if (kc == NULL && site) {
        kc = speculate_callee(c, callable, &self, &funcv);
    }
    const char *prim = primitive_name(kc);
    if (lu_gen_resumes(kc)) {
        lu_ev_side_exit(c, EXIT_SIDE);  /* the interpreter links the generator's frames */
        return 0;
    }
    if (prim != NULL && (((PyCFunctionObject *)kc)->m_ml->ml_flags & LU_METH_FRAME)) {
        prim = NULL;    /* it hands a frame to run: called as C calls it, which runs the frame nested */
    }
    if (kc == (PyObject *)&PyType_Type && self == LU_NOV && n == 1 && nkw == 0 && a[0] != LU_NOV) {
        /* type(x): what is known of x, else the word of its header. */
        PyTypeObject *t = lu_ev_learn_type(c, a[0], 0);
        if (t != NULL) {
            r = lu_ev_konst(c, (PyObject *)t);
        }
        else {
            r = lu_ev_emit(c, OP_LOAD, REP_OBJ, lu_ev_val(c, a[0]), LU_NOV, LU_NOV, offsetof(PyObject, ob_type));
            lu_ev_emit(c, OP_INCREF, REP_NONE, r, LU_NOV, LU_NOV, 0);
            lu_ev_own(c, r);
        }
    }
    else if (prim != NULL && self == LU_NOV && nkw == 0 && n == 1 && strcmp(prim, "seqiter_next") == 0
             && lu_ev_seqiter_next(c, a[0], nops)) {
        return 0;
    }
    else if (prim != NULL && self == LU_NOV && nkw == 0) {
        lu_v args[MAX_ARGS + 2];
        args[0] = callable;
        memcpy(args + 1, a, sizeof(lu_v) * (size_t)n);
        r = lu_ev_prim(c, prim, args + 1, n);
        int flags = ((PyCFunctionObject *)kc)->m_ml->ml_flags;
        int fast = (flags & (METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O | METH_KEYWORDS | METH_METHOD))
            == METH_FASTCALL;
        if (r == LU_EV_NONE && fast) {
            /* A primitive that runs no Python is called as it is; one that
               may is called with the frames observable. */
            r = lu_ev_helper(c, flags & LU_METH_NOPY ? H_prim : H_prim_call, n + 1, args, nops);
            if (lu_ev_prim_type(prim) != NULL) {
                lu_ev_def_type(c, r, lu_ev_prim_type(prim));
            }
        }
    }
    if (r != LU_EV_NONE) {
        for (int i = 0; i < nops; i++) {
            lu_ev_pop(c);
        }
        lu_ev_push(c, r);
        return 0;
    }
    lu_v args[MAX_ARGS + 4];
    PyFunctionObject *init;
    uint32_t version, meta;
    if (kc != NULL && PyType_Check(kc) && self == LU_NOV && nkw == 0
        && class_init(c, (PyTypeObject *)kc, n, &init, &version, &meta)) {
        PyTypeObject *tp = (PyTypeObject *)kc;
        args[0] = LU_NOV;       /* the instance, made once the call is known to inline */
        memcpy(args + 1, a, sizeof(lu_v) * (size_t)n);
        if (init == NULL || not_inlinable(c, init, LU_NOV) == NULL) {
            /* The class and its metaclass are as they were: the same __new__ and __init__. */
            lu_v gv = lu_ev_guard(c, OP_GUARD_VERSION, callable, version);
            I(c, gv)->imm2 = GV_TYPE_ITSELF;
            if (meta != 0) {
                gv = lu_ev_guard(c, OP_GUARD_VERSION, lu_ev_konst(c, (PyObject *)Py_TYPE(tp)), meta);
                I(c, gv)->imm2 = GV_TYPE_ITSELF;
            }
        }
        if (init == NULL) {
            lu_v obj = lu_ev_new_instance(c, tp);
            for (int i = 0; i < nops; i++) {
                lu_ev_pop(c);
            }
            lu_ev_push(c, obj);
            return 0;
        }
        int done = inline_call(c, init, LU_NOV, args, n + 1, NULL, nops, tp, LU_RETURN_INIT);
        if (done != 0) {
            return done < 0 ? -1 : 0;
        }
    }
    PyObject *bound = NULL;
    PyFunctionObject *fn = target_of(kc, &bound);
    if (fn == NULL && kc != NULL && PyType_Check(kc) && Py_IS_TYPE(kc, &PyType_Type) && self == LU_NOV) {
        /* A class called: type.__call__, the kernel's, with the class first. */
        PyObject *unused, *call = _PyType_Lookup(&PyType_Type, LU_ID(__call__));
        PyObject *kfn = call != NULL ? lu_kernel_function_of(call, &unused) : NULL;
        if (kfn != NULL && PyFunction_Check(kfn)) {
            fn = (PyFunctionObject *)kfn;
            self = callable;
        }
    }
    if (fn != NULL && !(bound != NULL && self != LU_NOV)) {
        int k = 0;
        if (bound != NULL) {
            args[k++] = lu_ev_konst(c, bound);
        }
        if (self != LU_NOV) {
            args[k++] = self;
        }
        memcpy(args + k, a, sizeof(lu_v) * (size_t)(n + nkw));
        int done = inline_call(c, fn, funcv, args, k + n, kwnames, nops, NULL, LU_RETURN_PUSH);
        if (done != 0) {
            return done < 0 ? -1 : 0;
        }
    }
    /* As the interpreter makes it: argv is the callable, self or NULL, the
       arguments (then the keyword names). */
    args[0] = callable;
    args[1] = written;
    memcpy(args + 2, a, sizeof(lu_v) * (size_t)(n + nkw));
    if (nkw > 0) {
        args[2 + n + nkw] = lu_ev_konst(c, kwnames);
    }
    r = lu_ev_helper(c, nkw > 0 ? H_call_kw : H_call, 2 + n + nkw + (nkw > 0), args, nops);
    for (int i = 0; i < nops; i++) {
        lu_ev_pop(c);
    }
    lu_ev_push(c, r);
    return 0;
}

int
lu_ev_kernel_op(struct ctx *c, int kid, lu_v *args, int n, int nops, int ret)
{
    PyObject *kf = PyInterpreterState_Get()->kernel[kid];
    int known = kf != NULL && PyFunction_Check(kf);
    for (int i = 0; i < n; i++) {
        PyTypeObject *t = lu_ev_learn_type(c, args[i], i);      /* every one asked: each is learned */
        /* What the interpreter does to a builtin container that other
           threads may change, it does in C under the container's lock:
           the kernel's Python for it is not that, and is left alone. */
        known &= t != NULL && t != &PyList_Type && t != &PyDict_Type && t != &PySet_Type && t != &PyByteArray_Type
            && t != &PyDictIterKey_Type && t != &PyDictIterValue_Type && t != &PyDictIterItem_Type;
    }
    return known ? inline_call(c, (PyFunctionObject *)kf, LU_NOV, args, n, NULL, nops, NULL, ret) : 0;
}

int
lu_ev_call(struct ctx *c)
{
    int n = c->oparg;
    if (n > MAX_ARGS) {
        lu_ev_side_exit(c, EXIT_SIDE);
        return 0;
    }
    lu_v a[MAX_ARGS + 1];
    for (int i = 0; i < n; i++) {
        a[i] = lu_ev_peek(c, n - i);
    }
    lu_ev_feedback(c, c->cache);
    return call_values(c, lu_ev_peek(c, n + 2), lu_ev_peek(c, n + 1), a, n, NULL, n + 2, 1);
}

int
lu_ev_call_kw(struct ctx *c)
{
    /* callable, self or NULL, the arguments, the names of the last of them */
    int n = c->oparg;
    PyObject *names = lu_ev_const(c, lu_ev_peek(c, 1));
    if (n > MAX_ARGS || names == NULL || !PyTuple_CheckExact(names)) {
        lu_ev_side_exit(c, EXIT_SIDE);
        return 0;
    }
    lu_v a[MAX_ARGS + 1];
    for (int i = 0; i < n; i++) {
        a[i] = lu_ev_peek(c, n - i + 1);
    }
    return call_values(c, lu_ev_peek(c, n + 3), lu_ev_peek(c, n + 2), a, n - (int)PyTuple_GET_SIZE(names), names,
                       n + 3, 0);
}

int
lu_ev_call_ex(struct ctx *c)
{
    /* callable, NULL, the arguments' tuple, the keywords' dict or NULL. A
       tuple and a dict that do not exist are the call's arguments
       themselves: the call is an ordinary one. */
    lu_v callable = lu_ev_peek(c, 4), star = lu_ev_peek(c, 2), kw = lu_ev_peek(c, 1), real;
    lu_v a[MAX_ARGS + 1];
    int n = -1, nkw = 0;
    PyObject *ks = lu_ev_const(c, star), *names = NULL;
    struct lu_vobj *so = LU_IS_VIRT(star) ? lu_ev_virt(c->g, &c->g->st, star, &real) : NULL;
    if (so != NULL && real == LU_NOV && so->kind == VK_TUPLE && so->n <= MAX_ARGS) {
        n = so->n;
        memcpy(a, so->f, sizeof(lu_v) * (size_t)n);
    }
    else if (ks != NULL && PyTuple_CheckExact(ks) && PyTuple_GET_SIZE(ks) <= MAX_ARGS) {
        n = (int)PyTuple_GET_SIZE(ks);
        for (int i = 0; i < n; i++) {
            a[i] = lu_ev_konst(c, PyTuple_GET_ITEM(ks, i));
        }
    }
    struct lu_vobj *ko = LU_IS_VIRT(kw) ? lu_ev_virt(c->g, &c->g->st, kw, &real) : NULL;
    int plain = kw == LU_NOV || (ko != NULL && real == LU_NOV && ko->kind == VK_DICT);
    if (ko != NULL && plain && n >= 0 && n + ko->n / 2 <= MAX_ARGS && ko->n > 0) {
        nkw = ko->n / 2;
        names = PyTuple_New(nkw);
        for (int i = 0; names != NULL && i < nkw; i++) {
            PyTuple_SET_ITEM(names, i, Py_NewRef(lu_ev_const(c, ko->f[2 * i])));
            a[n + i] = ko->f[2 * i + 1];
        }
        if (names == NULL) {
            PyErr_Clear();
            plain = 0;
        }
    }
    if (n < 0 || !plain) {
        lu_v args[3] = { callable, star, kw };
        lu_v w = lu_ev_helper(c, H_call_ex, 3, args, 4);
        for (int i = 0; i < 4; i++) {
            lu_ev_pop(c);
        }
        lu_ev_push(c, w);
        return 0;
    }
    if (names != NULL) {
        lu_ev_konst(c, names);  /* the unit keeps the names it calls with */
        Py_DECREF(names);
    }
    return call_values(c, callable, LU_NOV, a, n, names, 4, 0);
}
