/* The instructions (eval.h): what each does to the state, and what it
   emits when the state does not decide it. Operands are read where they
   lie and popped after the last use, so the state before the instruction
   stays an exit's to take; what a pop stops naming is released when the
   instruction ends (lu_ev_settle). */
#include "jit/eval.h"

#define INTRINSIC_LIST_TO_TUPLE 6       /* pycore_intrinsics.h */
#define MAX_OPERANDS 30

/* The class in the __class__ cell of the function the root frame runs,
   or NULL (borrowed: the unit keeps it as a constant once used). */
static PyObject *
root_class_cell(struct ctx *c)
{
    PyObject *fn = c->g->live != NULL ? c->g->live->func : NULL;
    PyObject *closure = fn != NULL && PyFunction_Check(fn) ? ((PyFunctionObject *)fn)->func_closure : NULL;
    PyObject *names = closure != NULL ? PyCode_GetFreevars(c->code) : NULL;
    PyObject *cls = NULL;
    for (Py_ssize_t i = 0; names != NULL && i < PyTuple_GET_SIZE(names) && i < PyTuple_GET_SIZE(closure); i++) {
        if (PyUnicode_CheckExact(PyTuple_GET_ITEM(names, i))
            && PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(names, i), "__class__") == 0) {
            PyObject *cell = PyTuple_GET_ITEM(closure, i);
            cls = PyCell_Check(cell) ? PyCell_GET(cell) : NULL;
        }
    }
    Py_XDECREF(names);
    if (names == NULL) {
        PyErr_Clear();
    }
    return cls != NULL && PyType_Check(cls) ? cls : NULL;
}

/* LOAD_SUPER_ATTR with super the builtin, the class a known class and
   self's class known or learned: the attribute found past the class in
   self's class's MRO is constant under that class's version (a change to
   any class in the MRO takes the version back), so the site is the
   function itself with self (the method form) or the plain value (a
   value with no __get__), and no super object or bound method is made.
   0 when the site is not of that shape (nothing was changed). */
static int
super_known(struct ctx *c, int oparg)
{
    lu_v self = lu_ev_peek(c, 1);
    PyObject *cls = lu_ev_const(c, lu_ev_peek(c, 2));
    if (lu_ev_const(c, lu_ev_peek(c, 3)) != (PyObject *)&PySuper_Type) {
        return 0;
    }
    if (cls == NULL && c->parent == NULL && !nospec(c, c->pc)) {
        cls = root_class_cell(c);
        if (cls != NULL) {
            /* The method's __class__ cell, set when its class was made: what
               it holds now, guarded. */
            lu_ev_konst(c, cls);
            lu_ev_guard(c, OP_GUARD_IS, lu_ev_val(c, lu_ev_peek(c, 2)), (int64_t)(intptr_t)cls);
        }
    }
    if (cls == NULL || !PyType_Check(cls)) {
        return 0;
    }
    /* Self's class: known, or named by the version a guard on self already
       holds (the method's own load through self). */
    PyTypeObject *t = lu_ev_type(c, self);
    struct fact *fa = t == NULL && !LU_IS_VIRT(self) ? lu_ev_fact(c, lu_ev_val(c, self), 0) : NULL;
    t = t != NULL ? (PyTypeObject *)Py_NewRef(t) : fa != NULL && fa->version != 0 ? lu_type_of_version(fa->version) : NULL;
    if (t == NULL && !LU_IS_VIRT(self) && !nospec(c, c->pc)) {
        /* Not known: the class the site's profile learned for self (one
           class: a guard; none yet or several: profiled, the generic way). */
        t = lu_ev_learn_type(c, self, 0);
        Py_XINCREF(t);
    }
    if (t == NULL || PyType_IsSubtype(t, &PyType_Type) || !PyType_IsSubtype(t, (PyTypeObject *)cls)
        || !lu_type_assign_version(t)) {
        Py_XDECREF(t);
        return 0;               /* a class as self (a classmethod's super): the generic way */
    }
    uint32_t version = __atomic_load_n(&t->tp_version_tag, __ATOMIC_ACQUIRE);
    PyObject *v = lu_super_lookup(t, cls, name_at(c, oparg >> 2));
    int method = oparg & 1;
    int fn = v != NULL && PyFunction_Check(v);
    if (v == NULL || __atomic_load_n(&t->tp_version_tag, __ATOMIC_ACQUIRE) != version
        || (fn ? !method : !lu_ev_plain_value(c, v))) {
        Py_XDECREF(v);
        Py_DECREF(t);
        return 0;
    }
    Py_DECREF(t);
    lu_ev_guard_version(c, self, version);
    lu_v k = lu_ev_konst(c, v);
    Py_DECREF(v);
    lu_ev_pop(c);
    lu_ev_pop(c);
    lu_ev_pop(c);
    lu_ev_push(c, k);
    if (method) {
        lu_ev_push(c, fn ? self : LU_NOV);
    }
    return 1;
}

int
lu_ev_op_supported(int base)
{
    switch (base) {
    case NOP: case NOT_TAKEN: case RESUME: case LOAD_FAST: case LOAD_FAST_BORROW:
    case LOAD_FAST_CHECK: case LOAD_FAST_LOAD_FAST: case LOAD_FAST_BORROW_LOAD_FAST_BORROW:
    case STORE_FAST: case STORE_FAST_LOAD_FAST: case STORE_FAST_STORE_FAST:
    case LOAD_CONST: case LOAD_SMALL_INT: case POP_TOP: case PUSH_NULL: case COPY: case SWAP:
    case END_FOR: case POP_ITER: case LOAD_GLOBAL: case LOAD_ATTR: case STORE_ATTR:
    case BINARY_OP: case COMPARE_OP: case TO_BOOL: case UNARY_NOT: case UNARY_NEGATIVE: case UNARY_INVERT:
    case IS_OP: case POP_JUMP_IF_FALSE: case POP_JUMP_IF_TRUE: case POP_JUMP_IF_NONE:
    case POP_JUMP_IF_NOT_NONE: case JUMP_FORWARD: case JUMP_BACKWARD:
    case JUMP_BACKWARD_NO_INTERRUPT: case GET_ITER: case FOR_ITER: case CALL:
    case RETURN_VALUE: case STORE_SUBSCR: case COPY_FREE_VARS: case LOAD_DEREF:
    case BUILD_LIST: case BUILD_TUPLE: case CONTAINS_OP: case UNPACK_SEQUENCE:
    case MAKE_FUNCTION: case SET_FUNCTION_ATTRIBUTE: case STORE_DEREF: case LIST_EXTEND:
    case DELETE_SUBSCR: case LOAD_FAST_AND_CLEAR: case LIST_APPEND: case SET_ADD: case MAP_ADD:
    case BUILD_SET: case BUILD_MAP: case CALL_INTRINSIC_1: case BINARY_SLICE: case STORE_SLICE:
    case LOAD_COMMON_CONSTANT: case CALL_KW: case CALL_FUNCTION_EX: case DICT_MERGE: case DICT_UPDATE:
    case DELETE_FAST: case LOAD_SUPER_ATTR: case STORE_GLOBAL: case BUILD_SLICE: case CONVERT_VALUE:
    case FORMAT_SIMPLE: case FORMAT_WITH_SPEC: case BUILD_STRING: case LOAD_SPECIAL:
    case IMPORT_NAME: case IMPORT_FROM:
    case RETURN_GENERATOR: case YIELD_VALUE:    /* a generator made in line (eval_gen.c) */
    case RAISE_VARARGS: case RERAISE:   /* the interpreter raises: an exit, on a path that is leaving anyway */
    case PUSH_EXC_INFO: case POP_EXCEPT: case CHECK_EXC_MATCH:  /* a handler the unit's raise reached (eval_exc.c) */
    case GET_AWAITABLE: case GET_YIELD_FROM_ITER: case SEND: case END_SEND:     /* in the root frame (eval_gen.c) */
    case GET_AITER: case GET_ANEXT:
        return 1;
    default:
        return 0;
    }
}

/* Whether the instruction is compiled only in the unit's root frame: a
   delegation, whose receiver runs nested under the root's unit. */
int
lu_ev_op_root_only(int base)
{
    return base == GET_AWAITABLE || base == GET_YIELD_FROM_ITER || base == SEND || base == END_SEND || base == GET_AITER
        || base == GET_ANEXT;
}

/* The record of r when it is a virtual object of the kind that nothing
   has made yet, else NULL. */
static struct lu_vobj *
unmade(struct ctx *c, lu_v r, int kind)
{
    if (!LU_IS_VIRT(r)) {
        return NULL;
    }
    lu_v real;
    struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
    return o != NULL && real == LU_NOV && o->kind == kind ? o : NULL;
}

/* A virtual object of the kind with the n fields of a, then the m of b. */
static lu_v
joined(struct ctx *c, int kind, PyTypeObject *type, const lu_v *a, int n, const lu_v *b, int m, struct lu_vobj **out)
{
    lu_v r = lu_ev_virt_new(c, kind, type, n + m, out);
    for (int i = 0; i < n; i++) {
        (*out)->f[i] = a[i];
    }
    for (int i = 0; i < m; i++) {
        (*out)->f[n + i] = b[i];
    }
    return r;
}

/* Whether the instruction after the current one is `base` (with `oparg`
   unless that is -1). */
static int
next_is(struct ctx *c, int base, int oparg)
{
    int b, a, op;
    lu_ev_decode(c, c->next, &b, &a, &op);
    return b == base && (oparg < 0 || a == oparg);
}

static void
pop_n(struct ctx *c, int n)
{
    for (int i = 0; i < n; i++) {
        lu_ev_pop(c);
    }
}

/* A helper over the top n stack entries, bottom first, then `extra`
   arguments: they are popped and the result pushed (when it has one). */
static lu_v
stack_helper(struct ctx *c, int id, int n, const lu_v *extra, int nextra)
{
    lu_v args[MAX_OPERANDS + 4];
    for (int i = 0; i < n; i++) {
        args[i] = lu_ev_peek(c, n - i);
    }
    for (int i = 0; i < nextra; i++) {
        args[n + i] = extra[i];
    }
    lu_v r = lu_ev_helper(c, id, n + nextra, args, n);
    pop_n(c, n);
    if (lu_helpers[id].rep == REP_OBJ) {
        lu_ev_push(c, r);
    }
    return r;
}

/* A local read where it may be unbound: one that came in from outside
   the unit, or through a join, is tested. */
static int
bound(struct ctx *c, int i)
{
    lu_v v = slots(c)[i];
    if (v == LU_NOV) {
        lu_ev_side_exit(c, EXIT_SIDE);  /* the interpreter raises */
        return 0;
    }
    if (!LU_IS_VIRT(v) && (I(c, v)->op == OP_ENTRY_LOCAL || I(c, v)->op == OP_PHI || I(c, v)->op == OP_LOAD) && I(c, v)->rep == REP_OBJ
        && lu_ev_type(c, v) == NULL) {
        lu_ev_guard(c, OP_GUARD_NOT_NULL, v, 0);
    }
    if (LU_IS_VIRT(v)) {
        /* A number a loop carries raw: unbound while the object beside it says so. */
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, v, &real);
        if (o != NULL && real == LU_NOV && (o->kind == VK_BOX_INT || o->kind == VK_BOX_FLOAT) && o->n == 2) {
            if (lu_ev_const(c, o->f[1]) == &lu_jit_unbound) {
                lu_ev_side_exit(c, EXIT_SIDE);  /* the interpreter raises */
                return 0;
            }
            if (I(c, o->f[1])->op == OP_PHI) {
                lu_v is = lu_ev_emit(c, OP_IS, REP_BIT, o->f[1], lu_ev_konst(c, &lu_jit_unbound), LU_NOV, 0);
                lu_ev_guard(c, OP_GUARD_BIT, is, 0);
            }
        }
    }
    return 1;
}

/* The bit of a value that is a bool (TO_BOOL or a comparison made it). */
static lu_v
bool_bit(struct ctx *c, lu_v v)
{
    if (LU_IS_VIRT(v) || lu_ev_const(c, v) != NULL) {
        return lu_ev_truth(c, v);
    }
    return lu_ev_emit(c, OP_IS, REP_BIT, v, lu_ev_konst(c, Py_True), LU_NOV, 0);
}

static int
pop_jump(struct ctx *c)
{
    lu_v v = lu_ev_peek(c, 1), bit;
    int target = lu_ev_jump_target(c->base, c->oparg, c->next), next = c->next;
    if (c->base == POP_JUMP_IF_FALSE || c->base == POP_JUMP_IF_TRUE) {
        bit = bool_bit(c, v);
    }
    else if (LU_IS_VIRT(v)) {
        bit = lu_ev_const_i(c, 0);      /* an object the code made is not None */
    }
    else if (lu_ev_const(c, v) != NULL || lu_ev_type(c, v) != NULL) {
        bit = lu_ev_const_i(c, lu_ev_type(c, v) == Py_TYPE(Py_None));
    }
    else {
        bit = lu_ev_emit(c, OP_IS, REP_BIT, v, lu_ev_konst(c, Py_None), LU_NOV, 0);
    }
    int jump_if = c->base == POP_JUMP_IF_TRUE || c->base == POP_JUMP_IF_NONE;
    int rare_jump = !nospec(c, c->pc) && lu_jit_branch_rare(c->cache, 1);
    int rare_fall = !nospec(c, c->pc) && lu_jit_branch_rare(c->cache, 0);
    if (I(c, bit)->op != OP_CONST_I && (rare_jump || rare_fall)) {
        /* A direction the interpreter took rarely or never is not built: a
           guard leaves for it. */
        int want = rare_fall == jump_if;
        lu_ev_guard(c, OP_GUARD_BIT, bit, want);
        bit = lu_ev_const_i(c, want);
    }
    lu_ev_pop(c);
    if (jump_if) {
        lu_ev_branch(c, bit, target, next);
    }
    else {
        lu_ev_branch(c, bit, next, target);
    }
    return 0;
}

static int
unpack_sequence(struct ctx *c)
{
    lu_v v = lu_ev_peek(c, 1);
    int n = c->oparg;
    PyObject *k = lu_ev_const(c, v);
    if (k != NULL && PyTuple_CheckExact(k) && PyTuple_GET_SIZE(k) == n) {
        lu_ev_pop(c);
        for (int i = n - 1; i >= 0; i--) {
            lu_ev_push(c, lu_ev_konst(c, PyTuple_GET_ITEM(k, i)));
        }
        return 0;
    }
    if (LU_IS_VIRT(v)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, v, &real);
        if (o != NULL && o->kind == VK_TUPLE && real == LU_NOV && o->n == n) {
            /* A tuple that does not exist: its items are at hand. */
            lu_v *items = lu_ir_alloc(c->g->f, sizeof(lu_v) * (size_t)(n ? n : 1));
            for (int i = 0; i < n; i++) {
                items[i] = o->f[i];
            }
            lu_ev_pop(c);
            for (int i = n - 1; i >= 0; i--) {
                lu_ev_push(c, items[i]);
            }
            return 0;
        }
    }
    int kinds = lu_ev_feedback(c, c->cache) & 0xff;
    lu_v items;
    if (lu_ev_type(c, v) == &PyTuple_Type || (kinds == LU_KIND_TUPLE && !nospec(c, c->pc) && n <= MAX_OPERANDS)) {
        lu_ev_guard_type(c, v, &PyTuple_Type);
        items = lu_ev_val(c, v);
        lu_v size = lu_ev_emit(c, OP_LOAD, REP_I64, items, LU_NOV, LU_NOV, offsetof(PyVarObject, ob_size));
        lu_ev_guard(c, OP_GUARD_BIT, lu_ev_emit(c, OP_CMP_I, REP_BIT, size, lu_ev_const_i(c, n), LU_NOV, Py_EQ), 1);
    }
    else {
        /* Any other sequence: the kernel unpacks it into a tuple of n. */
        lu_v args[2] = { v, lu_ev_const_i(c, n) };
        items = lu_ev_helper(c, H_unpack, 2, args, 1);
    }
    lu_v *loaded = lu_ir_alloc(c->g->f, sizeof(lu_v) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) {
        loaded[i] = lu_ev_emit(c, OP_LOAD, REP_OBJ, items, LU_NOV, LU_NOV,
                               (int64_t)(offsetof(PyTupleObject, ob_item) + sizeof(PyObject *) * (size_t)i));
        I(c, loaded[i])->imm2 = LU_LOAD_TUPLE_ITEM;     /* the tuple keeps it (opt.c borrow_items) */
        lu_ev_emit(c, OP_INCREF, REP_NONE, loaded[i], LU_NOV, LU_NOV, 0);
        lu_ev_own(c, loaded[i]);
    }
    lu_ev_pop(c);
    for (int i = n - 1; i >= 0; i--) {
        lu_ev_push(c, loaded[i]);
    }
    return 0;
}

/* Whether a value stored into a bytearray is an int: known or learned,
   or, where the site met several types, guarded to be one, as any other
   but an __index__ object is refused. */
static int
store_byte(struct ctx *c, lu_v val)
{
    PyTypeObject *t = lu_ev_learn_type(c, val, 2);
    if (t == NULL && val != LU_NOV && !LU_IS_VIRT(val) && I(c, val)->rep == REP_OBJ) {
        lu_ev_guard_type(c, val, &PyLong_Type);
        t = &PyLong_Type;
    }
    return t == &PyLong_Type;
}

static int
store_subscr(struct ctx *c)
{
    lu_v sub = lu_ev_peek(c, 1), cont = lu_ev_peek(c, 2), val = lu_ev_peek(c, 3);
    int kinds = lu_ev_feedback(c, c->cache);
    if (!nospec(c, c->pc) && (kinds & 0xff) == LU_KIND_LIST && (kinds >> 8) == LU_KIND_INT) {
        lu_ev_guard_type(c, cont, &PyList_Type);
        lu_v i = lu_ev_unbox_int(c, sub);
        lu_v st = lu_ev_emit(c, OP_LIST_STORE, REP_NONE, lu_ev_val(c, cont), i, lu_ev_val(c, val), 0);
        I(c, st)->fs = lu_ev_exit_fs(c);
        c->g->observed = 1;     /* the value it replaces is released */
    }
    else if (!nospec(c, c->pc) && (kinds & 0xff) == LU_KIND_OTHER && (kinds >> 8) == LU_KIND_INT
             && lu_ev_learn_type(c, cont, 0) == &PyByteArray_Type && store_byte(c, val)) {
        /* A bytearray's byte, stored under its lock as an array of 'B': a
           value out of range(256) leaves, for the interpreter to raise. */
        lu_v st = lu_ev_emit(c, OP_TYPED_STORE, REP_NONE, lu_ev_val(c, cont), lu_ev_unbox_int(c, sub),
                             lu_ev_unbox_int(c, val), 'B');
        I(c, st)->fs = lu_ev_exit_fs(c);
    }
    else {
        /* Through the kernel's setitem, which folds on the container's
           class to its __setitem__. */
        lu_v args[3] = { cont, sub, val };
        int done = lu_ev_kernel_arms(c, args, 1);
        done = done != 0 ? done : lu_ev_kernel_op(c, LU_K_setitem, args, 3, 3, LU_RETURN_DISCARD);
        if (done != 0) {
            return done < 0 ? -1 : 0;
        }
        lu_ev_helper(c, H_setitem, 3, args, 3);
    }
    pop_n(c, 3);
    return 0;
}

static int
load_common_constant(struct ctx *c)
{
    PyObject *v = PyThreadState_Get()->interp->common_consts[c->oparg];
    if (v == NULL) {
        lu_ev_side_exit(c, EXIT_SIDE);      /* the kernel booting */
        return 0;
    }
    lu_ev_push(c, lu_ev_konst(c, v));
    return 0;
}

/* A generator's root frame yields: its slots get their values, as
   objects, each with a reference of its own (compiled code's one
   reference per owned value goes to a slot that names it), and the unit
   returns the value yielded with the frame saved at its resume point,
   where a unit is entered again. Nothing goes through the exit's
   materializer. */
static int
yield_value(struct ctx *c)
{
    struct gctx *g = c->g;
    int n = c->nlocals + *depth_of(c) - 1;
    lu_v *vals = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(n + 1));
    int *need = lu_ir_alloc(g->f, sizeof(int) * (size_t)(n + 1));
    for (int i = 0; i <= n; i++) {
        lu_v r = slots(c)[i];           /* the last is the value yielded */
        vals[i] = r == LU_NOV ? lu_ev_konst(c, NULL) : lu_ev_val(c, r);
        if (g->f->fail != NULL) {
            return -1;
        }
    }
    /* What an object made real here gave up (an unmade tuple's items)
       is released before the yield, which ends the unit's run. */
    lu_ev_settle(c, c->pc);
    /* Each value needs a reference per slot that names it, and the one
       yielded one more; an owned value has one already. */
    for (int i = 0; i <= n; i++) {
        need[i] = 1;
        for (int k = 0; k < i; k++) {
            if (vals[k] == vals[i]) {
                need[k]++;
                need[i] = 0;
                break;
            }
        }
    }
    for (int i = 0; i <= n; i++) {
        for (int k = (I(c, vals[i])->flags & INS_OWNED) ? 1 : 0; k < need[i]; k++) {
            lu_ev_retain(c, vals[i]);
        }
    }
    for (int i = 0; i < n; i++) {
        lu_ev_emit(c, OP_FRAME_STORE, REP_NONE, vals[i], LU_NOV, LU_NOV, i);
    }
    lu_v y = lu_ev_emit(c, OP_YIELD, REP_NONE, vals[n], LU_NOV, LU_NOV, n);
    I(c, y)->imm2 = c->next;
    g->cur = -1;
    g->npending = 0;
    g->observed = 0;
    return 0;
}

int
lu_ev_instruction(struct ctx *c)
{
    int oparg = c->oparg;
    lu_v v, w;
    switch (c->base) {
    case NOP: case NOT_TAKEN:
        return 0;
    case RETURN_GENERATOR:
        return lu_ev_gen_make(c);
    case RESUME:
        if (c->parent != NULL && !c->kernel && !lu_ev_in_gen(c)) {
            /* An inlined call polls where the interpreter's would: a tool
               turned on since the compile sees the call. */
            I(c, lu_ev_guard(c, OP_POLL, LU_NOV, 0))->imm = POLL_CALL;
        }
        return 0;
    case LOAD_FAST: case LOAD_FAST_BORROW:
        lu_ev_push(c, slots(c)[oparg]);
        return 0;
    case LOAD_FAST_CHECK:
        if (bound(c, oparg)) {
            lu_ev_push(c, slots(c)[oparg]);
        }
        return 0;
    case LOAD_FAST_LOAD_FAST: case LOAD_FAST_BORROW_LOAD_FAST_BORROW:
        lu_ev_push(c, slots(c)[oparg >> 4]);
        lu_ev_push(c, slots(c)[oparg & 15]);
        return 0;
    case STORE_FAST:
        v = lu_ev_peek(c, 1);
        lu_ev_set_local(c, oparg, v);
        lu_ev_pop(c);
        return 0;
    case STORE_FAST_LOAD_FAST:
        v = lu_ev_peek(c, 1);
        lu_ev_set_local(c, oparg >> 4, v);
        lu_ev_pop(c);
        lu_ev_push(c, slots(c)[oparg & 15]);
        return 0;
    case STORE_FAST_STORE_FAST:
        lu_ev_set_local(c, oparg >> 4, lu_ev_peek(c, 1));
        lu_ev_pop(c);
        lu_ev_set_local(c, oparg & 15, lu_ev_peek(c, 1));
        lu_ev_pop(c);
        return 0;
    case LOAD_FAST_AND_CLEAR:
        /* A comprehension's variable, kept on the stack while it runs. */
        lu_ev_push(c, slots(c)[oparg]);
        lu_ev_set_local(c, oparg, LU_NOV);
        return 0;
    case DELETE_FAST:
        if (bound(c, oparg)) {
            lu_ev_set_local(c, oparg, LU_NOV);
        }
        return 0;
    case LOAD_CONST:
        lu_ev_push(c, lu_ev_konst(c, PyTuple_GET_ITEM(c->code->co_consts, oparg)));
        return 0;
    case LOAD_SMALL_INT:
        lu_ev_push(c, lu_ev_konst(c, lu_small_int(oparg)));
        return 0;
    case LOAD_COMMON_CONSTANT:
        return load_common_constant(c);
    case POP_TOP: case END_FOR: case POP_ITER:
        lu_ev_pop(c);
        return 0;
    case PUSH_NULL:
        lu_ev_push(c, LU_NOV);
        return 0;
    case COPY:
        lu_ev_push(c, lu_ev_peek(c, oparg));
        return 0;
    case SWAP: {
        lu_v *top = &slots(c)[c->nlocals + *depth_of(c) - 1], *other = &slots(c)[c->nlocals + *depth_of(c) - oparg];
        lu_v t = *top;
        *top = *other;
        *other = t;
        return 0;
    }
    case LOAD_GLOBAL:
        return lu_ev_load_global(c);
    case LOAD_ATTR:
        return lu_ev_load_attr(c);
    case STORE_ATTR:
        return lu_ev_store_attr(c);
    case BINARY_OP:
        return lu_ev_binary_op(c);
    case COMPARE_OP:
        return lu_ev_compare_op(c);
    case UNARY_NEGATIVE: case UNARY_INVERT:
        return lu_ev_unary(c);
    case TO_BOOL:
        /* The operand's type, learned where it is not known: a bool's or
           None's truth is a comparison, not a call. */
        lu_ev_learn_type(c, lu_ev_peek(c, 1), 0);
        w = lu_ev_truth(c, lu_ev_peek(c, 1));
        lu_ev_pop(c);
        lu_ev_push(c, lu_ev_bool(c, w, 0));
        return 0;
    case UNARY_NOT:
        w = bool_bit(c, lu_ev_peek(c, 1));
        lu_ev_pop(c);
        lu_ev_push(c, lu_ev_bool(c, w, 1));
        return 0;
    case IS_OP: {
        lu_v b = lu_ev_peek(c, 1), a = lu_ev_peek(c, 2);
        PyTypeObject *ta = lu_ev_type(c, a), *tb = lu_ev_type(c, b);
        /* Two names of one value are one object, and so are two values of
           a type that has one instance; values of two types are two
           objects. Else the objects are compared, made real if need be: a
           small int that does not exist yet is the one every other small
           int of its value is. */
        w = a == b || (ta == tb && (ta == Py_TYPE(Py_None) || ta == Py_TYPE(Py_NotImplemented)
                                    || ta == Py_TYPE(Py_Ellipsis))) ? lu_ev_const_i(c, 1)
            : (lu_ev_const(c, a) != NULL && lu_ev_const(c, b) != NULL)     /* two constants: two objects */
              || (ta != NULL && tb != NULL && ta != tb) ? lu_ev_const_i(c, 0)
            : lu_ev_emit(c, OP_IS, REP_BIT, a == LU_NOV ? lu_ev_konst(c, NULL) : lu_ev_val(c, a),
                         b == LU_NOV ? lu_ev_konst(c, NULL) : lu_ev_val(c, b), LU_NOV, 0);
        pop_n(c, 2);
        lu_ev_push(c, lu_ev_bool(c, w, oparg));
        return 0;
    }
    case POP_JUMP_IF_FALSE: case POP_JUMP_IF_TRUE: case POP_JUMP_IF_NONE: case POP_JUMP_IF_NOT_NONE:
        return pop_jump(c);
    case JUMP_FORWARD:
        lu_ev_jump(c, lu_ev_jump_target(c->base, oparg, c->next));
        return 0;
    case JUMP_BACKWARD:
        if (c->parent == NULL && lu_ev_jump_target(c->base, oparg, c->next) < c->g->f->entry_pc
            && c->blkat[c->g->f->entry_pc] >= 0 && c->bb[c->blkat[c->g->f->entry_pc]].loop) {
            /* A unit entered at a loop's header runs that loop, not the
               loop around it: its entry's state (an iterator the
               interpreter made, say) would be what every later turn of
               the outer loop must fit. The interpreter takes the outer
               back-edge, and enters that loop's own unit once it is hot.
               (A generator's unit, entered where it resumes, goes round
               its loop.) */
            lu_ev_side_exit(c, EXIT_SIDE);
            return 0;
        }
        lu_ev_guard(c, OP_POLL, LU_NOV, 0);
        lu_ev_jump(c, lu_ev_jump_target(c->base, oparg, c->next));
        return 0;
    case JUMP_BACKWARD_NO_INTERRUPT:
        lu_ev_jump(c, lu_ev_jump_target(c->base, oparg, c->next));
        return 0;
    case GET_ITER:
        return lu_ev_get_iter(c);
    case GET_AWAITABLE: case GET_YIELD_FROM_ITER: case GET_AITER: case GET_ANEXT:
        return lu_ev_delegate_iter(c);
    case SEND:
        return lu_ev_send(c);
    case END_SEND:
        v = lu_ev_peek(c, 1);
        lu_ev_pop(c);
        lu_ev_pop(c);
        lu_ev_push(c, v);
        return 0;
    case FOR_ITER:
        return lu_ev_for_iter(c);
    case CALL:
        return lu_ev_call(c);
    case CALL_KW:
        return lu_ev_call_kw(c);
    case RETURN_VALUE:
        return lu_ev_return(c);
    case CALL_FUNCTION_EX:
        return lu_ev_call_ex(c);
    case BUILD_TUPLE: {
        if (oparg == 0) {
            lu_ev_push(c, lu_ev_konst(c, (PyObject *)&_PyRuntime.singletons.empty_tuple));
            return 0;
        }
        /* A tuple nothing has needed yet: its items, under one name. */
        struct lu_vobj *o;
        w = lu_ev_virt_new(c, VK_TUPLE, &PyTuple_Type, oparg, &o);
        for (int i = 0; i < oparg; i++) {
            o->f[i] = lu_ev_peek(c, oparg - i);
            if (o->f[i] == LU_NOV) {
                c->g->f->fail = "a tuple of an unbound value";
                return -1;
            }
        }
        pop_n(c, oparg);
        lu_ev_push(c, w);
        return 0;
    }
    case BUILD_LIST: case BUILD_SET: case BUILD_MAP: case BUILD_STRING: {
        static const int helpers[] = { [BUILD_LIST] = H_build_list, [BUILD_SET] = H_build_set,
                                       [BUILD_MAP] = H_build_map, [BUILD_STRING] = H_build_string };
        static PyTypeObject *const built_types[] = { [BUILD_LIST] = &PyList_Type, [BUILD_SET] = &PySet_Type,
                                                [BUILD_MAP] = &PyDict_Type, [BUILD_STRING] = &PyUnicode_Type };
        int n = c->base == BUILD_MAP ? 2 * oparg : oparg;
        if (n > MAX_OPERANDS) {
            lu_ev_side_exit(c, EXIT_SIDE);
            return 0;
        }
        if (c->base == BUILD_LIST || (c->base == BUILD_MAP && n == 0)) {
            /* A list, or an empty dict, nothing has needed yet. */
            struct lu_vobj *o;
            lu_v items[MAX_OPERANDS + 1];
            int unbound = 0;
            for (int i = 0; i < n; i++) {
                items[i] = lu_ev_peek(c, n - i);
                unbound |= items[i] == LU_NOV;
            }
            if (!unbound) {
                w = c->base == BUILD_LIST ? joined(c, VK_LIST, &PyList_Type, items, n, NULL, 0, &o)
                    : lu_ev_virt_new(c, VK_DICT, &PyDict_Type, 0, &o);
                pop_n(c, n);
                lu_ev_push(c, w);
                return 0;
            }
        }
        w = stack_helper(c, helpers[c->base], n, NULL, 0);
        lu_ev_def_type(c, w, built_types[c->base]);
        return 0;
    }
    case UNPACK_SEQUENCE:
        return unpack_sequence(c);
    case LIST_APPEND: case SET_ADD: {
        lu_v args[2] = { lu_ev_peek(c, oparg + 1), lu_ev_peek(c, 1) };
        lu_ev_helper(c, c->base == LIST_APPEND ? H_list_push : H_set_add, 2, args, 1);
        lu_ev_pop(c);
        return 0;
    }
    case MAP_ADD: {
        lu_v args[3] = { lu_ev_peek(c, oparg + 2), lu_ev_peek(c, 2), lu_ev_peek(c, 1) };
        lu_ev_helper(c, H_setitem, 3, args, 2);
        pop_n(c, 2);
        return 0;
    }
    case LIST_EXTEND: {
        /* A list that does not exist yet takes the items of a tuple at hand. */
        lu_v list = lu_ev_peek(c, oparg + 1), src = lu_ev_peek(c, 1);
        struct lu_vobj *lo = unmade(c, list, VK_LIST), *so = unmade(c, src, VK_TUPLE), *o;
        PyObject *ks = lu_ev_const(c, src);
        if (lo != NULL && (so != NULL || (ks != NULL && PyTuple_CheckExact(ks) && PyTuple_GET_SIZE(ks) <= MAX_OPERANDS))) {
            lu_v more[MAX_OPERANDS + 1];
            int m = so != NULL ? 0 : (int)PyTuple_GET_SIZE(ks);
            for (int i = 0; i < m; i++) {
                more[i] = lu_ev_konst(c, PyTuple_GET_ITEM(ks, i));
            }
            joined(c, VK_LIST, &PyList_Type, lo->f, lo->n, so != NULL ? so->f : more, so != NULL ? so->n : m, &o);
            lu_ev_bind(c->g, &c->g->st, LU_VIRT_ID(list), LU_NOV, o);   /* the same list, longer */
            lu_ev_pop(c);
            return 0;
        }
        PyTypeObject *st = lu_ev_type(c, src);
        if (lo != NULL && !LU_IS_VIRT(src) && (st == NULL || st == &PyTuple_Type) && lo->n < MAX_OPERANDS && !nospec(c, c->pc)
            && next_is(c, CALL_INTRINSIC_1, INTRINSIC_LIST_TO_TUPLE)) {
            /* A call's f(a, *args): the list, and the tuple made of it, wait
               for the call, which takes the tuple's items as they are. */
            lu_ev_guard_type(c, src, &PyTuple_Type);
            joined(c, VK_SPLICE, &PyList_Type, lo->f, lo->n, &src, 1, &o);
            lu_ev_bind(c->g, &c->g->st, LU_VIRT_ID(list), LU_NOV, o);
            lu_ev_pop(c);
            return 0;
        }
        lu_v args[2] = { lu_ev_peek(c, oparg + 1), lu_ev_peek(c, 1) };
        lu_ev_helper(c, H_list_extend, 2, args, 1);
        lu_ev_pop(c);
        return 0;
    }
    case DICT_UPDATE: {
        lu_v args[2] = { lu_ev_peek(c, oparg + 1), lu_ev_peek(c, 1) };
        lu_ev_helper(c, H_dict_update, 2, args, 1);
        lu_ev_pop(c);
        return 0;
    }
    case DICT_MERGE: {
        /* A call's **update into its keyword dict, neither of which exists:
           the keywords of both, unless one is given twice (the call raises). */
        lu_v dict = lu_ev_peek(c, oparg + 1), update = lu_ev_peek(c, 1);
        struct lu_vobj *d = unmade(c, dict, VK_DICT), *u = unmade(c, update, VK_DICT), *o;
        int twice = 0;
        for (int i = 0; d != NULL && u != NULL && i < d->n; i += 2) {
            for (int k = 0; k < u->n; k += 2) {
                twice |= PyUnicode_Compare(lu_ev_const(c, d->f[i]), lu_ev_const(c, u->f[k])) == 0;
            }
        }
        if (d != NULL && u != NULL && !twice) {
            joined(c, VK_DICT, &PyDict_Type, d->f, d->n, u->f, u->n, &o);
            lu_ev_bind(c->g, &c->g->st, LU_VIRT_ID(dict), LU_NOV, o);
            lu_ev_pop(c);
            return 0;
        }
        PyTypeObject *ut = lu_ev_type(c, update);
        if (oparg == 1 && d != NULL && d->n == 0 && !LU_IS_VIRT(update) && (ut == NULL || ut == &PyDict_Type) && !nospec(c, c->pc)
            && next_is(c, CALL_FUNCTION_EX, -1)) {
            /* The keywords of a call of f(..., **kw) alone: the call reads
               them from kw itself, as it would from the copy, which nothing
               can see before it does. */
            lu_ev_guard_type(c, update, &PyDict_Type);
            pop_n(c, 2);
            lu_ev_push(c, update);
            return 0;
        }
        lu_v args[3] = { lu_ev_peek(c, oparg + 4), lu_ev_peek(c, oparg + 1), lu_ev_peek(c, 1) };
        lu_ev_helper(c, H_dict_merge, 3, args, 1);
        lu_ev_pop(c);
        return 0;
    }
    case CALL_INTRINSIC_1:
        if (oparg != INTRINSIC_LIST_TO_TUPLE) {
            lu_ev_side_exit(c, EXIT_SIDE);
            return 0;
        }
        struct lu_vobj *spliced = unmade(c, lu_ev_peek(c, 1), VK_SPLICE);
        if (spliced != NULL && spliced->type == &PyList_Type) {
            struct lu_vobj *o;
            w = joined(c, VK_SPLICE, &PyTuple_Type, spliced->f, spliced->n, NULL, 0, &o);
            lu_ev_pop(c);
            lu_ev_push(c, w);
            return 0;
        }
        if (unmade(c, lu_ev_peek(c, 1), VK_LIST) != NULL) {
            /* The tuple of a list that does not exist: its items, as a tuple that does not. */
            struct lu_vobj *lo = unmade(c, lu_ev_peek(c, 1), VK_LIST), *o;
            w = lo->n == 0 ? lu_ev_konst(c, (PyObject *)&_PyRuntime.singletons.empty_tuple)
                : joined(c, VK_TUPLE, &PyTuple_Type, lo->f, lo->n, NULL, 0, &o);
            lu_ev_pop(c);
            lu_ev_push(c, w);
            return 0;
        }
        v = lu_ev_const_i(c, -1);
        w = stack_helper(c, H_list_to_tuple, 1, &v, 1);
        lu_ev_def_type(c, w, &PyTuple_Type);
        return 0;
    case STORE_SUBSCR:
        return store_subscr(c);
    case DELETE_SUBSCR:
        stack_helper(c, H_delitem, 2, NULL, 0);
        return 0;
    case CONTAINS_OP: {
        lu_v kargs[3] = { lu_ev_peek(c, 1), lu_ev_peek(c, 2), lu_ev_konst(c, lu_small_int(oparg)) };   /* the container first */
        int done = lu_ev_kernel_arms(c, kargs, 2);
        done = done != 0 ? done : lu_ev_kernel_op(c, LU_K_contains, kargs, 3, 2, LU_RETURN_PUSH);
        if (done != 0) {
            return done < 0 ? -1 : 0;
        }
    }
        v = lu_ev_const_i(c, oparg);
        stack_helper(c, H_contains, 2, &v, 1);
        return 0;
    case STORE_GLOBAL: {
        if (!PyDict_CheckExact(c->globals)) {
            lu_ev_side_exit(c, EXIT_SIDE);
            return 0;
        }
        lu_v args[3] = { lu_ev_konst(c, c->globals), lu_ev_konst(c, name_at(c, oparg)), lu_ev_peek(c, 1) };
        lu_ev_helper(c, H_store_global, 3, args, 1);
        lu_ev_pop(c);
        return 0;
    }
    case BUILD_SLICE: {
        int n = oparg == 3 ? 3 : 2;
        lu_v args[3] = { lu_ev_peek(c, n), lu_ev_peek(c, n - 1), n == 3 ? lu_ev_peek(c, 1) : LU_NOV };
        w = lu_ev_helper(c, H_build_slice, 3, args, n);
        lu_ev_def_type(c, w, &PySlice_Type);
        pop_n(c, n);
        lu_ev_push(c, w);
        return 0;
    }
    case BINARY_SLICE:
        v = lu_ev_const_i(c, 1);
        stack_helper(c, H_binary_slice, 3, &v, 1);
        return 0;
    case STORE_SLICE:
        stack_helper(c, H_store_slice, 4, NULL, 0);
        return 0;
    case CONVERT_VALUE:
        v = lu_ev_const_i(c, oparg);
        stack_helper(c, H_convert_value, 1, &v, 1);
        return 0;
    case FORMAT_SIMPLE:
        v = lu_ev_konst(c, (PyObject *)&_PyRuntime.singletons.empty_str);
        stack_helper(c, H_format, 1, &v, 1);
        return 0;
    case FORMAT_WITH_SPEC:
        stack_helper(c, H_format, 2, NULL, 0);
        return 0;
    case MAKE_FUNCTION:
        v = lu_ev_konst(c, c->globals);
        stack_helper(c, H_make_function, 1, &v, 1);
        return 0;
    case SET_FUNCTION_ATTRIBUTE: {
        /* attr, func -- func */
        lu_v func = lu_ev_peek(c, 1);
        lu_v args[3] = { func, lu_ev_peek(c, 2), lu_ev_const_i(c, oparg) };
        lu_ev_helper(c, H_set_function_attribute, 3, args, 0);
        pop_n(c, 2);
        lu_ev_push(c, func);
        return 0;
    }
    case COPY_FREE_VARS: {
        /* The root's were copied before its entry; an inlined function's
           closure is a constant. */
        PyObject *closure = c->func != NULL ? c->func->func_closure : NULL;
        if (c->parent != NULL && c->funcv != LU_NOV) {
            /* The cells of the function called, which the frame's state
               names for as long as the frame is there. */
            lu_v cells = lu_ev_emit(c, OP_LOAD, REP_OBJ, c->funcv, LU_NOV, LU_NOV, offsetof(PyFunctionObject, func_closure));
            for (int i = 0; i < oparg; i++) {
                lu_v cell = lu_ev_emit(c, OP_LOAD, REP_OBJ, cells, LU_NOV, LU_NOV,
                                       (int64_t)(offsetof(PyTupleObject, ob_item) + sizeof(PyObject *) * (size_t)i));
                lu_ev_def_type(c, cell, &PyCell_Type);
                lu_ev_set_local(c, c->nlocals - oparg + i, cell);
            }
            return 0;
        }
        if (c->parent == NULL || closure == NULL) {
            c->g->f->fail = "free variables copied inside a unit";
            return -1;
        }
        for (int i = 0; i < oparg; i++) {
            lu_ev_set_local(c, c->nlocals - oparg + i, lu_ev_konst(c, PyTuple_GET_ITEM(closure, i)));
        }
        return 0;
    }
    case LOAD_DEREF:
        if (slots(c)[oparg] == LU_NOV) {
            lu_ev_side_exit(c, EXIT_SIDE);
            return 0;
        }
        v = slots(c)[oparg];
        if (lu_ev_const(c, v) != NULL && PyCell_Check(lu_ev_const(c, v)) && !nospec(c, c->pc)) {
            /* A known function's cell: what it holds now, guarded where it
               is read (most are bound once, before the function is made). */
            PyObject *now = lu_cell_get(lu_ev_const(c, v));
            PyErr_Clear();
            if (now != NULL) {
                lu_v gd = lu_ev_guard(c, OP_GUARD_IS, v, (int64_t)(intptr_t)now);
                I(c, gd)->imm2 = GI_FIELD | offsetof(PyCellObject, ob_ref);
                lu_ev_push(c, lu_ev_konst(c, now));
                Py_DECREF(now);         /* the unit holds its constants */
                return 0;
            }
        }
        if (lu_ev_type(c, v) == &PyCell_Type) {
            /* A cell's word read in line, as lu_cell_get reads it: empty
               (unbound), or another thread's value whose count cannot be
               taken in place, leaves for the interpreter. */
            w = lu_ev_emit(c, OP_SLOT_LOAD, REP_OBJ, lu_ev_val(c, v), LU_NOV, LU_NOV, offsetof(PyCellObject, ob_ref));
            I(c, w)->fs = lu_ev_exit_fs(c);
            lu_ev_push(c, lu_ev_own(c, w));
            return 0;
        }
        w = lu_ev_helper(c, H_cell_load, 1, &v, 0);
        lu_ev_guard(c, OP_GUARD_NOT_NULL, w, 0);        /* unbound: the interpreter raises */
        lu_ev_push(c, w);
        return 0;
    case STORE_DEREF: {
        if (slots(c)[oparg] == LU_NOV) {
            lu_ev_side_exit(c, EXIT_SIDE);
            return 0;
        }
        lu_v args[2] = { slots(c)[oparg], lu_ev_peek(c, 1) };
        lu_ev_helper(c, H_cell_store, 2, args, 0);
        lu_ev_pop(c);
        return 0;
    }
    case LOAD_SPECIAL:
        /* owner -- attr, NULL */
        v = lu_ev_const_i(c, oparg);
        stack_helper(c, H_load_special, 1, &v, 1);
        lu_ev_push(c, LU_NOV);
        return 0;
    case IMPORT_NAME: {
        /* level, fromlist -- module: the import system, called */
        lu_v extra[2] = { lu_ev_konst(c, name_at(c, oparg)), lu_ev_konst(c, c->globals) };
        stack_helper(c, H_import_name, 2, extra, 2);
        return 0;
    }
    case IMPORT_FROM: {
        /* module -- module, attr */
        lu_v args[2] = { lu_ev_peek(c, 1), lu_ev_konst(c, name_at(c, oparg)) };
        lu_ev_push(c, lu_ev_helper(c, H_import_from, 2, args, 0));
        return 0;
    }
    case LOAD_SUPER_ATTR: {
        /* global_super, class, self -- attr, NULL in the method form */
        if (super_known(c, oparg)) {
            return 0;
        }
        lu_v extra[2] = { lu_ev_konst(c, name_at(c, oparg >> 2)), lu_ev_const_i(c, oparg & 2) };
        stack_helper(c, H_super_attr, 3, extra, 2);
        if (oparg & 1) {
            lu_ev_push(c, LU_NOV);
        }
        return 0;
    }
    case RAISE_VARARGS:
        return lu_ev_raise(c);
    case PUSH_EXC_INFO:
        return lu_ev_push_exc_info(c);
    case POP_EXCEPT:
        return lu_ev_pop_except(c);
    case CHECK_EXC_MATCH:
        return lu_ev_check_exc_match(c);
    case YIELD_VALUE:
        if (lu_ev_in_gen(c)) {
            return lu_ev_gen_yield(c);
        }
        if (c->parent != NULL) {
            lu_ev_side_exit(c, EXIT_SIDE);
            return 0;
        }
        return yield_value(c);
    default:
        lu_ev_side_exit(c, EXIT_SIDE);
        return 0;
    }
}
