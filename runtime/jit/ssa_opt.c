/* The optimizer's passes over block SSA (DESIGN 6.2 step 3), in order:

   1. Trivial phis go (a phi of one value, or of itself and one value).
   2. Representation: a phi whose inputs are all int boxes (or float
      boxes), counting phis of the same kind, becomes a raw phi under one
      box, so loop-carried ints and floats stay in registers. A root
      local's phi may forget an object an input box was made from, which
      its edge then releases.
   3. Simplification: an unbox of a box is its raw value; guards on values
      whose type a box proves fold away.
   4. Escape analysis (the donor's scalar replacement, for boxes): a box
      whose every use is an unbox, a frame state or a release is virtual.
      It is never built in compiled code; an exit or a walker that needs
      it builds it (runtime/jit/deopt.c). A box a Python-running call could
      observe in an inlined frame pushed for it is materialized. So is a new dict
      (an inlined callee's **kwargs) whose uses are those or truth tests,
      which are false.
   5. References: the releases of virtual boxes go, and so
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
   phi or a reference operation): such a box is an object
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
        int benign = in->op == OP_DECREF || in->op == OP_INCREF || in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT || in->op == OP_GUARD_NOT_NULL
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

/* A release of o at the end of block b, before its jump. */
static void
release_on_edge(struct lu_func *f, int b, lu_v o)
{
    lu_v d = lu_ir_emit(f, b, OP_DECREF, REP_NONE, o, LU_NOV, LU_NOV, 0);
    struct lu_block *bl = &f->blocks[b];
    bl->ins[bl->nins - 1] = bl->ins[bl->nins - 2];
    bl->ins[bl->nins - 2] = d;
    fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
    for (int v = d; v < f->nins; v++) {
        fwd[v] = v;
    }
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
                /* A box made from an object holds the object's reference: a
                   root local's phi forgets it by releasing it on its edge,
                   which must be its predecessor's only one. */
                if (f->ins[a].op == box && f->ins[a].b != LU_NOV
                    && (!in->imm2 || f->blocks[f->blocks[in->block].preds[i]].nsucc != 1)) {
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
            int benign = in->op == OP_DECREF || in->op == OP_INCREF;
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
            if (a != v && !cand[a] && f->ins[a].op == box && f->ins[a].b != LU_NOV) {
                release_on_edge(f, f->blocks[in->block].preds[i], resolve(f->ins[a].b));
                in = &f->ins[v];
            }
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
        int allowed = in->op == OP_DECREF || in->op == OP_INCREF || in->op == OP_UNBOX_INT
            || in->op == OP_UNBOX_FLOAT || in->op == OP_GUARD_NOT_NULL
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
            /* A call that runs Python can read the locals of the frames
               pushed for it from inlined ones. The root's are read through
               the state it publishes, where a box can still be built. */
            for (struct lu_fs *fs = in->fs; fs != NULL && fs->caller != NULL; fs = fs->caller) {
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
        if ((in->op == OP_DECREF || in->op == OP_INCREF) && in->a != LU_NOV && ok[in->a]) {
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
