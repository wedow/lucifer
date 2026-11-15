/* Linear-scan register allocation (DESIGN 6.2 step 4).

   Positions number instructions in emission order, two apart. Liveness is
   the usual backward dataflow over blocks, with frame states counted as
   uses: an exit reads every value its state names (a virtual object's
   fields stand for it). Each
   value then gets one interval, from its definition to its last use,
   stretched over every block it is live through, so a value live around a
   loop holds its location through the whole loop. Values in one interval keep one location: no splitting.

   A release that publishes nothing (it runs no Python a walker could see
   the frame from) is not a call here: its slow path keeps the registers
   (lu_alloc_call). Calls clobber every register but the ones the target says every call
   preserves (the C ABI's callee-saved ones), so a value live across one
   gets one of those for its whole interval, or a spill word when none is
   free, when it is a float, or when an instruction it lives across uses
   them itself (clobbers_saved). A call that publishes its frame state
   (lu_publishes) lets walkers read the values it names during the call
   from memory, so they count as live across it and are spilled. The rest
   go through linear scan over the target's other registers first; when
   none is free, the interval that ends last is spilled. Constants take no location: the
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
    return !(in->flags & INS_DEAD) && in->rep != REP_NONE && in->op != OP_CONST
        && in->op != OP_CONST_I && in->op != OP_CONST_F;
}

static lu_v
used(struct lu_func *f, lu_v v)
{
    return v != LU_NOV && allocatable(f, v) ? v : LU_NOV;
}

#define BIT_SET(set, v) ((set)[(v) >> 6] |= (word)1 << ((v) & 63))
#define BIT_CLR(set, v) ((set)[(v) >> 6] &= ~((word)1 << ((v) & 63)))

struct visit {
    void (*fn)(struct lu_func *, lu_v, void *);
    void *arg;
};

static void
visit_used(struct lu_func *f, lu_v v, void *arg)
{
    struct visit *vis = arg;
    if (used(f, v) != LU_NOV) {
        vis->fn(f, v, vis->arg);
    }
}

/* Calls fn(f, v, arg) for every value the state fs names that has a
   location; with a mark, once per walk for what callers' frames name
   (lu_fs_each_once). */
static void
each_state_use(struct lu_func *f, struct lu_fs *fs, uint32_t mark, void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    struct visit vis = { fn, arg };
    if (mark != 0) {
        lu_fs_each_once(f, fs, mark, visit_used, &vis);
    }
    else {
        lu_fs_each(f, fs, visit_used, &vis);
    }
}

/* Calls fn(f, v, arg) for every value instruction `in` reads as an
   operand, phi inputs excepted. */
static void
each_operand_use(struct lu_func *f, struct lu_ins *in, void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    lu_v ops[3] = { in->a, in->b, in->c };
    for (int i = 0; i < 3; i++) {
        if (used(f, ops[i]) != LU_NOV) {
            fn(f, ops[i], arg);
        }
    }
    if (in->op != OP_PHI) {
        for (int i = 0; i < in->nargs; i++) {
            if (used(f, in->args[i]) != LU_NOV) {
                fn(f, in->args[i], arg);
            }
        }
    }
}

static void
mark_live(struct lu_func *f, lu_v v, void *set)
{
    BIT_SET((word *)set, v);
}

/* The backward dataflow, with each block's transfer made once: what its
   instructions read before they define it (gen), what they define
   (kill), and what its successors' phis read from it. A block's live-in
   is gen | (live-out & ~kill), live-out its successors' live-ins and the
   phi inputs. */
static void
liveness(struct lu_func *f, struct live *L)
{
    int nb = f->nblocks;
    size_t nw = (size_t)(f->nins + 63) / 64;
    L->nwords = (int)nw;
    L->in = calloc((size_t)nb * nw, sizeof(word));
    L->out = calloc((size_t)nb * nw, sizeof(word));
    word *gen = calloc((size_t)nb * nw, sizeof(word)), *kill = calloc((size_t)nb * nw, sizeof(word));
    word *phis = calloc((size_t)nb * nw, sizeof(word));
    for (int k = 0; k < f->norder; k++) {
        int b = f->order[k];
        struct lu_block *bl = &f->blocks[b];
        word *g = gen + (size_t)b * nw, *d = kill + (size_t)b * nw, *ph = phis + (size_t)b * nw;
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
                    BIT_SET(ph, u);
                }
            }
        }
        /* A value a state names was made before the state's instruction,
           so one sight of a caller's frame per block says what it reads. */
        uint32_t mark = ++f->marks;
        for (int i = bl->nins - 1; i >= 0; i--) {
            lu_v v = bl->ins[i];
            struct lu_ins *ins = &f->ins[v];
            BIT_CLR(g, v);
            BIT_SET(d, v);
            if (ins->op != OP_PHI) {
                each_operand_use(f, ins, mark_live, g);
                if (ins->fs != NULL) {
                    each_state_use(f, ins->fs, mark, mark_live, g);
                }
            }
        }
    }
    /* A worklist, first in reverse order: a block is visited again only
       when the live-in of a successor grew. */
    int *queue = malloc(sizeof(int) * (size_t)(nb + 1));
    char *queued = calloc((size_t)nb, 1);
    int head = 0, count = 0;
    for (int k = f->norder - 1; k >= 0; k--) {
        queue[count++] = f->order[k];
        queued[f->order[k]] = 1;
    }
    while (count > 0) {
        int b = queue[head];
        head = (head + 1) % (nb + 1);
        count--;
        queued[b] = 0;
        struct lu_block *bl = &f->blocks[b];
        word *out = L->out + (size_t)b * nw, *in = L->in + (size_t)b * nw;
        word *g = gen + (size_t)b * nw, *d = kill + (size_t)b * nw, *ph = phis + (size_t)b * nw;
        int grew = 0;
        for (size_t w = 0; w < nw; w++) {
            word o = ph[w];
            for (int s = 0; s < bl->nsucc; s++) {
                o |= L->in[(size_t)bl->succ[s] * nw + w];
            }
            word i = g[w] | (o & ~d[w]);
            grew |= i != in[w];
            out[w] = o;
            in[w] = i;
        }
        for (int p = 0; grew && p < bl->npreds; p++) {
            int pb = bl->preds[p];
            if (!queued[pb]) {
                queued[pb] = 1;
                queue[(head + count++) % (nb + 1)] = pb;
            }
        }
    }
    free(queue);
    free(queued);
    free(gen);
    free(kill);
    free(phis);
}

struct interval {
    lu_v v;
    int start, end;
    int fpr;
    int crosses;                /* a call */
    int memory;                 /* a call that clobbers the preserved registers, or one that publishes it */
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

static void
mark_memory(struct lu_func *f, lu_v v, void *set)
{
    ((char *)set)[v] = 1;
}

/* Whether a call at one of the n sorted positions falls inside (start, end). */
static int
crosses(const int *calls, int n, int start, int end)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (calls[mid] > start) {
            hi = mid;
        }
        else {
            lo = mid + 1;
        }
    }
    return lo < n && calls[lo] < end;
}

static int
saved_reg(int reg)
{
    for (int i = 0; i < lu_target.nsaved; i++) {
        if (lu_target.saved[i] == reg) {
            return 1;
        }
    }
    return 0;
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
    int *hard = malloc(sizeof(int) * (size_t)(f->nins + 1)), nhard = 0;
    char *memory = calloc((size_t)f->nins + 1, 1);
    for (int k = 0; k < f->norder; k++) {
        int b = f->order[k];
        struct lu_block *bl = &f->blocks[b];
        word *in = L.in + (size_t)b * L.nwords, *out = L.out + (size_t)b * L.nwords;
        for (int w = 0; w < L.nwords; w++) {
            for (word m = in[w]; m != 0; m &= m - 1) {
                extend(f, (lu_v)(w * 64 + __builtin_ctzll(m)), bl->start);
            }
            for (word m = out[w]; m != 0; m &= m - 1) {
                extend(f, (lu_v)(w * 64 + __builtin_ctzll(m)), bl->end);
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
                each_operand_use(f, ins, extend_use, &at);
            }
            if (lu_alloc_call(ins) && ins->fs != NULL && lu_publishes(ins)) {
                /* A call that publishes its state (lu_publishes) is read by
                   walkers during the call, from memory. */
                each_state_use(f, ins->fs, 0, mark_memory, memory);
            }
            if (lu_alloc_call(ins)) {
                calls[ncalls++] = ins->pos;
                if (lu_target.clobbers_saved(ins)) {
                    hard[nhard++] = ins->pos;
                }
            }
        }
        /* What states read, last first: a caller's frame is seen once per
           block, at its last reader (what it names was made before the
           first). A call's exits leave after it, so what their state
           reads lives across the call. */
        uint32_t mark = ++f->marks;
        for (int i = bl->nins - 1; i >= 0; i--) {
            struct lu_ins *ins = &f->ins[bl->ins[i]];
            if (ins->op != OP_PHI && ins->fs != NULL) {
                struct use_at at = { ins->pos + (lu_alloc_call(ins) ? 1 : 0) };
                each_state_use(f, ins->fs, mark, extend_use, &at);
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
        it->crosses = crosses(calls, ncalls, it->start, it->end);
        it->memory = it->crosses && (it->fpr || memory[v] || crosses(hard, nhard, it->start, it->end));
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
            int reg = -1;
            for (int r = 0; r < lu_target.nsaved && !it->memory; r++) {
                if (!busy[lu_target.saved[r]]) {
                    reg = lu_target.saved[r];
                    break;
                }
            }
            if (reg < 0) {
                f->ins[it->v].loc = LU_LOC_SPILL(nspill++);
                continue;
            }
            f->ins[it->v].loc = reg;
            busy[reg] = 1;
            active[nactive++] = it;
            continue;
        }
        const int8_t *regs = it->fpr ? lu_target.fpr : lu_target.gpr;
        int nregs = it->fpr ? lu_target.nfpr : lu_target.ngpr;
        int reg = -1;
        /* The registers calls preserve last: values that cross calls want them. */
        for (int pass = 0; pass < 2 && reg < 0; pass++) {
            for (int r = 0; r < nregs; r++) {
                if (!busy[regs[r]] && saved_reg(regs[r]) == pass) {
                    reg = regs[r];
                    break;
                }
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
    free(hard);
    free(memory);
    free(istart);
    free(iend);
    return 0;
}
