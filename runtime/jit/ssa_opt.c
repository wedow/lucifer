/* The optimizer's passes over block SSA (DESIGN 6.2 step 3), in order:

   1. Trivial phis go (a phi of one value, or of itself and one value).
      Then facts: guards leave loops, repeated guards go, and so do loads
      of what a load in the block already read (1b).
   2. Representation: a phi whose inputs are all int boxes (or float
      boxes), counting phis of the same kind, becomes a raw phi under one
      box, so loop-carried ints and floats stay in registers.
   3. Simplification: an unbox of a box is its raw value; guards on values
      whose type a box proves fold away.
   4. Escape analysis (the donor's scalar replacement, for boxes): a box
      whose every use is an unbox, a frame state, a release or a slot write
      is virtual. It is never built in compiled code; an exit that needs it
      builds it, once (runtime/jit/deopt.c). A box a Python-running call
      could observe through its frame is materialized. So is a new dict
      (an inlined callee's **kwargs) whose uses are those or truth tests,
      which are false.
   5. References: the releases and slot writes of virtual boxes go, and so
      do the reference operations on immortal objects.
   6. Dead code: unused instructions without effects.
   7. Critical edges are split and blocks get their emission order. */
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>

static __thread lu_v *fwd;      /* the pass in progress on this thread */

static lu_v
resolve(lu_v v)
{
    if (v == LU_NOV) {
        return v;
    }
    lu_v r = v;
    while (fwd[r] != r) {
        r = fwd[r];
    }
    while (fwd[v] != r) {
        lu_v n = fwd[v];
        fwd[v] = r;
        v = n;
    }
    return r;
}

static void
rewrite_fs(struct lu_fs *fs, char *seen, struct lu_func *f)
{
    for (; fs != NULL; fs = fs->caller) {
        for (int i = 0; i < fs->nlocals + fs->nstack; i++) {
            fs->vals[i] = resolve(fs->vals[i]);
        }
    }
}

/* Applies the forwarding map to every operand. */
static void
rewrite(struct lu_func *f)
{
    for (int v = 0; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        in->a = resolve(in->a);
        in->b = resolve(in->b);
        in->c = resolve(in->c);
        for (int i = 0; i < in->nargs; i++) {
            in->args[i] = resolve(in->args[i]);
        }
        if (in->fs != NULL) {
            rewrite_fs(in->fs, NULL, f);
        }
    }
}

static void
kill(struct lu_func *f, lu_v v, lu_v to)
{
    f->ins[v].flags |= INS_DEAD;
    fwd[v] = to;
}

/* 1. */
static int
trivial_phis(struct lu_func *f)
{
    int changed = 0;
    for (int v = 0; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->op != OP_PHI || (in->flags & INS_DEAD)) {
            continue;
        }
        lu_v same = LU_NOV;
        int trivial = 1;
        for (int i = 0; i < in->nargs; i++) {
            lu_v a = resolve(in->args[i]);
            if (a == v || a == same) {
                continue;
            }
            if (same != LU_NOV) {
                trivial = 0;
                break;
            }
            same = a;
        }
        if (trivial && same != LU_NOV) {
            kill(f, v, same);
            changed = 1;
        }
    }
    return changed;
}

/* The boxes some use makes real (an operand of anything but an unbox, a
   phi, a reference operation or a slot write): such a box is an object
   that may be observed later, so a phi of it must stay that object. */
static char *
escaping_boxes(struct lu_func *f, int box)
{
    char *esc = calloc((size_t)f->nins, 1);
    for (int v = 0; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        int benign = in->op == OP_STORE_LOCAL || in->op == OP_DECREF || in->op == OP_INCREF
            || in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT || in->op == OP_GUARD_NOT_NULL
            || in->op == OP_PHI;
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3; i++) {
            lu_v a = resolve(ops[i]);
            if (a != LU_NOV && f->ins[a].op == box && !(benign && i == 0) && !(in->op == box && i == 1)) {
                esc[a] = 1;
            }
        }
        for (int i = 0; in->op != OP_PHI && i < in->nargs; i++) {
            lu_v a = resolve(in->args[i]);
            if (a != LU_NOV && f->ins[a].op == box) {
                esc[a] = 1;
            }
        }
    }
    return esc;
}

/* A constant placed first in the entry block. */
static lu_v
entry_const(struct lu_func *f, int op, int rep, int64_t imm)
{
    lu_v k = lu_ir_emit(f, 0, op, rep, LU_NOV, LU_NOV, LU_NOV, imm);
    struct lu_block *bl = &f->blocks[0];
    memmove(&bl->ins[1], &bl->ins[0], sizeof(int) * (size_t)(bl->nins - 1));
    bl->ins[0] = k;
    return k;
}

/* 2. */
static void
unbox_phis(struct lu_func *f, int rep)
{
    int box = rep == REP_I64 ? OP_BOX_INT : OP_BOX_FLOAT;
    int n = f->nins;
    char *cand = calloc((size_t)n, 1);
    char *esc = escaping_boxes(f, box);
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        cand[v] = in->op == OP_PHI && in->rep == REP_OBJ && !(in->flags & INS_DEAD);
    }
    /* A NULL input (a local unbound on that path) makes the phi's box one
       that stands for NULL when a bit phi beside the raw one says so. */
    char *maybe_null = calloc((size_t)n, 1);
    for (int changed = 1; changed;) {
        changed = 0;
        for (int v = 0; v < n; v++) {
            if (!cand[v]) {
                continue;
            }
            struct lu_ins *in = &f->ins[v];
            for (int i = 0; i < in->nargs; i++) {
                lu_v a = resolve(in->args[i]);
                if (a == v) {
                    continue;
                }
                /* A box made from an object holds the object's reference unless a
                   root slot does: only such phis may forget it. */
                if (f->ins[a].op == box && f->ins[a].b != LU_NOV && !in->imm2) {
                    cand[v] = 0;
                    changed = 1;
                    break;
                }
                if (f->ins[a].op == OP_CONST && f->ins[a].imm == 0 && in->imm2) {
                    changed |= !maybe_null[v];
                    maybe_null[v] = 1;
                    continue;
                }
                if (a < n && cand[a] && maybe_null[a] && !maybe_null[v]) {
                    maybe_null[v] = 1;
                    changed = 1;
                }
                if ((!(f->ins[a].op == box) || esc[a]) && !(a < n && cand[a])) {
                    cand[v] = 0;
                    changed = 1;
                    break;
                }
            }
        }
        /* Such a box is only ever built at an exit or released: a phi that
           may be NULL keeps its object form for any other use. */
        for (int u = 0; u < n; u++) {
            struct lu_ins *in = &f->ins[u];
            if (in->flags & INS_DEAD) {
                continue;
            }
            int benign = in->op == OP_DECREF || in->op == OP_INCREF || in->op == OP_STORE_LOCAL;
            lu_v ops[3] = { in->a, in->b, in->c };
            for (int i = 0; i < 3; i++) {
                lu_v a = resolve(ops[i]);
                if (a != LU_NOV && a < n && cand[a] && maybe_null[a] && !(benign && i == 0)) {
                    cand[a] = 0;
                    changed = 1;
                }
            }
            for (int i = 0; i < in->nargs; i++) {
                lu_v a = resolve(in->args[i]);
                if (a != LU_NOV && a < n && cand[a] && maybe_null[a] && !(in->op == OP_PHI && cand[u])) {
                    cand[a] = 0;
                    changed = 1;
                }
            }
        }
    }
    /* Every candidate's inputs, resolved before any phi is replaced. */
    lu_v **inputs = calloc((size_t)n, sizeof(lu_v *));
    for (int v = 0; v < n; v++) {
        if (cand[v]) {
            struct lu_ins *in = &f->ins[v];
            inputs[v] = malloc(sizeof(lu_v) * (size_t)(in->nargs + 1));
            for (int i = 0; i < in->nargs; i++) {
                inputs[v][i] = resolve(in->args[i]);
            }
        }
    }
    lu_v *raw = malloc(sizeof(lu_v) * (size_t)n);
    lu_v *bit = malloc(sizeof(lu_v) * (size_t)n);
    lu_v zero = LU_NOV, unbound = LU_NOV, bound = LU_NOV;
    for (int v = 0; v < n; v++) {
        if (cand[v]) {
            /* lu_ir_emit may move the instructions: no pointer into them is kept. */
            int block = f->ins[v].block;
            int64_t imm = f->ins[v].imm;
            raw[v] = lu_ir_emit(f, block, OP_PHI, rep, LU_NOV, LU_NOV, LU_NOV, imm);
            f->ins[raw[v]].nargs = f->ins[v].nargs;
            f->ins[raw[v]].args = malloc(sizeof(lu_v) * (size_t)f->ins[v].nargs);
            bit[v] = LU_NOV;
            if (maybe_null[v]) {
                bit[v] = lu_ir_emit(f, block, OP_PHI, REP_BIT, LU_NOV, LU_NOV, LU_NOV, imm);
                f->ins[bit[v]].nargs = f->ins[v].nargs;
                f->ins[bit[v]].args = malloc(sizeof(lu_v) * (size_t)f->ins[v].nargs);
                if (zero == LU_NOV) {
                    zero = entry_const(f, rep == REP_F64 ? OP_CONST_F : OP_CONST_I, rep, 0);
                    unbound = entry_const(f, OP_CONST_I, REP_I64, 0);
                    bound = entry_const(f, OP_CONST_I, REP_I64, 1);
                }
            }
        }
    }
    fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
    for (int v = n; v < f->nins; v++) {
        fwd[v] = v;
    }
    for (int v = 0; v < n; v++) {
        if (!cand[v]) {
            continue;
        }
        struct lu_ins *in = &f->ins[v];
        for (int i = 0; i < in->nargs; i++) {
            lu_v a = inputs[v][i];
            int null = a != v && !cand[a] && f->ins[a].op == OP_CONST;
            f->ins[raw[v]].args[i] = a == v ? raw[v] : cand[a] ? raw[a] : null ? zero : resolve(f->ins[a].a);
            if (bit[v] != LU_NOV) {
                f->ins[bit[v]].args[i] = a == v ? bit[v] : cand[a] ? (bit[a] != LU_NOV ? bit[a] : bound)
                    : null ? unbound : bound;
            }
        }
        free(inputs[v]);
        /* The box goes first after the phis. */
        int block = in->block;
        lu_v b = lu_ir_emit(f, block, box, REP_OBJ, raw[v], LU_NOV, bit[v], 0);
        f->ins[b].imm2 = f->ins[v].imm2 ? f->ins[v].imm + 1 : 0;     /* a root slot's value: its slot */
        struct lu_block *bl = &f->blocks[block];
        int at = 0;
        while (at < bl->nins - 1 && f->ins[bl->ins[at]].op == OP_PHI) {
            at++;
        }
        memmove(&bl->ins[at + 1], &bl->ins[at], sizeof(int) * (size_t)(bl->nins - 1 - at));
        bl->ins[at] = b;
        fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
        fwd[b] = b;
        kill(f, v, b);
    }
    free(raw);
    free(bit);
    free(maybe_null);
    free(inputs);
    free(cand);
    free(esc);
}

/* Whether v is an object of exact type t whatever path reached it: a
   box, a constant, a range iterator made here, or a phi of those. */
static int
proves_type(struct lu_func *f, lu_v v, PyTypeObject *t, int depth)
{
    struct lu_ins *in = &f->ins[v];
    switch (in->op) {
    case OP_BOX_INT: return t == &PyLong_Type;
    case OP_BOX_FLOAT: return t == &PyFloat_Type;
    case OP_CONST: return in->imm != 0 && Py_TYPE((PyObject *)(intptr_t)in->imm) == t;
    case OP_HELPER: return in->imm == H_range_iter && t == &PyRangeIter_Type;
    case OP_PHI:
        if (depth > 4) {
            return 0;
        }
        for (int i = 0; i < in->nargs; i++) {
            lu_v a = resolve(in->args[i]);
            if (a != v && !proves_type(f, a, t, depth + 1)) {
                return 0;
            }
        }
        return 1;
    default:
        return 0;
    }
}

/* 3. */
static int
simplify(struct lu_func *f)
{
    int changed = 0;
    for (int v = 0; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        struct lu_ins *a = in->a != LU_NOV ? &f->ins[resolve(in->a)] : NULL;
        switch (in->op) {
        case OP_UNBOX_INT:
            if (a->op == OP_BOX_INT) {
                kill(f, v, resolve(a->a));
                changed = 1;
            }
            break;
        case OP_UNBOX_FLOAT:
            if (a->op == OP_BOX_FLOAT) {
                kill(f, v, resolve(a->a));
                changed = 1;
            }
            break;
        case OP_GUARD_TYPE:
            if (proves_type(f, resolve(in->a), (PyTypeObject *)(intptr_t)in->imm, 0)) {
                kill(f, v, LU_NOV);
                changed = 1;
            }
            break;
        case OP_GUARD_NOT_NULL:
            if (a->op == OP_BOX_INT || a->op == OP_BOX_FLOAT || a->op == OP_BOOL
                || (a->op == OP_CONST && a->imm != 0)) {
                kill(f, v, LU_NOV);
                changed = 1;
            }
            break;
        case OP_GUARD_BIT:
            if (a->op == OP_CONST_I && a->imm == in->imm) {
                kill(f, v, LU_NOV);
                changed = 1;
            }
            break;
        case OP_NOT:
            if (a->op == OP_NOT) {
                kill(f, v, resolve(a->a));
                changed = 1;
            }
            break;
        case OP_IS: {
            /* x is x; bool(x) is True is x; a box is never None. */
            struct lu_ins *b = &f->ins[resolve(in->b)];
            if (resolve(in->a) == resolve(in->b)) {
                in->op = OP_CONST_I;
                in->imm = 1;
                in->a = in->b = LU_NOV;
                changed = 1;
            }
            else if (a->op == OP_BOOL && b->op == OP_CONST && b->imm == (int64_t)(intptr_t)Py_True) {
                kill(f, v, resolve(a->a));
                changed = 1;
            }
            else if ((a->op == OP_BOX_INT || a->op == OP_BOX_FLOAT) && b->op == OP_CONST
                     && b->imm != (int64_t)(intptr_t)NULL && !PyLong_CheckExact((PyObject *)(intptr_t)b->imm)
                     && !PyFloat_CheckExact((PyObject *)(intptr_t)b->imm)) {
                in->op = OP_CONST_I;
                in->imm = 0;
                in->a = in->b = LU_NOV;
                changed = 1;
            }
            break;
        }
        default:
            break;
        }
    }
    return changed;
}

/* 1b. Facts and value numbering (DESIGN 6.2 step 7). A guard checks a
   fact. Some facts only a writer's change takes back, and the writer kills
   the units that rely on them (runtime/jit/deps.c): a type's version (of a
   constant type, or of a value's type), a constant dict's keys or the value
   of one of its keys, a constant function's version. Others never change:
   a value's exact immutable type, that it is not NULL, a bit's value. A
   guard inside a loop on a value the loop does not define moves before the
   loop, to a preheader, where it leaves at the loop's entry state and
   blames its old site; one that a guard of the same fact dominates is
   redundant and goes. Where a fact a writer may take back is relied on
   again after something that may run code or change state, since the last
   check or the entry, a check that the unit lives (CHECK_LIVE) stays in
   its place: that may have been the running thread's own change, or a call
   whose polls took the bit another thread's change raised. A poll is no
   such check, since a call before it may have taken the bit. Each fact
   relied on is a dependency of the unit. A site whose fact was taken back
   before (lu_jit_nohoist) keeps its guards.

   Then loads of a word of an object (its type, size, an inline attribute)
   that a load of the same word earlier in the block made, with nothing
   between that may run code, change state or poll, are that load: heap
   values are kept only between polls. */

enum { NO_FACT, WRITER_FACT, FIXED_FACT };

/* What a guard checks: NO_FACT, or the kind of fact with the dependency a
   WRITER_FACT is, watched so that its writers report it. A constant's fact
   must hold now. */
static int
fact_of(struct lu_func *f, struct lu_ins *in, struct lu_fdep *dep)
{
    if (in->fs == NULL || in->a == LU_NOV || lu_jit_nohoist(in->fs->code, in->fs->pc)) {
        return NO_FACT;
    }
    if (in->op == OP_GUARD_NOT_NULL || in->op == OP_GUARD_BIT) {
        return FIXED_FACT;
    }
    if (in->op == OP_GUARD_TYPE) {
        return ((PyTypeObject *)(intptr_t)in->imm)->tp_flags & Py_TPFLAGS_IMMUTABLETYPE ? FIXED_FACT : NO_FACT;
    }
    *dep = (struct lu_fdep){ .code = in->fs->code, .pc = in->fs->pc };
    if (in->op == OP_GUARD_VERSION && in->imm2 == GV_TYPE_OF) {
        dep->kind = LU_DEP_TYPE;
        dep->obj = (uint32_t)in->imm;
        return WRITER_FACT;
    }
    if (f->ins[in->a].op != OP_CONST) {
        return NO_FACT;
    }
    PyObject *o = (PyObject *)(intptr_t)f->ins[in->a].imm;
    dep->obj = (uintptr_t)o;
    if (in->op == OP_GUARD_VERSION && in->imm2 == GV_TYPE_ITSELF) {
        dep->kind = LU_DEP_TYPE;
        dep->obj = (uint32_t)in->imm;
        return __atomic_load_n(&((PyTypeObject *)o)->tp_version_tag, __ATOMIC_ACQUIRE) == (uint32_t)in->imm
            ? WRITER_FACT : NO_FACT;
    }
    if (in->op == OP_GUARD_VERSION && in->imm2 == GV_FUNC) {
        dep->kind = LU_DEP_FUNC;
        return __atomic_load_n(&((PyFunctionObject *)o)->func_version, __ATOMIC_ACQUIRE) == (uint32_t)in->imm
            ? WRITER_FACT : NO_FACT;
    }
    int keys = in->op == OP_GUARD_VERSION && in->imm2 == GV_DICT_KEYS;
    int value = in->op == OP_GUARD_IS && GI_MODE(in->imm2) == 2;
    if (!keys && !value) {
        return NO_FACT;
    }
    /* Watched before it is read, so a change after the read is reported. */
    __atomic_fetch_or(&((PyDictObject *)o)->_ma_watcher_tag, 1, __ATOMIC_SEQ_CST);
    PyDictKeysObject *dk = __atomic_load_n(&((PyDictObject *)o)->ma_keys, __ATOMIC_ACQUIRE);
    if (keys) {
        dep->kind = LU_DEP_KEYS;
        return __atomic_load_n(&dk->dk_version, __ATOMIC_ACQUIRE) == (uint32_t)in->imm ? WRITER_FACT : NO_FACT;
    }
    dep->kind = LU_DEP_VALUE;
    dep->sub = (uint32_t)GI_OFFSET(in->imm2);
    return __atomic_load_n((PyObject **)((char *)dk + GI_OFFSET(in->imm2)), __ATOMIC_ACQUIRE)
        == (PyObject *)(intptr_t)in->imm ? WRITER_FACT : NO_FACT;
}

/* The value a guard checks: a constant's object, or the SSA value. */
static int64_t
subject(struct lu_func *f, struct lu_ins *in)
{
    lu_v a = resolve(in->a);
    return f->ins[a].op == OP_CONST ? f->ins[a].imm : -2 - (int64_t)a;
}

static int
same_fact(struct lu_func *f, struct lu_ins *x, struct lu_ins *y)
{
    return x->op == y->op && x->imm == y->imm && x->imm2 == y->imm2 && x->a != LU_NOV && y->a != LU_NOV
        && subject(f, x) == subject(f, y);
}

/* Whether releasing v cannot run a finalizer: v is an int, a float, a bool
   or an immortal object on every path. */
static int
plain(struct lu_func *f, lu_v v, int depth)
{
    struct lu_ins *in = &f->ins[resolve(v)];
    switch (in->op) {
    case OP_BOX_INT: case OP_BOX_FLOAT: case OP_BOOL:
        return 1;
    case OP_CONST:
        return in->imm == 0 || _Py_IsImmortal((PyObject *)(intptr_t)in->imm);
    case OP_PHI:
        for (int i = 0; depth < 4 && i < in->nargs; i++) {
            if (resolve(in->args[i]) != resolve(v) && !plain(f, in->args[i], depth + 1)) {
                return 0;
            }
        }
        return depth < 4;
    default:
        return 0;
    }
}

/* Whether an instruction may run code or change what a fact names. */
static int
may_change(struct lu_func *f, struct lu_ins *in)
{
    switch (in->op) {
    case OP_BOX_INT: case OP_BOX_FLOAT: case OP_LIST_ITEM: case OP_ATTR_LOAD: case OP_GLOBAL_LOAD:
        return 0;
    case OP_DECREF:
        return !plain(f, in->a, 0);
    case OP_HELPER:
        return !(in->imm == H_box_int || in->imm == H_box_float || in->imm == H_list_item
                 || in->imm == H_attr_load || in->imm == H_slot_load || in->imm == H_dict_lacks
                 || in->imm == H_cell_load || in->imm == H_compare_ideq || in->imm == H_range_iter);
    default:
        return (lu_op_flags[in->op] & IF_CALL) || in->op == OP_TRUTH;
    }
}

/* Blocks in reverse postorder from the entry, and immediate dominators
   (Cooper, Harvey and Kennedy); -1 for blocks not reached. */
struct cfg {
    int *rpo, n;
    int *at;                    /* block -> position in rpo, or -1 */
    int *idom;
};

static void
cfg_build(struct lu_func *f, struct cfg *g)
{
    int nb = f->nblocks;
    g->rpo = malloc(sizeof(int) * (size_t)nb);
    g->at = malloc(sizeof(int) * (size_t)nb);
    g->idom = malloc(sizeof(int) * (size_t)nb);
    int *post = malloc(sizeof(int) * (size_t)nb), np = 0;
    int *stack = malloc(sizeof(int) * (size_t)nb), *next = calloc((size_t)nb, sizeof(int)), sp = 0;
    for (int b = 0; b < nb; b++) {
        g->at[b] = -1;
        g->idom[b] = -1;
    }
    g->at[0] = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        int b = stack[sp - 1];
        if (next[b] < f->blocks[b].nsucc) {
            int s = f->blocks[b].succ[next[b]++];
            if (g->at[s] < 0) {
                g->at[s] = 0;
                stack[sp++] = s;
            }
        }
        else {
            post[np++] = b;
            sp--;
        }
    }
    g->n = np;
    for (int i = 0; i < np; i++) {
        g->rpo[i] = post[np - 1 - i];
        g->at[g->rpo[i]] = i;
    }
    g->idom[0] = 0;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 1; i < np; i++) {
            int b = g->rpo[i], d = -1;
            for (int k = 0; k < f->blocks[b].npreds; k++) {
                int p = f->blocks[b].preds[k];
                if (g->at[p] < 0 || g->idom[p] < 0) {
                    continue;
                }
                if (d < 0) {
                    d = p;
                    continue;
                }
                int x = p, y = d;
                while (x != y) {
                    while (g->at[x] > g->at[y]) {
                        x = g->idom[x];
                    }
                    while (g->at[y] > g->at[x]) {
                        y = g->idom[y];
                    }
                }
                d = x;
            }
            if (d != g->idom[b]) {
                g->idom[b] = d;
                changed = 1;
            }
        }
    }
    free(post);
    free(stack);
    free(next);
}

static void
cfg_free(struct cfg *g)
{
    free(g->rpo);
    free(g->at);
    free(g->idom);
}

static int
dominates_block(struct cfg *g, int a, int b)
{
    while (b != a && b != 0) {
        b = g->idom[b];
    }
    return b == a;
}

/* The loop headers, and per header the blocks of its loop (a back edge's
   source reaches its header, which dominates it, through them). */
static char **
loops_find(struct lu_func *f, struct cfg *g)
{
    int nb = f->nblocks;
    char **body = calloc((size_t)nb, sizeof(char *));
    int *work = malloc(sizeof(int) * (size_t)nb);
    for (int i = 0; i < g->n; i++) {
        int b = g->rpo[i];
        for (int s = 0; s < f->blocks[b].nsucc; s++) {
            int h = f->blocks[b].succ[s];
            if (!dominates_block(g, h, b)) {
                continue;
            }
            if (body[h] == NULL) {
                body[h] = calloc((size_t)nb, 1);
                body[h][h] = 1;
            }
            int nw = 0;
            if (!body[h][b]) {
                body[h][b] = 1;
                work[nw++] = b;
            }
            while (nw > 0) {
                int x = work[--nw];
                for (int k = 0; k < f->blocks[x].npreds; k++) {
                    int p = f->blocks[x].preds[k];
                    if (g->at[p] >= 0 && !body[h][p]) {
                        body[h][p] = 1;
                        work[nw++] = p;
                    }
                }
            }
        }
    }
    free(work);
    return body;
}

/* The one predecessor of header h from outside its loop, made to have h as
   its only successor (an edge split), or -1. */
static int
preheader(struct lu_func *f, int h, char *body)
{
    int p = -1, at = -1;
    for (int k = 0; k < f->blocks[h].npreds; k++) {
        int q = f->blocks[h].preds[k];
        if (!body[q]) {
            if (p >= 0) {
                return -1;
            }
            p = q;
            at = k;
        }
    }
    if (p < 0 || f->blocks[p].nsucc == 1) {
        return p;
    }
    int mid = lu_ir_block(f);
    lu_ir_emit(f, mid, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    f->blocks[mid].succ[0] = h;
    f->blocks[mid].nsucc = 1;
    f->blocks[mid].preds = malloc(sizeof(int));
    f->blocks[mid].preds[0] = p;
    f->blocks[mid].npreds = f->blocks[mid].cappreds = 1;
    for (int s = 0; s < f->blocks[p].nsucc; s++) {
        if (f->blocks[p].succ[s] == h) {
            f->blocks[p].succ[s] = mid;
        }
    }
    f->blocks[h].preds[at] = mid;
    return mid;
}

/* Header h's entry state as it stands at the end of its preheader p: h's
   phis are their inputs from p. */
static struct lu_fs *
preheader_state(struct lu_func *f, int h, int p)
{
    struct lu_fs *in = f->blocks[h].entry_fs;
    int at = 0;
    while (f->blocks[h].preds[at] != p) {
        at++;
    }
    struct lu_fs *fs = lu_ir_alloc(f, sizeof(struct lu_fs));
    *fs = *in;
    int n = in->nlocals + in->nstack;
    fs->vals = lu_ir_alloc(f, sizeof(lu_v) * (size_t)(n ? n : 1));
    fs->owned = lu_ir_alloc(f, (size_t)(n ? n : 1));
    memcpy(fs->owned, in->owned, (size_t)n);
    for (int i = 0; i < n; i++) {
        lu_v v = resolve(in->vals[i]);
        if (v != LU_NOV && f->ins[v].op == OP_PHI && f->ins[v].block == h) {
            v = resolve(f->ins[v].args[at]);
            if (f->ins[v].op == OP_CONST && f->ins[v].imm == 0) {
                v = LU_NOV;     /* an unbound local */
            }
        }
        fs->vals[i] = v;
    }
    return fs;
}

/* Puts guard g's copy last before block p's terminator, leaving at fs. */
static lu_v
hoist_copy(struct lu_func *f, lu_v g, int p, struct lu_fs *fs)
{
    lu_v c = lu_ir_emit(f, p, f->ins[g].op, REP_NONE, f->ins[g].a, f->ins[g].b, f->ins[g].c, f->ins[g].imm);
    struct lu_ins *in = &f->ins[c], *from = &f->ins[g];
    in->imm2 = from->imm2;
    in->flags = from->flags;
    in->fs = fs;
    in->origin = from->fs;
    struct lu_block *bl = &f->blocks[p];
    bl->ins[bl->nins - 1] = bl->ins[bl->nins - 2];
    bl->ins[bl->nins - 2] = c;
    return c;
}

static void
note_dep(struct lu_func *f, struct lu_fdep *dep)
{
    for (int i = 0; i < f->ndeps; i++) {
        struct lu_fdep *d = &f->deps[i];
        if (d->kind == dep->kind && d->obj == dep->obj && d->sub == dep->sub) {
            return;
        }
    }
    if (f->ndeps == f->capdeps) {
        f->capdeps = f->capdeps ? 2 * f->capdeps : 8;
        f->deps = realloc(f->deps, sizeof(struct lu_fdep) * (size_t)f->capdeps);
    }
    f->deps[f->ndeps++] = *dep;
}

/* The guards of one fact seen on the way down the dominator tree, a
   chained hash on what they check; leaving a block drops what it added. */
struct seen {
    lu_v *guard, *next;         /* per instruction */
    lu_v head[256];
    lu_v *undo;
    int nundo;
};

static int
seen_hash(struct lu_func *f, struct lu_ins *in)
{
    uint64_t h = (uint64_t)subject(f, in) * 0x9E3779B97F4A7C15ull ^ (uint64_t)in->imm * 31 ^ in->op;
    return (int)(h >> 56);
}

static lu_v
seen_find(struct lu_func *f, struct seen *t, lu_v v)
{
    for (lu_v w = t->head[seen_hash(f, &f->ins[v])]; w != LU_NOV; w = t->next[w]) {
        if (same_fact(f, &f->ins[w], &f->ins[v])) {
            return w;
        }
    }
    return LU_NOV;
}

static void
seen_add(struct lu_func *f, struct seen *t, lu_v v)
{
    int h = seen_hash(f, &f->ins[v]);
    t->next[v] = t->head[h];
    t->head[h] = v;
    t->undo[t->nundo++] = v;
}

static void
seen_drop(struct lu_func *f, struct seen *t, int to)
{
    while (t->nundo > to) {
        lu_v v = t->undo[--t->nundo];
        t->head[seen_hash(f, &f->ins[v])] = t->next[v];
    }
}

/* Whether a loop (its blocks) does not define the value a guard checks. */
static int
invariant(struct lu_func *f, struct lu_ins *in, char *body)
{
    lu_v a = resolve(in->a);
    return f->ins[a].op == OP_CONST || !body[f->ins[a].block];
}

static void
facts(struct lu_func *f)
{
    /* Preheaders first: they change the graph the rest is computed on. */
    int nstart = f->nins;
    struct cfg g;
    cfg_build(f, &g);
    char **body = loops_find(f, &g);
    int nb0 = f->nblocks;
    int *pre = malloc(sizeof(int) * (size_t)nb0);
    for (int h = 0; h < nb0; h++) {
        pre[h] = body[h] != NULL && f->blocks[h].entry_fs != NULL ? preheader(f, h, body[h]) : -1;
        free(body[h]);
    }
    free(body);
    cfg_free(&g);
    cfg_build(f, &g);
    body = loops_find(f, &g);
    int nb = f->nblocks;
    /* Facts inside loops move to the outermost preheader of a loop that
       does not define what they check. */
    int nins0 = f->nins;
    struct lu_fs **live_at = calloc((size_t)nb, sizeof(struct lu_fs *));      /* preheaders given writers' facts */
    struct lu_fs **state_at = calloc((size_t)nb, sizeof(struct lu_fs *));
    struct lu_fdep dep;
    for (int i = 0; i < g.n; i++) {
        int b = g.rpo[i];
        for (int k = 0; k < f->blocks[b].nins; k++) {
            lu_v v = f->blocks[b].ins[k];
            int kind;
            if (v >= nins0 || (f->ins[v].flags & INS_DEAD) || (kind = fact_of(f, &f->ins[v], &dep)) == NO_FACT) {
                continue;
            }
            int best = -1;
            for (int h = 0; h < nb0; h++) {
                if (pre[h] >= 0 && body[h] != NULL && body[h][b] && invariant(f, &f->ins[v], body[h])
                    && (best < 0 || body[h][best])) {
                    best = h;
                }
            }
            if (best < 0) {
                continue;
            }
            int p = pre[best];
            int have = 0;
            for (int j = 0; j < f->blocks[p].nins && !have; j++) {
                lu_v w = f->blocks[p].ins[j];
                have = w >= nins0 && same_fact(f, &f->ins[w], &f->ins[v]);
            }
            if (!have) {
                if (state_at[p] == NULL) {
                    state_at[p] = preheader_state(f, best, p);
                }
                hoist_copy(f, v, p, state_at[p]);
            }
            if (kind == WRITER_FACT) {
                live_at[p] = state_at[p];
                note_dep(f, &dep);
            }
        }
    }
    /* A loop starts knowing the unit lives: a check before it, which goes
       again unless something before may have changed a fact. */
    int nhoisted = f->nins;
    for (int p = 0; p < nb; p++) {
        if (live_at[p] != NULL) {
            lu_v live = lu_ir_emit(f, p, OP_CHECK_LIVE, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
            f->ins[live].fs = live_at[p];
            struct lu_block *bl = &f->blocks[p];
            bl->ins[bl->nins - 1] = bl->ins[bl->nins - 2];
            bl->ins[bl->nins - 2] = live;
        }
    }
    fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
    for (int v = nstart; v < f->nins; v++) {
        fwd[v] = v;
    }
    /* Redundant guards: a guard of the same fact dominates them. The
       dominator tree is walked in preorder (children by a stack). */
    char *redundant = calloc((size_t)f->nins, 1);
    memset(redundant + nhoisted, WRITER_FACT, (size_t)(f->nins - nhoisted));
    struct seen t = { .guard = NULL };
    t.next = malloc(sizeof(lu_v) * (size_t)f->nins);
    t.undo = malloc(sizeof(lu_v) * (size_t)f->nins);
    for (int h = 0; h < 256; h++) {
        t.head[h] = LU_NOV;
    }
    int *child = malloc(sizeof(int) * (size_t)nb), *sib = malloc(sizeof(int) * (size_t)nb);
    for (int b = 0; b < nb; b++) {
        child[b] = sib[b] = -1;
    }
    for (int i = g.n - 1; i > 0; i--) {
        int b = g.rpo[i], d = g.idom[b];
        sib[b] = child[d];
        child[d] = b;
    }
    int *stack = malloc(sizeof(int) * (size_t)(2 * nb + 2)), sp = 0;
    stack[sp++] = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        int mark = stack[--sp], b = stack[--sp];
        if (b < 0) {
            seen_drop(f, &t, mark);
            continue;
        }
        int at = t.nundo;
        for (int k = 0; k < f->blocks[b].nins; k++) {
            lu_v v = f->blocks[b].ins[k];
            int kind;
            if ((f->ins[v].flags & INS_DEAD) || (kind = fact_of(f, &f->ins[v], &dep)) == NO_FACT) {
                continue;
            }
            if (seen_find(f, &t, v) != LU_NOV) {
                redundant[v] = (char)kind;
                if (kind == WRITER_FACT) {
                    note_dep(f, &dep);
                }
            }
            else {
                seen_add(f, &t, v);
            }
        }
        stack[sp++] = -1;
        stack[sp++] = at;
        for (int c = child[b]; c >= 0; c = sib[c]) {
            stack[sp++] = c;
            stack[sp++] = 0;
        }
    }
    free(stack);
    free(child);
    free(sib);
    free(t.next);
    free(t.undo);
    /* Whether the unit is known to live at each block's start: none of
       its predecessors' ends may have changed anything since a check. */
    char *live_in = malloc((size_t)nb), *live_out = malloc((size_t)nb);
    memset(live_in, 1, (size_t)nb);
    memset(live_out, 1, (size_t)nb);
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < g.n; i++) {
            int b = g.rpo[i];
            int live = 1;
            for (int k = 0; b != 0 && k < f->blocks[b].npreds; k++) {
                int p = f->blocks[b].preds[k];
                live &= g.at[p] < 0 || live_out[p];
            }
            live_in[b] = (char)live;
            for (int k = 0; k < f->blocks[b].nins; k++) {
                struct lu_ins *in = &f->ins[f->blocks[b].ins[k]];
                if (in->flags & INS_DEAD) {
                    continue;
                }
                if (redundant[f->blocks[b].ins[k]] == WRITER_FACT) {
                    live = 1;
                }
                else if (may_change(f, in)) {
                    live = 0;
                }
            }
            if (live != live_out[b]) {
                live_out[b] = (char)live;
                changed = 1;
            }
        }
    }
    for (int i = 0; i < g.n; i++) {
        int b = g.rpo[i];
        int live = live_in[b];
        for (int k = 0; k < f->blocks[b].nins; k++) {
            lu_v v = f->blocks[b].ins[k];
            struct lu_ins *in = &f->ins[v];
            if (in->flags & INS_DEAD) {
                continue;
            }
            if (redundant[v] == FIXED_FACT || (redundant[v] == WRITER_FACT && live)) {
                in->flags |= INS_DEAD;
                live |= redundant[v] == WRITER_FACT;
            }
            else if (redundant[v] == WRITER_FACT) {
                in->op = OP_CHECK_LIVE;
                in->a = in->b = in->c = LU_NOV;
                live = 1;
            }
            else if (may_change(f, in)) {
                live = 0;
            }
        }
    }
    for (int h = 0; h < nb; h++) {
        free(body[h]);
    }
    free(body);
    free(pre);
    free(live_at);
    free(state_at);
    free(redundant);
    free(live_in);
    free(live_out);
    cfg_free(&g);
}

/* Loads of a word an earlier load in the block made, with nothing between
   that may run code, change state or poll: an object's type or size, an
   inline attribute (whose reference the load took: the copy takes one
   too). */
static void
same_loads(struct lu_func *f)
{
    for (int b = 0; b < f->nblocks; b++) {
        struct lu_block *bl = &f->blocks[b];
        int from = 0;
        for (int k = 0; k < bl->nins; k++) {
            lu_v v = bl->ins[k];
            struct lu_ins *in = &f->ins[v];
            if (in->flags & INS_DEAD) {
                continue;
            }
            if (in->op == OP_POLL || in->op == OP_STORE || in->op == OP_RITER_NEXT || may_change(f, in)) {
                from = k + 1;
                continue;
            }
            if (in->op != OP_LOAD && in->op != OP_INLINE_LOAD) {
                continue;
            }
            for (int j = from; j < k; j++) {
                lu_v w = bl->ins[j];
                struct lu_ins *x = &f->ins[w];
                if ((x->flags & INS_DEAD) || x->op != in->op || resolve(x->a) != resolve(in->a) || x->imm != in->imm
                    || x->imm2 != in->imm2 || x->rep != in->rep) {
                    continue;
                }
                /* v's uses are w's; an attribute's load becomes the reference it took. */
                fwd[v] = w;
                if (in->op == OP_INLINE_LOAD) {
                    in->op = OP_INCREF;
                    in->a = w;
                    in->rep = REP_NONE;
                    in->fs = NULL;
                }
                else {
                    in->flags |= INS_DEAD;
                }
                break;
            }
        }
    }
}

/* Use counts, with frame-state uses apart. */
struct uses {
    int *n;                     /* every use */
    int *state;                 /* uses in frame states */
};

static void
count_uses(struct lu_func *f, struct uses *u)
{
    u->n = calloc((size_t)f->nins, sizeof(int));
    u->state = calloc((size_t)f->nins, sizeof(int));
    for (int v = 0; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3; i++) {
            if (ops[i] != LU_NOV) {
                u->n[ops[i]]++;
            }
        }
        for (int i = 0; i < in->nargs; i++) {
            if (in->args[i] != LU_NOV) {
                u->n[in->args[i]]++;
            }
        }
        for (struct lu_fs *fs = in->fs; fs != NULL; fs = fs->caller) {
            for (int i = 0; i < fs->nlocals + fs->nstack; i++) {
                if (fs->vals[i] != LU_NOV) {
                    u->n[fs->vals[i]]++;
                    u->state[fs->vals[i]]++;
                }
            }
        }
    }
}

/* 3b. Unboxed fields (DESIGN 4.5). A float (an int) loaded from an
   inline value only to be unboxed is read raw; one computed here and
   stored into one is stored raw, with no object. Either is allowed
   only while the box's identity cannot be observed: besides the unbox or
   the store, its uses are reference operations and frame states that hold
   it on an operand stack, where a deopt's box is a temporary the next
   instruction consumes. A box in a local, stored twice, or made from an
   object (a.x = b.x keeps one object) stays a box. */
static char *
stack_only(struct lu_func *f)
{
    char *bad = calloc((size_t)f->nins, 1);
    for (int v = 0; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        for (struct lu_fs *fs = in->fs; fs != NULL; fs = fs->caller) {
            for (int i = 0; i < fs->nlocals; i++) {
                if (fs->vals[i] != LU_NOV) {
                    bad[fs->vals[i]] = 1;
                }
            }
        }
    }
    return bad;
}

/* Instruction uses of v other than reference operations and unboxes, and
   the count of uses by op `want` as operand a (or b for a store). */
static int
other_uses(struct lu_func *f, lu_v v, int want, int *wanted)
{
    int others = 0;
    *wanted = 0;
    for (int u = 0; u < f->nins; u++) {
        struct lu_ins *in = &f->ins[u];
        if (in->flags & INS_DEAD) {
            continue;
        }
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3; i++) {
            if (ops[i] != v) {
                continue;
            }
            if (in->op == want && i == (want == OP_INLINE_STORE ? 1 : 0)) {
                (*wanted)++;
            }
            else if (!(i == 0 && (in->op == OP_DECREF || in->op == OP_INCREF || in->op == OP_UNBOX_FLOAT
                                  || in->op == OP_UNBOX_INT))) {
                others++;
            }
        }
        for (int i = 0; i < in->nargs; i++) {
            others += in->args[i] == v;
        }
    }
    return others;
}

static int
raw_fields(struct lu_func *f)
{
    int changed = 0;
    char *bad = stack_only(f);
    int n = f->nins;
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        int k;
        if (in->flags & INS_DEAD) {
            continue;
        }
        int kf = 0, ki = 0;
        if (in->op == OP_INLINE_LOAD && !bad[v] && other_uses(f, v, OP_UNBOX_FLOAT, &kf) == 0
            && (other_uses(f, v, OP_UNBOX_INT, &ki), (kf > 0) != (ki > 0))) {
            /* The load becomes a raw read before it, and it a box of that,
               which the unboxes then fold through. */
            int fl = kf > 0;
            lu_v r = lu_ir_emit(f, in->block, fl ? OP_INLINE_LOAD_F : OP_INLINE_LOAD_I, fl ? REP_F64 : REP_I64,
                                LU_NOV, LU_NOV, LU_NOV, 0);
            in = &f->ins[v];
            struct lu_ins *raw = &f->ins[r];
            raw->a = in->a;
            raw->imm = in->imm;
            raw->fs = in->fs;
            in->op = fl ? OP_BOX_FLOAT : OP_BOX_INT;
            in->rep = REP_OBJ;
            in->a = r;
            in->b = LU_NOV;
            in->fs = NULL;
            struct lu_block *bl = &f->blocks[in->block];
            int at = 0;
            while (bl->ins[at] != v) {
                at++;
            }
            memmove(&bl->ins[at + 1], &bl->ins[at], sizeof(int) * (size_t)(bl->nins - 1 - at));
            bl->ins[at] = r;
            changed = 1;
        }
        else if (in->op == OP_INLINE_STORE) {
            lu_v b = in->b;
            struct lu_ins *box = &f->ins[b];
            if ((box->op == OP_BOX_FLOAT || box->op == OP_BOX_INT) && box->b == LU_NOV && box->c == LU_NOV
                && !bad[b] && other_uses(f, b, OP_INLINE_STORE, &k) == 0 && k == 1) {
                in->op = box->op == OP_BOX_FLOAT ? OP_INLINE_STORE_F : OP_INLINE_STORE_I;
                in->b = box->a;
                changed = 1;
            }
        }
    }
    free(bad);
    if (changed) {
        fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
        for (int v = n; v < f->nins; v++) {
            fwd[v] = v;
        }
    }
    return changed;
}

/* 4 and 5. */
static void
escape(struct lu_func *f)
{
    int n = f->nins;
    char *ok = calloc((size_t)n, 1);
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        ok[v] = !(in->flags & INS_DEAD)
            && (in->op == OP_BOX_INT || in->op == OP_BOX_FLOAT || (in->op == OP_HELPER && in->imm == H_new_dict));
    }
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        int allowed = in->op == OP_STORE_LOCAL || in->op == OP_DECREF || in->op == OP_INCREF
            || in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT || in->op == OP_GUARD_NOT_NULL
            || (in->op == OP_TRUTH && f->ins[in->a].op == OP_HELPER);
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3; i++) {
            if (ops[i] != LU_NOV && !(allowed && i == 0)) {
                ok[ops[i]] = 0;
            }
        }
        for (int i = 0; i < in->nargs; i++) {
            if (in->args[i] != LU_NOV) {
                ok[in->args[i]] = 0;
            }
        }
        if (in->op == OP_HELPER && (lu_helpers[in->imm].flags & IF_PY) && in->fs != NULL) {
            /* A call that runs Python can read every frame's locals: the
               root's slots, and the frames pushed for it from inlined ones. */
            for (struct lu_fs *fs = in->fs; fs != NULL; fs = fs->caller) {
                for (int i = 0; i < fs->nlocals; i++) {
                    if (fs->vals[i] != LU_NOV) {
                        ok[fs->vals[i]] = 0;
                    }
                }
            }
        }
    }
    for (int v = 0; v < n; v++) {
        if (ok[v]) {
            f->ins[v].flags |= INS_VIRTUAL;
        }
        else if ((f->ins[v].op == OP_BOX_INT || f->ins[v].op == OP_BOX_FLOAT) && f->ins[v].imm2
                 && !(f->ins[v].flags & INS_DEAD)) {
            /* A root slot's value built from a raw phi: the slot holds the
               reference, as STORE_FAST would have made it. */
            int block = f->ins[v].block;
            lu_v st = lu_ir_emit(f, block, OP_STORE_LOCAL, REP_NONE, v, LU_NOV, LU_NOV, f->ins[v].imm2 - 1);
            struct lu_block *bl = &f->blocks[block];
            int at = 0;
            while (bl->ins[at] != v) {
                at++;
            }
            memmove(&bl->ins[at + 2], &bl->ins[at + 1], sizeof(int) * (size_t)(bl->nins - 2 - at));
            bl->ins[at + 1] = st;
        }
    }
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        if (in->op == OP_TRUTH && ok[in->a]) {
            /* A dict nothing filled is empty. */
            in->op = OP_CONST_I;
            in->imm = 0;
            in->a = LU_NOV;
            in->fs = NULL;
        }
        if ((in->op == OP_STORE_LOCAL || in->op == OP_DECREF || in->op == OP_INCREF) && in->a != LU_NOV
            && ok[in->a]) {
            /* A box made from an object is that object: its references are the object's. */
            if (f->ins[in->a].b != LU_NOV) {
                in->a = f->ins[in->a].b;
            }
            else {
                in->flags |= INS_DEAD;
            }
        }
        if ((in->op == OP_DECREF || in->op == OP_INCREF) && in->a != LU_NOV) {
            struct lu_ins *a = &f->ins[in->a];
            if (a->op == OP_BOOL || (a->op == OP_CONST && (a->imm == 0 || _Py_IsImmortal((PyObject *)(intptr_t)a->imm)))) {
                in->flags |= INS_DEAD;
            }
        }
    }
    free(ok);
}

/* 6. */
static void
dce(struct lu_func *f)
{
    for (int changed = 1; changed;) {
        changed = 0;
        struct uses u;
        count_uses(f, &u);
        for (int v = 0; v < f->nins; v++) {
            struct lu_ins *in = &f->ins[v];
            if ((in->flags & INS_DEAD) || u.n[v] > 0) {
                continue;
            }
            /* Guards may go too: what they protect has no use. Effects stay. */
            int fl = lu_op_flags[in->op];
            int removable = !(fl & (IF_EFFECT | IF_TERM)) && in->op != OP_POLL && in->op != OP_ENTRY_STACK
                && !(in->op >= OP_GUARD_TYPE && in->op <= OP_GUARD_NOT_NULL);
            if (removable) {
                in->flags |= INS_DEAD;
                changed = 1;
            }
        }
        free(u.n);
        free(u.state);
    }
}

/* 7. */
static void
split_edges(struct lu_func *f)
{
    int nb = f->nblocks;
    for (int b = 0; b < nb; b++) {
        if (f->blocks[b].nsucc != 2) {
            continue;
        }
        for (int s = 0; s < 2; s++) {
            int to = f->blocks[b].succ[s];
            if (f->blocks[to].npreds < 2) {
                continue;
            }
            int mid = lu_ir_block(f);
            lu_ir_emit(f, mid, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
            f->blocks[mid].succ[0] = to;
            f->blocks[mid].nsucc = 1;
            f->blocks[mid].preds = malloc(sizeof(int));
            f->blocks[mid].preds[0] = b;
            f->blocks[mid].npreds = f->blocks[mid].cappreds = 1;
            f->blocks[b].succ[s] = mid;
            for (int p = 0; p < f->blocks[to].npreds; p++) {
                if (f->blocks[to].preds[p] == b) {
                    f->blocks[to].preds[p] = mid;
                    break;
                }
            }
        }
    }
}

/* Reverse postorder from block 0, which is the entry. */
static void
order_blocks(struct lu_func *f)
{
    int nb = f->nblocks;
    char *seen = calloc((size_t)nb, 1);
    int *post = malloc(sizeof(int) * (size_t)nb), np = 0;
    int *stack = malloc(sizeof(int) * (size_t)nb * 3), *next = calloc((size_t)nb, sizeof(int)), sp = 0;
    stack[sp++] = 0;
    seen[0] = 1;
    while (sp > 0) {
        int b = stack[sp - 1];
        if (next[b] < f->blocks[b].nsucc) {
            /* The second successor first, so that a fall-through follows its branch. */
            int s = f->blocks[b].succ[f->blocks[b].nsucc - 1 - next[b]++];
            if (!seen[s]) {
                seen[s] = 1;
                stack[sp++] = s;
            }
        }
        else {
            post[np++] = b;
            sp--;
        }
    }
    free(f->order);
    f->order = malloc(sizeof(int) * (size_t)np);
    f->norder = np;
    for (int i = 0; i < np; i++) {
        f->order[i] = post[np - 1 - i];
        f->blocks[f->order[i]].order = i;
    }
    /* Unreachable blocks' instructions are dead. */
    for (int b = 0; b < nb; b++) {
        if (!seen[b]) {
            for (int i = 0; i < f->blocks[b].nins; i++) {
                f->ins[f->blocks[b].ins[i]].flags |= INS_DEAD;
            }
        }
    }
    free(seen);
    free(post);
    free(stack);
    free(next);
}

/* Drops phi inputs from predecessors that are gone, and dead instructions
   from block lists. */
static void
compact(struct lu_func *f)
{
    for (int b = 0; b < f->nblocks; b++) {
        struct lu_block *bl = &f->blocks[b];
        int k = 0;
        for (int i = 0; i < bl->nins; i++) {
            if (!(f->ins[bl->ins[i]].flags & INS_DEAD)) {
                bl->ins[k++] = bl->ins[i];
            }
        }
        bl->nins = k;
    }
}

int
lu_ssa_optimize(struct lu_func *f)
{
    fwd = malloc(sizeof(lu_v) * (size_t)f->capins);
    for (int v = 0; v < f->nins; v++) {
        fwd[v] = v;
    }
    while (trivial_phis(f)) {
    }
    rewrite(f);
    facts(f);
    same_loads(f);
    rewrite(f);
    unbox_phis(f, REP_I64);
    rewrite(f);
    unbox_phis(f, REP_F64);
    rewrite(f);
    for (int round = 0; round < 4; round++) {
        int changed = simplify(f);
        while (trivial_phis(f)) {
            changed = 1;
        }
        rewrite(f);
        if (!changed) {
            break;
        }
    }
    if (raw_fields(f)) {
        simplify(f);
        rewrite(f);
    }
    escape(f);
    dce(f);
    split_edges(f);
    order_blocks(f);
    compact(f);
    free(fwd);
    fwd = NULL;
    return 0;
}
