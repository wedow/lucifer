/* Exceptions raised and taken in the unit (eval.h): a raise whose handler
   is in one of the unit's frames goes there as the interpreter unwinds,
   when nothing can tell the difference. An exception the unit makes
   (`raise C` of a class that only BaseException constructs) and a handler
   that drops it unseen never make it, nor touch the thread's exc_info;
   one a call raised is fetched, and its handler sets exc_info as the
   interpreter does, when every frame it leaves is the kernel's, which
   makes no traceback entry. */
#include "jit/eval.h"
#include "interp/exceptions.h"

/* Stands on a handler's stack for the exc_info PUSH_EXC_INFO would have
   saved while the unit leaves exc_info alone: an exit takes what exc_info
   holds there (exit.c). */
PyObject lu_jit_exc_prev = { .ob_flags = _Py_STATICALLY_ALLOCATED_FLAG, .ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL,
                             .ob_type = &PyBaseObject_Type };

/* Whether f, found on an exception class, is the runtime's own code: C,
   or a kernel function. */
static int
runtime_own(PyObject *f)
{
    return f != NULL && (!PyFunction_Check(f) || (((PyCodeObject *)((PyFunctionObject *)f)->func_code)->co_flags & LU_CO_KERNEL));
}

/* Whether calling class k makes an exception and runs only the runtime's
   own __new__ and __init__, and its instances run nothing when they die:
   nothing can tell whether it was made. */
static int
plain_class(PyObject *k)
{
    PyTypeObject *tp = (PyTypeObject *)k;
    return k != NULL && Py_IS_TYPE(k, &PyType_Type) && PyType_IsSubtype(tp, (PyTypeObject *)PyExc_BaseException)
        && tp->tp_finalize == NULL && tp->tp_del == NULL && runtime_own(_PyType_Lookup(tp, LU_ID(__new__)))
        && runtime_own(_PyType_Lookup(tp, LU_ID(__init__)));
}

/* What global `name` of frame k holds now (borrowed), or NULL. */
static PyObject *
global_now(struct ctx *k, PyObject *name)
{
    PyObject *v = PyDict_CheckExact(k->globals) ? PyDict_GetItemWithError(k->globals, name) : NULL;
    if (v == NULL && !PyErr_Occurred() && k->builtins != NULL && PyDict_CheckExact(k->builtins)) {
        v = PyDict_GetItemWithError(k->builtins, name);
    }
    PyErr_Clear();
    return v;
}

static int
matches(PyTypeObject *t, PyObject *cls)
{
    return PyExceptionClass_Check(cls) && PyType_IsSubtype(t, (PyTypeObject *)cls);
}

/* Whether the handler of frame k at pc takes an exception of type t
   without anything seeing it: it matches t against globals (or catches
   all), drops it, and runs only loads, constants and stack moves until
   POP_EXCEPT. The match is read as the globals stand now; the handler's
   loads guard them. */
static int
drops_unseen(struct ctx *k, int pc, PyTypeObject *t)
{
    struct ctx d = { .code = k->code };
    int base, oparg, op, n = (int)Py_SIZE(k->code);
    PyObject *classes[16];
    int nclasses = 0, matched = 0;
    pc = lu_ev_decode(&d, pc, &base, &oparg, &op);
    if (base != PUSH_EXC_INFO) {
        return 0;
    }
    while (pc < n) {
        int next = lu_ev_decode(&d, pc, &base, &oparg, &op);
        if (base == LOAD_GLOBAL && !(oparg & 1) && nclasses < 16) {
            classes[nclasses] = global_now(k, PyTuple_GET_ITEM(k->code->co_names, oparg >> 1));
            if (classes[nclasses++] == NULL) {
                return 0;
            }
        }
        else if (base == BUILD_TUPLE && oparg == nclasses) {
        }
        else if (base == CHECK_EXC_MATCH) {
            for (int i = 0; i < nclasses; i++) {
                matched |= matches(t, classes[i]);
            }
            if (!matched) {
                return 0;
            }
            next = lu_ev_decode(&d, next, &base, &oparg, &op);
            if (base != POP_JUMP_IF_FALSE) {
                return 0;
            }
        }
        else if (base == NOT_TAKEN) {
        }
        else if (base == POP_TOP && (matched || nclasses == 0)) {
            pc = next;
            break;
        }
        else {
            return 0;
        }
        pc = next;
    }
    while (pc < n) {
        pc = lu_ev_decode(&d, pc, &base, &oparg, &op);
        switch (base) {
        case POP_EXCEPT:
            return 1;
        case LOAD_GLOBAL: case LOAD_CONST: case LOAD_SMALL_INT: case LOAD_FAST: case LOAD_FAST_BORROW:
        case LOAD_FAST_LOAD_FAST: case LOAD_FAST_BORROW_LOAD_FAST_BORROW: case LOAD_COMMON_CONSTANT:
        case SWAP: case COPY: case NOP: case NOT_TAKEN:
            break;
        default:
            return 0;
        }
    }
    return 0;
}

/* The frame, from c out, whose handler takes an exception of type t
   raised at c's instruction (handler and level set), or NULL when the
   exception leaves the unit or is not to be taken here: a generator's
   frame is on the way, or a frame that is not the kernel's would show in
   a traceback that someone may read (`fresh`: the unit made the
   exception and the handler drops it unseen). */
static struct ctx *
catcher(struct ctx *c, PyTypeObject *t, int fresh, int *handler, int *level)
{
    for (struct ctx *k = c; k != NULL; k = k->parent) {
        int lasti;
        if (lu_ev_in_gen(k) || (!fresh && !k->kernel)) {
            return NULL;
        }
        if (lu_exception_handler(k->code, k->pc, level, handler, &lasti)) {
            return !lasti && (!fresh || drops_unseen(k, *handler, t)) ? k : NULL;
        }
    }
    return NULL;
}

/* The exception exc, raised at c's instruction, taken by frame h's
   handler on this path: the frames inside h leave as the interpreter
   unwinds them, h's stack is cut to the handler's level, and h goes on
   at the handler with exc pushed. */
static void
throw_to(struct ctx *c, struct ctx *h, int handler, int level, lu_v exc)
{
    struct gctx *g = c->g;
    for (struct ctx *k = c; k != h; k = k->parent) {
        for (int i = 0; i < k->nlocals + *depth_of(k); i++) {
            lu_ev_drop(k, slots(k)[i]);
        }
        lu_ev_drop(k, k->funcv);
        lu_ev_state_pop(g);
    }
    while (*depth_of(h) > level) {
        lu_ev_pop(h);
    }
    lu_ev_push(h, exc);
    lu_ev_jump(h, handler);
    g->cur = -1;
}

struct thrown {
    struct ctx *h;
    int handler, level;
};

/* The way a helper raised: what it raised, fetched before anything else
   runs, to the handler. */
static void
raised_arm(struct ctx *c, void *arg)
{
    struct thrown *t = arg;
    lu_v exc = lu_ev_helper(c, H_exc_fetch, 0, NULL, 0);
    throw_to(c, t->h, t->handler, t->level, exc);
}

void
lu_ev_caught(struct ctx *c, lu_v v)
{
    struct thrown t;
    t.h = catcher(c, NULL, 0, &t.handler, &t.level);
    if (t.h == NULL || c->g->cur < 0) {
        return;
    }
    I(c, v)->flags |= INS_CAUGHT;
    lu_ev_fork_with(c, lu_ev_emit(c, OP_IS, REP_BIT, v, lu_ev_konst(c, NULL), LU_NOV, 0), raised_arm, &t);
}

int
lu_ev_raise(struct ctx *c)
{
    PyObject *k = c->oparg == 1 ? lu_ev_const(c, lu_ev_peek(c, 1)) : NULL;
    int handler, level;
    struct ctx *h = plain_class(k) ? catcher(c, (PyTypeObject *)k, 1, &handler, &level) : NULL;
    if (h == NULL) {
        lu_ev_side_exit(c, EXIT_SIDE);      /* the interpreter raises */
        return 0;
    }
    struct lu_vobj *o;
    lu_v exc = lu_ev_virt_new(c, VK_EXC, (PyTypeObject *)k, 0, &o);
    lu_ev_pop(c);
    throw_to(c, h, handler, level, exc);
    return 0;
}

/* An exception exc the unit made and has not needed. */
static int
unmade_exc(struct ctx *c, lu_v exc)
{
    lu_v real;
    struct lu_vobj *o = LU_IS_VIRT(exc) ? lu_ev_virt(c->g, &c->g->st, exc, &real) : NULL;
    return o != NULL && real == LU_NOV && o->kind == VK_EXC;
}

int
lu_ev_push_exc_info(struct ctx *c)
{
    lu_v exc = lu_ev_peek(c, 1);
    lu_v prev = unmade_exc(c, exc) ? lu_ev_konst(c, &lu_jit_exc_prev) : lu_ev_helper(c, H_exc_push, 1, &exc, 0);
    lu_ev_pop(c);
    lu_ev_push(c, prev);
    lu_ev_push(c, exc);
    return 0;
}

int
lu_ev_pop_except(struct ctx *c)
{
    lu_v prev = lu_ev_peek(c, 1);
    if (lu_ev_const(c, prev) != &lu_jit_exc_prev) {
        lu_ev_helper(c, H_exc_pop, 1, &prev, 0);
    }
    lu_ev_pop(c);
    return 0;
}

/* Whether exc, of type t, matches cls: 1 or 0 when the state decides it,
   else -1. */
static int
decided(struct ctx *c, PyTypeObject *t, lu_v cls)
{
    PyObject *k = lu_ev_const(c, cls);
    if (t == NULL) {
        return -1;
    }
    if (k != NULL && PyExceptionClass_Check(k)) {
        return matches(t, k);
    }
    lu_v real;
    struct lu_vobj *o = LU_IS_VIRT(cls) ? lu_ev_virt(c->g, &c->g->st, cls, &real) : NULL;
    if (o == NULL || real != LU_NOV || o->kind != VK_TUPLE) {
        return -1;
    }
    int any = 0;
    for (int i = 0; i < o->n; i++) {
        PyObject *item = lu_ev_const(c, o->f[i]);
        if (item == NULL || !PyExceptionClass_Check(item)) {
            return -1;
        }
        any |= matches(t, item);
    }
    return any;
}

int
lu_ev_check_exc_match(struct ctx *c)
{
    lu_v exc = lu_ev_peek(c, 2), cls = lu_ev_peek(c, 1);
    int known = decided(c, lu_ev_type(c, exc), cls);
    lu_v r;
    if (known >= 0) {
        r = lu_ev_konst(c, known ? Py_True : Py_False);
    }
    else {
        lu_v args[2] = { exc, cls };
        r = lu_ev_helper(c, H_exc_match, 2, args, 1);
        lu_ev_def_type(c, r, &PyBool_Type);
    }
    lu_ev_pop(c);
    lu_ev_push(c, r);
    return 0;
}
