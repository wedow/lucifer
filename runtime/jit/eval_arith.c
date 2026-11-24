/* Operators (eval.h): BINARY_OP, COMPARE_OP and the unary ones. Where
   the operands are ints or floats, known or met so far by the site, the
   arithmetic is raw and its result a box that does not exist; anything
   else is the interpreter's own path, called. */
#include "jit/eval.h"

int
lu_ev_feedback(struct ctx *c, lu_codeunit *word)
{
    int kinds = __atomic_load_n(&word->cache, __ATOMIC_RELAXED);
    /* Whether the interpreter has run the unit's own code is what puts a
       compile off: an inlined callee's sites say nothing of it. */
    if (c->parent == NULL) {
        c->g->f->sites++;
        c->g->f->cold += kinds == 0;
    }
    return kinds;
}

/* A constant int too wide for a machine word is no machine int. */
static int
wide_const(struct ctx *c, lu_v r)
{
    PyObject *k = lu_ev_const(c, r);
    if (k == NULL || !PyLong_CheckExact(k)) {
        return 0;
    }
    int overflow;
    long long x = PyLong_AsLongLongAndOverflow(k, &overflow);
    return overflow || x == INT64_MIN;
}

/* The operands' kinds: what is known of them, else what the site met, an
   unmet side taking the other's when that is a number. */
static void
operand_kinds(struct ctx *c, lu_v a, lu_v b, int kinds, int *lk, int *rk)
{
    *lk = kinds & 0xff;
    *rk = kinds >> 8;
    if (*lk == 0 && (*rk == LU_KIND_INT || *rk == LU_KIND_FLOAT)) *lk = *rk;
    if (*rk == 0 && (*lk == LU_KIND_INT || *lk == LU_KIND_FLOAT)) *rk = *lk;
    PyTypeObject *ta = lu_ev_type(c, a), *tb = lu_ev_type(c, b);
    static const struct { PyTypeObject *t; int kind; } kind_types[] = {
        { &PyFloat_Type, LU_KIND_FLOAT }, { &PyList_Type, LU_KIND_LIST }, { &PyTuple_Type, LU_KIND_TUPLE },
        { &PyUnicode_Type, LU_KIND_STR }, { &PyBytes_Type, LU_KIND_BYTES },
    };
    if (ta != NULL) *lk = ta == &PyLong_Type && !wide_const(c, a) ? LU_KIND_INT : LU_KIND_OTHER;
    if (tb != NULL) *rk = tb == &PyLong_Type && !wide_const(c, b) ? LU_KIND_INT : LU_KIND_OTHER;
    for (size_t i = 0; i < sizeof kind_types / sizeof kind_types[0]; i++) {
        if (ta == kind_types[i].t) *lk = kind_types[i].kind;
        if (tb == kind_types[i].t) *rk = kind_types[i].kind;
    }
    if (a == LU_NOV || b == LU_NOV || nospec(c, c->pc)) {
        /* A site whose guess kept failing takes the generic operation,
           whatever is known of its operands (an int shift past a word). */
        *lk = *rk = LU_KIND_OTHER;
    }
}

/* The unit, for the operator helper to count its passes in, when a or b is
   of a class the build does not know: the helper then stands where a build
   for their classes would have its way. Else 0. */
static lu_v
generic(struct ctx *c, lu_v a, lu_v b)
{
    return lu_ev_type(c, a) == NULL || lu_ev_type(c, b) == NULL ? lu_ev_unit(c) : lu_ev_const_i(c, 0);
}

static lu_v
int_op(struct ctx *c, int op, lu_v a, lu_v b)
{
    static const int ops[] = {
        [NB_ADD] = OP_ADD_I, [NB_SUBTRACT] = OP_SUB_I, [NB_MULTIPLY] = OP_MUL_I,
        [NB_AND] = OP_AND_I, [NB_OR] = OP_OR_I, [NB_XOR] = OP_XOR_I,
        [NB_LSHIFT] = OP_SHL_I, [NB_RSHIFT] = OP_SAR_I, [NB_FLOOR_DIVIDE] = OP_FLOORDIV_I,
        [NB_REMAINDER] = OP_MOD_I,
    };
    if (op < 0 || op >= (int)(sizeof ops / sizeof ops[0]) || ops[op] == 0) {
        return LU_NOV;
    }
    if (I(c, a)->op == OP_CONST_I && I(c, b)->op == OP_CONST_I) {
        int64_t x = I(c, a)->imm, y = I(c, b)->imm, z = 0;
        int bad = 0;
        switch (op) {
        case NB_ADD: bad = __builtin_add_overflow(x, y, &z); break;
        case NB_SUBTRACT: bad = __builtin_sub_overflow(x, y, &z); break;
        case NB_MULTIPLY: bad = __builtin_mul_overflow(x, y, &z); break;
        case NB_AND: z = x & y; break;
        case NB_OR: z = x | y; break;
        case NB_XOR: z = x ^ y; break;
        case NB_RSHIFT: bad = y < 0 || y > 63; z = bad ? 0 : x >> y; break;
        default: bad = 1; break;
        }
        if (!bad) {
            return lu_ev_const_i(c, z);
        }
    }
    lu_v r = lu_ev_emit(c, ops[op], REP_I64, a, b, LU_NOV, 0);
    if (lu_op_flags[ops[op]] & IF_GUARD) {
        I(c, r)->fs = lu_ev_exit_fs(c);
    }
    return r;
}

/* The operand as a double: a float's value, or an int's when the double
   is exact (below 2**53; any other leaves). */
static lu_v
as_double(struct ctx *c, lu_v r, int kind)
{
    if (kind == LU_KIND_FLOAT) {
        return lu_ev_unbox_float(c, r);
    }
    lu_v i = lu_ev_unbox_int(c, r);
    lu_v d = lu_ev_emit(c, OP_I2F, REP_F64, i, LU_NOV, LU_NOV, 1);
    I(c, d)->fs = lu_ev_exit_fs(c);
    return d;
}

static lu_v
float_op(struct ctx *c, int op, lu_v a, lu_v b, int lk, int rk)
{
    int ir = op == NB_ADD ? OP_ADD_F : op == NB_SUBTRACT ? OP_SUB_F : op == NB_MULTIPLY ? OP_MUL_F
        : op == NB_TRUE_DIVIDE ? OP_DIV_F : -1;
    if (ir < 0) {
        return LU_NOV;
    }
    lu_v x = as_double(c, a, lk), y = as_double(c, b, rk);
    lu_v r = lu_ev_emit(c, ir, REP_F64, x, y, LU_NOV, 0);
    if (lu_op_flags[ir] & IF_GUARD) {
        I(c, r)->fs = lu_ev_exit_fs(c);         /* a zero divisor: the interpreter raises */
    }
    return lu_ev_box_float(c, r);
}

/* a[b] where the state or the site says how. */
static lu_v
subscript(struct ctx *c, lu_v a, lu_v b, int lk, int rk)
{
    PyObject *ka = lu_ev_const(c, a), *kb = lu_ev_const(c, b);
    Py_ssize_t i = kb != NULL && PyLong_CheckExact(kb) ? PyLong_AsSsize_t(kb) : PY_SSIZE_T_MAX;
    PyErr_Clear();
    if (LU_IS_VIRT(b) && lu_ev_type(c, b) == &PyLong_Type) {
        lu_v x = lu_ev_unbox_int(c, b);         /* an int the code computed: its value, when that is decided */
        i = I(c, x)->op == OP_CONST_I ? (Py_ssize_t)I(c, x)->imm : i;
    }
    if (ka != NULL && PyTuple_CheckExact(ka) && i != PY_SSIZE_T_MAX && i >= -PyTuple_GET_SIZE(ka)
        && i < PyTuple_GET_SIZE(ka)) {
        return lu_ev_konst(c, PyTuple_GET_ITEM(ka, i < 0 ? i + PyTuple_GET_SIZE(ka) : i));
    }
    if (LU_IS_VIRT(a) && i != PY_SSIZE_T_MAX) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, a, &real);
        if (o != NULL && o->kind == VK_TUPLE && real == LU_NOV && i >= -o->n && i < o->n) {
            return o->f[i < 0 ? i + o->n : i];
        }
    }
    if ((lk == LU_KIND_LIST || lk == LU_KIND_TUPLE) && rk == LU_KIND_INT) {
        lu_ev_guard_type(c, a, lk == LU_KIND_LIST ? &PyList_Type : &PyTuple_Type);
        lu_v index = lu_ev_unbox_int(c, b);
        lu_v r = lu_ev_emit(c, OP_LIST_ITEM, REP_OBJ, lu_ev_val(c, a), index, LU_NOV, lk == LU_KIND_TUPLE);
        I(c, r)->fs = lu_ev_exit_fs(c);
        return lu_ev_own(c, r);
    }
    if (lk == LU_KIND_BYTES && rk == LU_KIND_INT) {
        lu_ev_guard_type(c, a, &PyBytes_Type);
        lu_v byte = lu_ev_emit(c, OP_BYTES_ITEM, REP_I64, lu_ev_val(c, a), lu_ev_unbox_int(c, b), LU_NOV, 0);
        I(c, byte)->fs = lu_ev_exit_fs(c);
        return lu_ev_box_int(c, byte);
    }
    if (lk == LU_KIND_OTHER && rk == LU_KIND_INT && !nospec(c, c->pc)
        && lu_ev_learn_type(c, a, 0) == &PyByteArray_Type) {
        /* A bytearray's byte, read under its lock as an array of 'B'. */
        lu_v byte = lu_ev_emit(c, OP_TYPED_LOAD, REP_I64, lu_ev_val(c, a), lu_ev_unbox_int(c, b), LU_NOV, 'B');
        I(c, byte)->fs = lu_ev_exit_fs(c);
        return lu_ev_box_int(c, byte);
    }
    return LU_NOV;
}

/* An operand the site met as an int at times and as a float at others,
   and nothing else, with its type not known here: the instruction is
   built both ways, switched on the operand's type where it runs (an int
   that turned float in a sum, a parameter given either). LU_NOV when
   neither operand is one. */
static lu_v
int_or_float(struct ctx *c, lu_v a, lu_v b, int lk, int rk)
{
    int both = LU_KIND_INT | LU_KIND_FLOAT;
    lu_v v = lk == both ? a : rk == both ? b : LU_NOV;
    return v >= 0 && I(c, v)->rep == REP_OBJ ? v : LU_NOV;
}

static int
both_ways(struct ctx *c, lu_v v, int (*build)(struct ctx *))
{
    lu_ev_guard(c, OP_GUARD_NOT_NULL, v, 0);    /* an unbound local: the interpreter raises */
    lu_v type = lu_ev_emit(c, OP_LOAD, REP_OBJ, v, LU_NOV, LU_NOV, offsetof(PyObject, ob_type));
    lu_v is = lu_ev_emit(c, OP_IS, REP_BIT, type, lu_ev_konst(c, (PyObject *)&PyFloat_Type), LU_NOV, 0);
    struct split s;
    lu_ev_split(c, is, &s);
    lu_ev_set_type(c, v, &PyFloat_Type);
    if (build(c) < 0) {
        return -1;
    }
    lu_ev_split_else(c, &s);
    lu_ev_guard_type(c, v, &PyLong_Type);
    if (build(c) < 0) {
        return -1;
    }
    lu_ev_split_end(c, &s);
    return 0;
}

int
lu_ev_binary_op(struct ctx *c)
{
    lu_v b = lu_ev_peek(c, 1), a = lu_ev_peek(c, 2);
    int lk, rk, op = c->oparg;
    operand_kinds(c, a, b, lu_ev_feedback(c, c->cache + 1), &lk, &rk);
    if (op != NB_SUBSCR && int_or_float(c, a, b, lk, rk) != LU_NOV) {
        return both_ways(c, int_or_float(c, a, b, lk, rk), lu_ev_binary_op);
    }
    int base = op >= NB_INPLACE_ADD && op != NB_SUBSCR ? op - NB_INPLACE_ADD : op;
    int num_l = lk == LU_KIND_INT || lk == LU_KIND_FLOAT, num_r = rk == LU_KIND_INT || rk == LU_KIND_FLOAT;
    lu_v r = LU_NOV;
    if (op == NB_SUBSCR) {
        r = subscript(c, a, b, lk, rk);
    }
    else if (lk == LU_KIND_INT && rk == LU_KIND_INT && base != NB_TRUE_DIVIDE) {
        lu_v z = int_op(c, base, lu_ev_unbox_int(c, a), lu_ev_unbox_int(c, b));
        r = z != LU_NOV ? lu_ev_box_int(c, z) : LU_NOV;
    }
    else if (num_l && num_r) {
        r = float_op(c, base, a, b, lk, rk);
    }
    else if (lk == rk && base == NB_ADD && (lk == LU_KIND_STR || lk == LU_KIND_BYTES)) {
        PyTypeObject *t = lk == LU_KIND_STR ? &PyUnicode_Type : &PyBytes_Type;
        lu_ev_guard_type(c, a, t);
        lu_ev_guard_type(c, b, t);
        lu_v args[2] = { a, b };
        r = lu_ev_helper(c, lk == LU_KIND_STR ? H_str_concat : H_bytes_concat, 2, args, 2);
        lu_ev_def_type(c, r, t);
    }
    else if (lk == LU_KIND_LIST && rk == LU_KIND_INT && op == NB_MULTIPLY) {
        lu_ev_guard_type(c, a, &PyList_Type);
        lu_v args[2] = { a, lu_ev_unbox_int(c, b) };
        r = lu_ev_helper(c, H_list_repeat, 2, args, 2);
        lu_ev_def_type(c, r, &PyList_Type);
    }
    else if (lk == rk && op == NB_ADD && (lk == LU_KIND_LIST || lk == LU_KIND_TUPLE)) {
        PyTypeObject *t = lk == LU_KIND_LIST ? &PyList_Type : &PyTuple_Type;
        lu_ev_guard_type(c, a, t);
        lu_ev_guard_type(c, b, t);
        lu_v args[2] = { a, b };
        r = lu_ev_helper(c, H_seq_concat, 2, args, 2);
        lu_ev_def_type(c, r, t);
    }
    if (r == LU_NOV) {
        lu_v kargs[3] = { a, b, lu_ev_konst(c, lu_small_int(op)) };
        int done = lu_ev_kernel_arms(c, kargs, 2);
        done = done != 0 ? done : lu_ev_kernel_op(c, op == NB_SUBSCR ? LU_K_getitem : LU_K_binary_op, kargs,
                                                  op == NB_SUBSCR ? 2 : 3, 2, LU_RETURN_PUSH);
        if (done != 0) {
            return done < 0 ? -1 : 0;
        }
        lu_v args[4] = { a, b, lu_ev_const_i(c, op), generic(c, a, b) };
        r = lu_ev_helper(c, H_binary_op, 4, args, 2);
    }
    lu_ev_pop(c);
    lu_ev_pop(c);
    lu_ev_push(c, r);
    return 0;
}

int
lu_ev_compare_op(struct ctx *c)
{
    lu_v b = lu_ev_peek(c, 1), a = lu_ev_peek(c, 2);
    int lk, rk, op = c->oparg >> 5;
    operand_kinds(c, a, b, lu_ev_feedback(c, c->cache), &lk, &rk);
    if (int_or_float(c, a, b, lk, rk) != LU_NOV) {
        return both_ways(c, int_or_float(c, a, b, lk, rk), lu_ev_compare_op);
    }
    lu_v bit = LU_NOV, r;
    if (lk == LU_KIND_INT && rk == LU_KIND_INT) {
        lu_v x = lu_ev_unbox_int(c, a), y = lu_ev_unbox_int(c, b);
        if (I(c, x)->op == OP_CONST_I && I(c, y)->op == OP_CONST_I) {
            int64_t p = I(c, x)->imm, q = I(c, y)->imm;
            bit = lu_ev_const_i(c, op == Py_LT ? p < q : op == Py_LE ? p <= q : op == Py_EQ ? p == q
                                : op == Py_NE ? p != q : op == Py_GT ? p > q : p >= q);
        }
        else {
            bit = lu_ev_emit(c, OP_CMP_I, REP_BIT, x, y, LU_NOV, op);
        }
    }
    else if ((lk == LU_KIND_FLOAT || lk == LU_KIND_INT) && (rk == LU_KIND_FLOAT || rk == LU_KIND_INT)) {
        /* An int against a float compares exactly: as doubles only where
           the int's double is exact. */
        bit = lu_ev_emit(c, OP_CMP_F, REP_BIT, as_double(c, a, lk), as_double(c, b, rk), LU_NOV, op);
    }
    if (bit != LU_NOV) {
        r = lu_ev_bool(c, bit, 0);
    }
    else {
        lu_v kargs[3] = { a, b, lu_ev_konst(c, lu_small_int(c->oparg)) };
        int done = lu_ev_kernel_arms(c, kargs, 2);
        done = done != 0 ? done : lu_ev_kernel_op(c, LU_K_compare, kargs, 3, 2, LU_RETURN_PUSH);
        if (done != 0) {
            return done < 0 ? -1 : 0;
        }
        lu_v args[4] = { a, b, lu_ev_const_i(c, c->oparg), generic(c, a, b) };
        r = lu_ev_helper(c, H_compare_op, 4, args, 2);
    }
    lu_ev_pop(c);
    lu_ev_pop(c);
    lu_ev_push(c, r);
    return 0;
}

int
lu_ev_unary(struct ctx *c)
{
    lu_v v = lu_ev_peek(c, 1), w = LU_NOV;
    PyTypeObject *t = lu_ev_type(c, v);
    int neg = c->base == UNARY_NEGATIVE;
    if (t == &PyFloat_Type && neg) {
        w = lu_ev_box_float(c, lu_ev_emit(c, OP_NEG_F, REP_F64, lu_ev_unbox_float(c, v), LU_NOV, LU_NOV, 0));
    }
    else if (t == &PyLong_Type && neg && !wide_const(c, v)) {
        lu_v x = lu_ev_emit(c, OP_NEG_I, REP_I64, lu_ev_unbox_int(c, v), LU_NOV, LU_NOV, 0);
        I(c, x)->fs = lu_ev_exit_fs(c);
        w = lu_ev_box_int(c, x);
    }
    else {
        lu_v args[2] = { v, lu_ev_const_i(c, !neg) };
        w = lu_ev_helper(c, H_unary, 2, args, 1);
    }
    lu_ev_pop(c);
    lu_ev_push(c, w);
    return 0;
}
