/* Runtime-internal declarations shared across runtime/. */
#ifndef LUCIFER_H
#define LUCIFER_H

#include "Python.h"
#include "lu_mimalloc.h"

#include <pthread.h>
#include <stdint.h>

#define LU_EXPORT __attribute__((visibility("default")))
#define LU_STUB __attribute__((weak))
#define LU_UNIMPLEMENTED(name) lu_unimplemented(name)

/* Report an unported C-API function by name and abort. */
_Noreturn void lu_unimplemented(const char *name);

/* Allocator domains (runtime/object/alloc.c). Each domain's default hooks are
   lu_raw_*, lu_mem_* and lu_obj_*; _PyRuntime.allocators starts out pointing
   at them. */
#define LU_ALLOCATOR_HOOKS(prefix) \
    void *prefix##_malloc(void *ctx, size_t size); \
    void *prefix##_calloc(void *ctx, size_t nelem, size_t elsize); \
    void *prefix##_realloc(void *ctx, void *ptr, size_t size); \
    void prefix##_free(void *ctx, void *ptr);
LU_ALLOCATOR_HOOKS(lu_raw)
LU_ALLOCATOR_HOOKS(lu_mem)
LU_ALLOCATOR_HOOKS(lu_obj)
#define LU_ALLOCATOR(prefix) \
    { NULL, prefix##_malloc, prefix##_calloc, prefix##_realloc, prefix##_free }

/* Allocate a GC object of `size` bytes after `presize` bytes of pre-header,
   from the heap its type selects, as gc_alloc does in
   Python/gc_free_threading.c. Returns NULL without setting an error. */
void *lu_gc_alloc(PyTypeObject *tp, size_t size, size_t presize);

/* The lock registry (FOUNDATIONS 2.9): every runtime-internal mutex is a
   lu_lock registered once, so that fork can take and reset them all. */
struct lu_lock {
    pthread_mutex_t mutex;
    struct lu_lock *next;       /* the registry's list */
};
void lu_lock_register(struct lu_lock *lock);

/* Biased reference counting (runtime/object/brc.c). A thread that owns
   objects has an owner record in each interpreter; the interpreter's buckets
   find it by thread id. */
#define LU_BRC_NUM_BUCKETS 257          /* _Py_BRC_NUM_BUCKETS */

struct lu_stack {
    PyObject **items;
    size_t n, cap;
};

struct lu_brc_owner {
    uintptr_t tid;
    struct lu_brc_owner *next;  /* in its bucket */
    struct lu_stack to_merge;   /* pushed by other threads, under the bucket lock */
    struct lu_stack local;      /* the owner's own copy, merged without the lock */
};

struct lu_brc_bucket {
    struct lu_lock lock;
    struct lu_brc_owner *head;
};

/* PyInterpreterState (private layout; DESIGN 4.1). */
struct _is {
    mi_subproc_id_t subproc;    /* segments abandoned by one interpreter's carriers
                                   are reclaimed only by its own */
    struct {
        struct lu_brc_bucket buckets[LU_BRC_NUM_BUCKETS];
    } brc;
};

/* A carrier's state in one interpreter, created on first use there. */
struct lu_entry {
    struct _is *interp;
    struct lu_entry *next;      /* the carrier's entries */
    struct lu_mimalloc_state mimalloc;
    struct lu_brc_owner brc;
};

/* The carrier record is per pthread, reached through the one thread-local
   word (DESIGN 5.3). It is created by the thread's first allocation, or at
   its start if pthread_create made it, and retired when the thread exits. */
struct lu_carrier {
    struct lu_entry *current;   /* the entry of the interpreter running here */
    struct lu_entry *entries;
};

extern _Thread_local struct lu_carrier *lu_carrier;

/* Creates the calling thread's carrier and its entry in the main interpreter. */
struct lu_entry *lu_carrier_bind(void);

static inline struct lu_entry *
lu_current_entry(void)
{
    struct lu_carrier *c = lu_carrier;
    return c != NULL ? c->current : lu_carrier_bind();
}

/* Called when a carrier binds and retires. */
void lu_mimalloc_bind(struct lu_entry *entry);
void lu_mimalloc_abandon(struct lu_entry *entry);
void lu_brc_register(struct lu_entry *entry);
void lu_brc_retire(struct lu_entry *entry);

void lu_brc_queue_object(PyObject *ob);
Py_ssize_t lu_explicit_merge_refcount(PyObject *op, Py_ssize_t extra);

/* _Py_DebugOffsets: external inspectors find the runtime by its cookie. The
   layout of tasks and frames is filled in with them (L3). */
struct lu_debug_offsets {
    char cookie[8];
    uint64_t version;
    uint64_t free_threaded;
    unsigned char reserved[1000];
};

/* The runtime record (FOUNDATIONS 2.1). Its size is the oracle's. */
#define LU_RUNTIME_SIZE 365624

extern LU_EXPORT union lu_runtime {
    struct {
        struct lu_debug_offsets debug_offsets;  /* must stay first */
        pthread_once_t started;
        struct {
            PyMemAllocatorEx raw, mem, obj;
            struct lu_lock lock;        /* PyMem_SetAllocator/GetAllocator */
        } allocators;
        struct {
            PyRefTracer func;
            void *data;
        } ref_tracer;
        struct {
            pthread_mutex_t mutex;
            struct lu_lock *head;
        } locks;
        pthread_key_t carrier_key;      /* retires a thread's carrier */
        struct _is main_interp;
    };
    unsigned char oracle_size[LU_RUNTIME_SIZE];
} _PyRuntime;

#endif
