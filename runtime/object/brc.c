/* The biased-refcount merge queue (Python/brc.c).

   A non-owner whose decref finds an object's shared count at zero queues the
   object to its owning thread, which merges the two counts. Owners are found
   by thread id in the interpreter's bucket table. If the owner is not
   registered, it has exited, and the queuing thread merges the counts itself.

   An owner's record lives in its carrier's entry (runtime/thread/carrier.c).
   It is registered when the first task of a thread is bound in the
   interpreter, as CPython registers in bind_tstate, and retired when the last
   is unbound; a scheduler carrier (runtime/thread/sched.c) keeps it until it
   exits, since tasks come and go while the carrier's objects stay. A thread must be registered before its first refcount
   operation: it may reuse an exited owner's TCB, and with it the thread id of
   objects merged as abandoned under the bucket lock. Queueing an object
   raises the explicit-merge reason in the owner's carrier, which merges at its
   next poll; an unbinding thread merges what is left. */

#include "lucifer.h"

static struct lu_brc_bucket *
bucket_of(struct _is *interp, uintptr_t tid)
{
    return &interp->brc.buckets[tid % LU_BRC_NUM_BUCKETS];
}

static void
push(struct lu_stack *s, PyObject *ob)
{
    if (s->n == s->cap) {
        size_t cap = s->cap ? 2 * s->cap : 16;
        /* Runtime bookkeeping is libc's, not a hookable allocator domain. */
        PyObject **items = realloc(s->items, cap * sizeof *items);
        if (items == NULL) {
            fputs("lucifer: out of memory in the refcount merge queue\n", stderr);
            abort();
        }
        s->items = items;
        s->cap = cap;
    }
    s->items[s->n++] = ob;
}

/* The entry an owner record is part of. */
static struct lu_entry *
entry_of(struct lu_brc_owner *t)
{
    return (struct lu_entry *)((char *)t - offsetof(struct lu_entry, brc));
}

/* Move every item of src onto dst. */
static void
take_all(struct lu_stack *dst, struct lu_stack *src)
{
    for (size_t i = 0; i < src->n; i++) {
        push(dst, src->items[i]);
    }
    src->n = 0;
}

static void
merge_queued_objects(struct lu_stack *s)
{
    while (s->n > 0) {
        PyObject *ob = s->items[--s->n];
        /* Subtract one: the queue held a reference. */
        if (lu_explicit_merge_refcount(ob, -1) == 0) {
            _Py_Dealloc(ob);
        }
    }
}

void
lu_brc_register(struct lu_entry *entry)
{
    struct lu_brc_owner *t = &entry->brc;
    t->tid = _Py_ThreadId();
    struct lu_brc_bucket *b = bucket_of(entry->interp, t->tid);
    pthread_mutex_lock(&b->lock.mutex);
    t->next = b->head;
    b->head = t;
    pthread_mutex_unlock(&b->lock.mutex);
}

/* The owner may touch no refcount after leaving the table: other threads
   then treat its objects as abandoned and merge them directly. */
void
lu_brc_retire(struct lu_entry *entry)
{
    struct lu_brc_owner *t = &entry->brc;
    struct lu_brc_bucket *b = bucket_of(entry->interp, t->tid);
    for (;;) {
        merge_queued_objects(&t->local);
        pthread_mutex_lock(&b->lock.mutex);
        if (t->to_merge.n == 0) {
            struct lu_brc_owner **p = &b->head;
            while (*p != t) {
                p = &(*p)->next;
            }
            *p = t->next;
            pthread_mutex_unlock(&b->lock.mutex);
            break;
        }
        take_all(&t->local, &t->to_merge);
        pthread_mutex_unlock(&b->lock.mutex);
    }
    free(t->to_merge.items);
    free(t->local.items);
    memset(t, 0, sizeof *t);            /* a later bind registers it afresh */
}

/* Steals a reference to ob. */
void
lu_brc_queue_object(PyObject *ob)
{
    uintptr_t tid = _Py_atomic_load_uintptr(&ob->ob_tid);
    if (tid == 0) {
        /* The owner merged concurrently. */
        Py_DECREF(ob);
        return;
    }
    struct lu_brc_bucket *b = bucket_of(lu_current_entry()->interp, tid);
    pthread_mutex_lock(&b->lock.mutex);
    struct lu_brc_owner *t = b->head;
    while (t != NULL && t->tid != tid) {
        t = t->next;
    }
    if (t == NULL) {
        /* The owner exited: merge here, dropping the stolen reference. */
        Py_ssize_t refcount = lu_explicit_merge_refcount(ob, -1);
        pthread_mutex_unlock(&b->lock.mutex);
        if (refcount == 0) {
            _Py_Dealloc(ob);
        }
        return;
    }
    push(&t->to_merge, ob);
    /* The owner is registered, so its carrier is alive while the bucket lock
       is held: it merges at its next poll. */
    lu_poll_set(entry_of(t)->carrier, LU_POLL_EXPLICIT_MERGE);
    pthread_mutex_unlock(&b->lock.mutex);
}

/* _Py_brc_merge_refcounts: the poll handler's merge of what other threads
   queued. */
void
lu_brc_merge(struct lu_entry *entry)
{
    struct lu_brc_owner *t = &entry->brc;
    if (t->tid == 0) {
        return;                 /* not registered: no task bound here yet */
    }
    struct lu_brc_bucket *b = bucket_of(entry->interp, t->tid);
    pthread_mutex_lock(&b->lock.mutex);
    take_all(&t->local, &t->to_merge);
    pthread_mutex_unlock(&b->lock.mutex);
    merge_queued_objects(&t->local);
}
