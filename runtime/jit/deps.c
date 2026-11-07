/* What compiled code relies on between polls, and taking it back
   (DESIGN 6.2 step 7, FOUNDATIONS 2.4).

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

   A compile reads facts while writers run. The epoch counts changes, and
   the last LOG of them are kept: a unit whose dependencies match a change
   made since its compile started (or that started more than LOG changes
   ago) is not published, since it may have read the state from before. A
   writer logs after its change, so a compile that started after the log
   entry read the new state. */
#include "jit/target.h"

#include <stdlib.h>

#define NBUCKETS 1024
#define LOG 64

struct lu_dep {
    struct lu_dep *next, **pprev;       /* the bucket's chain; pprev is NULL off it */
    struct lu_unit *unit;
    struct lu_fdep want;
};

static struct {
    PyMutex lock;
    struct lu_dep *buckets[NBUCKETS];
    uint64_t epoch;                     /* changes made */
    struct lu_fdep log[LOG];            /* the last LOG, change k at k % LOG */
} deps;

static struct lu_dep **
bucket(uintptr_t obj)
{
    return &deps.buckets[(obj * 0x9E3779B97F4A7C15ull) >> (64 - 10)];
}

/* Whether a change of `what` takes `dep` back. A dict's keys changing
   takes its values too. */
static int
matches(const struct lu_fdep *what, const struct lu_fdep *dep)
{
    if (what->obj != dep->obj) {
        return 0;
    }
    if (dep->kind == LU_DEP_VALUE) {
        return what->kind == LU_DEP_KEYS || (what->kind == LU_DEP_VALUE && what->sub == dep->sub);
    }
    return what->kind == dep->kind;
}

uint64_t
lu_jit_deps_epoch(void)
{
    return __atomic_load_n(&deps.epoch, __ATOMIC_ACQUIRE);
}

static void
unlink_dep(struct lu_dep *d)
{
    if (d->pprev != NULL) {
        *d->pprev = d->next;
        if (d->next != NULL) {
            d->next->pprev = d->pprev;
        }
        d->pprev = NULL;
    }
}

int
lu_jit_deps_publish(struct lu_unit *u, const struct lu_fdep *want, int n, uint64_t epoch0)
{
    if (n == 0) {
        return 0;
    }
    PyMutex_Lock(&deps.lock);
    int stale = deps.epoch - epoch0 > LOG;
    for (uint64_t e = epoch0; !stale && e < deps.epoch; e++) {
        for (int i = 0; i < n && !stale; i++) {
            stale = matches(&deps.log[e % LOG], &want[i]);
        }
    }
    if (!stale) {
        u->deps = calloc((size_t)n, sizeof(struct lu_dep));
        u->ndeps = n;
        for (int i = 0; i < n; i++) {
            struct lu_dep *d = &u->deps[i];
            struct lu_dep **head = bucket(want[i].obj);
            d->unit = u;
            d->want = want[i];
            d->next = *head;
            d->pprev = head;
            if (*head != NULL) {
                (*head)->pprev = &d->next;
            }
            *head = d;
        }
    }
    PyMutex_Unlock(&deps.lock);
    return stale ? -1 : 0;
}

void
lu_jit_deps_free(struct lu_unit *u)
{
    if (u->deps == NULL) {
        return;
    }
    PyMutex_Lock(&deps.lock);
    for (int i = 0; i < u->ndeps; i++) {
        unlink_dep(&u->deps[i]);
    }
    PyMutex_Unlock(&deps.lock);
    free(u->deps);
    u->deps = NULL;
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
    struct lu_fdep what = { .kind = kind, .obj = obj, .sub = sub };
    int killed = 0;
    PyMutex_Lock(&deps.lock);
    deps.log[deps.epoch % LOG] = what;
    __atomic_store_n(&deps.epoch, deps.epoch + 1, __ATOMIC_RELEASE);
    struct lu_dep **head = bucket(obj);
    for (struct lu_dep *d = *head; d != NULL;) {
        if (!matches(&what, &d->want)) {
            d = d->next;
            continue;
        }
        /* The unit is dead, and so are its other dependencies; its site
           keeps its guard in place from now on. */
        struct lu_unit *u = d->unit;
        lu_jit_nohoist_set(d->want.code, d->want.pc);
        for (int i = 0; i < u->ndeps; i++) {
            unlink_dep(&u->deps[i]);
        }
        lu_jit_unit_kill(u);
        killed = 1;
        d = *head;
    }
    PyMutex_Unlock(&deps.lock);
    if (killed) {
        signal_all();
    }
}
