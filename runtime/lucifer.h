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

/* A PyMethodDef flag of the runtime's own: ml_meth is a frame primitive,
   struct lu_frame *(*)(PyThreadState *, PyObject *const *, Py_ssize_t),
   which returns a frame for the interpreter to push (DESIGN 5.2). */
#define LU_METH_FRAME 0x4000

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

/* A static builtin type's per-interpreter state (FOUNDATIONS 2.1): what
   CPython keeps in managed_static_type_state. The type's tp_subclasses holds
   its index + 1 and _Py_TPFLAGS_STATIC_BUILTIN is set. */
#define LU_MAX_STATIC_TYPES 200         /* _Py_MAX_MANAGED_STATIC_BUILTIN_TYPES */

struct lu_static_type_state {
    PyObject *tp_dict;
    PyObject *tp_subclasses;    /* a list of the heap subclasses, or NULL */
};

/* Kernel functions the runtime calls (FOUNDATIONS 2.1): per interpreter,
   since they bind its module globals. Kernel Python registers each one
   with _lucifer.kernel_set(NAME, function). */
#define LU_KERNEL_FUNCTIONS(X) \
    X(getattr)                  /* PyObject_GetAttr: __getattribute__, then __getattr__ */ \
    X(generic_getattr)          /* PyObject_GenericGetAttr */ \
    X(setattr)                  /* PyObject_SetAttr: __setattr__ */ \
    X(delattr)                  /* PyObject_DelAttr: __delattr__ */ \
    X(generic_setattr)          /* PyObject_GenericSetAttr, value or MISSING */ \
    X(super_getattr)            /* _PySuper_Lookup(class, self, name) */ \
    X(binary_op)                /* PyNumber_<op>(a, b) for BINARY_OP's oparg: reflection */ \
    X(compare)                  /* PyObject_RichCompare(a, b, oparg >> 5), bool if oparg & 16 */ \
    X(negative)                 /* PyNumber_Negative */ \
    X(positive)                 /* PyNumber_Positive */ \
    X(invert)                   /* PyNumber_Invert */ \
    X(truth)                    /* PyObject_IsTrue, as a bool */ \
    X(contains)                 /* a in b, or not in: (container, item, invert) */ \
    X(do_raise)                 /* RAISE_VARARGS: do_raise(exc, cause or MISSING) */ \
    X(load_special)             /* LOAD_SPECIAL: load_special(owner, which) */ \
    X(exc_group_match)          /* CHECK_EG_MATCH: (rest, match) of an exception and a type */ \
    X(prep_reraise_star)        /* CALL_INTRINSIC_2 PREP_RERAISE_STAR */ \
    X(getitem)                  /* PyObject_GetItem: BINARY_OP NB_SUBSCR, BINARY_SLICE */ \
    X(setitem)                  /* PyObject_SetItem(container, key, value): STORE_SUBSCR, STORE_SLICE */ \
    X(delitem)                  /* PyObject_DelItem: DELETE_SUBSCR */ \
    X(length)                   /* PyObject_Size as an int: GET_LEN */ \
    X(get_iter)                 /* PyObject_GetIter: GET_ITER */ \
    X(next_item)                /* the next item of an iterator, or MISSING: FOR_ITER */ \
    X(list_extend)              /* _PyList_Extend(list, iterable): LIST_EXTEND */ \
    X(build_map)                /* BUILD_MAP of keys that are not all exact strs: a tuple of key, value, ... */ \
    X(build_set)                /* BUILD_SET: a tuple of the items */ \
    X(dict_merge)               /* DICT_MERGE: (kwargs, mapping, the callee's name) for f(**mapping) */ \
    X(dict_update)              /* DICT_UPDATE: dict.update(mapping) for {**mapping} */ \
    X(set_add)                  /* SET_ADD: (set, item) */ \
    X(set_update)               /* SET_UPDATE: (set, iterable) */ \
    X(unpack)                   /* the n items of an iterable, as a tuple: UNPACK_SEQUENCE */ \
    X(unpack_ex)                /* (before, starred list, after) as a tuple: UNPACK_EX */ \
    X(tuple_of)                 /* PySequence_Tuple(iterable) */ \
    X(str)                      /* PyObject_Str, for CONVERT_VALUE 1 */ \
    X(repr)                     /* PyObject_Repr, for CONVERT_VALUE 2 */ \
    X(ascii)                    /* PyObject_ASCII, for CONVERT_VALUE 3 */ \
    X(format)                   /* PyObject_Format(value, spec): FORMAT_SIMPLE, FORMAT_WITH_SPEC */ \
    X(send)                     /* SEND to a receiver that is not a generator: (value, returned) */ \
    X(get_awaitable)            /* GET_AWAITABLE: get_awaitable(obj, oparg) */ \
    X(get_aiter)                /* GET_AITER */ \
    X(get_anext)                /* GET_ANEXT */ \
    X(stopiteration_error)      /* CALL_INTRINSIC_1 STOPITERATION_ERROR: (exc, kind) */ \
    X(gen_close)                /* close() of a generator or coroutine, from its finalizer */ \
    X(import_from)              /* IMPORT_FROM: import_from(module, name) */
enum lu_kernel_id {
#define LU_KERNEL_ID(name) LU_K_##name,
    LU_KERNEL_FUNCTIONS(LU_KERNEL_ID)
#undef LU_KERNEL_ID
    LU_K_COUNT
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
    struct lu_static_type_state static_types[LU_MAX_STATIC_TYPES];
    PyObject *kernel[LU_K_COUNT];       /* strong; NULL until registered */
    unsigned int next_version_tag;      /* heap types' tp_version_tag */
    int max_str_digits;                 /* sys.get_int_max_str_digits() */
    PyObject *importlib;                /* _frozen_importlib, once installed */
    PyObject *import_func;              /* the original builtins.__import__ */
    PyConfig config;                    /* what Py_InitializeFromConfig was given, read */
};

/* -X lucifer_stress and -X lucifer_handoff (FOUNDATIONS 2.7), parsed once by
   lu_runtime_options from the config's -X options. The rungs that act on
   them read these fields; a count of 0 is off. */
struct lu_stress {
    unsigned long gc;           /* gc:N, every Nth allocation */
    unsigned long deopt;        /* deopt:N, every Nth guard execution */
    unsigned long seed;
    int seeded;
};
struct lu_handoff {
    int on;
    unsigned long seed;
    int seeded;
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
    struct lu_carrier *carrier; /* the carrier whose thread it is bound to, or NULL */
    uintptr_t pending;          /* targeted poll reasons, raised into the carrier's word at bind */
};

/* A carrier's state in one interpreter, created on first use there. */
struct lu_entry {
    struct _is *interp;
    struct lu_entry *next;      /* the carrier's entries */
    int nbound;                 /* tasks bound here; the owner record is registered while > 0 */
    struct lu_mimalloc_state mimalloc;
    struct lu_brc_owner brc;
};

/* The carrier record is per pthread, reached through the one thread-local
   word (DESIGN 5.3). It is created by the thread's first allocation or its
   first task, and retired when the thread exits. */
struct lu_carrier {
    struct lu_entry *current;   /* the entry of the interpreter running here */
    struct lu_entry *entries;
    uintptr_t poll;             /* the poll word */
    uint32_t wake;              /* the futex a park sleeps on; bumped by each unpark */
    struct lu_task *task;       /* the task last attached here: the thread's tstate
                                   while its state is attached */
    struct lu_task *gilstate;   /* the thread's PyGILState tstate (CPython's autoTSSkey) */
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

/* PyThreadState._whence values (Include/cpython/pystate.h). */
enum {
    LU_WHENCE_UNKNOWN = 0,
    LU_WHENCE_INIT = 1,
    LU_WHENCE_GILSTATE = 4,
};

/* The carrier's entry in `interp`, created on first use there. */
struct lu_entry *lu_entry_of(struct lu_carrier *c, struct _is *interp);

/* The task lifecycle (runtime/thread/task.c), after Python/pystate.c.
   lu_task_new makes a task in `interp`, unbound and detached, pinned for life
   (the degenerate scheduler). lu_task_bind binds it to the calling thread
   (bind_tstate): thread ids, and the carrier's owner record in its
   interpreter, registered by the first task bound there; and makes it the
   thread's gilstate tstate if there is none. lu_task_attach makes a bound task
   the one running on this carrier (_PyThreadState_Attach). lu_task_delete
   unlinks, unbinds and frees a task that is not attached. The thread's exit
   unbinds and frees the tasks still bound to it (lu_task_carrier_exit). */
struct lu_task *lu_task_new(struct _is *interp, int whence);
void lu_task_bind(struct lu_task *t);
void lu_task_attach(PyThreadState *ts);
void lu_task_delete(struct lu_task *t);
void lu_task_carrier_exit(struct lu_carrier *c);

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
void lu_set_immortal(PyObject *op);
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
    PyObject missing;                   /* _lucifer.MISSING: "no value", distinct from None */
    PyObject set_dummy;                 /* the key of a deleted set entry (setobject.c's dummy) */
    /* Py_EMPTY_KEYS: the keys table of every empty dict, a PyDictKeysObject
       (runtime/object/dict.h) with its eight index bytes. */
    _Alignas(8) unsigned char empty_dict_keys[40];
};
#define LU_SET_DUMMY (&_PyRuntime.singletons.set_dummy)
#define LU_MISSING (&_PyRuntime.singletons.missing)

/* Interned names the runtime looks up, made immortal at startup (CPython's
   _Py_ID strings). LU_ID(__name__) is the str "__name__". */
#define LU_IDENTIFIERS(X) \
    X(__all__) X(__build_class__) X(__builtins__) X(__call__) X(__class__) X(__delete__) \
    X(__doc__) X(__get__) X(__getitem__) X(__init__) X(__iter__) \
    X(__len__) X(__loader__) X(__main__) X(__name__) X(__new__) X(__next__) \
    X(__package__) X(__repr__) X(__set__) X(__spec__) X(__str__) X(__hash__) \
    X(__eq__) X(__ne__) X(__lt__) X(__le__) X(__gt__) X(__ge__) X(__contains__) \
    X(__setitem__) X(__delitem__) X(__bool__) X(_lucifer) X(builtins) \
    X(__add__) X(__radd__) X(__sub__) X(__rsub__) X(__mul__) X(__rmul__) X(__mod__) \
    X(__rmod__) X(__divmod__) X(__rdivmod__) X(__pow__) X(__rpow__) X(__lshift__) \
    X(__rlshift__) X(__rshift__) X(__rrshift__) X(__and__) X(__rand__) X(__xor__) \
    X(__rxor__) X(__or__) X(__ror__) X(__floordiv__) X(__rfloordiv__) X(__truediv__) \
    X(__rtruediv__) X(__matmul__) X(__rmatmul__) X(__iadd__) X(__isub__) X(__imul__) \
    X(__imod__) X(__ipow__) X(__ilshift__) X(__irshift__) X(__iand__) X(__ixor__) \
    X(__ior__) X(__import__) X(__ifloordiv__) X(__itruediv__) X(__imatmul__) X(__neg__) X(__pos__) \
    X(__abs__) X(__invert__) X(__int__) X(__float__) X(__index__)
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
        struct lu_stress stress;
        struct lu_handoff handoff;
        int finalizing;                 /* Py_FinalizeEx has begun */
        PyMutex weakref_locks[64];      /* guard the weakref lists, by referent address */
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
/* Drops one reference. When it was the last, returns 1 with the count at 0
   and the object not yet freed, so that a dealloc can free a chain in a loop. */
static inline int
lu_release_last(PyObject *op)
{
    if (_Py_IsOwnedByCurrentThread(op) && op->ob_ref_local == 1
        && _Py_atomic_load_ssize_relaxed(&op->ob_ref_shared) == 0) {
        op->ob_ref_local = 0;
        return 1;
    }
    Py_DECREF(op);
    return 0;
}
void lu_str_init(void);
void lu_set_init(void);
void lu_dict_init(void);
void lu_dict_views_init(void);          /* dict iterators and views, runtime/prim/dict.c */
void lu_set_iter_init(void);            /* the set iterator, runtime/prim/set.c */
void lu_code_init(void);
void lu_function_init(void);
void lu_weakref_init(void);
/* The referent is dying: its references go dead and their callbacks run. */
void lu_clear_weakrefs(PyObject *ob);
void lu_descr_init(void);
void lu_storage_init(void);
void lu_module_init(void);
void lu_exc_init(void);
void lu_gen_init(void);
void lu_frame_init(void);
PyObject *lu_small_int(int value);
PyObject *lu_long_from_digits(int negative, Py_ssize_t ndigits, const digit *digits);
int lu_long_as_long(PyObject *v, long *result);
Py_hash_t lu_long_hash(PyLongObject *v);
/* Operations on ints (runtime/prim/int.c); operands are any PyLong. */
PyObject *lu_long_add(PyObject *a, PyObject *b);
PyObject *lu_long_sub(PyObject *a, PyObject *b);
PyObject *lu_long_mul(PyObject *a, PyObject *b);
PyObject *lu_long_floordiv(PyObject *a, PyObject *b);
PyObject *lu_long_mod(PyObject *a, PyObject *b);
PyObject *lu_long_truediv(PyObject *a, PyObject *b);
PyObject *lu_long_pow(PyObject *a, PyObject *b, PyObject *c);
PyObject *lu_long_negate(PyObject *a);
PyObject *lu_long_invert(PyObject *a);
int lu_long_compare(PyObject *a, PyObject *b);
double lu_long_as_double(PyObject *a);
PyObject *lu_long_from_double(double d);
/* The NB_* binary operator `op` on two ints; NotImplemented when the kernel must. */
PyObject *lu_long_binary(int op, PyObject *a, PyObject *b);
Py_hash_t lu_xxhash_items(PyObject *const *items, Py_ssize_t n, int mangle_length);
Py_hash_t lu_str_hash(PyObject *s);
int lu_str_eq(PyObject *a, PyObject *b);
void lu_intern(PyInterpreterState *interp, PyObject **p);
PyObject *lu_str_from_utf8(const char *s, Py_ssize_t n, int surrogatepass);
Py_hash_t lu_hash(PyObject *v);
int lu_eq(PyObject *a, PyObject *b);
/* Whether two keys of an equal hash are equal when that needs no Python: 1, 0,
   or -1 when __eq__ must decide (runtime/object/dict.c). */
int lu_native_eq(PyObject *a, PyObject *b);
/* Whether the dict's keys are all exact strs, which its unicode table guarantees. */
int lu_dict_str_keys(PyObject *dict);
PyObject *lu_set_new(PyTypeObject *type);
int lu_set_add(PyObject *set, PyObject *key);
Py_hash_t lu_frozenset_hash(PyObject *set);
int lu_hash_secret_init(int use_seed, unsigned long seed);

/* runtime/interp/config.c */
PyStatus lu_runtime_options(const PyConfig *config);
PyStatus lu_config_read_copy(PyConfig *dst, const PyConfig *src);
int lu_utf8_mode(const PyConfig *config);

/* runtime/interp/sysmodule.c: sys's native functions, and sys.configure(config). */
extern PyMethodDef lu_sys_native_methods[];
int lu_sys_configure(struct _is *interp);

/* runtime/interp/import.c: _imp, then _frozen_importlib and its install. */
int lu_import_init(struct _is *interp);

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

/* Types (runtime/prim/type.c). Static builtin types are made ready natively
   at startup: bases, MRO and an empty dict in the interpreter's state, which
   the kernel's @kernel_type then fills. */
#define LU_VERSION_TAG_STATIC_MAX ((1u << 17) - 1)     /* _Py_MAX_GLOBAL_TYPE_VERSION_TAG */
extern PyTypeObject lu_method_wrapper_type;
int lu_types_init(PyInterpreterState *interp);
/* Fills each static type's unset slots from its bases (inherit_slots); run
   after every kernel module, as kernel classes install slots. */
void lu_static_types_inherit(void);
/* The type's dict: in interpreter state for static builtin types. Borrowed. */
PyObject *lu_type_dict(PyTypeObject *tp);
/* PyType_Modified: the version tag of tp and its subclasses becomes 0. */
void lu_type_modified(PyTypeObject *tp);
/* "__name__" of a type, new: ht_name, or tp_name after its last dot. */
PyObject *lu_type_name(PyTypeObject *tp);
/* Adds descriptors for tp_members and tp_getset to the type's dict. */
int lu_type_add_descriptors(PyTypeObject *tp);
/* The module of the nearest frame that is not kernel-hidden (PyEval_GetGlobals). */
PyObject *lu_caller_globals(PyThreadState *ts);

/* Descriptors and kernel functions (runtime/object/descr.c). A kernel
   function is a Python function that looks native: a PyCFunctionObject or
   a method, wrapper or classmethod descriptor whose PyMethodDef is the head
   of an lu_kernel_def (DESIGN 4.4). */
struct lu_kernel_def {
    PyMethodDef def;
    PyObject *func;             /* the Python function; strong */
    PyObject *name;             /* keeps def.ml_name's UTF-8 alive */
    int bind;                   /* a PyCFunction made from it passes m_self first */
};
PyObject *lu_kernel_meth(PyObject *self, PyObject *args);
static inline struct lu_kernel_def *
lu_kernel_def_of(PyMethodDef *ml)
{
    return ml != NULL && ml->ml_meth == lu_kernel_meth ? (struct lu_kernel_def *)ml : NULL;
}
struct lu_kernel_def *lu_kernel_def_new(PyObject *name, PyObject *func, int flags, int bind);
/* A kernel function as a builtin of `module` (builtin_function_or_method). */
PyObject *lu_kernel_builtin(PyObject *module, PyObject *name, PyObject *func);
PyObject *lu_descr_new_method(PyTypeObject *type, PyMethodDef *ml);
PyObject *lu_descr_new_classmethod(PyTypeObject *type, PyMethodDef *ml);
PyObject *lu_descr_new_wrapper(PyTypeObject *type, PyObject *name, struct lu_kernel_def *k);
PyObject *lu_descr_new_member(PyTypeObject *type, PyMemberDef *member);
PyObject *lu_descr_new_getset(PyTypeObject *type, PyGetSetDef *getset);
PyObject *lu_method_wrapper_new(PyObject *descr, PyObject *self);
/* The Python function the interpreter runs for a kernel callable, and the
   self it prepends (borrowed, or NULL). NULL when callable is none. Checks
   an unbound descriptor's first argument: -1 with TypeError set. */
int lu_kernel_target(PyObject *callable, PyObject *const *args, Py_ssize_t nargs,
                     PyObject **func, PyObject **self);
/* Calls a callable from C (PyObject_Vectorcall): vectorcall, else tp_call. */
PyObject *lu_call(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames);
/* Calls interp->kernel[id] from C, in a nested eval loop. */
PyObject *lu_call_kernel(enum lu_kernel_id id, PyObject *const *args, Py_ssize_t nargs);
/* PyObject_IsTrue: 1, 0 or -1 with an error set. */
int lu_is_true(PyObject *v);
/* The truth of a value whose type's truth is its layout: 1, 0, or -1 when __bool__
   or __len__ must run. */
int lu_truth_exact(PyObject *v);

/* Slots (runtime/types/slots.c): the C slot thunks that run kernel Python
   for C callers, and the slot table _lucifer.set_slot installs them from. */
PyObject *lu_slot_tp_getattro(PyObject *self, PyObject *name);
int lu_slot_tp_setattro(PyObject *self, PyObject *name, PyObject *value);
PyObject *lu_slot_tp_call(PyObject *self, PyObject *args, PyObject *kwargs);
/* Sets slot `name` of tp: how 0 clears it, 1 installs the thunk, 2 copies
   it from `from`, 3 installs PyObject_HashNotImplemented (tp_hash only). */
int lu_set_slot(PyTypeObject *tp, PyObject *name, int how, PyTypeObject *from);
/* Calls special method `name` of self's type, as vectorcall_method does. */
PyObject *lu_call_special(PyObject *self, PyObject *name, PyObject *const *args, Py_ssize_t nargs);

/* The one path that tells watchers about a dict change (FOUNDATIONS 2.4):
   the namespace primitives and the kernel's dict primitives call it before
   they change `mp`. key and value are borrowed. */
void lu_dict_notify(PyDictObject *mp, PyDict_WatchEvent event, PyObject *key, PyObject *value);

/* Frees replaced storage (a list's array, a dict's keys table) once no
   lock-free reader can still hold it (runtime/prim/storage.c). */
void lu_qsbr_retire(void *p);

/* Appends to a list (borrowed item), growing its storage. */
int lu_list_append(PyObject *list, PyObject *item);

/* The tuple, list, reverse list and sequence-protocol iterators' layout
   (runtime/prim/storage.c); it_seq is NULL once exhausted. */
typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index;
    PyObject *it_seq;
} seqiterobject;
/* The next item of a tuple, list or list_reverseiterator, or a range_iterator's
   next int; NULL when exhausted. */
PyObject *lu_seqiter_next(PyObject *iter);
PyObject *lu_rangeiter_next(PyObject *iter);
/* A new range_iterator or longrange_iterator over the range r, or NULL with an error. */
PyObject *lu_range_iter(PyObject *r);

/* Exceptions raised natively (runtime/capi/errors.c), before and beneath the
   kernel's exception classes. */
void lu_err_format(PyObject *type, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
