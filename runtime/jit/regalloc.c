/* Linear-scan register allocation (DESIGN 6.2 step 4).

   Positions number instructions in emission order, two apart. Liveness is
   the usual backward dataflow over blocks, with frame states counted as
   uses: an exit reads every value its state names (a virtual box's raw
   input stands for it). Each value then gets one interval, from its
   definition to its last use, stretched over every block it is live
   through, so a value live around a loop holds its location through the
   whole loop. Values in one interval keep one location: no splitting.

   Calls clobber every register (the donor's rule: all registers are dead
   across a call, FOUNDATIONS 2.11), so a value live across one gets a
   spill word for its whole interval. The rest go through linear scan over
   the target's general and float registers; when none is free, the
   interval that ends last is spilled. Constants take no location: the
   backend makes them at each use. Phis get locations like any value, and
   the backend moves their inputs on the edges. */
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>

typedef uint64_t word;

struct live {
    int nwords;
    word *in, *out;             /* per block */
};

static int
allocatable(struct lu_func *f, lu_v v)
{
    struct lu_ins *in = &f->ins[v];
    return !(in->flags & (INS_DEAD | INS_VIRTUAL)) && in->rep != REP_NONE && in->op != OP_CONST
        && in->op != OP_CONST_I && in->op != OP_CONST_F;
}

/* What a use of v reads: a virtual box reads its raw input. */
static lu_v
used(struct lu_func *f, lu_v v)
{
    if (v == LU_NOV) {
        return LU_NOV;
    }
    if (f->ins[v].flags & INS_VIRTUAL) {
        v = f->ins[v].b != LU_NOV ? f->ins[v].b : f->ins[v].a;
    }
    return v != LU_NOV && allocatable(f, v) ? v : LU_NOV;
}

#define BIT_SET(set, v) ((set)[(v) >> 6] |= (word)1 << ((v) & 63))
#define BIT_CLR(set, v) ((set)[(v) >> 6] &= ~((word)1 << ((v) & 63)))
#define BIT_GET(set, v) (((set)[(v) >> 6] >> ((v) & 63)) & 1)

/* Calls fn(f, v, arg) for every value instruction `in` reads, phi inputs
   excepted. */
static void
use_of(struct lu_func *f, lu_v v, void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    lu_v u = used(f, v);
    if (u != LU_NOV) {
        fn(f, u, arg);
    }
    if (v != LU_NOV && (f->ins[v].flags & INS_VIRTUAL) && (u = used(f, f->ins[v].c)) != LU_NOV) {
        fn(f, u, arg);          /* a box that may stand for NULL reads its bit too */
    }
}

static void
each_use(struct lu_func *f, struct lu_ins *in, void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    lu_v ops[3] = { in->a, in->b, in->c };
    for (int i = 0; i < 3; i++) {
        use_of(f, ops[i], fn, arg);
    }
    if (in->op != OP_PHI) {
        for (int i = 0; i < in->nargs; i++) {
            use_of(f, in->args[i], fn, arg);
        }
    }
    for (struct lu_fs *fs = in->fs; fs != NULL; fs = fs->caller) {
        for (int i = 0; i < fs->nlocals + fs->nstack; i++) {
            use_of(f, fs->vals[i], fn, arg);
        }
    }
}

static void
mark_live(struct lu_func *f, lu_v v, void *set)
{
    BIT_SET((word *)set, v);
}

static void
liveness(struct lu_func *f, struct live *L)
{
    int nb = f->nblocks;
    L->nwords = (f->nins + 63) / 64;
    L->in = calloc((size_t)nb * (size_t)L->nwords, sizeof(word));
    L->out = calloc((size_t)nb * (size_t)L->nwords, sizeof(word));
    word *tmp = malloc(sizeof(word) * (size_t)L->nwords);
    for (int changed = 1; changed;) {
        changed = 0;
        for (int k = f->norder - 1; k >= 0; k--) {
            int b = f->order[k];
            struct lu_block *bl = &f->blocks[b];
            word *out = L->out + (size_t)b * L->nwords, *in = L->in + (size_t)b * L->nwords;
            memset(tmp, 0, sizeof(word) * (size_t)L->nwords);
            for (int s = 0; s < bl->nsucc; s++) {
                int sb = bl->succ[s];
                word *sin = L->in + (size_t)sb * L->nwords;
                for (int w = 0; w < L->nwords; w++) {
                    tmp[w] |= sin[w];
                }
                /* This block's phi inputs into the successor. */
                struct lu_block *sbl = &f->blocks[sb];
                int p = 0;
                while (p < sbl->npreds && sbl->preds[p] != b) {
                    p++;
                }
                for (int i = 0; i < sbl->nins; i++) {
                    struct lu_ins *phi = &f->ins[sbl->ins[i]];
                    if (phi->op != OP_PHI) {
                        break;
                    }
                    lu_v u = p < phi->nargs ? used(f, phi->args[p]) : LU_NOV;
                    if (u != LU_NOV) {
                        BIT_SET(tmp, u);
                    }
                }
            }
            if (memcmp(tmp, out, sizeof(word) * (size_t)L->nwords) != 0) {
                memcpy(out, tmp, sizeof(word) * (size_t)L->nwords);
                changed = 1;
            }
            for (int i = bl->nins - 1; i >= 0; i--) {
                lu_v v = bl->ins[i];
                struct lu_ins *ins = &f->ins[v];
                BIT_CLR(tmp, v);
                if (ins->op != OP_PHI) {
                    each_use(f, ins, mark_live, tmp);
                }
            }
            if (memcmp(tmp, in, sizeof(word) * (size_t)L->nwords) != 0) {
                memcpy(in, tmp, sizeof(word) * (size_t)L->nwords);
                changed = 1;
            }
        }
    }
    free(tmp);
}

struct interval {
    lu_v v;
    int start, end;
    int fpr;
    int crosses;
};

static __thread int *istart, *iend;     /* the allocation in progress on this thread */

static void
extend(struct lu_func *f, lu_v v, int pos)
{
    if (istart[v] > pos) istart[v] = pos;
    if (iend[v] < pos) iend[v] = pos;
}

struct use_at {
    int pos;
};

static void
extend_use(struct lu_func *f, lu_v v, void *arg)
{
    extend(f, v, ((struct use_at *)arg)->pos);
}

static int
by_start(const void *a, const void *b)
{
    const struct interval *x = a, *y = b;
    return x->start != y->start ? x->start - y->start : x->v - y->v;
}

int
lu_regalloc(struct lu_func *f)
{
    if (lu_target.name == NULL) {
        f->fail = "no backend";
        return -1;
    }
    /* Positions. */
    int pos = 0;
    for (int k = 0; k < f->norder; k++) {
        struct lu_block *bl = &f->blocks[f->order[k]];
        bl->start = pos;
        pos += 2;
        for (int i = 0; i < bl->nins; i++) {
            struct lu_ins *in = &f->ins[bl->ins[i]];
            in->pos = in->op == OP_PHI ? bl->start : pos;
            if (in->op != OP_PHI) {
                pos += 2;
            }
        }
        bl->end = pos;
        pos += 2;
    }
    struct live L;
    liveness(f, &L);
    istart = malloc(sizeof(int) * (size_t)f->nins);
    iend = malloc(sizeof(int) * (size_t)f->nins);
    for (int v = 0; v < f->nins; v++) {
        istart[v] = INT32_MAX;
        iend[v] = -1;
    }
    int *calls = malloc(sizeof(int) * (size_t)(f->nins + 1)), ncalls = 0;
    for (int k = 0; k < f->norder; k++) {
        int b = f->order[k];
        struct lu_block *bl = &f->blocks[b];
        word *in = L.in + (size_t)b * L.nwords, *out = L.out + (size_t)b * L.nwords;
        for (int v = 0; v < f->nins; v++) {
            if (BIT_GET(in, v)) {
                extend(f, v, bl->start);
            }
            if (BIT_GET(out, v)) {
                extend(f, v, bl->end);
            }
        }
        for (int i = 0; i < bl->nins; i++) {
            lu_v v = bl->ins[i];
            struct lu_ins *ins = &f->ins[v];
            if (allocatable(f, v)) {
                extend(f, v, ins->pos);
            }
            if (ins->op != OP_PHI) {
                struct use_at at = { ins->pos };
                each_use(f, ins, extend_use, &at);
            }
            if ((lu_op_flags[ins->op] & IF_CALL) && ins->op != OP_PHI) {
                calls[ncalls++] = ins->pos;
            }
        }
        /* Phi inputs are read at the end of their predecessor. */
        for (int s = 0; s < bl->nsucc; s++) {
            struct lu_block *sbl = &f->blocks[bl->succ[s]];
            int p = 0;
            while (p < sbl->npreds && sbl->preds[p] != b) {
                p++;
            }
            for (int i = 0; i < sbl->nins; i++) {
                struct lu_ins *phi = &f->ins[sbl->ins[i]];
                if (phi->op != OP_PHI) {
                    break;
                }
                lu_v u = p < phi->nargs ? used(f, phi->args[p]) : LU_NOV;
                if (u != LU_NOV) {
                    extend(f, u, bl->end);
                }
            }
        }
    }
    free(L.in);
    free(L.out);
    /* Intervals, then linear scan. */
    struct interval *iv = malloc(sizeof(struct interval) * (size_t)(f->nins + 1));
    int n = 0;
    for (int v = 0; v < f->nins; v++) {
        if (!allocatable(f, v) || iend[v] < 0) {
            continue;
        }
        struct interval *it = &iv[n++];
        it->v = v;
        it->start = istart[v];
        it->end = iend[v];
        it->fpr = f->ins[v].rep == REP_F64;
        it->crosses = 0;
        for (int c = 0; c < ncalls; c++) {
            if (calls[c] > it->start && calls[c] < it->end) {
                it->crosses = 1;
                break;
            }
        }
    }
    qsort(iv, (size_t)n, sizeof(struct interval), by_start);
    int nspill = 0;
    struct interval *active[64];
    int nactive = 0;
    int busy[32] = { 0 };       /* by register number: the interval holding it, + 1 */
    for (int i = 0; i < n; i++) {
        struct interval *it = &iv[i];
        /* Expire what ended before this starts. */
        for (int a = 0; a < nactive;) {
            if (active[a]->end < it->start) {
                busy[f->ins[active[a]->v].loc] = 0;
                active[a] = active[--nactive];
            }
            else {
                a++;
            }
        }
        if (it->crosses) {
            f->ins[it->v].loc = LU_LOC_SPILL(nspill++);
            continue;
        }
        const int8_t *regs = it->fpr ? lu_target.fpr : lu_target.gpr;
        int nregs = it->fpr ? lu_target.nfpr : lu_target.ngpr;
        int reg = -1;
        for (int r = 0; r < nregs; r++) {
            if (!busy[regs[r]]) {
                reg = regs[r];
                break;
            }
        }
        if (reg < 0) {
            /* Spill whichever of this and the same-class active ends last. */
            int victim = -1;
            for (int a = 0; a < nactive; a++) {
                if (active[a]->fpr == it->fpr && (victim < 0 || active[a]->end > active[victim]->end)) {
                    victim = a;
                }
            }
            if (victim >= 0 && active[victim]->end > it->end) {
                reg = f->ins[active[victim]->v].loc;
                f->ins[active[victim]->v].loc = LU_LOC_SPILL(nspill++);
                active[victim] = active[--nactive];
            }
            else {
                f->ins[it->v].loc = LU_LOC_SPILL(nspill++);
                continue;
            }
        }
        f->ins[it->v].loc = reg;
        busy[reg] = 1;
        active[nactive++] = it;
    }
    f->nspill = nspill;
    free(iv);
    free(calls);
    free(istart);
    free(iend);
    return 0;
}
