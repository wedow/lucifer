/* What is left to do over the finished graph (docs/design/jit-architecture.md
   2, optimizer): the evaluator folded, forwarded and kept virtual what it
   could while it built, so the passes here only tidy. Phis every input of
   which is one value go, with their uses forwarded; values nothing reads
   go; critical edges get a block for their phi moves; blocks are ordered
   for emission. */
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
            lu_fs_rewrite(f, in->fs, resolve);
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
        if ((f->ins[v].flags & INS_DEAD) || f->ins[v].op != OP_INLINE_LOAD || !unboxed[v]
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
    if (in->fs != NULL) {
        lu_fs_each(f, in->fs, fn, arg);
    }
}

/* Values nothing reads go, and then what only they read. Guards stay:
   what they prove was used to fold code that is no longer there. */
static void
dce(struct lu_func *f)
{
    int *uses = calloc((size_t)f->nins + 1, sizeof(int));
    for (int v = 0; v < f->nins; v++) {
        if (!(f->ins[v].flags & INS_DEAD)) {
            each_operand(f, &f->ins[v], count_use, uses);
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
    while (trivial_phis(f)) {
    }
    rewrite(f);
    if (raw_loads(f)) {
        rewrite(f);
    }
    free(fwd);
    fwd = NULL;
    order_blocks(f);            /* what cannot be reached is dead before its uses count */
    dce(f);
    split_edges(f);
    order_blocks(f);
    lu_ssa_facts(f);
    compact(f);
    return 0;
}
