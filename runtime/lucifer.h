/* Runtime-internal declarations shared across runtime/. */
#ifndef LUCIFER_H
#define LUCIFER_H

#include "Python.h"
#include "lu_mimalloc.h"
#include "thread/park.h"

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

/* The wait table (runtime/thread/park.c): pending waits by id. */
#define LU_WAIT_BUCKETS 257

struct lu_wait_bucket {
    struct lu_lock lock;
    struct lu_wait *head;
};

/* The parking lot (runtime/thread/parking_lot.c): waiters by address. */
#define LU_LOT_BUCKETS 257              /* NUM_BUCKETS */

struct lu_lot_entry;
struct lu_lot_bucket {
    struct lu_lock lock;
    struct lu_lot_entry *head, *tail;   /* in park order */
    size_t num_waiters;
};

/* The syscall bracket (DESIGN 5.5), cp314t's detach and attach. begin
   suspends the task's critical sections and marks it detached; it returns
   the task's tstate, or NULL if the thread had no attached task, in which
   case end does nothing. end reattaches, resumes the top critical section
   and keeps errno. Blocking calls are made only in runtime/prim/os.c,
   inside the bracket (tools/blockcheck). */
PyThreadState *lu_syscall_begin(void);
void lu_syscall_end(PyThreadState *ts);

/* lu_park's sleep (runtime/prim/os.c): waits while *word == expected, until
   a wake or the absolute CLOCK_MONOTONIC deadline (-1 for none). Returns 0,
   or ETIMEDOUT, EINTR or EAGAIN. */
int lu_futex_wait(uint32_t *word, uint32_t expected, PyTime_t deadline);
void lu_futex_wake(uint32_t *word);

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
    struct lu_lock tasks_lock;  /* the task list (HEAD_LOCK) */
    PyThreadState *tasks;       /* every task of this interpreter, in any state */
    uint64_t next_task_id;
    PyObject *modules;          /* sys.modules */
    PyObject *builtins;         /* builtins.__dict__ */
    PyObject *sysdict;          /* sys.__dict__ */
    PyObject *interned;         /* interned str -> itself */
    _PyFrameEvalFunction eval_frame;    /* PEP 523; NULL runs frames inline */
    uintptr_t monitoring_version;       /* a multiple of LU_POLL_VERSION_UNIT */
};

/* The poll word (FOUNDATIONS 2.5): each carrier's eval breaker. The low bits
   are CPython's _PY_*_BIT reasons, then Lucifer's; the bits above them hold
   the monitoring version of the interpreter the carrier runs, as in 3.13.
   The interpreter reads the word at calls and back-edges, and RESUME compares
   its version with the code's. */
enum {
    LU_POLL_GIL_DROP_REQUEST = 1 << 0,
    LU_POLL_SIGNALS_PENDING = 1 << 1,
    LU_POLL_CALLS_TO_DO = 1 << 2,
    LU_POLL_ASYNC_EXCEPTION = 1 << 3,
    LU_POLL_GC_SCHEDULED = 1 << 4,
    LU_POLL_PLEASE_STOP = 1 << 5,
    LU_POLL_EXPLICIT_MERGE = 1 << 6,
    LU_POLL_JIT_INVALIDATE_COLD = 1 << 7,
    LU_POLL_PREEMPT = 1 << 8,
    LU_POLL_DEOPT_SELF = 1 << 9,
    LU_POLL_STRESS_TICK = 1 << 10,
    LU_POLL_REMOTE_EXEC = 1 << 11,
    LU_POLL_EVENTS_BITS = 12,
};
#define LU_POLL_EVENTS_MASK (((uintptr_t)1 << LU_POLL_EVENTS_BITS) - 1)
#define LU_POLL_VERSION_UNIT ((uintptr_t)1 << LU_POLL_EVENTS_BITS)

/* PyThreadState.state (Include/internal/pycore_pystate.h). */
enum {
    _Py_THREAD_DETACHED = 0,
    _Py_THREAD_ATTACHED = 1,
    _Py_THREAD_SUSPENDED = 2,
};

/* The task record (DESIGN 5.3): a Python thread of control. The PyThreadState
   comes first, so a task is its tstate; the tail is Lucifer's. The datastack
   fields of the tstate hold the task's frames. */
struct lu_task {
    PyThreadState ts;
    uintptr_t pin;              /* transient pins: foreign C and nested evals (DESIGN 5.4) */
    int pinned_for_life;        /* main, foreign-thread and thread-scoped tasks */
    uintptr_t pending;          /* targeted poll reasons, raised into the carrier's word at bind */
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
    uintptr_t poll;             /* the poll word */
    uint32_t wake;              /* the futex a park sleeps on; bumped by each unpark */
    struct lu_task *task;       /* the task bound here, or NULL */
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

/* Creates a task in `interp` and binds it to the calling carrier for life
   (the degenerate scheduler). Returns NULL on memory exhaustion. */
struct lu_task *lu_task_new(struct _is *interp);

/* The one handler for every poll reason (FOUNDATIONS 2.5), called by the
   interpreter and the JIT when the carrier's poll word has a reason set.
   Returns -1 with an exception set when the reason raises. */
int lu_poll_slow(PyThreadState *ts);

static inline struct lu_task *
lu_current_task(void)
{
    return lu_carrier != NULL ? lu_carrier->task : NULL;
}

/* Called when a carrier binds and retires. */
void lu_mimalloc_bind(struct lu_entry *entry);
void lu_mimalloc_abandon(struct lu_entry *entry);
void lu_brc_register(struct lu_entry *entry);
void lu_brc_retire(struct lu_entry *entry);

void lu_brc_queue_object(PyObject *ob);
Py_ssize_t lu_explicit_merge_refcount(PyObject *op, Py_ssize_t extra);

/* _Py_DebugOffsets: external inspectors find the runtime by its cookie. The
   cookie stays zero until L3 fills in real offsets, so no 3.14t reader trusts
   a half-written table. */
struct lu_debug_offsets {
    char cookie[8];
    uint64_t version;
    uint64_t free_threaded;
    unsigned char reserved[1000];
};

/* Statically allocated, immortal objects shared by every interpreter
   (CPython's _Py_SINGLETON set), built at startup before anything is hashed. */
#define LU_NSMALLNEGINTS 5              /* _PY_NSMALLNEGINTS */
#define LU_NSMALLPOSINTS 257            /* _PY_NSMALLPOSINTS */

struct lu_static_objects {
    PyLongObject small_ints[LU_NSMALLNEGINTS + LU_NSMALLPOSINTS];
    struct {
        PyCompactUnicodeObject head;    /* an ASCII char uses the PyASCIIObject prefix */
        Py_UCS1 data[2];
    } latin1[256];
    struct {
        PyASCIIObject head;
        Py_UCS1 data[1];
    } empty_str;
    PyBytesObject bytes_chars[256];     /* ob_sval[1] plus padding holds char and NUL */
    PyBytesObject empty_bytes;
    PyTupleObject empty_tuple;
};

/* Interned names the runtime looks up, made immortal at startup (CPython's
   _Py_ID strings). LU_ID(__name__) is the str "__name__". */
#define LU_IDENTIFIERS(X) \
    X(__all__) X(__builtins__) X(__doc__) X(__loader__) X(__main__) X(__name__) \
    X(__package__) X(__spec__) X(builtins)
struct lu_identifiers {
#define LU_ID_FIELD(name) PyObject *name;
    LU_IDENTIFIERS(LU_ID_FIELD)
#undef LU_ID_FIELD
};
#define LU_ID(name) (_PyRuntime.ids.name)

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
        /* The locks below start zeroed, which is PTHREAD_MUTEX_INITIALIZER:
           a thread may take one before the runtime has started. */
        struct {
            struct lu_wait_bucket buckets[LU_WAIT_BUCKETS];
            uint64_t last_id;
        } waits;
        struct lu_lot_bucket parking_lot[LU_LOT_BUCKETS];
        struct lu_static_objects singletons;
        struct lu_identifiers ids;
        struct _is main_interp;
    };
    unsigned char oracle_size[LU_RUNTIME_SIZE];
} _PyRuntime;

/* Layouts (runtime/object/). Constructors return a new reference, or NULL
   with an exception set. lu_hash and lu_eq cover the layouts marshal yields,
   before the kernel installs Python-level __hash__ and __eq__. */
void lu_layouts_init(void);
PyObject *lu_object_new(PyTypeObject *tp, size_t size);    /* zeroed; GC types tracked */
void lu_object_free(PyObject *op);
void lu_str_init(void);
void lu_set_init(void);
void lu_dict_init(void);
void lu_code_init(void);
void lu_function_init(void);
void lu_module_init(void);
void lu_exc_init(void);
PyObject *lu_small_int(int value);
PyObject *lu_long_from_digits(int negative, Py_ssize_t ndigits, const digit *digits);
int lu_long_as_long(PyObject *v, long *result);
Py_hash_t lu_long_hash(PyLongObject *v);
Py_hash_t lu_xxhash_items(PyObject *const *items, Py_ssize_t n, int mangle_length);
Py_hash_t lu_str_hash(PyObject *s);
int lu_str_eq(PyObject *a, PyObject *b);
void lu_intern(PyInterpreterState *interp, PyObject **p);
PyObject *lu_str_from_utf8(const char *s, Py_ssize_t n, int surrogatepass);
Py_hash_t lu_hash(PyObject *v);
int lu_eq(PyObject *a, PyObject *b);
PyObject *lu_set_new(PyTypeObject *type);
int lu_set_add(PyObject *set, PyObject *key);
Py_hash_t lu_frozenset_hash(PyObject *set);
int lu_hash_secret_init(int use_seed, unsigned long seed);

/* PyModuleObject (Include/internal/pycore_moduleobject.h). */
typedef struct {
    PyObject_HEAD
    PyObject *md_dict;
    PyModuleDef *md_def;
    void *md_state;
    PyObject *md_weaklist;
    PyObject *md_name;
    void *md_gil;
} PyModuleObject;

/* A module object with CPython's initial namespace (runtime/prim/module.c). */
PyObject *lu_module_new(const char *name);
/* A builtin module from a multi-phase definition: create, then exec slots. */
PyObject *lu_module_from_def(PyModuleDef *def);
extern PyModuleDef lu_lucifer_module;

/* A function for `code` closing over `globals` (runtime/object/function.c). */
PyObject *lu_function_new(PyObject *code, PyObject *globals);

/* The namespace dict (runtime/object/dict.c): lookups and stores with exact
   str keys, which LOAD_NAME/LOAD_GLOBAL, module attributes and the kernel's
   dict code all use (DESIGN 4.4). Lookup returns a borrowed value or NULL,
   and never raises. */
PyObject *lu_ns_lookup(PyObject *dict, PyObject *key);
int lu_ns_insert(PyObject *dict, PyObject *key, PyObject *value);
/* Removes `key`: 1 when it was there, 0 when not. */
int lu_ns_delete(PyObject *dict, PyObject *key);
/* PyDict_Next over the namespace: *pos starts at 0; key and value are borrowed. */
int lu_ns_next(PyObject *dict, Py_ssize_t *pos, PyObject **key, PyObject **value);

/* Appends to a list (borrowed item), growing its storage. */
int lu_list_append(PyObject *list, PyObject *item);

/* Exceptions raised natively (runtime/capi/errors.c), before and beneath the
   kernel's exception classes. */
void lu_err_format(PyObject *type, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
