/* The carrier record (DESIGN 5.3), reached through the one thread-local word.
   It holds the per-(carrier, interpreter) entries: mimalloc heaps, bound at
   the thread's first allocation, and the biased-refcount owner record, which
   binding a task registers (runtime/thread/task.c). */

#define _GNU_SOURCE
#include "lucifer.h"

#include <unistd.h>

_Thread_local struct lu_carrier *lu_carrier;

static void
retire(void *arg)
{
    struct lu_carrier *c = arg;
    /* Unbinding merges the owner records, which may free objects and so
       allocate: the heaps stay until the owner records are gone. */
    lu_task_carrier_exit(c);
    /* The heaps pass from the registry to the abandoned segments in one step
       as far as a collection can see (runtime/interp/collector.c). */
    pthread_mutex_lock(&_PyRuntime.heaps_lock.mutex);
    lu_qsbr_unregister(c);
    for (struct lu_entry *e = c->entries; e != NULL; e = e->next) {
        lu_mimalloc_abandon(e);
    }
    pthread_mutex_unlock(&_PyRuntime.heaps_lock.mutex);
    lu_carrier = NULL;
    while (c->entries != NULL) {
        struct lu_entry *e = c->entries;
        c->entries = e->next;
        free(e);
    }
    free(c);
}

static void
start_runtime(void)
{
    struct _is *interp = &_PyRuntime.main_interp;
    interp->subproc = mi_subproc_main();
    _PyRuntime.main_thread = pthread_self();
    _PyRuntime.qsbr.wr_seq = _PyRuntime.qsbr.rd_seq = 1;        /* QSBR_INITIAL */
    interp->gc.enabled = 1;
    interp->gc.young_threshold = 2000;
    interp->gc.old_threshold[0] = interp->gc.old_threshold[1] = 10;
    lu_lock_register(&_PyRuntime.qsbr.lock);
    lu_lock_register(&_PyRuntime.heaps_lock);
    lu_lock_register(&_PyRuntime.allocators.lock);
    lu_lock_register(&interp->tasks_lock);
    lu_lock_register(&_PyRuntime.sched.lock);
    for (int i = 0; i < LU_WAIT_BUCKETS; i++) {
        lu_lock_register(&_PyRuntime.waits.buckets[i].lock);
    }
    for (int i = 0; i < LU_LOT_BUCKETS; i++) {
        lu_lock_register(&_PyRuntime.parking_lot[i].lock);
    }
    for (int i = 0; i < LU_BRC_NUM_BUCKETS; i++) {
        pthread_mutex_init(&interp->brc.buckets[i].lock.mutex, NULL);
        lu_lock_register(&interp->brc.buckets[i].lock);
    }
    if (pthread_key_create(&_PyRuntime.carrier_key, retire) != 0) {
        abort();
    }
}

static struct lu_entry *
entry_new(struct lu_carrier *c, struct _is *interp)
{
    struct lu_entry *e = calloc(1, sizeof *e);
    if (e == NULL) {
        abort();
    }
    e->interp = interp;
    e->carrier = c;
    lu_mimalloc_bind(e);
    pthread_mutex_lock(&_PyRuntime.heaps_lock.mutex);
    e->next = c->entries;
    c->entries = e;
    pthread_mutex_unlock(&_PyRuntime.heaps_lock.mutex);
    return e;
}

struct lu_entry *
lu_entry_of(struct lu_carrier *c, struct _is *interp)
{
    struct lu_entry *e = c->entries;
    while (e != NULL && e->interp != interp) {
        e = e->next;
    }
    return e != NULL ? e : entry_new(c, interp);
}

struct lu_entry *
lu_carrier_bind(void)
{
    pthread_once(&_PyRuntime.started, start_runtime);
    struct lu_carrier *c = calloc(1, sizeof *c);
    if (c == NULL) {
        abort();
    }
    lu_carrier = c;
    c->native_id = (unsigned long)gettid();
    c->thread = pthread_self();
    c->current = entry_new(c, &_PyRuntime.main_interp);
    lu_qsbr_register(c);
    pthread_setspecific(_PyRuntime.carrier_key, c);
    return c->current;
}
