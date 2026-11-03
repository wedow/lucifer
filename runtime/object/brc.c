/* The biased-refcount merge queue (Python/brc.c).

   A non-owner whose decref finds an object's shared count at zero queues the
   object to its owning thread, which merges the two counts. Owners are found
   by thread id in the interpreter's bucket table. If the owner is not
   registered, it has exited, and the queuing thread merges the counts itself.

   An owner's record lives in its carrier's entry (runtime/thread/carrier.c), which is
   created before the thread's first allocation, retired when it exits, and
   registered here. A thread must register before its first refcount
   operation: it may reuse an exited owner's TCB, and with it the thread id of
   objects merged as abandoned under the bucket lock. CPython registers when a
   thread state is bound. Until PyThreadState exists, a thread binds as it
   starts, through the pthread_create below, or at its first allocation (the
   main thread). It merges its queue when it exits; there is no eval breaker
   yet to make it merge earlier. */

#define _GNU_SOURCE
#include "lucifer.h"

#include <dlfcn.h>
#include <errno.h>

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
    pthread_mutex_unlock(&b->lock.mutex);
}

struct start {
    void *(*fn)(void *);
    void *arg;
};

static void *
start_registered(void *arg)
{
    struct start s = *(struct start *)arg;
    free(arg);
    lu_current_entry();
    return s.fn(s.arg);
}

/* Interposes libc's for the runtime and every extension. */
LU_EXPORT int
pthread_create(pthread_t *thread, const pthread_attr_t *attr,
               void *(*fn)(void *), void *arg)
{
    int (*create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *) =
        dlsym(RTLD_NEXT, "pthread_create");
    struct start *s = malloc(sizeof *s);
    if (s == NULL) {
        return EAGAIN;
    }
    *s = (struct start){ fn, arg };
    int err = create(thread, attr, start_registered, s);
    if (err != 0) {
        free(s);
    }
    return err;
}
