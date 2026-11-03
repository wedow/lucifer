/* The biased-refcount merge queue (Python/brc.c).

   A non-owner whose decref finds an object's shared count at zero queues the
   object to its owning thread, which merges the two counts. Owners are found
   by thread id in a fixed hash table. If the owner is not registered, it has
   exited, and the queuing thread merges the counts itself.

   A thread must register before its first refcount operation: it may reuse
   an exited owner's TCB, and with it the thread id of objects merged as
   abandoned under the bucket lock. CPython registers when a thread state is
   bound. Until PyThreadState exists, a thread registers as it starts, through
   the pthread_create below, or when it creates its first object (the main
   thread). It merges its queue when it exits; there is no eval breaker yet
   to make it merge earlier. */

#define _GNU_SOURCE
#include "lucifer.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>

#define NUM_BUCKETS 257         /* _Py_BRC_NUM_BUCKETS */

struct stack {
    PyObject **items;
    size_t n, cap;
};

struct brc_thread {
    uintptr_t tid;
    struct brc_thread *next;    /* in its bucket */
    struct stack to_merge;      /* pushed by other threads, under the bucket lock */
    struct stack local;         /* the owner's own copy, merged without the lock */
};

static struct bucket {
    pthread_mutex_t mutex;
    struct brc_thread *head;
} table[NUM_BUCKETS] = { [0 ... NUM_BUCKETS - 1] = { PTHREAD_MUTEX_INITIALIZER, NULL } };

static _Thread_local struct brc_thread *self;
static pthread_key_t exit_key;
static pthread_once_t exit_key_once = PTHREAD_ONCE_INIT;

static struct bucket *
bucket_of(uintptr_t tid)
{
    return &table[tid % NUM_BUCKETS];
}

static void
push(struct stack *s, PyObject *ob)
{
    if (s->n == s->cap) {
        size_t cap = s->cap ? 2 * s->cap : 16;
        PyObject **items = PyMem_RawRealloc(s->items, cap * sizeof *items);
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
take_all(struct stack *dst, struct stack *src)
{
    for (size_t i = 0; i < src->n; i++) {
        push(dst, src->items[i]);
    }
    src->n = 0;
}

static void
merge_queued_objects(struct stack *s)
{
    while (s->n > 0) {
        PyObject *ob = s->items[--s->n];
        /* Subtract one: the queue held a reference. */
        if (lu_explicit_merge_refcount(ob, -1) == 0) {
            _Py_Dealloc(ob);
        }
    }
}

/* The owner may touch no refcount after leaving the table: other threads
   then treat its objects as abandoned and merge them directly. */
static void
remove_thread(void *arg)
{
    struct brc_thread *t = arg;
    struct bucket *b = bucket_of(t->tid);
    for (;;) {
        merge_queued_objects(&t->local);
        pthread_mutex_lock(&b->mutex);
        if (t->to_merge.n == 0) {
            struct brc_thread **p = &b->head;
            while (*p != t) {
                p = &(*p)->next;
            }
            *p = t->next;
            pthread_mutex_unlock(&b->mutex);
            break;
        }
        take_all(&t->local, &t->to_merge);
        pthread_mutex_unlock(&b->mutex);
    }
    PyMem_RawFree(t->to_merge.items);
    PyMem_RawFree(t->local.items);
    PyMem_RawFree(t);
    self = NULL;
}

static void
make_exit_key(void)
{
    if (pthread_key_create(&exit_key, remove_thread) != 0) {
        abort();
    }
}

void
lu_brc_register_thread(void)
{
    if (self != NULL) {
        return;
    }
    pthread_once(&exit_key_once, make_exit_key);
    struct brc_thread *t = PyMem_RawCalloc(1, sizeof *t);
    if (t == NULL) {
        abort();
    }
    t->tid = _Py_ThreadId();
    struct bucket *b = bucket_of(t->tid);
    pthread_mutex_lock(&b->mutex);
    t->next = b->head;
    b->head = t;
    pthread_mutex_unlock(&b->mutex);
    self = t;
    pthread_setspecific(exit_key, t);
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
    struct bucket *b = bucket_of(tid);
    pthread_mutex_lock(&b->mutex);
    struct brc_thread *t = b->head;
    while (t != NULL && t->tid != tid) {
        t = t->next;
    }
    if (t == NULL) {
        /* The owner exited: merge here, dropping the stolen reference. */
        Py_ssize_t refcount = lu_explicit_merge_refcount(ob, -1);
        pthread_mutex_unlock(&b->mutex);
        if (refcount == 0) {
            _Py_Dealloc(ob);
        }
        return;
    }
    push(&t->to_merge, ob);
    pthread_mutex_unlock(&b->mutex);
}

struct start {
    void *(*fn)(void *);
    void *arg;
};

static void *
start_registered(void *arg)
{
    struct start s = *(struct start *)arg;
    PyMem_RawFree(arg);
    lu_brc_register_thread();
    return s.fn(s.arg);
}

/* Interposes libc's for the runtime and every extension. */
LU_EXPORT int
pthread_create(pthread_t *thread, const pthread_attr_t *attr,
               void *(*fn)(void *), void *arg)
{
    int (*create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *) =
        dlsym(RTLD_NEXT, "pthread_create");
    struct start *s = PyMem_RawMalloc(sizeof *s);
    if (s == NULL) {
        return EAGAIN;
    }
    *s = (struct start){ fn, arg };
    int err = create(thread, attr, start_registered, s);
    if (err != 0) {
        PyMem_RawFree(s);
    }
    return err;
}
