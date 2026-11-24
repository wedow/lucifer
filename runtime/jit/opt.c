/* What is left to do over the finished graph (docs/design/jit-architecture.md
   2, optimizer): the evaluator folded, forwarded and kept virtual what it
   could while it built, so the passes here only tidy. Phis every input of
   which is one value go, with their uses forwarded, and a branch they make
   constant jumps; values nothing reads go; critical edges get a block for
   their phi moves; blocks are ordered for emission. */
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>

static __thread lu_v *fwd;      /* the pass in progress on this thread */

static lu_v
resolve(lu_v v)
{
    if (v < 0) {
        return v;               /* nothing, or a virtual object */
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

/* Applies the forwarding map to every operand, state and virtual object. */
static void
rewrite(struct lu_func *f)
{
    uint32_t mark = ++f->marks;
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
            lu_fs_rewrite(f, in->fs, mark, resolve);
            for (int i = 0; i < in->fs->nbinds; i++) {
                struct lu_vobj *o = in->fs->binds[i].obj;
                for (int k = 0; o != NULL && k < o->n; k++) {
                    o->f[k] = resolve(o->f[k]);
                }
            }
        }
    }
    for (int i = 0; i < f->nvobjs; i++) {
        struct lu_vobj *o = f->vobjs[i];
        for (int k = 0; k < o->n; k++) {
            o->f[k] = resolve(o->f[k]);
        }
    }
}

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
        /* An owned phi took a reference on each edge (eval_flow.c): its
           inputs did not carry it, so it stays, unless it is only ever NULL. */
        int null = same != LU_NOV && f->ins[same].op == OP_CONST && f->ins[same].imm == 0;
        if (trivial && same != LU_NOV && (!(in->flags & INS_OWNED) || null)) {
            in->flags |= INS_DEAD;
            fwd[v] = same;
            changed = 1;
        }
    }
    return changed;
}

/* The constant a condition has once phis are forwarded, or -1. */
static int
known_bit(struct lu_func *f, lu_v v)
{
    struct lu_ins *in = &f->ins[resolve(v)];
    if (in->op == OP_CONST_I) {
        return in->imm != 0;
    }
    if (in->op != OP_CMP_I || f->ins[resolve(in->a)].op != OP_CONST_I || f->ins[resolve(in->b)].op != OP_CONST_I) {
        return -1;
    }
    int64_t p = f->ins[resolve(in->a)].imm, q = f->ins[resolve(in->b)].imm;
    int op = (int)in->imm;
    return op == Py_LT ? p < q : op == Py_LE ? p <= q : op == Py_EQ ? p == q
        : op == Py_NE ? p != q : op == Py_GT ? p > q : p >= q;
}

/* A branch on a condition a trivial phi made constant jumps: the arm it
   never takes loses the edge, and is dead if nothing else reaches it. A
   peeled loop's first turn tests for every yield of the generator it runs,
   and only its start can come there: the other arms' states are not the
   generator's (an unbound local it reads), so they must not be emitted. */
static int
fold_branches(struct lu_func *f)
{
    int changed = 0;
    for (int b = 0; b < f->nblocks; b++) {
        struct lu_block *bl = &f->blocks[b];
        struct lu_ins *term = bl->nins ? &f->ins[bl->ins[bl->nins - 1]] : NULL;
        int bit = term != NULL && term->op == OP_BRANCH && bl->succ[0] != bl->succ[1] ? known_bit(f, term->a) : -1;
        if (bit < 0) {
            continue;
        }
        struct lu_block *gone = &f->blocks[bl->succ[bit]];
        term->op = OP_JUMP;
        term->a = LU_NOV;
        bl->succ[0] = bl->succ[!bit];
        bl->nsucc = 1;
        int p = 0;
        while (gone->preds[p] != b) {
            p++;
        }
        gone->npreds--;
        memmove(gone->preds + p, gone->preds + p + 1, sizeof(int) * (size_t)(gone->npreds - p));
        for (int i = 0; i < gone->nins && f->ins[gone->ins[i]].op == OP_PHI; i++) {
            struct lu_ins *phi = &f->ins[gone->ins[i]];
            if (p < phi->nargs) {
                phi->nargs--;
                memmove(phi->args + p, phi->args + p + 1, sizeof(lu_v) * (size_t)(phi->nargs - p));
            }
        }
        changed = 1;
    }
    return changed;
}

/* An attribute read from an instance's inline values only to be unboxed
   is read as the number (INLINE_LOAD_I, _F), so that a value stored
   unboxed (eval_attr.c) never becomes an object. The load qualifies when
   compiled code does nothing with its value but unbox it, as one kind and
   in the load's own block, so that the read leaves no oftener than the
   unboxing did. What frame states name is then a number that is no
   object yet (a virtual box), in one of two forms:

   - States name it on their stacks only: an exit makes an object for the
     instruction that would have consumed it, which no program can tell
     from the attribute's. Nothing is held.
   - States name it in a local or in an object that does not exist, where
     the program could compare it with the attribute's own object. The
     load keeps the object the word held when it held one
     (INLINE_ORIGIN), and the box names the instance, so that an exit
     takes the attribute's object while the attribute is still that
     number (docs/design/jit-architecture.md 10, concession 3). Every
     state that names the value must name the instance too. */
enum { WANT_INT = 1, WANT_FLOAT = 2, WANT_BAD = 4, WANT_DEEP = 8, WANT_LOOSE = 16 };

struct naming {
    uint8_t *want;
    int *seen;                  /* per value: 1 + the instruction whose state last named it */
    int at;
};

static void
note_named(struct lu_func *f, lu_v v, void *arg)
{
    struct naming *n = arg;
    n->seen[v] = n->at + 1;
}

/* A load a state names without its instance cannot find the attribute
   again at an exit. */
static void
note_loose(struct lu_func *f, lu_v v, void *arg)
{
    struct naming *n = arg;
    struct lu_ins *in = &f->ins[v];
    if (in->op == OP_INLINE_LOAD && n->seen[in->a] != n->at + 1) {
        n->want[v] |= WANT_LOOSE;
    }
}

static void
record_names(struct lu_vobj *o, uint8_t *want)
{
    for (int k = 0; o != NULL && k < o->n; k++) {
        if (o->f[k] >= 0) {
            want[o->f[k]] |= WANT_DEEP;
        }
    }
}

/* The slot or field names the load's box from now on. */
static void
sink(lu_v *r, const lu_v *box)
{
    if (*r >= 0 && box[*r] != LU_NOV) {
        *r = box[*r];
    }
}

static void
sink_record(struct lu_vobj *o, const lu_v *box)
{
    for (int k = 0; o != NULL && k < o->n; k++) {
        sink(&o->f[k], box);
    }
}

static int
raw_loads(struct lu_func *f)
{
    /* Only a load some unboxing reads in its block qualifies: without one
       there is nothing to learn from the states. */
    int any = 0;
    for (int v = 0; v < f->nins && !any; v++) {
        struct lu_ins *in = &f->ins[v];
        any = !(in->flags & INS_DEAD) && (in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT) && in->a >= 0
            && f->ins[in->a].op == OP_INLINE_LOAD && in->block == f->ins[in->a].block;
    }
    if (!any) {
        return 0;
    }
    int n = f->nins, records = f->nvobjs, changed = 0;
    uint8_t *want = calloc((size_t)n + 1, 1);
    char *unboxed = calloc((size_t)n + 1, 1);
    struct naming names = { want, calloc((size_t)n + 1, sizeof(int)), 0 };
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        lu_v x = in->a;
        int unbox = in->op == OP_UNBOX_INT ? WANT_INT : in->op == OP_UNBOX_FLOAT ? WANT_FLOAT : 0;
        if (x >= 0 && unbox && in->block == f->ins[x].block) {
            want[x] |= unbox;
            unboxed[x] = 1;     /* a type guard asks for no number: only a load that is unboxed counts */
        }
        else if (x >= 0 && in->op == OP_GUARD_TYPE && (in->imm == (int64_t)(intptr_t)&PyLong_Type
                                                         || in->imm == (int64_t)(intptr_t)&PyFloat_Type)) {
            want[x] |= in->imm == (int64_t)(intptr_t)&PyLong_Type ? WANT_INT : WANT_FLOAT;
        }
        else if (x >= 0 && in->op != OP_DECREF) {
            want[x] |= WANT_BAD;
        }
        if (in->b >= 0) {
            want[in->b] |= WANT_BAD;
        }
        if (in->c >= 0) {
            want[in->c] |= WANT_BAD;
        }
        for (int i = 0; i < in->nargs; i++) {
            if (in->args[i] >= 0) {
                want[in->args[i]] |= WANT_BAD;
            }
        }
        if (in->fs == NULL) {
            continue;
        }
        for (int i = 0; i < in->fs->nbinds; i++) {
            if (in->fs->binds[i].real >= 0) {
                want[in->fs->binds[i].real] |= WANT_BAD;
            }
            record_names(in->fs->binds[i].obj, want);
        }
        for (struct lu_fs *p = in->fs; p != NULL; p = p->caller) {
            for (int i = 0; i < p->nlocals; i++) {
                if (p->locals[i] >= 0) {
                    want[p->locals[i]] |= WANT_DEEP;
                }
            }
        }
        struct lu_fs slots = *in->fs;
        slots.nheld = 0;
        names.at = v;
        lu_fs_each(f, &slots, note_named, &names);
        lu_fs_each(f, &slots, note_loose, &names);
    }
    for (int i = 0; i < f->nvobjs; i++) {
        record_names(f->vobjs[i], want);
    }
    lu_v *box = malloc(sizeof(lu_v) * (size_t)(n + 1));
    for (int v = 0; v < n; v++) {
        box[v] = LU_NOV;
        int kind = want[v] & (WANT_INT | WANT_FLOAT | WANT_BAD), deep = want[v] & WANT_DEEP;
        if ((f->ins[v].flags & INS_DEAD) || f->ins[v].op != OP_INLINE_LOAD || LU_LOAD_CLASS_VALUE(f->ins[v].imm2) != 0 || !unboxed[v]
            || (kind != WANT_INT && kind != WANT_FLOAT) || (deep && (want[v] & WANT_LOOSE))) {
            continue;
        }
        int fl = kind == WANT_FLOAT, op = fl ? OP_INLINE_LOAD_F : OP_INLINE_LOAD_I, rep = fl ? REP_F64 : REP_I64;
        struct lu_vobj *o = lu_ir_vobj(f, fl ? VK_BOX_FLOAT : VK_BOX_INT, fl ? &PyFloat_Type : &PyLong_Type, deep ? 4 : 1);
        box[v] = LU_VIRT(lu_ir_vobj_id(f, o));
        changed = 1;
        if (!deep) {
            o->f[0] = v;
            f->ins[v].op = (uint8_t)op;
            f->ins[v].rep = (uint8_t)rep;
            f->ins[v].flags &= ~INS_OWNED;
            continue;
        }
        /* The load becomes the word's object, and the number is read after it. */
        int block = f->ins[v].block;
        lu_v r = lu_ir_emit(f, block, op, rep, f->ins[v].a, v, LU_NOV, f->ins[v].imm);
        struct lu_ins *in = &f->ins[v], *raw = &f->ins[r];
        struct lu_block *bl = &f->blocks[block];
        int at = 0;
        while (bl->ins[at] != v) {
            at++;
        }
        memmove(&bl->ins[at + 2], &bl->ins[at + 1], sizeof(int) * (size_t)(bl->nins - at - 2));
        bl->ins[at + 1] = r;
        raw->src = in->src;
        raw->src_pc = in->src_pc;
        raw->imm2 = in->imm2;   /* the kind and whether the flag goes first */
        raw->fs = lu_ir_alloc(f, sizeof(struct lu_fs));
        *raw->fs = *in->fs;     /* an exit from the read gives the object back */
        raw->fs->held = lu_ir_alloc(f, sizeof(lu_v) * (size_t)(in->fs->nheld + 1));
        memcpy(raw->fs->held, in->fs->held, sizeof(lu_v) * (size_t)in->fs->nheld);
        raw->fs->held[raw->fs->nheld++] = v;
        in->op = OP_INLINE_ORIGIN;
        o->f[0] = r;
        o->f[1] = v;
        o->f[2] = in->a;
        o->f[3] = lu_ir_emit(f, -1, OP_CONST_I, REP_I64, LU_NOV, LU_NOV, LU_NOV, in->imm);
    }
    if (changed) {
        fwd = realloc(fwd, sizeof(lu_v) * (size_t)(f->nins + 1));
        for (int v = n; v < f->nins; v++) {
            fwd[v] = v;
        }
    }
    for (int v = 0; changed && v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        if (in->fs != NULL) {
            for (int i = 0; i < in->fs->nbinds; i++) {
                sink_record(in->fs->binds[i].obj, box);
            }
            for (struct lu_fs *p = in->fs; p != NULL; p = p->caller) {
                for (int i = 0; i < p->nlocals + p->nstack; i++) {
                    sink(lu_fs_slot(p, i), box);
                }
            }
        }
        if (in->a < 0 || box[in->a] == LU_NOV) {
            continue;
        }
        struct lu_ins *load = &f->ins[in->a];
        if (in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT) {
            fwd[v] = load->op == OP_INLINE_ORIGIN ? f->vobjs[LU_VIRT_ID(box[in->a])]->f[0] : in->a;
            in->flags |= INS_DEAD;
        }
        else if (in->op == OP_GUARD_TYPE || load->op != OP_INLINE_ORIGIN) {
            in->flags |= INS_DEAD;      /* a type guard of what is a number; a release of what is no object */
        }
    }
    for (int i = 0; changed && i < records; i++) {
        sink_record(f->vobjs[i], box);
    }
    free(want);
    free(unboxed);
    free(names.seen);
    free(box);
    return changed;
}

/* Whether nothing the instruction does can free an object the code does
   not hold a reference to: it runs no code, releases nothing that may
   hold others, writes no object and never parks the task (where another
   task may stop the world and merge counts). */
static int
lets_borrow(const struct lu_ins *in)
{
    switch (in->op) {
    case OP_GUARD_TYPE: case OP_GUARD_KIND: case OP_GUARD_VERSION: case OP_GUARD_IS: case OP_GUARD_BIT:
    case OP_GUARD_NOT_NULL: case OP_CHECK_LIVE: case OP_LOAD: case OP_INCREF: case OP_UNBOX_INT: case OP_UNBOX_FLOAT:
    case OP_BOX_INT: case OP_BOX_FLOAT: case OP_ADD_I: case OP_SUB_I: case OP_MUL_I: case OP_NEG_I: case OP_ABS_I:
    case OP_SHL_I: case OP_SAR_I: case OP_FLOORDIV_I: case OP_MOD_I: case OP_DIV_F: case OP_SQRT_F:
    case OP_RITER_DONE: case OP_TYPED_LOAD: case OP_TYPED_STORE:
        return 1;
    case OP_DECREF:
        return in->imm == 0;    /* a number's or a string's, which holds nothing */
    default:
        return (lu_op_flags[in->op] & IF_PURE) != 0;
    }
}

/* List and tuple items read without a count (luc-5bi7 stage 1), while
   serial (lu_serial): no other task runs, so an item stays alive while its
   container keeps it, and a container keeps it until code runs, a store
   writes it, or it is released.

   A web is a set of values that must be counted alike: the reads (a
   LIST_ITEM, or a tuple item's LOAD with its INCREF) and the phis that
   carry them around loops, joined through the phis' inputs. A web goes
   uncounted when nothing in the range of its values can free what they
   hold: no instruction outside borrow_safe while a member is live after it
   or is its operand (a frame state an instruction carries counts as a use:
   a published call's state names what it may need), no release of a
   container on the chain its reads came from (an item of a row of a grid
   needs the grid), and members of these kinds only, or constants.

   The one counted input allowed is an OSR entry's (ENTRY_LOCAL or
   ENTRY_STACK of the entry block: what a local held as the loop began),
   and only where a holder the unit names keeps it: a read of the web whose
   container is an entry value, at the item that read gave last (the item
   before the index it enters with, or the last item when its loop starts
   in the unit). The entry checks that the value is that item
   (ENTRY_BORROW, a guard whose failures count at the entry, after which
   the loop is built counting the web from its entry: lu_jit_noborrow),
   and from then on the value is a read of that container like the web's
   others, so everything that ends their borrow ends its too. Every check
   comes before any entry reference goes, and what goes is no longer
   named by any state after.

   A poll with a member live across it leaves the unit for its work in
   place (count merges and collections, which run finalizers and decide
   garbage from counts) instead of doing it there (INS_LEAVES). No member
   is live across an instruction that publishes or parks (where another
   task may begin, the switch to parallel complete, and an exit later take
   references to what it freed): those are outside borrow_safe, and so is
   a LIST_ITEM that stays counted (its slow path parks), and an
   INLINE_LOAD with a member live across leaves as a failed guard where it
   would park to make an unboxed value's box (INS_LEAVES; its cached
   small ints, wide ints and raw words take no park).

   An exit needs nothing more: it owns what its state names only if
   INS_OWNED, and every slot it fills takes a new reference. Borrowed reads
   depend on the serial state as unlocked stores do (facts.c locks_in_line).
   Invalidation set: luc-5bi7's ticket. */
static int
borrow_safe(const struct lu_ins *in)
{
    switch (in->op) {
    case OP_GUARD_FRAMES: case OP_GUARD_DEPTH: case OP_GUARD_ENTRY:
    case OP_INLINE_LOAD: case OP_INLINE_LOAD_I: case OP_INLINE_LOAD_F: case OP_INLINE_ORIGIN:
    case OP_RITER_NEXT: case OP_JUMP: case OP_BRANCH: case OP_PHI: case OP_ENTRY_LOCAL: case OP_ENTRY_STACK:
    case OP_ENTRY_PC:
        return 1;
    case OP_HELPER:
        /* Runs no Python and releases nothing that may hold others (FX_SEEN), nor parks. */
        return !(lu_helpers[in->imm].fx & (FX_PY | FX_SEEN));
    default:
        return lets_borrow(in);
    }
}

enum { NOT_IN, READ, CARRIER, NO_COUNT, ENTRY_COUNT };

struct borrow {
    struct lu_func *f;
    int *uf;                    /* union-find over values */
    uint8_t *kind;
    int *bit;                   /* a member's index in the live sets, else -1 */
    lu_v *of_bit;
    int nbits, words;
    char *bad;                  /* per root: counted after all */
    uint64_t *out;              /* per block: members live out */
    int entry;                  /* the entry block */
};

static int
uf_root(int *uf, int v)
{
    while (uf[v] != v) {
        uf[v] = uf[uf[v]];
        v = uf[v];
    }
    return v;
}

static int
is_tuple_item(const struct lu_ins *in)
{
    return in->op == OP_LOAD && in->imm2 == LU_LOAD_TUPLE_ITEM;
}

/* The one INCREF of a tuple item's LOAD, or -1. */
static lu_v
item_incref(struct lu_func *f, lu_v v)
{
    lu_v found = -1;
    for (int x = 0; x < f->nins; x++) {
        struct lu_ins *in = &f->ins[x];
        if (!(in->flags & INS_DEAD) && in->op == OP_INCREF && in->a == v) {
            if (found >= 0) {
                return -1;
            }
            found = x;
        }
    }
    return found;
}

static int
live_member(struct borrow *w, lu_v v)
{
    return v >= 0 && w->bit[v] >= 0 && !w->bad[uf_root(w->uf, v)];
}

/* A read that goes uncounted, in its serial form, which never parks. */
static int
borrowed_read(struct borrow *w, lu_v v)
{
    return live_member(w, v) && w->kind[v] == READ;
}

/* An instruction that goes with its web: a member's release, a read's INCREF. */
static int
goes(struct borrow *w, const struct lu_ins *in)
{
    if ((in->op != OP_DECREF && in->op != OP_INCREF) || !live_member(w, in->a)) {
        return 0;
    }
    int k = w->kind[in->a];
    return (k == READ || k == CARRIER) && (in->op == OP_DECREF || is_tuple_item(&w->f->ins[in->a]));
}

static void
use_bit(struct lu_func *f, lu_v v, void *arg)
{
    struct borrow *w = arg;
    if (live_member(w, v)) {
        w->out[(size_t)f->nblocks * (size_t)w->words + (size_t)(w->bit[v] / 64)] |= 1ull << (w->bit[v] % 64);
    }
}

/* The members an instruction uses, into the scratch set after the blocks'. */
static uint64_t *
uses_of(struct borrow *w, struct lu_ins *in)
{
    struct lu_func *f = w->f;
    uint64_t *u = w->out + (size_t)f->nblocks * (size_t)w->words;
    memset(u, 0, sizeof(uint64_t) * (size_t)w->words);
    if (in->op != OP_PHI && !goes(w, in)) {
        use_bit(f, in->a, w);
        use_bit(f, in->b, w);
        use_bit(f, in->c, w);
        for (int i = 0; i < in->nargs; i++) {
            use_bit(f, in->args[i], w);
        }
        if (in->fs != NULL) {
            lu_fs_each(f, in->fs, use_bit, w);
        }
    }
    return u;
}

struct finding {
    lu_v v;
    int seen;
};

static void
find_value(struct lu_func *f, lu_v v, void *arg)
{
    struct finding *p = arg;
    p->seen |= v == p->v;
}

/* The containers a web's reads hang from, transitively through borrowed ones. */
static int
on_chain(struct borrow *w, int root, lu_v c, int depth)
{
    struct lu_func *f = w->f;
    if (depth > 8) {
        return 1;               /* deep: say yes, which keeps the web counted */
    }
    for (int v = 0; v < f->nins; v++) {
        if (w->bit[v] < 0 || w->kind[v] != READ || uf_root(w->uf, v) != root) {
            continue;
        }
        lu_v k = f->ins[v].a;
        if (k == c) {
            return 1;
        }
        if (live_member(w, k) && on_chain(w, uf_root(w->uf, k), c, depth + 1)) {
            return 1;
        }
    }
    return 0;
}

/* Marks the webs of the members in `set` bad; whether any was good. */
static int
spoil(struct borrow *w, const uint64_t *set, lu_v at)
{
    int changed = 0;
    for (int i = 0; i < w->nbits; i++) {
        if (set[i / 64] & (1ull << (i % 64))) {
            int r = uf_root(w->uf, w->of_bit[i]);
            if (!w->bad[r] && _PyRuntime.jit.debug) {
                fprintf(stderr, "jit: borrow: v%d counted: live across v%d %s\n", w->of_bit[i], at,
                        lu_op_names[w->f->ins[at].op]);
            }
            changed |= !w->bad[r];
            w->bad[r] = 1;
        }
    }
    return changed;
}

/* One round: members' liveness to a fixpoint, then every instruction checked. */
static int
borrow_round(struct borrow *w)
{
    struct lu_func *f = w->f;
    int W = w->words, nb = f->nblocks;
    uint64_t *in_ = calloc((size_t)nb * (size_t)W, sizeof(uint64_t));
    memset(w->out, 0, sizeof(uint64_t) * (size_t)(nb + 2) * (size_t)W);
    uint64_t *live = w->out + (size_t)(nb + 1) * (size_t)W;
    for (int changed = 1; changed;) {
        changed = 0;
        for (int k = f->norder - 1; k >= 0; k--) {
            int b = f->order[k];
            struct lu_block *bl = &f->blocks[b];
            uint64_t *o = w->out + (size_t)b * (size_t)W;
            for (int s = 0; s < bl->nsucc; s++) {
                int sb = bl->succ[s];
                struct lu_block *sbl = &f->blocks[sb];
                int pi = 0;
                while (pi < sbl->npreds && sbl->preds[pi] != b) {
                    pi++;
                }
                for (int j = 0; j < W; j++) {
                    uint64_t x = in_[(size_t)sb * (size_t)W + (size_t)j];
                    if ((o[j] | x) != o[j]) {
                        o[j] |= x;
                        changed = 1;
                    }
                }
                for (int i = 0; i < sbl->nins; i++) {
                    struct lu_ins *p = &f->ins[sbl->ins[i]];
                    if (p->op != OP_PHI) {
                        break;
                    }
                    lu_v a = pi < p->nargs ? p->args[pi] : LU_NOV;
                    if (!(p->flags & INS_DEAD) && live_member(w, a)
                        && !(o[w->bit[a] / 64] & (1ull << (w->bit[a] % 64)))) {
                        o[w->bit[a] / 64] |= 1ull << (w->bit[a] % 64);
                        changed = 1;
                    }
                }
            }
            memcpy(live, o, sizeof(uint64_t) * (size_t)W);
            for (int i = bl->nins - 1; i >= 0; i--) {
                lu_v v = bl->ins[i];
                struct lu_ins *in = &f->ins[v];
                if (in->flags & INS_DEAD) {
                    continue;
                }
                if (live_member(w, v)) {
                    live[w->bit[v] / 64] &= ~(1ull << (w->bit[v] % 64));
                }
                uint64_t *u = uses_of(w, in);
                for (int j = 0; j < W; j++) {
                    live[j] |= u[j];
                }
            }
            uint64_t *li = in_ + (size_t)b * (size_t)W;
            if (memcmp(li, live, sizeof(uint64_t) * (size_t)W) != 0) {
                memcpy(li, live, sizeof(uint64_t) * (size_t)W);
                changed = 1;
            }
        }
    }
    /* What crosses an instruction that may free what a member holds. */
    int spoiled = 0;
    uint64_t *across = malloc(sizeof(uint64_t) * (size_t)W);
    for (int b = 0; b < nb; b++) {
        struct lu_block *bl = &f->blocks[b];
        memcpy(live, w->out + (size_t)b * (size_t)W, sizeof(uint64_t) * (size_t)W);
        for (int i = bl->nins - 1; i >= 0; i--) {
            lu_v v = bl->ins[i];
            struct lu_ins *in = &f->ins[v];
            if (in->flags & INS_DEAD) {
                continue;
            }
            if (live_member(w, v)) {
                live[w->bit[v] / 64] &= ~(1ull << (w->bit[v] % 64));
            }
            uint64_t *u = uses_of(w, in);
            for (int j = 0; j < W; j++) {
                across[j] = live[j] | u[j];
            }
            if (in->op == OP_POLL || in->op == OP_INLINE_LOAD) {
                /* What parks only on a slow path leaves there instead while a member is live across. */
                in->flags &= (uint16_t)~INS_LEAVES;
                for (int j = 0; j < W; j++) {
                    if (across[j] != 0) {
                        in->flags |= INS_LEAVES;
                    }
                }
            }
            else if (!goes(w, in) && !borrow_safe(in) && !(in->op == OP_LIST_ITEM && borrowed_read(w, v))) {
                spoiled |= spoil(w, across, v);
            }
            else if (!goes(w, in) && in->op == OP_DECREF) {
                /* A release that frees nothing that holds others, unless it is a container's. */
                for (int x = 0; x < w->nbits; x++) {
                    if ((across[x / 64] & (1ull << (x % 64))) && !w->bad[uf_root(w->uf, w->of_bit[x])]
                        && on_chain(w, uf_root(w->uf, w->of_bit[x]), in->a, 0)) {
                        if (_PyRuntime.jit.debug) {
                            fprintf(stderr, "jit: borrow: v%d counted: its container released at v%d\n", w->of_bit[x], v);
                        }
                        w->bad[uf_root(w->uf, w->of_bit[x])] = 1;
                        spoiled = 1;
                    }
                }
            }
            for (int j = 0; j < W; j++) {
                live[j] |= u[j];
            }
        }
    }
    free(across);
    free(in_);
    return spoiled;
}

/* The index an entry value's item would have had in a read's container:
   through the read's index phi, LU_NOV for the last item (the loop starts
   in the unit, from a constant), or the entry value it enters with, which
   is one past the item read last. */
static int
entry_index(struct borrow *w, lu_v i, lu_v *index)
{
    struct lu_func *f = w->f;
    if (i < 0 || f->ins[i].op != OP_PHI) {
        return 0;
    }
    for (int k = 0; k < f->ins[i].nargs; k++) {
        lu_v x = f->ins[i].args[k];
        if (x >= 0 && f->ins[x].op == OP_CONST_I) {
            *index = LU_NOV;
            return 1;
        }
        if (x >= 0 && f->ins[x].block == w->entry && f->ins[x].rep == REP_I64) {
            *index = x;
            return 1;
        }
    }
    return 0;
}

/* The read of an entry value's web that names its holder: a LIST_ITEM of a
   container the entry has, or a tuple item of such a read that goes
   uncounted itself (so that its container is on the web's chain); else -1. */
static lu_v
entry_read(struct borrow *w, lu_v e, lu_v *index)
{
    struct lu_func *f = w->f;
    int root = uf_root(w->uf, e);
    for (int v = 0; v < f->nins; v++) {
        if (w->kind[v] != READ || uf_root(w->uf, v) != root) {
            continue;
        }
        lu_v item = is_tuple_item(&f->ins[v]) ? f->ins[v].a : v;
        if (item != v && !borrowed_read(w, item)) {
            continue;
        }
        struct lu_ins *li = &f->ins[item];
        if (li->op == OP_LIST_ITEM && li->a >= 0 && f->ins[li->a].block == w->entry && entry_index(w, li->b, index)) {
            return v;
        }
    }
    return -1;
}

/* Entry values left without a holder the unit names stay counted, and so
   do they where the entry's check has kept failing. Whether any changed. */
static int
unheld_entries(struct borrow *w, int can_enter)
{
    int changed = 0;
    for (int v = 0; v < w->f->nins; v++) {
        lu_v index;
        if (w->kind[v] == ENTRY_COUNT && live_member(w, v) && (!can_enter || entry_read(w, v, &index) < 0)) {
            if (_PyRuntime.jit.debug) {
                fprintf(stderr, "jit: borrow: v%d counted: an entry value no named holder keeps\n", v);
            }
            w->bad[uf_root(w->uf, v)] = 1;
            changed = 1;
        }
    }
    return changed;
}

/* At the entry, before its jump: every entry value checked to be its
   holder's item (ENTRY_BORROW, which stands for the value in the web from
   then on), then every entry reference released; none of them can free,
   and no state after names them. */
static void
borrow_entries(struct borrow *w, struct lu_fs *entry_fs)
{
    struct lu_func *f = w->f;
    int n = f->nins;
    lu_v *entries = malloc(sizeof(lu_v) * (size_t)n);
    int nentries = 0;
    for (int e = 0; e < n; e++) {
        lu_v index;
        lu_v r = w->kind[e] == ENTRY_COUNT && live_member(w, e) ? entry_read(w, e, &index) : -1;
        if (r < 0) {
            continue;
        }
        lu_v item = is_tuple_item(&f->ins[r]) ? f->ins[r].a : r;
        lu_v x = lu_ir_emit(f, w->entry, OP_ENTRY_BORROW, REP_OBJ, e, f->ins[item].a, index, f->ins[item].imm);
        f->ins[x].imm2 = item != r ? f->ins[r].imm : -1;
        f->ins[x].fs = entry_fs;
        for (int p = 0; p < n; p++) {
            struct lu_ins *in = &f->ins[p];
            for (int k = 0; in->op == OP_PHI && !(in->flags & INS_DEAD) && k < in->nargs; k++) {
                if (in->args[k] == e) {
                    in->args[k] = x;
                }
            }
        }
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: borrow: entry v%d from v%d's container, as v%d\n", e, r, x);
        }
        entries[nentries++] = e;
    }
    for (int i = 0; i < nentries; i++) {
        lu_ir_emit(f, w->entry, OP_DECREF, REP_NONE, entries[i], LU_NOV, LU_NOV, 0);  /* its holder keeps it */
    }
    free(entries);
    struct lu_block *eb = &f->blocks[w->entry];
    for (int i = 0; nentries > 0 && i < eb->nins - 1; i++) {
        if (f->ins[eb->ins[i]].op == OP_JUMP) {
            int jump = eb->ins[i];
            memmove(&eb->ins[i], &eb->ins[i + 1], sizeof(int) * (size_t)(eb->nins - 1 - i));
            eb->ins[eb->nins - 1] = jump;   /* the jump last again */
            break;
        }
    }
}

static void
borrow_items(struct lu_func *f)
{
    if (!lu_serial() || f->norder == 0) {
        return;
    }
    int n = f->nins;
    struct borrow w = { .f = f, .entry = f->order[0] };
    w.uf = malloc(sizeof(int) * (size_t)n);
    w.kind = calloc((size_t)n, 1);
    w.bit = malloc(sizeof(int) * (size_t)n);
    w.of_bit = malloc(sizeof(lu_v) * (size_t)n);
    w.bad = calloc((size_t)n, 1);
    int any = 0;
    for (int v = 0; v < n; v++) {
        w.uf[v] = v;
        w.bit[v] = -1;
        struct lu_ins *in = &f->ins[v];
        if (!(in->flags & INS_DEAD) && (in->flags & INS_OWNED) && in->rep == REP_OBJ
            && (in->op == OP_LIST_ITEM || (is_tuple_item(in) && item_incref(f, v) >= 0))) {
            w.kind[v] = READ;
            any = 1;
        }
    }
    if (!any) {
        goto done;
    }
    /* Phis that carry reads join them, with their other inputs. */
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (!(in->flags & INS_DEAD) && in->op == OP_PHI && in->rep == REP_OBJ && (in->flags & INS_OWNED)) {
            w.kind[v] = CARRIER;
        }
    }
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (w.kind[v] != CARRIER) {
            continue;
        }
        for (int i = 0; i < in->nargs; i++) {
            lu_v a = in->args[i];
            if (a < 0) {
                continue;
            }
            w.uf[uf_root(w.uf, a)] = uf_root(w.uf, v);
            if (w.kind[a] == NOT_IN) {
                struct lu_ins *x = &f->ins[a];
                w.kind[a] = !(x->flags & INS_OWNED) && x->op == OP_CONST ? NO_COUNT
                    : (x->op == OP_ENTRY_LOCAL || x->op == OP_ENTRY_STACK) && (x->flags & INS_OWNED)
                      && x->block == w.entry ? ENTRY_COUNT
                    : 0xff;     /* anything else: the web stays counted */
            }
        }
    }
    /* Webs without a read, or with a member of another kind, stay counted. */
    char *has_read = calloc((size_t)n, 1);
    for (int v = 0; v < n; v++) {
        if (w.kind[v] == READ) {
            has_read[uf_root(w.uf, v)] = 1;
        }
    }
    for (int v = 0; v < n; v++) {
        if (w.kind[v] != NOT_IN && (w.kind[v] == 0xff || !has_read[uf_root(w.uf, v)])) {
            if (_PyRuntime.jit.debug && w.kind[v] == 0xff && has_read[uf_root(w.uf, v)]) {
                fprintf(stderr, "jit: borrow: v%d counted: a member of another kind\n", v);
            }
            w.bad[uf_root(w.uf, v)] = 1;
        }
    }
    free(has_read);
    /* An entry's counted input: an entry block that only enters (so its
       states are the entry's), one with a state to leave at, a site whose
       entry check has not kept failing, a holder (unheld_entries), and no
       use of the value outside the entry block but the web's phis. */
    struct lu_block *eb = &f->blocks[w.entry];
    struct lu_fs *entry_fs = NULL;
    int pure = 1;
    for (int i = 0; i < eb->nins; i++) {
        struct lu_ins *in = &f->ins[eb->ins[i]];
        if (in->flags & INS_DEAD) {
            continue;
        }
        if (entry_fs == NULL && in->fs != NULL) {
            entry_fs = in->fs;
        }
        pure &= in->op == OP_ENTRY_LOCAL || in->op == OP_ENTRY_STACK || in->op == OP_ENTRY_PC || in->op == OP_CONST
                || in->op == OP_CONST_I || in->op == OP_CONST_F || in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT
                || in->op == OP_GUARD_TYPE || in->op == OP_GUARD_DEPTH || in->op == OP_GUARD_ENTRY || in->op == OP_JUMP;
    }
    int can_enter = pure && entry_fs != NULL && entry_fs->caller == NULL
                    && !lu_jit_noborrow(entry_fs->code, entry_fs->pc);
    for (int v = 0; v < n; v++) {
        if (w.kind[v] != ENTRY_COUNT || w.bad[uf_root(w.uf, v)]) {
            continue;
        }
        int outside = 0;
        for (int x = 0; x < n && !outside; x++) {
            struct lu_ins *in = &f->ins[x];
            if ((in->flags & INS_DEAD) || in->block == w.entry) {
                continue;
            }
            if (in->op == OP_PHI) {
                continue;       /* a phi's input: joined above, or in a web that stays counted */
            }
            outside = in->a == v || in->b == v || in->c == v;
            for (int i = 0; i < in->nargs && !outside; i++) {
                outside = in->args[i] == v;
            }
            if (!outside && in->fs != NULL) {
                struct finding probe = { v, 0 };
                lu_fs_each(f, in->fs, find_value, &probe);
                outside = probe.seen;
            }
        }
        if (outside) {
            if (_PyRuntime.jit.debug) {
                fprintf(stderr, "jit: borrow: v%d counted: an entry value used past the entry\n", v);
            }
            w.bad[uf_root(w.uf, v)] = 1;
        }
    }
    /* Live sets over the members of webs still in question; a constant,
       which the unit keeps alive, needs none. */
    for (int v = 0; v < n; v++) {
        if (w.kind[v] != NOT_IN && w.kind[v] != NO_COUNT && !w.bad[uf_root(w.uf, v)]) {
            w.of_bit[w.nbits] = v;
            w.bit[v] = w.nbits++;
        }
    }
    if (w.nbits == 0 || w.nbits > 4096) {
        goto done;
    }
    w.words = (w.nbits + 63) / 64;
    w.out = calloc((size_t)(f->nblocks + 2) * (size_t)w.words, sizeof(uint64_t));
    for (int again = 1; again;) {
        again = unheld_entries(&w, can_enter);
        again |= borrow_round(&w);
    }
    /* What is left goes uncounted; the last round marked the polls. */
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (!(in->flags & INS_DEAD) && goes(&w, in)) {
            in->flags |= INS_DEAD;
        }
    }
    for (int v = 0; v < n; v++) {
        if (live_member(&w, v) && (w.kind[v] == READ || w.kind[v] == CARRIER)) {
            f->ins[v].flags &= ~INS_OWNED;
        }
    }
    borrow_entries(&w, entry_fs);
done:
    free(w.uf);
    free(w.kind);
    free(w.bit);
    free(w.of_bit);
    free(w.bad);
    free(w.out);
}

/* An attribute read from an instance's inline values whose reference the
   code releases in the read's own block, with nothing between that could
   free the value (lets_borrow), takes no reference: the instance's keeps
   it. The read still leaves for a value another thread owns, which that
   thread may free at any time. */
static void
borrowed_loads(struct lu_func *f)
{
    for (int b = 0; b < f->nblocks; b++) {
        struct lu_block *bl = &f->blocks[b];
        for (int k = 0; k < bl->nins; k++) {
            lu_v v = bl->ins[k];
            struct lu_ins *in = &f->ins[v];
            if ((in->flags & INS_DEAD) || in->op != OP_INLINE_LOAD || !(in->flags & INS_OWNED)) {
                continue;
            }
            for (int j = k + 1; j < bl->nins; j++) {
                struct lu_ins *x = &f->ins[bl->ins[j]];
                if (x->flags & INS_DEAD) {
                    continue;
                }
                if (x->op == OP_DECREF && x->a == v) {
                    in->flags &= ~INS_OWNED;
                    x->flags |= INS_DEAD;
                    break;
                }
                if (!lets_borrow(x)) {
                    break;
                }
            }
        }
    }
}

static void
count_use(struct lu_func *f, lu_v v, void *arg)
{
    if (v >= 0) {
        ((int *)arg)[v]++;
    }
}

static void
drop_use(struct lu_func *f, lu_v v, void *arg)
{
    if (v >= 0) {
        ((int *)arg)[v]--;
    }
}

static void
each_operand(struct lu_func *f, struct lu_ins *in, void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    fn(f, in->a, arg);
    fn(f, in->b, arg);
    fn(f, in->c, arg);
    for (int i = 0; i < in->nargs; i++) {
        fn(f, in->args[i], arg);
    }
}

/* Values nothing reads go, and then what only they read. Guards stay:
   what they prove was used to fold code that is no longer there. An
   instruction with a state is one that stays, so what a state names is
   read for good: counted once. */
static void
dce(struct lu_func *f)
{
    int *uses = calloc((size_t)f->nins + 1, sizeof(int));
    uint32_t mark = ++f->marks;
    for (int v = 0; v < f->nins; v++) {
        if (!(f->ins[v].flags & INS_DEAD)) {
            each_operand(f, &f->ins[v], count_use, uses);
            if (f->ins[v].fs != NULL) {
                lu_fs_each_once(f, f->ins[v].fs, mark, count_use, uses);
            }
        }
    }
    for (int changed = 1; changed;) {
        changed = 0;
        for (int v = f->nins - 1; v >= 0; v--) {
            struct lu_ins *in = &f->ins[v];
            if ((in->flags & INS_DEAD) || uses[v] > 0 || in->rep == REP_NONE) {
                continue;
            }
            int fl = lu_op_flags[in->op];
            if ((fl & (IF_EFFECT | IF_TERM | IF_GUARD)) || in->op == OP_ENTRY_STACK) {
                continue;
            }
            in->flags |= INS_DEAD;
            each_operand(f, in, drop_use, uses);
            changed = 1;
        }
    }
    free(uses);
}

/* An edge from a block with two successors to one with several
   predecessors gets a block of its own, where the phi moves go. */
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
    int *stack = malloc(sizeof(int) * (size_t)nb), *next = calloc((size_t)nb, sizeof(int)), sp = 0;
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

/* Dead instructions leave the block lists. */
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
    fwd = malloc(sizeof(lu_v) * (size_t)(f->nins + 1));
    for (int v = 0; v < f->nins; v++) {
        fwd[v] = v;
    }
    while (trivial_phis(f) | fold_branches(f)) {
    }
    rewrite(f);
    if (raw_loads(f)) {
        rewrite(f);
    }
    free(fwd);
    fwd = NULL;
    borrowed_loads(f);
    order_blocks(f);            /* what cannot be reached is dead before its uses count */
    dce(f);
    split_edges(f);
    order_blocks(f);
    borrow_items(f);
    lu_ssa_facts(f);
    compact(f);
    return 0;
}
