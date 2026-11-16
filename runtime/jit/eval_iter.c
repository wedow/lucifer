/* Iteration (eval.h): GET_ITER and FOR_ITER. A loop over an exact list
   or tuple has an iterator that does not exist: the sequence and an index,
   which a loop header turns into a phi. A range's iterator is stepped in
   place. Anything else is stepped as the interpreter steps it. */
#include "jit/eval.h"

int
lu_ev_get_iter(struct ctx *c)
{
    lu_v v = lu_ev_peek(c, 1);
    PyTypeObject *t = lu_ev_type(c, v);
    if (t == &PyGen_Type) {
        return 0;               /* a generator is its own iterator */
    }
    int base, oparg, op;
    if (t == NULL && !LU_IS_VIRT(v) && c->next < c->ncode && !nospec(c, c->pc)
        && (lu_ev_decode(c, c->next, &base, &oparg, &op), base == FOR_ITER)) {
        /* The sequence the loop's iterators came from, when it met one kind. */
        int at = c->next;
        while (lu_base_opcode(c->code, at) == EXTENDED_ARG) {
            at++;
        }
        int seen = __atomic_load_n(&(lu_code_start(c->code) + at + 1)->cache, __ATOMIC_RELAXED);
        t = seen == LU_ITER_TUPLE ? &PyTuple_Type : seen == LU_ITER_LIST ? &PyList_Type : NULL;
        if (t != NULL) {
            lu_ev_guard_type(c, v, t);
        }
    }
    lu_v w;
    if (t == &PyTuple_Type || t == &PyList_Type) {
        struct lu_vobj *o;
        lu_v seq = lu_ev_val(c, v);
        w = lu_ev_virt_new(c, VK_SEQITER, t == &PyTuple_Type ? &PyTupleIter_Type : &PyListIter_Type, 2, &o);
        o->f[0] = seq;
        o->f[1] = lu_ev_const_i(c, 0);
    }
    else {
        int done = lu_ev_kernel_op(c, LU_K_get_iter, &v, 1, 1, LU_RETURN_PUSH);
        if (done != 0) {
            return done < 0 ? -1 : 0;
        }
        w = lu_ev_helper(c, H_get_iter, 1, &v, 1);
    }
    lu_ev_pop(c);
    lu_ev_push(c, w);
    return 0;
}

/* The step of an iterator that does not exist: past the end the loop is
   left; else the item is read and the iterator is the same one, an index
   on. A list's size is read each time, so its changes in the loop show. */
static int
virtual_step(struct ctx *c, lu_v it, struct lu_vobj *o, int exit_pc)
{
    struct gctx *g = c->g;
    int tuple = o->type == &PyTupleIter_Type;
    lu_v seq = o->f[0], index = o->f[1];
    PyObject *k = lu_ev_const(c, seq);
    if (k != NULL && PyTuple_CheckExact(k) && PyTuple_GET_SIZE(k) == 0) {
        lu_ev_fork(c, lu_ev_const_i(c, 1), exit_pc);    /* nothing to step over: the loop is not built */
        return 0;
    }
    lu_v size = k != NULL && PyTuple_CheckExact(k) ? lu_ev_const_i(c, PyTuple_GET_SIZE(k))
        : lu_ev_emit(c, OP_LOAD, REP_I64, seq, LU_NOV, LU_NOV, offsetof(PyVarObject, ob_size));
    lu_ev_fork(c, lu_ev_emit(c, OP_CMP_I, REP_BIT, index, size, LU_NOV, Py_GE), exit_pc);
    if (g->cur < 0) {
        return 0;
    }
    lu_v item = lu_ev_emit(c, OP_LIST_ITEM, REP_OBJ, seq, index, LU_NOV, tuple);
    I(c, item)->fs = lu_ev_exit_fs(c);
    lu_ev_own(c, item);
    lu_v next = lu_ev_emit(c, OP_ADD_I, REP_I64, index, lu_ev_const_i(c, 1), LU_NOV, 0);
    I(c, next)->fs = lu_ev_exit_fs(c);
    struct lu_vobj *stepped = lu_ir_vobj(g->f, VK_SEQITER, o->type, 2);
    stepped->f[0] = seq;
    stepped->f[1] = next;
    lu_ev_bind(g, &g->st, LU_VIRT_ID(it), LU_NOV, stepped);
    lu_ev_push(c, item);
    return 0;
}

int
lu_ev_for_iter(struct ctx *c)
{
    lu_v it = lu_ev_peek(c, 1);
    int exit_pc = lu_ev_jump_target(FOR_ITER, c->oparg, c->next);
    if (LU_IS_VIRT(it)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, it, &real);
        if (o != NULL && real == LU_NOV && o->kind == VK_SEQITER) {
            return virtual_step(c, it, o, exit_pc);
        }
        if (o != NULL && real == LU_NOV && o->kind == VK_GEN) {
            int done = lu_ev_gen_step(c, it, exit_pc);
            if (done != 0) {
                return done < 0 ? -1 : 0;
            }
        }
    }
    int kind = nospec(c, c->pc) ? LU_ITER_OTHER : lu_ev_feedback(c, c->cache);
    PyTypeObject *t = lu_ev_type(c, it);
    if (t != NULL) {
        kind = t == &PyRangeIter_Type ? LU_ITER_RANGE : t == &PyListIter_Type ? LU_ITER_LIST
            : t == &PyTupleIter_Type ? LU_ITER_TUPLE : LU_ITER_OTHER;
    }
    lu_v v;
    if (kind == LU_ITER_RANGE) {
        lu_ev_guard_type(c, it, &PyRangeIter_Type);
        lu_v obj = lu_ev_val(c, it);
        lu_ev_fork(c, lu_ev_emit(c, OP_RITER_DONE, REP_BIT, obj, LU_NOV, LU_NOV, 0), exit_pc);
        if (c->g->cur < 0) {
            return 0;
        }
        v = lu_ev_box_int(c, lu_ev_emit(c, OP_RITER_NEXT, REP_I64, obj, LU_NOV, LU_NOV, 0));
    }
    else if (kind == LU_ITER_LIST || kind == LU_ITER_TUPLE) {
        lu_ev_guard_type(c, it, kind == LU_ITER_LIST ? &PyListIter_Type : &PyTupleIter_Type);
        v = lu_ev_helper(c, H_seqiter_next, 1, &it, 0);         /* NULL at the end */
        lu_ev_fork(c, lu_ev_emit(c, OP_IS, REP_BIT, v, lu_ev_konst(c, NULL), LU_NOV, 0), exit_pc);
    }
    else {
        /* Any other iterator but a generator (resumed, not called): the
           kernel's step in line, MISSING at the end. */
        int done = lu_ev_learn_type(c, it, 0) != &PyGen_Type
            ? lu_ev_kernel_op(c, LU_K_next_item, &it, 1, 0, LU_RETURN_FOR_ITER) : 0;
        if (done < 0) {
            return -1;
        }
        if (done && c->g->cur >= 0 && c->g->ret_missing) {
            v = lu_ev_peek(c, 1);
            lu_ev_pop(c);
            lu_ev_fork(c, lu_ev_emit(c, OP_IS, REP_BIT, lu_ev_val(c, v), lu_ev_konst(c, LU_MISSING), LU_NOV, 0), exit_pc);
            if (c->g->cur >= 0) {
                lu_ev_push(c, v);
            }
        }
        if (done) {
            return 0;
        }
        lu_v args[2] = { it, lu_ev_const_i(c, 0) };
        v = lu_ev_helper(c, H_for_iter, 2, args, 0);            /* MISSING at the end */
        lu_ev_fork(c, lu_ev_emit(c, OP_IS, REP_BIT, v, lu_ev_konst(c, LU_MISSING), LU_NOV, 0), exit_pc);
    }
    if (c->g->cur >= 0) {
        lu_ev_push(c, v);
    }
    return 0;
}
