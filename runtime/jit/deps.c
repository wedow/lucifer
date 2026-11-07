/* What compiled code relies on between polls, and taking it back
   (DESIGN 6.2 step 8, FOUNDATIONS 2.4).

   A unit may rely on a fact past the guard that checked it: that a type
   keeps a version, a dict its keys or the value of one key, a function its
   code and defaults. Each such fact is a dependency of the unit, recorded
   here when the unit is published. A writer that changes one calls
   lu_jit_invalidate after the change: every unit that depends on it is
   marked dead, and every task's poll bit is raised. Compiled code checks
   that its unit lives at its polls and, after any instruction that may run
   code or mutate (lu_ssa_optimize's facts pass), before it next relies on a
   fact, so another thread's change reaches it at its next poll and the
   running thread's own change at once.

   Dependencies hang in buckets by the change that takes them back: a
   type's version, a dict's keys, one value of a dict, a function. A value
   hangs twice, under its own change and under its dict's keys. Each bucket
   has its own lock, and a writer that finds its bucket empty while no
   compile runs takes no lock at all, so writers of different facts never
   wait for each other.

   A compile reads facts while writers run. It takes a ticket and counts
   itself running before it reads anything; a writer that sees a compile
   running stamps its bucket with the tickets taken, and a compile does not
   publish a dependency whose bucket bears its ticket, since it may have
   read the state from before. Why that misses nothing, in C11 terms. A
   writer makes its change, then a seq_cst fence, then loads the running
   count (acquire), the tickets and its bucket's head; a compile takes its
   ticket and counts itself running, then a seq_cst fence, then reads, and
   when done publishes under each bucket's lock and uncounts itself
   (release). If the writer's fence comes first in the fences' total order,
   the compile's reads (atomic loads of a version, after its fence) see the
   change. If the compile's comes first, the writer's loads see its ticket
   and count or later: a count of 0 is then the compile's uncount or later,
   so its publication happens before the writer's load of the head, which
   finds the bucket full; any other count sends the writer to its bucket's
   lock with a stamp of at least the ticket, and that lock orders the stamp
   and the publication: a stamp first stops the publication, a publication
   first is killed by the writer. A dict reports only once watched, so the
   compile watches it and reads its fact inside the dict's critical
   section, where dict.c makes every change to a shared dict and reports
   it; that lock stands in for the writer's fence: the writer's section comes
   either wholly before the compile's, whose read then sees the change, or
   wholly after, when the writer sees the watch and the compile's ticket
   and count. Nothing rests on how soon a store becomes visible. */
#include "jit/target.h"

#include <stdio.h>
#include <stdlib.h>

#define BUCKET_BITS 12

struct lu_dep {
    struct lu_dep *next, **pprev;       /* the bucket's chain; pprev is NULL off it */
    struct lu_unit *unit;
    struct lu_fdep want;
    int on;                             /* the kind of change that takes it back */
};

struct bucket {
    PyMutex lock;
    struct lu_dep *head;                /* written under the lock, tested without */
    uint64_t stamp;                     /* the tickets taken when last changed amid a compile */
};

static struct bucket buckets[1 << BUCKET_BITS];

static struct {
    uint64_t tickets;                   /* compiles begun */
    uint64_t running;                   /* compiles begun and not ended */
} compiles;

static struct bucket *
bucket(int on, uintptr_t obj, uint32_t sub)
{
    uint64_t h = obj ^ ((uint64_t)sub << 32) ^ (uint64_t)on;
    return &buckets[(h * 0x9E3779B97F4A7C15ull) >> (64 - BUCKET_BITS)];
}

static struct bucket *
bucket_of(const struct lu_dep *d)
{
    return bucket(d->on, d->want.obj, d->on == LU_DEP_VALUE ? d->want.sub : 0);
}

uint64_t
lu_jit_deps_begin(void)
{
    uint64_t ticket = __atomic_add_fetch(&compiles.tickets, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&compiles.running, 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return ticket;
}

void
lu_jit_deps_end(void)
{
    __atomic_sub_fetch(&compiles.running, 1, __ATOMIC_RELEASE);
}

static void
unlink_dep(struct lu_dep *d)
{
    if (d->pprev != NULL) {
        __atomic_store_n(d->pprev, d->next, __ATOMIC_RELAXED);
        if (d->next != NULL) {
            d->next->pprev = d->pprev;
        }
        d->pprev = NULL;
    }
}

int
lu_jit_deps_publish(struct lu_unit *u, const struct lu_fdep *want, int n, uint64_t ticket)
{
    if (n == 0) {
        return 0;
    }
    u->deps = calloc(2 * (size_t)n, sizeof(struct lu_dep));
    for (int i = 0; i < n; i++) {
        u->deps[u->ndeps++] = (struct lu_dep){ .unit = u, .want = want[i], .on = want[i].kind };
        if (want[i].kind == LU_DEP_VALUE) {
            u->deps[u->ndeps++] = (struct lu_dep){ .unit = u, .want = want[i], .on = LU_DEP_KEYS };
        }
    }
    int stale = 0;
    for (int i = 0; i < u->ndeps && !stale; i++) {
        struct lu_dep *d = &u->deps[i];
        struct bucket *b = bucket_of(d);
        PyMutex_Lock(&b->lock);
        stale = b->stamp >= ticket;
        if (!stale) {
            d->next = b->head;
            d->pprev = &b->head;
            if (b->head != NULL) {
                b->head->pprev = &d->next;
            }
            __atomic_store_n(&b->head, d, __ATOMIC_RELAXED);
        }
        PyMutex_Unlock(&b->lock);
    }
    if (stale) {
        lu_jit_deps_free(u);
    }
    return stale ? -1 : 0;
}

void
lu_jit_deps_free(struct lu_unit *u)
{
    for (int i = 0; i < u->ndeps; i++) {
        struct bucket *b = bucket_of(&u->deps[i]);
        PyMutex_Lock(&b->lock);
        unlink_dep(&u->deps[i]);
        PyMutex_Unlock(&b->lock);
    }
    free(u->deps);
    u->deps = NULL;
    u->ndeps = 0;
}

/* Every task leaves compiled code at its next poll. */
static void
signal_all(void)
{
    PyThreadState *self = PyThreadState_GetUnchecked();
    if (self == NULL) {
        return;
    }
    PyInterpreterState *interp = self->interp;
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        lu_task_signal((struct lu_task *)ts, LU_POLL_DEOPT_SELF);
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
}

void
lu_jit_invalidate(int kind, uintptr_t obj, uint32_t sub)
{
    struct bucket *b = bucket(kind, obj, kind == LU_DEP_VALUE ? sub : 0);
    if (kind == LU_DEP_TYPE || kind == LU_DEP_FUNC) {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);       /* a dict's critical section orders its changes */
    }
    if (__atomic_load_n(&compiles.running, __ATOMIC_ACQUIRE) == 0
        && __atomic_load_n(&b->head, __ATOMIC_RELAXED) == NULL) {
        return;
    }
    int killed = 0;
    PyMutex_Lock(&b->lock);
    b->stamp = __atomic_load_n(&compiles.tickets, __ATOMIC_RELAXED);
    for (struct lu_dep *d = b->head, *next; d != NULL; d = next) {
        next = d->next;
        if (d->on != kind || d->want.obj != obj || (kind == LU_DEP_VALUE && d->want.sub != sub)) {
            continue;
        }
        /* The unit is dead; its other dependencies leave their buckets
           when it is freed. Its site keeps its guard in place from now on. */
        unlink_dep(d);
        struct lu_unit *u = d->unit;
        if (__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
            continue;
        }
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: invalidate kind %d of %s@%d\n", kind, PyUnicode_AsUTF8(d->want.code->co_qualname),
                    d->want.pc);
        }
        lu_jit_nohoist_set(d->want.code, d->want.pc);
        lu_jit_unit_kill(u);
        killed = 1;
    }
    PyMutex_Unlock(&b->lock);
    if (killed) {
        signal_all();
    }
}
