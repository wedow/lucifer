/* The carrier record (DESIGN 5.3), reached through the one thread-local word.
   It binds the per-(carrier, interpreter) entries: mimalloc heaps and the
   biased-refcount owner record. Python/pystate.c does this for a thread
   state, which does not exist yet; K1 binds the carrier with the first. */

#include "lucifer.h"

_Thread_local struct lu_carrier *lu_carrier;

static void
retire(void *arg)
{
    struct lu_carrier *c = arg;
    for (struct lu_entry *e = c->entries; e != NULL; e = e->next) {
        /* Merging may free objects and so allocate: the heaps stay until the
           owner record is gone. */
        lu_brc_retire(e);
        lu_mimalloc_abandon(e);
    }
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
    for (int i = 0; i < LU_BRC_NUM_BUCKETS; i++) {
        pthread_mutex_init(&interp->brc.buckets[i].lock.mutex, NULL);
        lu_lock_register(&interp->brc.buckets[i].lock);
    }
    if (pthread_key_create(&_PyRuntime.carrier_key, retire) != 0) {
        abort();
    }
}

/* The carrier's entry in an interpreter. */
static struct lu_entry *
entry_new(struct lu_carrier *c, struct _is *interp)
{
    struct lu_entry *e = calloc(1, sizeof *e);
    if (e == NULL) {
        abort();
    }
    e->interp = interp;
    e->next = c->entries;
    c->entries = e;
    lu_mimalloc_bind(e);
    lu_brc_register(e);
    return e;
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
    c->current = entry_new(c, &_PyRuntime.main_interp);
    pthread_setspecific(_PyRuntime.carrier_key, c);
    return c->current;
}
