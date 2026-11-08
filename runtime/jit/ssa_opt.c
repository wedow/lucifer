/* The optimizer's passes over block SSA (DESIGN 6.2 step 3), in order:

   1. Trivial phis go (a phi of one value, or of itself and one value).
      Then facts: guards leave loops, repeated guards go, and so do loads
      of what a load in the block already read (1b).
   2. Representation: a phi whose inputs are all int boxes (or float
      boxes, or such constants), counting phis of the same kind, becomes a
      raw phi under one box, so loop-carried ints and floats stay in
      registers. A root
      local's phi may forget an object an input box was made from, which
      its edge then releases.
   3. Simplification: an unbox of a box is its raw value; guards on values
      whose type a box proves fold away. Then virtual objects (3c): tuples,
      instances, loop iterators and phis of them that nothing outside
      compiled code sees are not made.
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
        for (int i = 0; in->op == OP_PHI && i < in->nfields; i++) {
            in->fields[i] = resolve(in->fields[i]);     /* a virtual phi's */
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

/* Whether a is a constant exact int (float) that unboxes to a raw value
   of rep, which goes into bits: a phi takes it as a box of that value. A
   box made at the phi stands for it after, a small int being the same
   object. */
static int
raw_const(struct lu_func *f, lu_v a, int rep, int64_t *bits)
{
    struct lu_ins *in = &f->ins[a];
    PyObject *o = in->op == OP_CONST ? (PyObject *)(intptr_t)in->imm : NULL;
    if (o == NULL) {
        return 0;
    }
    if (rep == REP_F64 && PyFloat_CheckExact(o)) {
        double d = PyFloat_AS_DOUBLE(o);
        if (bits != NULL) {
            memcpy(bits, &d, 8);
        }
        return 1;
    }
    if (rep == REP_I64 && PyLong_CheckExact(o)) {
        int overflow;
        long long x = PyLong_AsLongLongAndOverflow(o, &overflow);
        if (bits != NULL) {
            *bits = x;
        }
        return !overflow;
    }
    return 0;
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
                if ((!(f->ins[a].op == box) || esc[a]) && !(a < n && cand[a]) && !raw_const(f, a, rep, NULL)) {
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
    lu_v mapped = f->nins;      /* instructions fwd has */
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
            int64_t bits;
            int konst = a != v && !cand[a] && raw_const(f, a, rep, &bits);
            int null = a != v && !cand[a] && !konst && f->ins[a].op == OP_CONST;
            lu_v k = konst ? entry_const(f, rep == REP_F64 ? OP_CONST_F : OP_CONST_I, rep, bits) : LU_NOV;
            in = &f->ins[v];
            f->ins[raw[v]].args[i] = a == v ? raw[v] : cand[a] ? raw[a] : null ? zero : konst ? k : resolve(f->ins[a].a);
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
        for (; mapped < f->nins; mapped++) {
            fwd[mapped] = mapped;
        }
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
    case OP_HELPER: return (in->imm == H_range_iter && t == &PyRangeIter_Type) || (lu_iter_type(in) == t && t != NULL);
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

/* 1b. Facts and value numbering (DESIGN 6.2 step 8). A guard checks a
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
    /* Watched and read in the dict's critical section, where every change
       to it is made and reported (dict.c): a change the compile does not
       read is one made after, which sees the watch and the compile's
       ticket (runtime/jit/deps.c). */
    int holds;
    dep->kind = keys ? LU_DEP_KEYS : LU_DEP_VALUE;
    dep->sub = keys ? 0 : (uint32_t)GI_OFFSET(in->imm2);
    Py_BEGIN_CRITICAL_SECTION(o);
    __atomic_fetch_or(&((PyDictObject *)o)->_ma_watcher_tag, 1, __ATOMIC_RELAXED);
    PyDictKeysObject *dk = ((PyDictObject *)o)->ma_keys;
    char *word = (char *)dk + GI_OFFSET(in->imm2);
    holds = keys ? dk->dk_version == (uint32_t)in->imm
                 : lu_dk_is_unicode(dk) && dk->dk_nentries > 0 && word <= (char *)lu_dk_value_slot(dk, dk->dk_nentries - 1)
                   && *(PyObject **)word == (PyObject *)(intptr_t)in->imm;
    Py_END_CRITICAL_SECTION();
    return holds ? WRITER_FACT : NO_FACT;
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

static int plain(struct lu_func *f, lu_v v, int depth);
static int plain_item(struct lu_func *f, lu_v t, int64_t k, int depth);

/* Whether releasing list display v cannot run a finalizer: its items are
   plain and nothing but a copy (list_repeat_fresh) and reference
   operations uses it, and no state a walker reads names it in a local,
   so nothing can have put another item in it. */
static int
quiet_display(struct lu_func *f, lu_v v)
{
    struct lu_ins *in = &f->ins[v];
    if (in->op != OP_HELPER || in->imm != H_build_list) {
        return 0;
    }
    for (int i = 0; i < in->nargs; i++) {
        if (!plain(f, in->args[i], 1)) {
            return 0;
        }
    }
    for (int u = 0; u < f->nins; u++) {
        struct lu_ins *x = &f->ins[u];
        if (x->flags & INS_DEAD) {
            continue;
        }
        int ok = x->op == OP_INCREF || x->op == OP_DECREF || (x->op == OP_HELPER && x->imm == H_list_repeat_fresh);
        lu_v ops[3] = { x->a, x->b, x->c };
        for (int i = 0; i < 3; i++) {
            if (ops[i] == v && !(ok && i == 0)) {
                return 0;
            }
        }
        for (int i = 0; i < x->nargs; i++) {
            if (x->args[i] == v && !(ok && i == 0 && x->op == OP_HELPER)) {
                return 0;
            }
        }
        for (struct lu_fs *fs = x->fs; fs != NULL && lu_publishes(x); fs = fs->caller) {
            for (int i = 0; i < fs->nlocals; i++) {
                if (fs->vals[i] == v) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Whether releasing v cannot run a finalizer: v is an int, a float, a bool
   or an immortal object on every path, or a tuple this code built of such
   values (an item read from one is one), or a range's loop iterator. */
static int
plain(struct lu_func *f, lu_v v, int depth)
{
    struct lu_ins *in = &f->ins[resolve(v)];
    int64_t first = offsetof(PyTupleObject, ob_item);
    if (depth >= 8) {
        return 0;
    }
    switch (in->op) {
    case OP_BOX_INT: case OP_BOX_FLOAT: case OP_BOOL:
        return 1;
    case OP_CONST:
        return in->imm == 0 || _Py_IsImmortal((PyObject *)(intptr_t)in->imm);
    case OP_PHI:
        for (int i = 0; i < in->nargs; i++) {
            if (resolve(in->args[i]) != resolve(v) && !plain(f, in->args[i], depth + 1)) {
                return 0;
            }
        }
        return 1;
    case OP_HELPER:
        for (int i = 0; in->imm == H_build_tuple && i < in->nargs; i++) {
            if (!plain(f, in->args[i], depth + 1)) {
                return 0;
            }
        }
        return in->imm == H_build_tuple || in->imm == H_rangeiter_new;
    case OP_LOAD:
        return in->rep == REP_OBJ && in->imm >= first && (in->imm - first) % 8 == 0
            && plain_item(f, in->a, (in->imm - first) / 8, depth + 1);
    case OP_LIST_ITEM:
        return in->imm == 1 && f->ins[resolve(in->b)].op == OP_CONST_I
            && plain_item(f, in->a, f->ins[resolve(in->b)].imm, depth + 1);
    default:
        return 0;
    }
}

/* Whether item k of tuple t is plain: t is a tuple this code built, or a
   phi of such tuples. */
static int
plain_item(struct lu_func *f, lu_v t, int64_t k, int depth)
{
    struct lu_ins *in = &f->ins[resolve(t)];
    if (depth >= 8) {
        return 0;
    }
    if (in->op == OP_HELPER && in->imm == H_build_tuple) {
        k = k < 0 ? k + in->nargs : k;
        return k >= 0 && k < in->nargs && plain(f, in->args[k], depth + 1);
    }
    if (in->op != OP_PHI || in->rep != REP_OBJ || (in->flags & INS_VIRTUAL)) {
        return 0;
    }
    for (int i = 0; i < in->nargs; i++) {
        if (resolve(in->args[i]) != resolve(t) && !plain_item(f, in->args[i], k, depth + 1)) {
            return 0;
        }
    }
    return 1;
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
                 || in->imm == H_cell_load || in->imm == H_compare_ideq || in->imm == H_range_iter || lu_iter_type(in) != NULL
                 || in->imm == H_new_object);
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
                lu_ir_dep(f, &dep);
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
                    lu_ir_dep(f, &dep);
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
        for (int i = 0; in->op == OP_PHI && i < in->nfields; i++) {
            u->n[in->fields[i]]++;      /* a virtual phi's: exits read them */
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

/* 3c. Virtual objects (DESIGN 4.5, the donor's scalar replacement for
   objects). A tuple this code builds, or an instance it makes (a class
   call whose __init__ inlined) of a class without a finalizer, whose
   every use is one of these is never made:
     - a reference operation;
     - a frame state (only exits see it, and every exit taken while it
       lives names it), but for the locals of a state an instruction
       publishes, which a walker reads;
     - a read of its type, a guard of its type or its type's version, an
       identity test (nothing else can be it: it never leaves);
     - a tuple's size or a constant index into it;
     - an instance's inline store (as the owner) or load, or the test of
       its dict for a method, which it has none of.
   A loop iterator (ssa_build.c for_step) is one too: its fields are its
   arguments, as a tuple's are, read by loads. A phi whose every input is
   such an object of one shape (phi_shapes) is virtual with them
   (phis_agree): its fields are phis of their fields (phi_fields), which
   take the references the inputs held, so the edge into it is the
   input's last use.
   An instance's fields are the stores into it: each name is stored once,
   in the order of the class's names (the insertion order the words keep),
   and wherever it is used a store either ran on every path there or on
   none, so that each exit sees one set of fields. A load reads its
   store's value. The object's references become its fields': the build
   (a store) takes a reference to each, and the release that ends it (its
   count is the same on every path, found by counting) releases them; the
   rest go. An exit makes it once, its fields moving in
   (runtime/jit/deopt.c). */

enum { OBJ_NONE, OBJ_TUPLE, OBJ_INSTANCE, OBJ_ITER, OBJ_PHI };

static PyTypeObject *
object_type(struct lu_func *f, struct lu_ins *in)
{
    return in->imm == H_build_tuple ? &PyTuple_Type : lu_iter_type(in) != NULL ? lu_iter_type(in)
        : (PyTypeObject *)(intptr_t)f->ins[in->args[0]].imm;
}

static int
object_kind(struct lu_func *f, lu_v v)
{
    struct lu_ins *in = &f->ins[v];
    if (in->op != OP_HELPER || (in->flags & (INS_DEAD | INS_VIRTUAL))) {
        return OBJ_NONE;
    }
    if (in->imm == H_build_tuple && in->nargs > 0) {
        return OBJ_TUPLE;
    }
    if (lu_iter_type(in) != NULL) {
        return OBJ_ITER;
    }
    if (in->imm == H_new_object) {
        PyTypeObject *tp = object_type(f, in);
        return tp->tp_finalize == NULL && tp->tp_del == NULL && (tp->tp_flags & Py_TPFLAGS_INLINE_VALUES)
            ? OBJ_INSTANCE : OBJ_NONE;
    }
    return OBJ_NONE;
}

/* The field of iterator o that load u reads, or -1. */
static int
iter_field(struct lu_func *f, struct lu_ins *o, struct lu_ins *u)
{
    for (int k = 0; k < o->nargs; k++) {
        if (u->imm == lu_object_place(o, k) && u->rep == f->ins[o->args[k]].rep && u->imm2 == 0) {
            return k;
        }
    }
    return -1;
}

/* Whether u's operand at pos (0..2: a, b, c; 3: an argument) may be
   object v of kind k (of a phi: v and k are its shape's). */
static int
object_use(struct lu_func *f, struct lu_ins *u, int pos, lu_v v, int k)
{
    struct lu_ins *o = &f->ins[v];
    int64_t first = offsetof(PyTupleObject, ob_item);
    switch (u->op) {
    case OP_INCREF: case OP_DECREF:
        return pos == 0;
    case OP_IS:
        return pos < 2;
    case OP_GUARD_TYPE:
        return pos == 0 && u->imm == (int64_t)(intptr_t)object_type(f, o);
    case OP_GUARD_VERSION:
        return pos == 0 && u->imm2 == GV_TYPE_OF;
    case OP_LOAD:
        return pos == 0 && (u->imm == offsetof(PyObject, ob_type)
                            || (k == OBJ_TUPLE && u->imm == offsetof(PyVarObject, ob_size) && u->rep == REP_I64)
                            || (k == OBJ_TUPLE && u->rep == REP_OBJ && u->imm >= first
                                && u->imm < first + 8 * o->nargs && (u->imm - first) % 8 == 0)
                            || (k == OBJ_ITER && iter_field(f, o, u) >= 0));
    case OP_LIST_ITEM:
        return k == OBJ_TUPLE && pos == 0 && u->imm == 1 && f->ins[u->b].op == OP_CONST_I
            && f->ins[u->b].imm >= -o->nargs && f->ins[u->b].imm < o->nargs;
    case OP_INLINE_STORE: case OP_INLINE_LOAD:
        return k == OBJ_INSTANCE && pos == 0;
    case OP_HELPER:
        return k == OBJ_INSTANCE && pos == 3 && u->imm == H_dict_lacks && u->args[0] == v;
    default:
        return 0;
    }
}

/* LUCIFER_JIT_DEBUG: why object v stays real (u: the instruction that
   decided it, or NULL); OBJ_NONE. */
static int
not_virtual(struct lu_func *f, lu_v v, struct lu_ins *u, const char *why)
{
    if (_PyRuntime.jit.debug) {
        struct lu_ins *o = &f->ins[v];
        fprintf(stderr, "jit: not virtual: %s at %s@%d: %s", o->op == OP_PHI ? "phi" : o->imm == H_build_tuple ? "tuple"
                : lu_iter_type(o) != NULL ? "iterator" : "instance",
                o->src != NULL ? PyUnicode_AsUTF8(o->src->co_qualname) : "?", o->src_pc, why);
        if (u != NULL) {
            fprintf(stderr, " (%s at %s@%d)", lu_op_names[u->op], u->src != NULL ? PyUnicode_AsUTF8(u->src->co_qualname) : "?",
                    u->src_pc);
            if (u->op == OP_HELPER) {
                fprintf(stderr, " %s", lu_helpers[u->imm].name);
            }
        }
        fprintf(stderr, "\n");
    }
    return OBJ_NONE;
}

/* Whether u may leave compiled code at its frame state. */
static int
exits(struct lu_ins *u)
{
    return u->fs != NULL && ((lu_op_flags[u->op] & (IF_GUARD | IF_RAISE)) || u->op == OP_DEOPT
                             || (u->op == OP_HELPER && (lu_helpers[u->imm].flags & IF_RAISE)));
}

struct objs {
    struct lu_func *f;
    int nold;                   /* instructions before the pass added any */
    char *mark;                 /* per instruction: how it uses the object looked at (1 operand, 2 state) */
    struct cfg g;
    int *blk, *idx;             /* per instruction: its block and position */
    char *seen;                 /* per block: scratch */
    int *work;
};

/* Whether v is in a block control reaches. */
static int
reached(struct objs *x, lu_v v)
{
    return x->blk[v] >= 0 && x->g.at[x->blk[v]] >= 0;
}

static int
before_ins(struct objs *x, lu_v s, lu_v p)
{
    if (!reached(x, s) || !reached(x, p)) {
        return 0;
    }
    if (x->blk[s] == x->blk[p]) {
        return x->idx[s] < x->idx[p];
    }
    return x->g.at[x->blk[p]] >= 0 && dominates_block(&x->g, x->blk[s], x->blk[p]);
}

/* Whether p can run after s without object v's allocation between. */
static int
reaches(struct objs *x, lu_v v, lu_v s, lu_v p)
{
    struct lu_func *f = x->f;
    if (!reached(x, s) || !reached(x, p)) {
        return 0;
    }
    if (x->blk[s] == x->blk[p] && x->idx[s] < x->idx[p]) {
        return 1;
    }
    memset(x->seen, 0, (size_t)f->nblocks);
    int nw = 0;
    x->work[nw++] = x->blk[s];
    while (nw > 0) {
        int b = x->work[--nw];
        for (int k = 0; k < f->blocks[b].nsucc; k++) {
            int t = f->blocks[b].succ[k];
            if (x->seen[t]) {
                continue;
            }
            x->seen[t] = 1;
            if (t == x->blk[p] && (t != x->blk[v] || x->idx[p] < x->idx[v])) {
                return 1;
            }
            if (t != x->blk[v]) {
                x->work[nw++] = t;
            }
        }
    }
    return 0;
}

/* The phis of block t that take v on the edge from block b. */
static int
phi_takes(struct lu_func *f, int b, int t, lu_v v)
{
    struct lu_block *bl = &f->blocks[t];
    int p = 0, took = 0;
    while (p < bl->npreds && bl->preds[p] != b) {
        p++;
    }
    for (int i = 0; i < bl->nins && f->ins[bl->ins[i]].op == OP_PHI; i++) {
        struct lu_ins *phi = &f->ins[bl->ins[i]];
        took += !(phi->flags & INS_DEAD) && p < phi->nargs && phi->args[p] == v;
    }
    return took;
}

/* Whether objects a and b (tuples or iterators) have one shape. */
static int
same_shape(struct lu_func *f, lu_v a, lu_v b)
{
    return f->ins[a].imm == f->ins[b].imm && f->ins[a].nargs == f->ins[b].nargs;
}

/* Per value: the object whose shape it has (a tuple or an iterator
   itself, or a phi whose every input has one shape, OBJ_PHI in kind), or
   LU_NOV. A phi's inputs are found optimistically, then a phi any input
   of which has no such shape, or another, drops out. */
static lu_v *
phi_shapes(struct lu_func *f, int *kind)
{
    int n = f->nins;
    lu_v *shape = malloc(sizeof(lu_v) * (size_t)n);
    for (int v = 0; v < n; v++) {
        shape[v] = kind[v] == OBJ_TUPLE || kind[v] == OBJ_ITER ? v : LU_NOV;
    }
    for (int changed = 1; changed;) {
        changed = 0;
        for (int v = 0; v < n; v++) {
            struct lu_ins *in = &f->ins[v];
            if (in->op != OP_PHI || in->rep != REP_OBJ || (in->flags & (INS_DEAD | INS_VIRTUAL)) || shape[v] != LU_NOV) {
                continue;
            }
            for (int i = 0; i < in->nargs && shape[v] == LU_NOV; i++) {
                shape[v] = in->args[i] != LU_NOV ? shape[in->args[i]] : LU_NOV;
            }
            changed |= shape[v] != LU_NOV;
        }
    }
    for (int changed = 1; changed;) {
        changed = 0;
        for (int v = 0; v < n; v++) {
            struct lu_ins *in = &f->ins[v];
            if (in->op != OP_PHI || shape[v] == LU_NOV) {
                continue;
            }
            for (int i = 0; i < in->nargs; i++) {
                lu_v a = in->args[i];
                if (a != v && (a == LU_NOV || shape[a] == LU_NOV || !same_shape(f, shape[a], shape[v]))) {
                    shape[v] = LU_NOV;
                    changed = 1;
                    break;
                }
            }
        }
    }
    for (int v = 0; v < n; v++) {
        if (f->ins[v].op == OP_PHI && shape[v] != LU_NOV) {
            kind[v] = OBJ_PHI;
        }
    }
    return shape;
}

/* A phi is virtual only with every input, and an input only with every
   phi that takes it: a real one keeps the rest real. */
static void
phis_agree(struct lu_func *f, int *kind, int n)
{
    for (int changed = 1; changed;) {
        changed = 0;
        for (int u = 0; u < n; u++) {
            struct lu_ins *in = &f->ins[u];
            if (in->op != OP_PHI || (in->flags & INS_DEAD)) {
                continue;
            }
            for (int i = 0; i < in->nargs; i++) {
                lu_v a = in->args[i];
                if (a == u || a == LU_NOV || a >= n) {
                    continue;
                }
                if (kind[u] == OBJ_PHI && !kind[a]) {
                    kind[u] = not_virtual(f, u, &f->ins[a], "an input stays real");
                    changed = 1;
                }
                else if (kind[u] != OBJ_PHI && kind[a]) {
                    kind[a] = not_virtual(f, a, in, "an argument");
                    changed = 1;
                }
            }
        }
    }
}

/* Each virtual phi's fields: a phi per field of its shape, of its inputs'
   fields, which take the references the inputs held for them. */
static void
phi_fields(struct lu_func *f, int *kind, lu_v *shape, int n)
{
    for (int v = 0; v < n; v++) {
        if (kind[v] != OBJ_PHI) {
            continue;
        }
        int nf = f->ins[shape[v]].nargs, block = f->ins[v].block;
        lu_v *fields = lu_ir_alloc(f, sizeof(lu_v) * (size_t)nf);
        for (int k = 0; k < nf; k++) {
            int rep = f->ins[f->ins[shape[v]].args[k]].rep;
            fields[k] = lu_ir_emit(f, block, OP_PHI, rep, LU_NOV, LU_NOV, LU_NOV, -1);
        }
        f->ins[v].fields = fields;
        f->ins[v].nfields = nf;
        f->ins[v].c = shape[v];
    }
    for (int v = 0; v < n; v++) {
        if (kind[v] != OBJ_PHI) {
            continue;
        }
        struct lu_ins *in = &f->ins[v];
        for (int k = 0; k < in->nfields; k++) {
            struct lu_ins *fk = &f->ins[in->fields[k]];
            fk->nargs = in->nargs;
            fk->args = malloc(sizeof(lu_v) * (size_t)in->nargs);    /* a phi's, freed with it */
            for (int i = 0; i < in->nargs; i++) {
                struct lu_ins *a = &f->ins[in->args[i]];
                fk->args[i] = a->op == OP_PHI ? a->fields[k] : a->args[k];
            }
        }
        in->nargs = 0;          /* its fields' phis move the values */
    }
    fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
    for (int v = n; v < f->nins; v++) {
        fwd[v] = v;
    }
}

/* The release that ends v, per instruction (1), found by counting its
   references along every path; 0 when the counts disagree or an exit
   taken while it lives would not name it. */
static int
object_counts(struct objs *x, lu_v v, char *ends)
{
    struct lu_func *f = x->f;
    int *rc = malloc(sizeof(int) * (size_t)f->nblocks);
    for (int b = 0; b < f->nblocks; b++) {
        rc[b] = -1;
    }
    rc[0] = 0;
    const char *why = NULL;
    lu_v at = LU_NOV;
    for (int i = 0; i < x->g.n && why == NULL; i++) {
        int b = x->g.rpo[i], n = rc[b];
        if (n < 0) {
            continue;
        }
        struct lu_block *bl = &f->blocks[b];
        for (int k = 0; k < bl->nins && why == NULL; k++) {
            lu_v u = bl->ins[k];
            struct lu_ins *in = &f->ins[u];
            at = u;
            if (in->flags & INS_DEAD) {
                continue;
            }
            if (u == v) {
                why = n != 0 ? "made again while it lives" : NULL;
                n = 1;
                continue;
            }
            if (in->op == OP_INCREF && in->a == v) {
                n++;
            }
            else if (in->op == OP_DECREF && in->a == v) {
                why = n <= 0 ? "released more than taken" : NULL;
                ends[u] = --n == 0;
                if (ends[u] && (x->mark[u] & 2)) {
                    why = "its last release's state names it";  /* its fields' releases publish that state */
                }
                continue;
            }
            int own = (in->op == OP_INLINE_STORE || in->op == OP_INLINE_LOAD || in->op == OP_GUARD_TYPE)
                && in->a == v;
            if (!own && (x->mark[u] & 2)) {
                why = n <= 0 ? "a state names it where it is gone" : NULL;
            }
            else if (!own && n > 0 && exits(in)) {
                why = "an exit while it lives does not name it";
            }
            else if (in->op == OP_RETURN && n != 0) {
                why = "it lives at a return";
            }
        }
        for (int k = 0; k < bl->nsucc && why == NULL; k++) {
            int t = bl->succ[k];
            /* A phi of the successor that takes it takes its reference. */
            int took = phi_takes(f, b, t, v);
            int m = n - took;
            why = took > 1 || (took && m != 0) ? "a phi takes it while it lives on" : NULL;
            if (rc[t] < 0) {
                rc[t] = m;
            }
            why = why != NULL ? why : rc[t] != m ? "its count differs by path" : NULL;
            at = bl->ins[bl->nins - 1];
        }
    }
    free(rc);
    if (why != NULL) {
        not_virtual(f, v, &f->ins[at], why);
    }
    return why == NULL;
}

/* Whether instance v's stores make one set of fields at every use. */
static int
object_fields(struct objs *x, lu_v v, lu_v *uses, int nuses, lu_v *stores, int nstores)
{
    struct lu_func *f = x->f;
    for (int i = 0; i < nstores; i++) {
        for (int j = 0; j < nstores; j++) {
            struct lu_ins *a = &f->ins[stores[i]], *b = &f->ins[stores[j]];
            if (i != j && (a->imm == b->imm || (before_ins(x, stores[i], stores[j]) && a->imm > b->imm))) {
                return 0;       /* a name stored twice, or out of the names' order */
            }
        }
        for (int k = 0; k < nuses; k++) {
            if (uses[k] != stores[i] && !before_ins(x, stores[i], uses[k]) && reaches(x, v, stores[i], uses[k])) {
                return 0;
            }
        }
    }
    for (int k = 0; k < nuses; k++) {
        struct lu_ins *u = &f->ins[uses[k]];
        if (u->op != OP_INLINE_LOAD) {
            continue;
        }
        int found = 0;
        for (int i = 0; i < nstores; i++) {
            found |= f->ins[stores[i]].imm == u->imm && before_ins(x, stores[i], uses[k]);
        }
        if (!found) {
            return 0;           /* a load of a name not stored: it leaves */
        }
    }
    return 1;
}

/* Whether a field's value holds no reference: a raw word, a bool, an
   immortal constant. Another constant's is counted, since a phi of fields
   may join it with values that are not constants. */
static int
immortal_value(struct lu_func *f, lu_v v)
{
    struct lu_ins *in = &f->ins[v];
    return in->rep != REP_OBJ || in->op == OP_BOOL
        || (in->op == OP_CONST && (in->imm == 0 || _Py_IsImmortal((PyObject *)(intptr_t)in->imm)));
}

static lu_v
insert_before(struct lu_func *f, lu_v at, int op, lu_v a, int64_t imm)
{
    int b = f->ins[at].block;
    lu_v v = lu_ir_emit(f, b, op, REP_NONE, a, LU_NOV, LU_NOV, imm);
    struct lu_block *bl = &f->blocks[b];
    int k = 0;
    while (bl->ins[k] != at) {
        k++;
    }
    memmove(&bl->ins[k + 1], &bl->ins[k], sizeof(int) * (size_t)(bl->nins - 1 - k));
    bl->ins[k] = v;
    return v;
}

/* The value a load of v at u reads. */
static lu_v
field_at(struct lu_func *f, lu_v v, lu_v u, int64_t off)
{
    struct lu_ins *o = &f->ins[v];
    if (o->op == OP_PHI || o->imm == H_build_tuple || lu_iter_type(o) != NULL) {
        return o->op == OP_PHI ? o->fields[off] : o->args[off];
    }
    for (int i = 0; i < o->nfields; i++) {
        if (f->ins[o->fields[i]].imm == off && lu_ssa_before(f, o->fields[i], u)) {
            return f->ins[o->fields[i]].b;
        }
    }
    return LU_NOV;
}

/* v's shape: its build, or a virtual phi's input that is one. */
static struct lu_ins *
shape_of(struct lu_func *f, lu_v v)
{
    return &f->ins[f->ins[v].op == OP_PHI ? f->ins[v].c : v];
}

static void
object_rewrite(struct lu_func *f, lu_v v, int k, lu_v *uses, int nuses, char *ends)
{
    if (k == OBJ_TUPLE || k == OBJ_ITER) {
        for (int i = 0, nargs = f->ins[v].nargs; i < nargs; i++) {
            if (!immortal_value(f, f->ins[v].args[i])) {
                insert_before(f, v, OP_INCREF, f->ins[v].args[i], 0);
            }
        }
    }
    else if (k == OBJ_PHI) {
        /* Its fields' phis hold the references its inputs' fields moved in (phi_fields). */
    }
    else {
        int n = 0;
        for (int i = 0; i < nuses; i++) {
            n += f->ins[uses[i]].op == OP_INLINE_STORE && f->ins[uses[i]].a == v;
        }
        lu_v *fields = lu_ir_alloc(f, sizeof(lu_v) * (size_t)(n ? n : 1));
        n = 0;
        for (int i = 0; i < nuses; i++) {
            struct lu_ins *u = &f->ins[uses[i]];
            if (u->op == OP_INLINE_STORE && u->a == v) {
                fields[n++] = uses[i];
                if (!immortal_value(f, u->b)) {
                    insert_before(f, uses[i], OP_INCREF, f->ins[uses[i]].b, 0);
                }
                f->ins[uses[i]].flags |= INS_VIRTUAL;
                f->ins[uses[i]].fs = NULL;     /* it never leaves */
            }
        }
        f->ins[v].fields = fields;
        f->ins[v].nfields = n;
    }
    f->ins[v].flags |= INS_VIRTUAL;
    struct lu_ins *sh = shape_of(f, v);
    PyTypeObject *tp = object_type(f, sh);
    lu_v type = sh->imm == H_new_object ? sh->args[0] : entry_const(f, OP_CONST, REP_OBJ, (int64_t)(intptr_t)tp);
    sh = NULL;              /* entry_const may move the instructions */
    for (int i = 0; i < nuses; i++) {
        lu_v u = uses[i];
        struct lu_ins *in = &f->ins[u];
        int operand = in->a == v || in->b == v || in->c == v || (in->op == OP_HELPER && in->nargs > 0 && in->args[0] == v);
        if (!operand) {
            continue;           /* a frame state names it */
        }
        switch (in->op) {
        case OP_INCREF:
            in->flags |= INS_DEAD;
            break;
        case OP_DECREF: {
            if (ends[u]) {
                lu_v vals[64];
                int32_t offs[64];
                int nv = f->ins[v].nargs > f->ins[v].nfields ? f->ins[v].nargs : f->ins[v].nfields;
                lu_v *pv = nv <= 64 ? vals : malloc(sizeof(lu_v) * (size_t)nv);
                int32_t *po = nv <= 64 ? offs : malloc(sizeof(int32_t) * (size_t)nv);
                nv = lu_object_fields(f, v, u, pv, po);
                for (int j = 0; j < nv; j++) {
                    if (!immortal_value(f, pv[j])) {
                        int seen = !plain(f, pv[j], 0);
                        lu_v d = insert_before(f, u, OP_DECREF, pv[j], seen);
                        f->ins[d].fs = seen ? f->ins[u].fs : NULL;
                    }
                }
                if (pv != vals) {
                    free(pv);
                    free(po);
                }
            }
            f->ins[u].flags |= INS_DEAD;
            break;
        }
        case OP_IS:
            in->op = OP_CONST_I;
            in->imm = in->a == in->b;
            in->a = in->b = LU_NOV;
            break;
        case OP_GUARD_TYPE:
            in->flags |= INS_DEAD;
            break;
        case OP_GUARD_VERSION:
            in->a = type;
            in->imm2 = GV_TYPE_ITSELF;
            break;
        case OP_LOAD:
            if (in->imm == offsetof(PyObject, ob_type)) {
                kill(f, u, type);
            }
            else if (shape_of(f, v)->imm != H_build_tuple) {
                kill(f, u, field_at(f, v, u, iter_field(f, shape_of(f, v), in)));
            }
            else if (in->imm == offsetof(PyVarObject, ob_size)) {
                kill(f, u, entry_const(f, OP_CONST_I, REP_I64, shape_of(f, v)->nargs));
            }
            else {
                kill(f, u, field_at(f, v, u, (in->imm - (int64_t)offsetof(PyTupleObject, ob_item)) / 8));
            }
            break;
        case OP_LIST_ITEM: case OP_INLINE_LOAD: {
            /* A new reference to the field's value. */
            int64_t i = in->op == OP_LIST_ITEM ? f->ins[in->b].imm : 0;
            int nt = shape_of(f, v)->nargs;
            lu_v w = field_at(f, v, u, in->op == OP_LIST_ITEM ? (i < 0 ? i + nt : i) : in->imm);
            in = &f->ins[u];
            in->op = OP_INCREF;
            in->rep = REP_NONE;
            in->a = w;
            in->b = in->c = LU_NOV;
            in->fs = NULL;
            fwd[u] = w;
            break;
        }
        case OP_HELPER:         /* dict_lacks: it has no dict */
            in->op = OP_CONST_I;
            in->rep = REP_BIT;
            in->imm = 1;
            in->nargs = 0;
            in->fs = NULL;
            break;
        default:
            break;
        }
    }
}

/* Stores into an instance this code made that no other thread can have
   seen yet, of a name it does not have yet (INS_FRESH): they take no lock
   and release nothing, so they never park or run a finalizer, and
   publish nothing. The instance may have reached another thread once any
   use of it but its own stores, loads, guards and reference operations
   has run (it was passed, stored, returned or joined), or a state a
   walker can read names it in a local: a store any of those reaches is
   not fresh, nor is one another store of its name reaches; and a store
   that is not fresh publishes, so it is such a use for the stores after. */
static void
fresh_stores(struct objs *x, lu_v v)
{
    struct lu_func *f = x->f;
    int n = x->nold, nst = 0, nesc = 0;
    lu_v *st = malloc(sizeof(lu_v) * (size_t)(n + 1)), *esc = malloc(sizeof(lu_v) * (size_t)(n + 1));
    for (int u = 0; u < n; u++) {
        struct lu_ins *in = &f->ins[u];
        if (in->flags & INS_DEAD) {
            continue;
        }
        if (in->op == OP_INLINE_STORE && in->a == v && in->b != v) {
            st[nst++] = u;
            continue;
        }
        int escapes = 0;
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3; i++) {
            escapes |= ops[i] == v && !object_use(f, in, i, v, OBJ_INSTANCE);
        }
        for (int i = 0; i < in->nargs; i++) {
            escapes |= in->args[i] == v && (in->op == OP_PHI || !object_use(f, in, 3, v, OBJ_INSTANCE));
        }
        for (struct lu_fs *fs = in->fs; fs != NULL && !escapes && lu_publishes(in); fs = fs->caller) {
            for (int i = 0; i < fs->nlocals; i++) {
                escapes |= fs->vals[i] == v;
            }
        }
        if (escapes) {
            esc[nesc++] = u;
        }
    }
    char *stale = calloc((size_t)(nst ? nst : 1), 1);
    for (int changed = 1; changed;) {
        changed = 0;
        for (int i = 0; i < nst; i++) {
            int fresh = !stale[i];
            for (int k = 0; k < nesc && fresh; k++) {
                fresh = !reaches(x, v, esc[k], st[i]);
            }
            for (int k = 0; k < nst && fresh; k++) {
                fresh = k == i || f->ins[st[k]].imm != f->ins[st[i]].imm || !reaches(x, v, st[k], st[i]);
            }
            if (!fresh && !stale[i]) {
                stale[i] = 1;
                esc[nesc++] = st[i];
                changed = 1;
            }
        }
    }
    for (int i = 0; i < nst; i++) {
        if (!stale[i]) {
            f->ins[st[i]].flags |= INS_FRESH;
        }
    }
    free(stale);
    free(st);
    free(esc);
}

static int
objects(struct lu_func *f)
{
    int n = f->nins, changed = 0;
    int *kind = calloc((size_t)n, sizeof(int)), ncand = 0;
    for (int v = 0; v < n; v++) {
        kind[v] = object_kind(f, v);
        ncand += kind[v] != OBJ_NONE || (f->ins[v].op == OP_HELPER && f->ins[v].imm == H_new_object);
    }
    if (ncand == 0) {
        free(kind);
        return 0;
    }
    lu_v *shape = phi_shapes(f, kind);
    struct objs x = { .f = f, .nold = n };
    x.mark = calloc((size_t)n, 1);
    cfg_build(f, &x.g);
    x.blk = malloc(sizeof(int) * (size_t)n);
    x.idx = malloc(sizeof(int) * (size_t)n);
    for (int v = 0; v < n; v++) {
        x.blk[v] = -1;          /* in no block */
    }
    x.seen = malloc((size_t)f->nblocks);
    x.work = malloc(sizeof(int) * (size_t)f->nblocks);
    for (int b = 0; b < f->nblocks; b++) {
        for (int i = 0; i < f->blocks[b].nins; i++) {
            x.blk[f->blocks[b].ins[i]] = b;
            x.idx[f->blocks[b].ins[i]] = i;
        }
    }
    /* A release of a box, a bool, an immortal, or a value an unbox before it
       proved an exact int or float runs no finalizer, so it publishes nothing. */
    lu_v *unboxed = malloc(sizeof(lu_v) * (size_t)n);
    for (int u = 0; u < n; u++) {
        unboxed[u] = LU_NOV;
    }
    for (int u = n - 1; u >= 0; u--) {
        struct lu_ins *in = &f->ins[u];
        if ((in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT) && !(in->flags & INS_DEAD) && in->a != LU_NOV) {
            unboxed[in->a] = u;
        }
    }
    for (int d = 0; d < n; d++) {
        struct lu_ins *r = &f->ins[d];
        if (r->op != OP_DECREF || r->imm != 1 || r->a == LU_NOV || (r->flags & INS_DEAD)) {
            continue;
        }
        lu_v u = unboxed[r->a];
        if (plain(f, r->a, 0) || quiet_display(f, r->a)
            || (u != LU_NOV && reached(&x, d) && reached(&x, u) && before_ins(&x, u, d))) {
            r->imm = 0;
            r->fs = NULL;
        }
    }
    free(unboxed);
    /* Fresh stores first: they publish nothing, so they keep no object real. */
    for (int v = 0; v < n; v++) {
        if (f->ins[v].op == OP_HELPER && f->ins[v].imm == H_new_object && !(f->ins[v].flags & INS_DEAD)
            && reached(&x, v)) {
            fresh_stores(&x, v);
        }
    }
    /* Disqualifying uses; the rest listed per object. */
    int *nuses = calloc((size_t)n, sizeof(int));
    for (int u = 0; u < n; u++) {
        struct lu_ins *in = &f->ins[u];
        if (in->flags & INS_DEAD) {
            continue;
        }
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3; i++) {
            if (ops[i] != LU_NOV && kind[ops[i]]) {
                lu_v sh = kind[ops[i]] == OBJ_PHI ? shape[ops[i]] : ops[i];
                if (object_use(f, in, i, sh, object_kind(f, sh))) {
                    nuses[ops[i]]++;
                }
                else {
                    kind[ops[i]] = not_virtual(f, ops[i], in, "a use");
                }
            }
        }
        for (int i = 0; i < in->nargs; i++) {
            lu_v a = in->args[i];
            if (a != LU_NOV && kind[a]) {
                lu_v sh = kind[a] == OBJ_PHI ? shape[a] : a;
                if (in->op == OP_PHI ? kind[u] == OBJ_PHI : object_use(f, in, 3, sh, object_kind(f, sh))) {
                    nuses[a]++;
                }
                else {
                    kind[a] = not_virtual(f, a, in, "an argument");
                }
            }
        }
        /* A walker reads the locals of a state an instruction publishes,
           the stack only an exit; the object's own stores and reference
           operations go with it (its last release's state cannot name it). */
        int published = in->fs != NULL && lu_publishes(in);
        for (struct lu_fs *fs = in->fs; fs != NULL; fs = fs->caller) {
            for (int i = 0; i < fs->nlocals + fs->nstack; i++) {
                lu_v a = fs->vals[i];
                if (a != LU_NOV && kind[a]) {
                    nuses[a]++;
                    int own = in->a == a && (in->op == OP_INLINE_STORE || in->op == OP_DECREF || in->op == OP_INCREF);
                    if (published && i < fs->nlocals && !own) {
                        kind[a] = not_virtual(f, a, in, "a local where a walker reads it");
                    }
                }
            }
        }
    }
    phis_agree(f, kind, n);
    /* Each object's uses, in order of instruction: 1 an operand, 2 its state names it. */
    lu_v **ul = calloc((size_t)n, sizeof(lu_v *));
    char **uf = calloc((size_t)n, sizeof(char *));
    int *nul = calloc((size_t)n, sizeof(int));
    for (int v = 0; v < n; v++) {
        if (kind[v]) {
            ul[v] = malloc(sizeof(lu_v) * (size_t)(nuses[v] + 1));
            uf[v] = malloc((size_t)(nuses[v] + 1));
        }
    }
    for (int u = 0; u < n; u++) {
        struct lu_ins *in = &f->ins[u];
        if (in->flags & INS_DEAD) {
            continue;
        }
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3 + in->nargs; i++) {
            lu_v a = i < 3 ? ops[i] : in->args[i - 3];
            if (a != LU_NOV && kind[a]) {
                if (nul[a] == 0 || ul[a][nul[a] - 1] != u) {
                    ul[a][nul[a]] = u;
                    uf[a][nul[a]++] = 0;
                }
                uf[a][nul[a] - 1] |= 1;
            }
        }
        for (struct lu_fs *fs = in->fs; fs != NULL; fs = fs->caller) {
            for (int i = 0; i < fs->nlocals + fs->nstack; i++) {
                lu_v a = fs->vals[i];
                if (a != LU_NOV && kind[a]) {
                    if (nul[a] == 0 || ul[a][nul[a] - 1] != u) {
                        ul[a][nul[a]] = u;
                        uf[a][nul[a]++] = 0;
                    }
                    uf[a][nul[a] - 1] |= 2;
                }
            }
        }
    }
    char *ends = calloc((size_t)n, 1);
    f->idom = x.g.idom;         /* lu_ssa_before's, while it rewrites */
    for (int v = 0; v < n; v++) {
        if (!kind[v] || !reached(&x, v)) {
            kind[v] = OBJ_NONE;
            continue;
        }
        lu_v *uses = ul[v];
        int nu = nul[v], ns = 0;
        lu_v *stores = malloc(sizeof(lu_v) * (size_t)(nu + 1));
        for (int i = 0; i < nu; i++) {
            x.mark[uses[i]] = uf[v][i];
            if (f->ins[uses[i]].op == OP_INLINE_STORE && f->ins[uses[i]].a == v) {
                stores[ns++] = uses[i];
            }
        }
        int ok = object_counts(&x, v, ends) && (kind[v] != OBJ_INSTANCE || object_fields(&x, v, uses, nu, stores, ns)
                                               || not_virtual(f, v, NULL, "its fields differ by path"));
        for (int i = 0; i < nu; i++) {
            x.mark[uses[i]] = 0;
        }
        if (!ok) {
            kind[v] = OBJ_NONE;
        }
        free(stores);
    }
    /* A phi and its inputs are virtual together. */
    phis_agree(f, kind, n);
    phi_fields(f, kind, shape, n);
    for (int v = 0; v < n; v++) {
        if (kind[v]) {
            object_rewrite(f, v, kind[v], ul[v], nul[v], ends);
            changed = 1;
        }
    }
    for (int v = 0; v < n; v++) {
        free(ul[v]);
        free(uf[v]);
    }
    free(ul);
    free(uf);
    free(nul);
    free(ends);
    free(x.mark);
    f->idom = NULL;
    free(x.blk);
    free(x.idx);
    free(x.seen);
    free(x.work);
    cfg_free(&x.g);
    free(kind);
    free(shape);
    free(nuses);
    if (changed) {
        fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
        for (int v = n; v < f->nins; v++) {
            fwd[v] = v;
        }
    }
    return changed;
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
        else if (in->op == OP_INLINE_STORE && !(in->flags & INS_VIRTUAL)) {
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

/* Whether in returns a box of a raw value (one that cannot stand for NULL). */
static int
returns_raw(struct lu_func *f, struct lu_ins *in)
{
    struct lu_ins *b = in->op == OP_RETURN && in->a != LU_NOV ? &f->ins[in->a] : NULL;
    return b != NULL && (b->op == OP_BOX_INT || b->op == OP_BOX_FLOAT) && b->b == LU_NOV && b->c == LU_NOV;
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
        if (in->flags & (INS_DEAD | INS_VIRTUAL)) {
            continue;           /* a virtual object's fields are like a frame state's values */
        }
        int allowed = in->op == OP_DECREF || in->op == OP_INCREF || in->op == OP_UNBOX_INT
            || in->op == OP_UNBOX_FLOAT || in->op == OP_GUARD_NOT_NULL
            || (in->op == OP_TRUTH && f->ins[in->a].op == OP_HELPER) || returns_raw(f, in);
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
        /* A return makes the box it returns: one the loop carried raw
           (s = 0, s += x, return s) is made once, where it leaves. */
        struct lu_ins *in = &f->ins[v];
        if (!(in->flags & INS_DEAD) && returns_raw(f, in) && ok[in->a]) {
            lu_v raw = f->ins[in->a].a;
            int op = f->ins[in->a].op;
            lu_v box = insert_before(f, v, op, raw, 0);
            f->ins[v].a = box;
        }
    }
    fwd = realloc(fwd, sizeof(lu_v) * (size_t)f->capins);
    for (int v = n; v < f->nins; v++) {
        fwd[v] = v;
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

/* 5b. A bool only frame states hold (one a test pushed for the branch
   after it) is made by the exits that need it, from its bit, its NOTs
   folded into imm (1: negated). Lent frames' locals stay real
   (lu_jit_frames_push reads them). */
static void
virtual_bools(struct lu_func *f)
{
    int n = f->nins;
    char *ok = calloc((size_t)n, 1);
    for (int v = 0; v < n; v++) {
        ok[v] = f->ins[v].op == OP_BOOL && !(f->ins[v].flags & INS_DEAD);
    }
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        lu_v ops[3] = { in->a, in->b, in->c };
        for (int i = 0; i < 3; i++) {
            if (ops[i] != LU_NOV && in->op != OP_DECREF && in->op != OP_INCREF) {
                ok[ops[i]] = 0;
            }
        }
        for (int i = 0; i < in->nargs; i++) {
            if (in->args[i] != LU_NOV) {
                ok[in->args[i]] = 0;
            }
        }
        if (in->op == OP_HELPER && (lu_helpers[in->imm].flags & IF_PY) && in->fs != NULL) {
            for (struct lu_fs *fs = in->fs; fs->caller != NULL; fs = fs->caller) {
                for (int i = 0; i < fs->nlocals; i++) {
                    if (fs->vals[i] != LU_NOV) {
                        ok[fs->vals[i]] = 0;
                    }
                }
            }
        }
    }
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (!ok[v]) {
            continue;
        }
        in->flags |= INS_VIRTUAL;
        while (f->ins[in->a].op == OP_NOT) {
            in->a = f->ins[in->a].a;
            in->imm ^= 1;
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
    /* Twice: an object's release that kept another real may go with it. */
    for (int pass = 0; pass < 2 && objects(f); pass++) {
        rewrite(f);
        while (trivial_phis(f)) {
        }
        rewrite(f);
        for (int round = 0; round < 4 && simplify(f); round++) {
            rewrite(f);
        }
        rewrite(f);
    }
    if (raw_fields(f)) {
        simplify(f);
        rewrite(f);
    }
    escape(f);
    virtual_bools(f);
    dce(f);
    split_edges(f);
    order_blocks(f);
    compact(f);
    struct cfg g;
    cfg_build(f, &g);
    f->idom = g.idom;           /* for lu_ssa_before */
    free(g.rpo);
    free(g.at);
    free(fwd);
    fwd = NULL;
    return 0;
}
