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

/* What the one frame state names of the loads in question (want[v] != 0):
   a stack entry may; a local, a virtual object's field or the object one
   became may not (want[v] = BAD). `sink`: the stack entries that name a
   load whose box[v] is set name that virtual object instead. */
enum { WANT_INT = 1, WANT_FLOAT = 2, WANT_BAD = 4 };

static void
state_names(struct lu_fs *fs, uint8_t *want, const lu_v *box)
{
    for (int i = 0; i < fs->nbinds; i++) {
        struct lu_vobj *o = fs->binds[i].obj;
        if (fs->binds[i].real >= 0) {
            want[fs->binds[i].real] |= WANT_BAD;
        }
        for (int k = 0; o != NULL && k < o->n; k++) {
            if (o->f[k] >= 0) {
                want[o->f[k]] |= WANT_BAD;
            }
        }
    }
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        for (int i = 0; i < p->nlocals; i++) {
            if (p->locals[i] >= 0) {
                want[p->locals[i]] |= WANT_BAD;
            }
        }
        for (int i = 0; box != NULL && i < p->nstack; i++) {
            if (p->stack[i] >= 0 && box[p->stack[i]] != LU_NOV) {
                p->stack[i] = box[p->stack[i]];
            }
        }
    }
}

/* An attribute read from an instance's inline values only to be unboxed
   is read as the number (INLINE_LOAD_I, _F): no reference is taken, and a
   value stored unboxed (eval_attr.c) never becomes an object. The load
   qualifies when compiled code does nothing with its value but unbox it,
   as one kind and in the load's own block, so that the read leaves no
   oftener than the unboxing did, and when frame states name the value on
   their stacks only: there it is a number that is no object yet, and an
   exit makes one for the instruction that would have consumed it. A
   local would let the program compare it with the attribute's own. */
static int
raw_loads(struct lu_func *f)
{
    int n = f->nins, changed = 0;
    uint8_t *want = calloc((size_t)n + 1, 1);
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        lu_v x = in->a;
        int unbox = in->op == OP_UNBOX_INT ? WANT_INT : in->op == OP_UNBOX_FLOAT ? WANT_FLOAT : 0;
        if (x >= 0 && unbox && in->block == f->ins[x].block) {
            want[x] |= unbox;
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
        if (in->fs != NULL) {
            state_names(in->fs, want, NULL);
        }
    }
    for (int i = 0; i < f->nvobjs; i++) {
        for (int k = 0; k < f->vobjs[i]->n; k++) {
            if (f->vobjs[i]->f[k] >= 0) {
                want[f->vobjs[i]->f[k]] |= WANT_BAD;
            }
        }
    }
    /* A type guard asks for no number: only a load that is unboxed counts. */
    char *unboxed = calloc((size_t)n + 1, 1);
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        unboxed[in->a >= 0 ? in->a : n] |= !(in->flags & INS_DEAD) && (in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT);
    }
    lu_v *box = malloc(sizeof(lu_v) * (size_t)(n + 1));
    for (int v = 0; v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        box[v] = LU_NOV;
        if ((in->flags & INS_DEAD) || in->op != OP_INLINE_LOAD || !unboxed[v]
            || (want[v] != WANT_INT && want[v] != WANT_FLOAT)) {
            continue;
        }
        int fl = want[v] == WANT_FLOAT;
        struct lu_vobj *o = lu_ir_vobj(f, fl ? VK_BOX_FLOAT : VK_BOX_INT, fl ? &PyFloat_Type : &PyLong_Type, 1);
        o->f[0] = v;
        box[v] = LU_VIRT(lu_ir_vobj_id(f, o));
        in->op = fl ? OP_INLINE_LOAD_F : OP_INLINE_LOAD_I;
        in->rep = fl ? REP_F64 : REP_I64;
        in->flags &= ~INS_OWNED;
        changed = 1;
    }
    for (int v = 0; changed && v < n; v++) {
        struct lu_ins *in = &f->ins[v];
        if (in->flags & INS_DEAD) {
            continue;
        }
        if (in->fs != NULL) {
            state_names(in->fs, want, box);
        }
        if (in->a < 0 || box[in->a] == LU_NOV) {
            continue;
        }
        if (in->op == OP_UNBOX_INT || in->op == OP_UNBOX_FLOAT) {
            fwd[v] = in->a;
        }
        in->flags |= INS_DEAD;  /* an unboxing, a type guard or a release of what is a number now */
    }
    free(want);
    free(unboxed);
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
