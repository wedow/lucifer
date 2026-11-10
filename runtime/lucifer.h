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
/* A PyMethodDef flag of the runtime's own: the primitive never runs Python
   code (it may allocate and raise), so compiled code calls it directly, also
   from an inlined frame. */
#define LU_METH_NOPY 0x8000
/* Set on the def of a kernel function: its ml_meth is a stub, so a C caller that
   dispatches on ml_flags (Cython does) sees a calling convention it does not know
   and takes the generic call. */
#define LU_METH_KERNEL 0x2000

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

/* ob_gc_bits (Include/internal/pycore_gc.h): tracked objects are the
   collector's; the rest of the bits are its working state. */
enum {
    LU_GC_TRACKED = 1 << 0,
    LU_GC_FINALIZED = 1 << 1,   /* tp_finalize has run */
    LU_GC_UNREACHABLE = 1 << 2, /* working: not (yet) known to be reachable */
    LU_GC_FROZEN = 1 << 3,      /* gc.freeze(): skipped by collections */
    LU_GC_ALIVE = 1 << 5,       /* working: known reachable */
    LU_GC_DEFERRED = 1 << 6,    /* ob_ref_shared carries LU_REF_DEFERRED */
};
/* _Py_REF_DEFERRED: added to the shared count of an object whose references
   from frames are not counted, so that its count never reaches zero by
   decrefs; only a collection frees it. */
#define LU_REF_DEFERRED (PY_SSIZE_T_MAX / 8)

static inline int
lu_gc_is_tracked(PyObject *op)
{
    return (op->ob_gc_bits & LU_GC_TRACKED) != 0;
}

/* PyObject_IS_GC: whether the collector can hold the object. */
static inline int
lu_is_gc(PyObject *op)
{
    PyTypeObject *tp = Py_TYPE(op);
    return (tp->tp_flags & Py_TPFLAGS_HAVE_GC) && (tp->tp_is_gc == NULL || tp->tp_is_gc(op));
}

static inline int
lu_has_deferred_refcount(PyObject *op)
{
    return (op->ob_gc_bits & LU_GC_DEFERRED) != 0;
}

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
Py_ssize_t lu_write_fd(int fd, const void *buf, size_t count);
/* Evaluates the blocking call `call` between the two, which keeps errno. */
#define LU_SYSCALL(call) ({ \
        PyThreadState *lu_ts_ = lu_syscall_begin(); \
        __typeof__(call) lu_r_ = (call); \
        lu_syscall_end(lu_ts_); \
        lu_r_; })

/* lu_park's sleep (runtime/prim/os.c): waits while *word == expected, until
   a wake or the absolute CLOCK_MONOTONIC deadline (-1 for none). Returns 0,
   or ETIMEDOUT, EINTR or EAGAIN. */
int lu_futex_wait(uint32_t *word, uint32_t expected, PyTime_t deadline);
void lu_futex_wake(uint32_t *word);
/* Waits for a thread to exit, detached (runtime/prim/os.c); pthread_join's
   result. */
int lu_join_thread(pthread_t thread);
/* The fault handler's write(2), safe in a signal handler, and its watchdog's wait (runtime/prim/os.c). */
void lu_write_signal_safe(int fd, const char *s, size_t n);
int lu_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *lock, const struct timespec *deadline);

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
    int idle;                   /* its carrier runs no task, so touches none of its objects:
                                   a queuing thread merges them itself (under the bucket lock) */
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
    X(dict_get)                 /* dict.get, len and isinstance: CALL answers them itself */ \
    X(len)                      /* for exact builtin types */ \
    X(builtin_getattr)          /* the builtin getattr: CALL answers the common reads itself */ \
    X(builtin_hasattr)          /* the builtin hasattr */ \
    X(builtin_setattr)          /* the builtin setattr */ \
    X(index)                    /* PyNumber_Index: CALL answers it for an exact int */ \
    X(bytearray_append)         /* bytearray.append */ \
    X(tuplegetter)              /* _collections._tuplegetter, a class: LOAD_ATTR reads its items itself */ \
    X(list_append)              /* list.append, dict.items: CALL answers them for builtin lists and dicts */ \
    X(dict_items)               /* */ \
    X(isinstance)               /* */ \
    X(dict_setitem)             /* PyDict_SetItem(dict, key, value): the dict's own, whatever the subclass overrides */ \
    X(dict_delitem)             /* PyDict_DelItem(dict, key) */ \
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
    X(import_from)              /* IMPORT_FROM: import_from(module, name) */ \
    X(import_star)              /* INTRINSIC_IMPORT_STAR: import_star(locals, module) */ \
    X(intrinsic_1)              /* CALL_INTRINSIC_1 of PEP 695: (value, oparg) */ \
    X(intrinsic_2)              /* CALL_INTRINSIC_2 of PEP 695: (a, b, oparg) */ \
    X(match_class)              /* MATCH_CLASS: (subject, type, names, nargs): the attributes or None */ \
    X(match_keys)               /* MATCH_KEYS: (subject, keys): the values or None */ \
    X(match_mapping)            /* MATCH_MAPPING: whether the subject is a mapping */ \
    X(match_sequence)           /* MATCH_SEQUENCE: whether the subject is a sequence for patterns */ \
    X(build_template)           /* BUILD_TEMPLATE: (strings, interpolations) */ \
    X(build_interpolation)      /* BUILD_INTERPOLATION: (value, expression, conversion, format_spec) */ \
    X(generic_alias)            /* Py_GenericAlias(origin, args): type[int] before the kernel has the type */
enum lu_kernel_id {
#define LU_KERNEL_ID(name) LU_K_##name,
    LU_KERNEL_FUNCTIONS(LU_KERNEL_ID)
#undef LU_KERNEL_ID
    LU_K_COUNT
};

/* The dict pointer a Py_TPFLAGS_MANAGED_DICT instance carries before its header. */
#define LU_MANAGED_DICT_OFFSET (-(Py_ssize_t)sizeof(PyObject *))
#define LU_MANAGED_DICT(obj) (*(PyObject **)((char *)(obj) + LU_MANAGED_DICT_OFFSET))

/* Instance attributes (runtime/object/attrs.c): inline values after
   tp_basicsize while LU_ATTRS_IN_DICT is clear, else the instance dict. */
#define LU_ATTRS_IN_DICT (1 << 8)       /* ob_flags: set once the values moved to the dict */
#define LU_ATTRS_UNORDERED (1 << 9)     /* ob_flags: the words' insertion order is in the order record */
#define LU_ATTRS_ROOM_SHIFT 10
#define LU_ATTRS_ROOM (63 << LU_ATTRS_ROOM_SHIFT)  /* ob_flags: the names the instance has room for (0: the table's) */
static inline int
lu_inline_live(PyObject *obj)
{
    return (Py_TYPE(obj)->tp_flags & Py_TPFLAGS_INLINE_VALUES)
        && !(__atomic_load_n(&obj->ob_flags, __ATOMIC_ACQUIRE) & LU_ATTRS_IN_DICT);
}
static inline PyObject **
lu_inline_values(PyObject *obj)
{
    return (PyObject **)((char *)obj + Py_TYPE(obj)->tp_basicsize);
}
/* Each name has two words: the value's, and after it a raw value. The
   value word holds the object, NULL when absent, or a marker for a float
   (LU_UNBOXED, binary64) or an int (LU_UNBOXED_INT, int64) stored without
   an object, its value in the second word, whose box the first generic
   reader makes and installs (DESIGN 4.5). */
#define LU_INLINE_STRIDE 2
#define LU_INLINE_WORD(values, i) ((values)[LU_INLINE_STRIDE * (i)])
#define LU_UNBOXED ((PyObject *)1)
#define LU_UNBOXED_INT ((PyObject *)3)
static inline int
lu_unboxed(PyObject *word)
{
    return word == LU_UNBOXED || word == LU_UNBOXED_INT;
}
static inline uint64_t *
lu_inline_raw(PyObject **word)
{
    return (uint64_t *)(word + 1);
}
/* Class setup and teardown, and the bytes each instance carries. */
int lu_inline_values_init(PyTypeObject *tp);
void lu_inline_values_fini(PyTypeObject *tp);
/* A new instance's room (names), taken for one instance; settled for
   every later one (a unit that allocates instances in line); the bytes a
   room takes after tp_basicsize, and the ob_flags bits that record it. */
Py_ssize_t lu_inline_take_room(PyTypeObject *tp);
Py_ssize_t lu_inline_settle_room(PyTypeObject *tp);
size_t lu_inline_room_size(Py_ssize_t room);
uint16_t lu_inline_room_flags(PyTypeObject *tp, Py_ssize_t room);
/* The index of name in the class's shared names, or -1. */
Py_ssize_t lu_inline_index(PyTypeObject *tp, PyObject *name);
/* The address of the instance's dict pointer, or NULL when it has none. */
PyObject **lu_dict_ptr(PyObject *obj);
/* The instance dict (borrowed), made on demand when `create`; the inline
   values move into it. NULL when there is none, or on error. */
PyObject *lu_instance_dict(PyObject *obj, int create);
/* __dict__ assignment (dict NULL deletes): -1 with an error, else 0. */
int lu_instance_set_dict(PyObject *obj, PyObject *dict);
/* An instance attribute: 1 with a new reference in *v, 0 when absent, -1
   when only the kernel can answer (a dict that is not exact, a name that is
   not an exact str); never raises. */
int lu_attr_lookup(PyObject *obj, PyObject *name, PyObject **v);
/* Stores v, or deletes the name when v is NULL, on an object lu_dict_ptr
   answers for: 1 done, 0 when the name to delete is absent, -1 on error. */
int lu_attr_store(PyObject *obj, PyObject *name, PyObject *v);
/* STORE_ATTR_INSTANCE_VALUE: v into the inline word at byte offset of an
   instance of a class whose name there is the attribute's; 0 when only
   lu_attr_store can store it. */
int lu_inline_store(PyObject *obj, Py_ssize_t offset, PyObject *v);
/* The same for a float compiled code holds as binary64, or an int as
   int64: no object is made. */
int lu_inline_store_f(PyObject *obj, Py_ssize_t offset, double v);
int lu_inline_store_i(PyObject *obj, Py_ssize_t offset, int64_t v);
/* A new reference to the value word at `word` (an instance's, read
   without its lock): NULL when absent or when a try fails; an unboxed
   float gets its box. */
PyObject *lu_inline_get(PyObject *obj, PyObject **word);
/* An exit's box of the value compiled code stored raw in word: installed
   while the word still holds that value unboxed, so that the box the
   exit's frame names and the attribute are one object. */
void lu_inline_install(PyObject *obj, PyObject **word, PyObject *box);
int lu_inline_traverse(PyObject *obj, visitproc visit, void *arg);
/* Every unboxed value of the instance gets its box, as a reader would make
   it: before gc.get_referents shows what the instance references. 0, or -1
   with MemoryError. */
int lu_inline_box_all(PyObject *obj);
void lu_inline_clear(PyObject *obj);

/* Watcher ids per kind, as CPython's TYPE_/DICT_/FUNC_/CODE_MAX_WATCHERS. */
#define LU_MAX_WATCHERS 8
typedef void (*lu_watcher)(void);

/* PyEvent (Include/internal/pycore_lock.h): a one-time notification. */
typedef struct PyEvent {
    uint8_t v;
} PyEvent;

/* Stop-the-world (runtime/thread/stw.c), after struct _stoptheworld_state.
   The task list lock guards everything but the mutex, which serializes
   requests, and world_stopped, which the requester writes. */
struct lu_stw {
    PyMutex mutex;
    int requested;
    int world_stopped;
    int countdown;              /* attached tasks that have yet to stop */
    struct lu_task *requester;  /* its own task, or NULL */
    PyEvent stop_event;         /* set when countdown reaches 0 */
};

/* The collector's state (runtime/interp/gc.c), after GCState in
   Python/gc_free_threading.c. */
struct lu_gc {
    int enabled;
    int collecting;
    int young_count;            /* GC objects allocated since the last collection */
    int young_threshold;
    int old_threshold[2];
    int old_count[2];
    Py_ssize_t deferred_count;
    Py_ssize_t last_mem;
    Py_ssize_t long_lived_total;
    PyObject *hook;             /* gc.py's callback runner; strong */
    PyObject *garbage;          /* gc.garbage; strong */
    int debug;                  /* gc.set_debug flags */
    int freeze_active;          /* gc.freeze() was used */
    Py_ssize_t stats[3][3];     /* per generation: collections, collected, uncollectable */
};

/* A function queued by Py_AddPendingCall (runtime/interp/poll.c). */
struct lu_pending_call {
    struct lu_pending_call *next;
    int (*func)(void *);
    void *arg;
    int main_only;
    int rawfree;                /* arg is PyMem_RawFree'd after the call */
};
struct lu_pending {
    PyMutex mutex;
    struct lu_pending_call *head, **tail;
    struct lu_task *handling;   /* the task running the queue: it is not re-entered */
    int main_count;             /* queued main-only calls (CPython caps them at 32) */
    int other_count;            /* queued other calls (CPython caps them at 300) */
};

/* sys.monitoring (runtime/interp/instrument.c): the tool ids, event counts
   and tables of which tools watch which events (pycore_instruments.h). */
#define LU_MON_TOOL_IDS 8
#define LU_MON_LOCAL_EVENTS 11      /* events that instrument bytecode */
#define LU_MON_UNGROUPED_EVENTS 16  /* events a tool can ask for singly */
#define LU_MON_EVENTS 19
#define _PY_MONITORING_LOCAL_EVENTS LU_MON_LOCAL_EVENTS    /* cpython/monitoring.h */
typedef struct { uint8_t tools[LU_MON_LOCAL_EVENTS]; } lu_local_monitors;
typedef struct { uint8_t tools[LU_MON_UNGROUPED_EVENTS]; } lu_global_monitors;

/* PyInterpreterState (private layout; DESIGN 4.1). */
#define LU_MCACHE_SIZE 4096
struct _is {
    mi_subproc_id_t subproc;    /* segments abandoned by one interpreter's carriers
                                   are reclaimed only by its own */
    struct {
        struct lu_brc_bucket buckets[LU_BRC_NUM_BUCKETS];
    } brc;
    struct lu_lock tasks_lock;  /* the task list (HEAD_LOCK) */
    PyThreadState *tasks;       /* every task of this interpreter, in any state */
    uint64_t next_task_id;
    int tasks_closed;           /* finalization removed the other tasks: new ones hang at attach */
    PyObject *modules;          /* sys.modules */
    PyObject *modules_by_index; /* single-phase modules, for PyState_FindModule */
    PyObject *builtins;         /* builtins.__dict__ */
    PyObject *sysdict;          /* sys.__dict__ */
    PyObject *dict;             /* PyInterpreterState_GetDict, made on first use */
    PyObject *last_resort_memory_error; /* raised when no MemoryError can be made */
    Py_ssize_t interactive_src_count;   /* names each interactive statement's code */
    struct lu_atexit *atexit;   /* PyUnstable_AtExit callbacks, newest first */
    PyMutex atexit_lock;
    PyObject *interned;         /* interned str -> itself */
    int co_extra_user_count;    /* PyUnstable_Eval_RequestCodeExtraIndex hands out the next */
    freefunc co_extra_freefuncs[MAX_CO_EXTRA_USERS];
    PyMutex co_extra_lock;      /* hands out the indexes */
    _PyFrameEvalFunction eval_frame;    /* PEP 523; NULL runs frames inline */
    uintptr_t monitoring_version;       /* a multiple of LU_POLL_VERSION_UNIT */
    PyMutex tracing_setup_lock;         /* the first sys.settrace or setprofile registers callbacks */
    int sys_trace_setup, sys_profile_setup;
    int sys_tracing_threads, sys_profiling_threads;     /* tasks with a trace or profile function */
    uintptr_t last_restart_version;     /* sys.monitoring.restart_events */
    lu_global_monitors monitors;        /* the tools watching each event, everywhere */
    PyObject *monitoring_callables[LU_MON_TOOL_IDS][LU_MON_EVENTS];     /* strong */
    PyObject *monitoring_tool_names[LU_MON_TOOL_IDS];                   /* strong */
    uintptr_t monitoring_tool_versions[LU_MON_TOOL_IDS];
    struct lu_static_type_state static_types[LU_MAX_STATIC_TYPES];
    PyObject *kernel[LU_K_COUNT];       /* strong; NULL until registered */
    PyObject *kernel_rebound;           /* strong: name -> None for each global a kernel function rebinds */
    PyObject *kernel_twins;             /* strong: (a kernel function's code, its twin) pairs (kernel_twin) */
    PyObject *kernel_nexts;             /* strong: (iterator type, its step) pairs (next_missing_set) */
    unsigned int next_version_tag;      /* heap types' tp_version_tag; 0 once spent */
    uint32_t next_keys_version;         /* dk_version of the next keys table versioned; 0 once spent */
    uint32_t next_func_version;         /* co_version of the next code object; 0 once spent */
    /* Watchers (FOUNDATIONS 2.4): a callback per id; a type, dict, function
       or code is watched by the ids set in its own bits. */
    lu_watcher type_watchers[LU_MAX_WATCHERS];      /* PyType_WatchCallback */
    lu_watcher dict_watchers[LU_MAX_WATCHERS];      /* PyDict_WatchCallback */
    lu_watcher func_watchers[LU_MAX_WATCHERS];      /* PyFunction_WatchCallback */
    lu_watcher code_watchers[LU_MAX_WATCHERS];      /* PyCode_WatchCallback */
    lu_watcher context_watchers[LU_MAX_WATCHERS];   /* PyContext_WatchCallback */
    int specializing;                   /* set once the kernel has booted: instructions may specialize */
    /* The type attribute cache (CPython's method cache): what _PyType_Lookup
       found for an interned name in a type of that version. Read without a
       lock: an entry is valid when its sequence is even and the same before
       and after the read; a writer claims an even sequence by making it odd. */
    struct lu_mcache_entry {
        uint32_t seq;
        uint32_t version;
        PyObject *name;
        PyObject *value;            /* borrowed; NULL for an absent name */
    } mcache[LU_MCACHE_SIZE];
    int kernel_booted;                  /* set with it: the kernel's functions are all registered */
    int max_str_digits;                 /* sys.get_int_max_str_digits() */
    PyObject *importlib;                /* _frozen_importlib, once installed */
    PyObject *import_func;              /* the original builtins.__import__ */
    PyConfig config;                    /* what Py_InitializeFromConfig was given, read */
    struct lu_stw stw;
    struct lu_gc gc;
    struct lu_pending pending;
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
/* -X lucifer_jit=off|on|always and -X lucifer_jit_stats (runtime/jit/jit.h). */
struct lu_jit_options {
    int mode;                   /* enum lu_jit_mode */
    int stats;                  /* print the tier's counts at exit */
    /* Debugging aids, from the environment: LUCIFER_JIT_DEBUG logs entries
       and exits, LUCIFER_JIT_DUMP prints each unit's IR, LUCIFER_JIT_ONLY
       compiles only the qualnames it lists, comma-separated. */
    int debug, dump;
    const char *only;
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
    LU_POLL_QSBR = 1 << 12,             /* the write sequence moved: report a quiescent state */
    LU_POLL_EVENTS_BITS = 13,
};
#define LU_POLL_EVENTS_MASK (((uintptr_t)1 << LU_POLL_EVENTS_BITS) - 1)
/* The reasons whose effects Python sees, which wait while a builtin's
   frame runs (lu_poll_slow). */
#define LU_POLL_VISIBLE (LU_POLL_SIGNALS_PENDING | LU_POLL_CALLS_TO_DO | LU_POLL_ASYNC_EXCEPTION \
    | LU_POLL_GC_SCHEDULED | LU_POLL_STRESS_TICK)
#define LU_POLL_VERSION_UNIT ((uintptr_t)1 << LU_POLL_EVENTS_BITS)

/* PyThreadState.state (Include/internal/pycore_pystate.h). */
enum {
    _Py_THREAD_DETACHED = 0,
    _Py_THREAD_ATTACHED = 1,
    _Py_THREAD_SUSPENDED = 2,
    _Py_THREAD_SHUTTING_DOWN = 3,       /* a daemon thread at finalization: it hangs when it attaches */
};

/* The task record (DESIGN 5.3): a Python thread of control. The PyThreadState
   comes first, so a task is its tstate; the tail is Lucifer's. The datastack
   fields of the tstate hold the task's frames. */
struct lu_task {
    PyThreadState ts;
    /* CPython's _PyThreadStateImpl continues here: a reference count, then
       the C stack bounds PyUnstable_ThreadState_SetStackProtection records
       (an extension reads them at these offsets, pycore_tstate.h). lucifer's
       recursion limit is a counter, so the bounds are bookkeeping only; the
       init pair backs ResetStackProtection. */
    Py_ssize_t tstate_refcount;
    uintptr_t c_stack_top, c_stack_soft_limit, c_stack_hard_limit;
    uintptr_t c_stack_init_base, c_stack_init_top;
    uintptr_t pin;              /* transient pins: foreign C and nested evals (DESIGN 5.4) */
    int pinned_for_life;        /* main, foreign-thread and thread-scoped tasks */
    struct lu_carrier *carrier; /* the carrier whose thread it is bound to, or NULL */
    uintptr_t pending;          /* targeted poll reasons, raised into the carrier's word at bind */
    int in_pending_calls;       /* running the pending calls: a poll inside them leaves the rest */
    /* Scheduling (runtime/thread/sched.c). */
    void *base_entry;           /* the C-entry marker of the task's outermost activation, which
                                   outlives that activation's C stack when it parks */
    struct lu_carrier *home;    /* the carrier whose C stack holds that activation's caller */
    struct lu_wait wait;        /* the wait of an unpinned park: the activation is gone, so the
                                   record cannot be on the stack */
    struct lu_task *runq_next;  /* in a carrier's run queue */
    PyObject *result;           /* what the outermost activation returned, on its way home */
    int done;                   /* result is set: this task is going home to return it */
    uint64_t rng;               /* seeded handoff's choices */
    int gc_alloc_count;         /* the carrier's buffered GC allocation count, carried
                                   while the task is between carriers */
};

/* Quiescent-state-based reclamation (runtime/thread/qsbr.c), after
   Python/qsbr.c: storage retired by lu_qsbr_retire is freed once every
   attached carrier has passed a poll since. Each carrier holds its queue. */
struct lu_qsbr_item {
    void *ptr;
    uint64_t goal;              /* the write sequence readers must reach */
};
struct lu_qsbr_queue {
    struct lu_qsbr_item *items;
    size_t first, n, cap;
};
struct lu_qsbr {
    uint64_t seq;               /* the write sequence last observed; 0 while detached */
    struct lu_qsbr_queue queue;
    int deferred_count;         /* retired since the sequence last advanced */
    size_t deferred_page_memory;/* bytes of empty mimalloc pages waiting for a goal */
    int should_process;
    struct lu_carrier *next;    /* the registry's list */
};

/* A carrier's state in one interpreter, created on first use there. */
struct lu_entry {
    struct _is *interp;
    struct lu_carrier *carrier;
    struct lu_entry *next;      /* the carrier's entries */
    int gc_alloc_count;         /* GC allocations not yet added to the interpreter's count */
    unsigned long stress_count; /* allocations since the last stress tick */
    int nbound;                 /* tasks bound here; the owner record is registered while > 0 */
    struct lu_mimalloc_state mimalloc;
    struct lu_brc_owner brc;
    /* Freed floats and one-digit ints kept for reuse (CPython's per-thread
       freelists), linked through the word after the header (LU_FREE_NEXT);
       runtime/object/layouts.c, and compiled code's boxes. */
    struct lu_freelist { PyObject *head; int n; } free_floats, free_ints;
};

#define LU_FREE_NEXT(op) (*(PyObject **)((char *)(op) + sizeof(PyObject)))

/* The carrier record is per pthread, reached through the one thread-local
   word (DESIGN 5.3). It is created by the thread's first allocation or its
   first task, and retired when the thread exits. */
struct lu_carrier {
    struct lu_entry *current;   /* the entry of the interpreter running here */
    struct lu_qsbr qsbr;
    struct lu_entry *entries;
    uintptr_t poll;             /* the poll word */
    uint32_t wake;              /* the futex a park sleeps on; bumped by each unpark */
    unsigned long native_id;    /* the thread's TID: a syscall, so read once */
    pthread_t thread;           /* the carrier's thread */
    uint32_t sleeping;          /* the carrier's thread is in, or about to enter, a futex sleep on `wake` */
    struct lu_task *task;       /* the task last attached here: the thread's tstate
                                   while its state is attached */
    struct lu_task *gilstate;   /* the thread's PyGILState tstate (CPython's autoTSSkey) */
    /* A scheduler carrier (runtime/thread/sched.c) runs tasks from its queue,
       guarded by _PyRuntime.sched.lock. It outlives its tasks, so its owner
       record stays registered until the carrier exits. */
    struct lu_task *runq_head, *runq_tail;
    int scheduled;
    /* Below this the thread's C stack keeps room for an activation of the
       interpreter: compiled code calls a callee only above it. */
    char *jit_stack_floor;
    /* The thread's C stack, [stack_base, stack_top), or zeros when unknown:
       a task bound here takes it as the bounds C recursion checks watch. */
    uintptr_t stack_base, stack_top;
};

extern _Thread_local struct lu_carrier *lu_carrier;

/* How much C stack a call from compiled code leaves for the callee's
   activation: below it the interpreter makes the call, without
   recursing (DESIGN 5.2). */
#define LU_JIT_STACK_RESERVE (192 * 1024)

/* Raising and clearing reasons from any thread. A reason aimed at a task
   (lu_task_signal) is kept in the task, so that binding it raises the
   reason in whichever word the task runs under; the rest belong to the
   carrier. A set is a release: the data it announces is published first. */
static inline void
lu_poll_set(struct lu_carrier *c, uintptr_t bits)
{
    __atomic_fetch_or(&c->poll, bits, __ATOMIC_SEQ_CST);
}

static inline void
lu_poll_clear(struct lu_carrier *c, uintptr_t bits)
{
    __atomic_fetch_and(&c->poll, ~bits, __ATOMIC_SEQ_CST);
}

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
void lu_task_set_c_stack(struct lu_task *t, uintptr_t base, uintptr_t top);
void lu_task_attach(PyThreadState *ts);
/* The task stops using objects: its critical sections release their locks,
   its carrier goes offline for QSBR, and its state becomes `state`, detached
   or (for a stop-the-world) suspended. */
void lu_task_detach(struct lu_task *t, int state);
void lu_task_delete(struct lu_task *t);
/* Finalization (CPython's _PyEval_StopTheWorldAll and _PyThreadState_RemoveExcept): every task
   but the caller's is dropped from the interpreter and marked shutting down, so a daemon thread
   hangs when it next attaches. The caller is attached. */
void lu_task_shutdown_others(struct _is *interp);
void lu_task_carrier_exit(struct lu_carrier *c);
/* The task leaves its carrier at a migration point (DESIGN 5.4): detached,
   unbound, and no longer the carrier's task. The carrier's owner record stays
   registered. lu_task_bind and lu_task_attach put it on the next carrier. */
void lu_task_leave(struct lu_task *t);
/* The calling task calls a platform primitive whose effect belongs to its OS thread
   (sched_setaffinity(0, ...), pthread_sigmask, unshare, setns): it stays on this carrier for life. */
void lu_task_pin_for_life(void);
/* Holds a detached task suspended while another thread acts for it, and lets it go: it cannot attach
   in between. The first answers whether the task was detached. */
int lu_task_try_suspend_detached(struct lu_task *t);
void lu_task_resume_detached(struct lu_task *t);
/* Clears targeted reasons from the task and its carrier's word. */
void lu_task_unsignal(struct lu_task *t, uintptr_t bits);

/* The scheduler (runtime/thread/sched.c, DESIGN 5.5 and 5.8). Under -X
   lucifer_handoff a second carrier runs, and an unpinned task parks at
   safepoints and resumes on the other carrier.

   An eval-loop activation that parks returns LU_EVAL_PARKED to its caller,
   which is lu_eval on the carrier whose C stack holds the task's C caller (its
   home). lu_sched_home commits the park and runs that carrier's loop until
   the task is back, bound to the home carrier, with the result. */
#define LU_EVAL_PARKED ((PyObject *)1)
void lu_sched_start(void);
void lu_sched_stop(void);
/* A safepoint of a task's outermost activation: whether it parks here. When
   it does, the task has registered and completed its wait, and the loop
   must publish the frames and return LU_EVAL_PARKED. */
int lu_sched_safepoint(struct lu_task *t);
PyObject *lu_sched_home(PyThreadState *ts);
/* Queues a detached task on `c` and wakes it. */
void lu_sched_enqueue(struct lu_task *t, struct lu_carrier *c);

/* The one handler for every poll reason (FOUNDATIONS 2.5), called by the
   interpreter and the JIT when the carrier's poll word has a reason set.
   Returns -1 with an exception set when the reason raises. */
int lu_poll_slow(PyThreadState *ts);
/* The poll's LU_POLL_EXPLICIT_MERGE: objects other threads queued have their
   refcounts merged (their releases may run destructors), and storage retired
   to QSBR is processed. Returns None (immortal). */
PyObject *lu_poll_merge(void);
/* The merge and a scheduled collection, which compiled code outside a
   builtin makes at its poll instead of leaving for them, its frames
   published (emit.c's parked_call). Returns None. */
PyObject *lu_poll_in_place(void);

/* Raises `bits` (targeted reasons) at task `t`: kept in the task, and set in
   its carrier's word when it is bound. The caller holds the interpreter's
   task list lock, which keeps the task and its carrier alive. */
void lu_task_signal(struct lu_task *t, uintptr_t bits);

/* os.fork (runtime/thread/fork.c): the pid in the parent, 0 in the child, -1 with errno set. */
long lu_fork(void);
/* The child of a fork made by the main task on the second carrier has no home carrier thread to
   return the program's result to: the thread that finished it finishes the program
   (runtime/pylifecycle.c). */
void lu_main_finished(PyObject *result);

/* Stop-the-world (runtime/thread/stw.c). A stop completes when every other
   task has stopped: attached ones at their next poll, detached ones at once.
   The world restarts at the matching start. The requester stays attached. */
void lu_stw_stop(struct _is *interp);
void lu_stw_start(struct _is *interp);
/* A poll found the please-stop reason: acknowledge, wait out the stop, and
   attach again. */
void lu_stw_acknowledge(struct lu_task *t);
/* Whether the interpreter's world is stopped. */
static inline int
lu_world_stopped(struct _is *interp)
{
    return __atomic_load_n(&interp->stw.world_stopped, __ATOMIC_ACQUIRE);
}
/* Task creation and deletion tell a stop in progress (called with the task
   list lock held): a new task starts suspended; a deleted one is not waited for. */
void lu_stw_task_new(struct _is *interp, struct lu_task *t);
void lu_stw_task_delete(struct _is *interp, struct lu_task *t);

/* The collector's trigger (runtime/interp/gc.c). */
/* A carrier's GC allocations reach the interpreter's count in batches of
   this many (LOCAL_ALLOC_COUNT_THRESHOLD); compiled code's in-line
   allocations count in the same word. */
#define LU_GC_ALLOC_FLUSH 512
void lu_gc_record_allocation(struct lu_entry *entry);
/* Sets _PyRuntime.allocators.observed after the allocators or the reference
   tracer change: a mem or object allocator other than the runtime's, or a
   tracer (runtime/object/alloc.c). */
void lu_alloc_observe(void);
void lu_gc_record_deallocation(struct lu_entry *entry);
/* Why a collection runs (runtime/interp/collector.c). */
enum { LU_GC_REASON_HEAP, LU_GC_REASON_MANUAL, LU_GC_REASON_SHUTDOWN, LU_GC_REASON_STRESS };
/* gc.set_debug flags. */
enum {
    LU_GC_DEBUG_STATS = 1,
    LU_GC_DEBUG_COLLECTABLE = 2,
    LU_GC_DEBUG_UNCOLLECTABLE = 4,
    LU_GC_DEBUG_SAVEALL = 32,
};
/* The cycle collector (runtime/interp/collector.c): one collection of
   `generation`, with the counters it keeps, finalizers, weakref callbacks and
   tp_clear run; reports what it collected and could not. */
void lu_gc_collect_garbage(PyThreadState *ts, int generation, int reason, Py_ssize_t *collected,
                           Py_ssize_t *uncollectable);
/* gc.get_objects and gc.get_referrers: new lists. */
PyObject *lu_gc_get_objects(struct _is *interp);
PyObject *lu_gc_get_referrers(struct _is *interp, PyObject *objs);
/* gc.freeze (mode 1), gc.unfreeze (0) and gc.get_freeze_count (-1): the count. */
Py_ssize_t lu_gc_freeze(struct _is *interp, int mode);
/* Resident plus swapped memory in KB, or -1. */
Py_ssize_t lu_gc_mem_usage(void);
/* The last collection, before the interpreter ends: finalizes what only cycles hold. */
void lu_gc_collect_shutdown(PyThreadState *ts);
/* The poll handler's GC reason: collects the youngest generation if the
   collector is enabled and still wants to. */
void lu_gc_run(PyThreadState *ts);
/* The stress tick's collection (-X lucifer_stress=gc:N), every N GC allocations:
   as a manual one, but it leaves the counts, statistics and callbacks as they were. */
void lu_gc_stress(PyThreadState *ts);

/* Signals (runtime/interp/signals.c): the trip handler sets a flag and the
   main task's poll reason; the main task runs the Python handlers. */
int lu_signals_handle(PyThreadState *ts);
int lu_pending_calls_handle(PyThreadState *ts);
int lu_thread_can_handle_signals(void);

/* QSBR (runtime/thread/qsbr.c). A carrier reports a quiescent state at every
   poll, goes offline when it detaches and online when it attaches. */
void lu_qsbr_register(struct lu_carrier *c);
void lu_qsbr_unregister(struct lu_carrier *c);
void lu_qsbr_attach(struct lu_carrier *c);
void lu_qsbr_detach(struct lu_carrier *c);
void lu_qsbr_process(struct lu_carrier *c);
/* The goals that readers must reach before something is freed: for mimalloc
   pages (runtime/object/alloc.c). Advance moves the write sequence on and
   answers the goal that follows; next answers it without moving. */
int lu_qsbr_goal_reached(uint64_t goal);
uint64_t lu_qsbr_advance(void);
uint64_t lu_qsbr_next_goal(void);
/* Drains the carrier's queued biased-refcount merges for `interp`. */
void lu_brc_merge(struct lu_entry *entry);

static inline struct lu_task *
lu_current_task(void)
{
    return lu_carrier != NULL ? lu_carrier->task : NULL;
}

/* Called when a carrier binds and retires. */
void lu_mimalloc_bind(struct lu_entry *entry);
void lu_mimalloc_abandon(struct lu_entry *entry);
void lu_freelists_clear(struct lu_entry *entry);       /* runtime/object/layouts.c */
void lu_brc_register(struct lu_entry *entry);
void lu_brc_retire(struct lu_entry *entry);
/* A scheduler carrier stops or starts running a task (runtime/thread/task.c). */
void lu_brc_set_idle(struct lu_entry *entry, int idle);

void lu_brc_queue_object(PyObject *ob);
void lu_stack_push(struct lu_stack *s, PyObject *ob);
void lu_brc_take_queued(struct _is *interp, void (*fn)(PyObject *, void *), void *arg);
void lu_set_immortal(PyObject *op);
/* _PyObject_SetDeferredRefcount: for a new object its creator owns. */
void lu_set_deferred(PyObject *op);
/* Gives a dying object one reference for a call that may look at it; End
   returns 0 when it is dead again, -1 when the call kept it. */
void lu_resurrect_start(PyObject *self);
int lu_resurrect_end(PyObject *self);
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
    PyObject *null_item;                /* "<NULL>", what an uninitialized list's repr shows */
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
    X(__class_getitem__) X(__del__) X(__doc__) X(__get__) X(__getitem__) X(__init__) X(__iter__) \
    X(__len__) X(__loader__) X(__main__) X(__name__) X(__new__) X(__next__) \
    X(__package__) X(__repr__) X(__set__) X(__spec__) X(__str__) X(__hash__) \
    X(__eq__) X(__ne__) X(__lt__) X(__le__) X(__gt__) X(__ge__) X(__contains__) \
    X(__setitem__) X(__delitem__) X(__bool__) X(_lucifer) X(_index) X(builtins) \
    X(__add__) X(__radd__) X(__sub__) X(__rsub__) X(__mul__) X(__rmul__) X(__mod__) \
    X(__rmod__) X(__divmod__) X(__rdivmod__) X(__pow__) X(__rpow__) X(__lshift__) \
    X(__rlshift__) X(__rshift__) X(__rrshift__) X(__and__) X(__rand__) X(__xor__) \
    X(__rxor__) X(__or__) X(__ror__) X(__floordiv__) X(__rfloordiv__) X(__truediv__) \
    X(__rtruediv__) X(__matmul__) X(__rmatmul__) X(__iadd__) X(__isub__) X(__imul__) \
    X(__imod__) X(__ipow__) X(__ilshift__) X(__irshift__) X(__iand__) X(__ixor__) \
    X(__ior__) X(__import__) X(__ifloordiv__) X(__itruediv__) X(__imatmul__) X(__neg__) X(__pos__) \
    X(__abs__) X(__invert__) X(__int__) X(__float__) X(__index__) X(mro) X(__getattribute__) X(__static_attributes__) X(__getattr__) X(__setattr__) X(__delattr__) \
    X(_data) X(__enter__) X(__exit__) X(__aenter__) X(__aexit__)
struct lu_identifiers {
#define LU_ID_FIELD(name) PyObject *name;
    LU_IDENTIFIERS(LU_ID_FIELD)
#undef LU_ID_FIELD
};
#define LU_ID(name) (_PyRuntime.ids.name)

#define LU_NSIG 65                      /* the _signal module's NSIG */

/* The pyc magic token (PYC_MAGIC_NUMBER_TOKEN, Include/pycore_magic_number.h);
   _imp.pyc_magic_number_token and tools/freeze agree with it. */
#define LU_PYC_MAGIC (3627L | ((long)'\r' << 16) | ((long)'\n' << 24))

/* The runtime record (FOUNDATIONS 2.1). Its size is the oracle's. */
#define LU_RUNTIME_SIZE 365624

extern LU_EXPORT union lu_runtime {
    struct {
        struct lu_debug_offsets debug_offsets;  /* must stay first */
        pthread_once_t started;
        struct {
            PyMemAllocatorEx raw, mem, obj;
            struct lu_lock lock;        /* PyMem_SetAllocator/GetAllocator */
            int observed;               /* a hook sees allocations (lu_alloc_observe):
                                           compiled code allocates through C */
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
        struct lu_jit_options jit;
        unsigned int next_static_version_tag;   /* last tag of the range static types share */
        unsigned next_static_type;      /* the interpreter state slots after the startup list */
        PyMutex type_lock;              /* every type's subclass list */
        PyMutex type_ready_lock;        /* PyType_Ready of a type defined outside the runtime */
        int finalizing;                 /* Py_FinalizeEx has begun */
        Py_ssize_t next_module_index;   /* the last PyModuleDef_Base.m_index given */
        PyMutex getargs_lock;           /* builds an argument parser's keyword tuple once */
        PyMutex specialize_lock;        /* one rewrite of an instruction's inline cache at a time */
        PyMutex shared_names_lock;      /* a class's shared names grow one at a time (object/attrs.c) */
        PyMutex pic_lock;               /* a polymorphic inline cache grows one entry at a time (jit/jit.c) */
        pthread_t main_thread;          /* the thread that started the runtime */
        struct lu_task *main_task;      /* signals and the main thread's pending calls are aimed at it */
        struct {
            struct lu_lock lock;        /* every carrier's run queue */
            struct lu_carrier *carriers[2];     /* the main thread's, and the second */
            pthread_t second;           /* the second carrier's thread */
            uint64_t ready_id;          /* the wait the second carrier ends once it is a carrier */
            int active;                 /* tasks migrate: the second carrier is running */
            int stop;                   /* the second carrier is asked to exit */
            int orphaned;               /* a fork left the main task without its home carrier's thread */
        } sched;
        struct {
            struct lu_lock lock;        /* the registry and the orphaned queue */
            uint64_t wr_seq;            /* always odd, advanced by 2 */
            uint64_t rd_seq;            /* the least sequence any attached carrier has seen */
            struct lu_carrier *head;    /* every carrier */
            struct lu_qsbr_queue orphans;   /* what exited carriers left */
        } qsbr;
        struct {
            int tripped;                /* some handler's flag is set */
            int unhandled_keyboard_interrupt;   /* a KeyboardInterrupt ended the program: exit by SIGINT */
            struct {
                int tripped;
                int redirected;         /* signals sent on to the main task's thread, not yet arrived */
                PyObject *func;         /* the Python handler, strong; NULL for none */
            } handlers[LU_NSIG];
        } signals;
        PyMutex weakref_locks[64];      /* guard the weakref lists, by referent address */
        struct {
            PyMutex mutex;
            Py_ssize_t next_index, size;
            PyObject **array;           /* each _Py_Identifier's interned str, by its index */
        } identifiers;                  /* _PyUnicode_FromId (runtime/capi/unicodeapi.c) */
        struct lu_lock heaps_lock;      /* a carrier's heaps are in the registry or abandoned,
                                           never between: held by its retirement and by the
                                           collector's walk, and by entry creation */
        struct _is main_interp;
    };
    unsigned char oracle_size[LU_RUNTIME_SIZE];
} _PyRuntime;

static inline void
lu_qsbr_quiescent(struct lu_carrier *c)
{
    uint64_t seq = __atomic_load_n(&_PyRuntime.qsbr.wr_seq, __ATOMIC_ACQUIRE);
    if (seq != c->qsbr.seq) {
        __atomic_store_n(&c->qsbr.seq, seq, __ATOMIC_RELEASE);
    }
}

/* Lock-free reads of a container's items (Include/internal/pycore_object.h).
   A reader that holds no lock may find the item freed or replaced between its
   load and its incref, so it takes a reference only if the object is still
   alive (try-incref) and the container's slot still holds it. A failed try
   sends the reader to the locked path, whose Py_NewRef marks the object
   shared so that later tries succeed. */
static inline int
lu_try_incref_fast(PyObject *op)
{
    uint32_t local = _Py_atomic_load_uint32_relaxed(&op->ob_ref_local) + 1;
    if (local == 0) {
        return 1;                       /* immortal */
    }
    if (_Py_IsOwnedByCurrentThread(op)) {
        _Py_atomic_store_uint32_relaxed(&op->ob_ref_local, local);
        return 1;
    }
    return 0;
}

static inline int
lu_try_incref_shared(PyObject *op)
{
    Py_ssize_t shared = _Py_atomic_load_ssize_relaxed(&op->ob_ref_shared);
    for (;;) {
        /* Zero shared count: the owner may free it at any time. */
        if (shared == 0 || shared == _Py_REF_MERGED) {
            return 0;
        }
        if (_Py_atomic_compare_exchange_ssize(&op->ob_ref_shared, &shared,
                                              shared + (1 << _Py_REF_SHARED_SHIFT))) {
            return 1;
        }
    }
}

/* A reference to op, if it is alive (_Py_TryIncref). */
static inline int
lu_try_incref(PyObject *op)
{
    return lu_try_incref_fast(op) || lu_try_incref_shared(op);
}

/* A reference to op, if it is alive and *src still points to it. */
static inline int
lu_try_incref_compare(PyObject **src, PyObject *op)
{
    if (lu_try_incref_fast(op)) {
        return 1;
    }
    if (!lu_try_incref_shared(op)) {
        return 0;
    }
    if (op != _Py_atomic_load_ptr(src)) {
        Py_DECREF(op);
        return 0;
    }
    return 1;
}

/* A new reference to *ptr, NULL if it holds NULL or a try fails. */
static inline PyObject *
lu_try_xget_ref(PyObject **ptr)
{
    PyObject *value = _Py_atomic_load_ptr(ptr);
    return value != NULL && lu_try_incref_compare(ptr, value) ? value : NULL;
}

/* _PyObject_SetMaybeWeakref: op is about to be reachable through a weak
   reference, so its shared count is nonzero and a try-incref from another
   thread can succeed while the owner holds it. */
static inline void
lu_set_maybe_weakref(PyObject *op)
{
    if (_Py_IsImmortal(op)) {
        return;
    }
    Py_ssize_t shared = _Py_atomic_load_ssize_relaxed(&op->ob_ref_shared);
    while ((shared & _Py_REF_SHARED_FLAG_MASK) == 0
           && !_Py_atomic_compare_exchange_ssize(&op->ob_ref_shared, &shared, shared | _Py_REF_MAYBE_WEAKREF)) {
    }
}

/* Py_NewRef under the container's lock: marks an object another thread owns
   as possibly shared, so that lock-free tries on it can succeed. */
static inline PyObject *
lu_new_ref_with_lock(PyObject *op)
{
    if (lu_try_incref_fast(op)) {
        return op;
    }
    for (;;) {
        Py_ssize_t shared = _Py_atomic_load_ssize_relaxed(&op->ob_ref_shared);
        Py_ssize_t new_shared = shared + (1 << _Py_REF_SHARED_SHIFT);
        if ((shared & _Py_REF_SHARED_FLAG_MASK) == 0) {
            new_shared |= _Py_REF_MAYBE_WEAKREF;
        }
        if (_Py_atomic_compare_exchange_ssize(&op->ob_ref_shared, &shared, new_shared)) {
            return op;
        }
    }
}

/* Layouts (runtime/object/). Constructors return a new reference, or NULL
   with an exception set. lu_hash and lu_eq cover the layouts marshal yields,
   before the kernel installs Python-level __hash__ and __eq__. */
void lu_layouts_init(void);
PyObject *lu_object_new(PyTypeObject *tp, size_t size);    /* zeroed; GC types tracked */
void lu_object_free(PyObject *op);
/* tp_traverse and tp_clear of a layout whose references are the object
   pointers in [first, end) bytes of the object. LU_GC_FIELDS names the first
   and last member and defines NAME_traverse and NAME_clear. */
int lu_fields_traverse(PyObject *op, Py_ssize_t first, Py_ssize_t end, visitproc visit, void *arg);
int lu_fields_clear(PyObject *op, Py_ssize_t first, Py_ssize_t end);
#define LU_GC_FIELDS(name, type, first_member, last_member) \
    static int name##_traverse(PyObject *op, visitproc visit, void *arg) \
    { return lu_fields_traverse(op, offsetof(type, first_member), \
                                offsetof(type, last_member) + sizeof(PyObject *), visit, arg); } \
    static int name##_clear(PyObject *op) \
    { return lu_fields_clear(op, offsetof(type, first_member), \
                             offsetof(type, last_member) + sizeof(PyObject *)); }
/* Drops one reference. When it was the last, returns 1 with the count at 0
   and the object not yet freed, so that a dealloc can free a chain in a loop. */
static inline int
lu_release_last(PyObject *op)
{
    if (_Py_IsOwnedByCurrentThread(op) && op->ob_ref_local == 1
        && _Py_atomic_load_ssize_relaxed(&op->ob_ref_shared) == 0) {
        op->ob_ref_local = 0;
        op->ob_gc_bits &= ~LU_GC_TRACKED;       /* dying: see _Py_Dealloc */
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
PyObject *lu_weakref_new(PyTypeObject *type, PyObject *ob, PyObject *callback);
PyObject *lu_weakref_get(PyObject *ref);
/* The head of ob's list of weak references, or NULL if its type has none. */
PyWeakReference **lu_weakref_list(PyObject *ob);
void lu_descr_init(void);
void lu_capsule_init(void);
void lu_picklebuf_init(void);
int lu_fileobject_init(void);
/* The last-resort MemoryError of PyErr_NoMemory (runtime/capi/errors.c). */
void lu_errors_init(PyInterpreterState *interp);
void lu_storage_init(void);
void lu_module_init(void);
void lu_exc_init(void);
void lu_gen_init(void);
void lu_frame_init(void);
PyObject *lu_small_int(int value);
PyLongObject *lu_long_alloc(Py_ssize_t ndigits);
PyObject *lu_long_from_digits(int negative, Py_ssize_t ndigits, const digit *digits);
PyObject *lu_long_from_bytes(const unsigned char *s, Py_ssize_t n, int little, int is_signed);
int lu_long_as_long(PyObject *v, long *result);
PyObject *lu_long_decimal(PyObject *v);
int lu_hash_native(PyObject *key, Py_hash_t *hash);   /* a key's hash when no Python computes it (dict.c) */      /* str() of an exact int, as int.__repr__ */
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
/* The xxHash lanes of tuple_hash and slice_hash (pycore_tuple.h): start from
   LU_XXHASH_START, add each item's hash with lu_xxhash_step, and finish with
   lu_xxhash_end over the item count (mangle_length for a tuple). */
#define LU_XXPRIME_1 11400714785074694791ULL
#define LU_XXPRIME_2 14029467366897019727ULL
#define LU_XXPRIME_5 2870177450012600261ULL
#define LU_XXHASH_START ((Py_uhash_t)LU_XXPRIME_5)
static inline Py_uhash_t
lu_xxhash_step(Py_uhash_t acc, Py_hash_t lane)
{
    acc += (Py_uhash_t)lane * LU_XXPRIME_2;
    acc = (acc << 31) | (acc >> 33);
    return acc * LU_XXPRIME_1;
}
static inline Py_hash_t
lu_xxhash_end(Py_uhash_t acc, Py_ssize_t n, int mangle_length)
{
    if (mangle_length) {
        acc += (Py_uhash_t)n ^ (LU_XXPRIME_5 ^ 3527539UL);
    }
    return acc == (Py_uhash_t)-1 ? 1546275796 : (Py_hash_t)acc;
}
Py_hash_t lu_str_hash(PyObject *s);
uint64_t lu_keyed_hash(uint64_t key, const void *src, Py_ssize_t size);
int lu_str_eq(PyObject *a, PyObject *b);
/* Reports the first keyword argument no name of kwtuple matches, with a
   suggestion when one is close (runtime/capi/getargs.c). */
void lu_error_unexpected_keyword_arg(PyObject *kwargs, PyObject *kwnames,
                                     PyObject *kwtuple, const char *fname);
void lu_intern(PyInterpreterState *interp, PyObject **p);
PyObject *lu_str_from_utf8(const char *s, Py_ssize_t n, int surrogatepass);
Py_hash_t lu_hash(PyObject *v);
Py_hash_t lu_tuple_hash(PyObject *tuple);
int lu_eq(PyObject *a, PyObject *b);
/* Whether two keys of an equal hash are equal when that needs no Python: 1, 0,
   or -1 when __eq__ must decide (runtime/object/dict.c). */
int lu_native_eq(PyObject *a, PyObject *b);
/* Whether the dict's keys are all exact strs, which its unicode table guarantees. */
int lu_dict_str_keys(PyObject *dict);
PyObject *lu_set_new(PyTypeObject *type);
int lu_set_add(PyObject *set, PyObject *key);
/* set.add(key) where no Python decides it: a key of a type whose hash and
   equality are the runtime's (exact int, str, float, bytes, bool, None),
   compared with keys no __eq__ judges. 1 when done, 0 when the kernel must
   add it (nothing changed), -1 with an error. */
int lu_set_add_simple(PyObject *set, PyObject *key);
Py_hash_t lu_frozenset_hash(PyObject *set);
int lu_hash_secret_init(int use_seed, unsigned long seed);

/* runtime/interp/config.c */
PyStatus lu_runtime_options(const PyConfig *config);

/* CPython's compiler (runtime/compiler/lu_cc.c). lu_cc_init runs at interpreter
   start; lu_cc_compile is compile() after its arguments are checked: source is
   a str, a bytes or an AST object. */
int lu_cc_init(void);

/* The builtin modules written in C, by their PyInit_ function (runtime/interp/import.c). */
struct lu_native {
    const char *name;
    PyObject *(*init)(void);
};
extern const struct lu_native lu_native_modules[];
PyObject *lu_cc_compile(PyObject *source, PyObject *filename, const char *mode, int flags,
                        int optimize, int feature_version);
PyStatus lu_config_read_copy(PyConfig *dst, const PyConfig *src);
int lu_utf8_mode(const PyConfig *config);

/* runtime/interp/sysmodule.c: sys's native functions, and sys.configure(config). */
extern PyMethodDef lu_sys_native_methods[];
int lu_sys_configure(struct _is *interp);
int lu_sys_init_streams(struct _is *interp);

/* runtime/interp/import.c: _imp, then _frozen_importlib and its install. */
int lu_import_init(struct _is *interp);

/* runtime/interp/marshal.c: marshal.dumps. */
PyObject *lu_marshal_dumps(PyObject *x, int version, int allow_code);

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
/* PyModule_AddFunctions by namespace stores (a module the runtime made itself). */
int lu_module_add_functions(PyObject *m, PyMethodDef *defs);
extern PyModuleDef lu_lucifer_module;

/* A function for `code` closing over `globals` (runtime/object/function.c). */
/* The threads a dump of all threads lists, and the fatal-error report (runtime/prim/faulthandler.c). */
void lu_fh_thread_enter(PyThreadState *ts);
void lu_fh_thread_exit(PyThreadState *ts);
void lu_fh_fatal(const char *func, const char *msg) __attribute__((noreturn));
PyObject *lu_function_new(PyObject *code, PyObject *globals);
/* A function of code in globals that no watcher hears of: what compiled
   code names the frames of an inlined closure by (runtime/jit/ssa_build.c). */
PyObject *lu_function_template(PyObject *code, PyObject *globals);
/* function.__new__ over (code, globals, name, defaults, closure, kwdefaults). */
PyObject *lu_function_from_args(PyObject *const *args);
/* A cell's value as a new reference, or NULL when empty (_PyCell_GetStackRef):
   taken as a dict's value is, since another thread may replace and free it. */
PyObject *lu_cell_get(PyObject *cell);
/* Stores v (stolen; NULL empties the cell) under the cell's lock and returns
   the old value, which the caller releases (PyCell_SetTakeRef). */
PyObject *lu_cell_exchange(PyObject *cell, PyObject *v);

/* The namespace dict (runtime/object/dict.c): lookups and stores with exact
   str keys, which LOAD_NAME/LOAD_GLOBAL, module attributes and the kernel's
   dict code all use (DESIGN 4.4). Lookup returns a borrowed value or NULL,
   and never raises. */
PyObject *lu_ns_lookup(PyObject *dict, PyObject *key);
/* The same as a new reference, which is what a reader needs when other
   threads may replace or delete the value (the borrowed form is for
   startup and for dicts that nothing else writes). */
PyObject *lu_ns_get(PyObject *dict, PyObject *key);
int lu_ns_insert(PyObject *dict, PyObject *key, PyObject *value);
/* lu_ns_insert in the dict's critical section, for a dict another thread
   may see: globals, a module's, an instance's. A reader whose lock-free try
   fails reads under that lock (lu_dict_find_native), so an unlocked store
   could free the value it is taking. lu_ns_insert alone is for a dict no
   other thread can reach yet. */
int lu_ns_store(PyObject *dict, PyObject *key, PyObject *value);
/* Removes `key`: 1 when it was there, 0 when not. */
int lu_ns_delete(PyObject *dict, PyObject *key);
/* lu_ns_store, or lu_ns_delete when value is NULL, handing the value it
   replaced or removed to the caller (*old, a reference, or NULL) instead of
   releasing it: 1 when stored or removed, 0 when there was nothing to
   remove, -1 on error. */
int lu_ns_exchange(PyObject *dict, PyObject *key, PyObject *value, PyObject **old);
/* PyDict_Next over the namespace: *pos starts at 0; key and value are
   borrowed. A dict other threads may write is iterated in its critical
   section, as cp314t's dict_merge and PyDict_Copy do. */
int lu_ns_next(PyObject *dict, Py_ssize_t *pos, PyObject **key, PyObject **value);

/* Types (runtime/prim/type.c). Static builtin types are made ready natively
   at startup: bases, MRO and an empty dict in the interpreter's state, which
   the kernel's @kernel_type then fills. */
#define LU_VERSION_TAG_STATIC_MAX ((1u << 17) - 1)     /* _Py_MAX_GLOBAL_TYPE_VERSION_TAG */
extern PyTypeObject lu_method_wrapper_type;
/* The exporter a class's __buffer__ memoryview is wrapped in, so that
   releasing the buffer runs the class's __release_buffer__ (slots.c). */
typedef struct { PyObject_HEAD PyObject *mv, *obj; } lu_buffer_wrapper;
extern PyTypeObject lu_buffer_wrapper_type;
extern PyBufferProcs lu_buffer_wrapper_procs;
int lu_types_init(PyInterpreterState *interp);
/* Fills each static type's unset slots from its bases (inherit_slots); run
   after every kernel module, as kernel classes install slots. */
int lu_static_types_inherit(void);
/* The type's dict: in interpreter state for static builtin types. Borrowed. */
PyObject *lu_type_dict(PyTypeObject *tp);
/* The fully qualified name, module<sep>qualname (runtime/capi/typespec.c). */
PyObject *lu_type_fullyqualname(PyTypeObject *type, char sep);
/* The doc after its introspection signature, and tp_methods / the bound
   __new__ of a type with a tp_new into its dict (typespec.c). */
const char *lu_doc_without_signature(const char *name, const char *doc);
int lu_type_add_methods(PyTypeObject *tp, PyObject *dict);
extern PyMethodDef lu_float_methods[], lu_long_methods[];   /* prim/float.c, prim/int.c */
int lu_type_add_new_wrapper(PyTypeObject *tp, PyObject *dict);
void lu_free_object(void *p);
/* PyType_Modified: tells tp's watchers, then tp and its subclasses lose
   their version tags; only a type that has one is touched. */
void lu_type_modified(PyTypeObject *tp);
/* assign_version_tag: tp (bases first) gets a tag if it has none. 1 when it
   has one; 0 when the type is not ready, has used its 1000 tags, or the
   counter is spent. The caller needs the tag to cache anything about tp. */
int lu_type_assign_version(PyTypeObject *tp);
/* "__name__" of a type, new: ht_name, or tp_name after its last dot. */
PyObject *lu_type_name(PyTypeObject *tp);
/* Stores v (or NULL) into the object member at addr under the object's lock,
   as PyMember_SetOne and STORE_ATTR_SLOT do; returns the old value. */
PyObject *lu_member_exchange(PyObject *obj, PyObject **addr, PyObject *v);

/* Adds descriptors for tp_members and tp_getset to the type's dict. */
int lu_type_add_descriptors(PyTypeObject *tp);
/* Adds wrapper descriptors (__add__, __len__, ...) for the C slots tp has
   of its own (runtime/types/wrappers.c). */
int lu_type_add_operators(PyTypeObject *tp);
/* object.__new__(tp): a new instance of tp, refusing a tp whose static base is not owner. */
PyObject *lu_alloc_instance(PyTypeObject *tp, PyTypeObject *owner);

/* The next tp_new_wrapper check (owner.__new__(cls) is safe) passes: C called owner's tp_new itself. */
void lu_new_check_skip(int on);
int lu_tp_new_is_thunk(newfunc f);
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
/* A member or native getset descriptor read or written by C alone, for an
   instance of a type that has it: 1 done (*out a new reference when reading;
   value NULL deletes), 0 when kernel code must decide (another kind of
   descriptor, an unset member, a getset with no setter), -1 with an exception
   set. */
int lu_descr_get_native(PyObject *descr, PyObject *obj, PyObject **out);
/* An exact staticmethod's callable, as its __get__ returns it (a new reference), or NULL. */
PyObject *lu_staticmethod_get(PyObject *descr);
/* A method descriptor or slot wrapper bound to obj, as its __get__ binds it
   (a builtin method, a method-wrapper), or NULL without an error for any
   other descriptor or an obj not of its class. */
PyObject *lu_method_descr_bind(PyObject *descr, PyObject *obj);
/* A function or a method descriptor bound to obj, as its __get__ binds it. */
PyObject *lu_bind_method(PyObject *descr, PyObject *obj);
/* A property's getter, or a kernel getset's (borrowed, read once; NULL or None
   for none, and NULL for another getset). */
PyObject *lu_property_fget(PyObject *prop);
/* Where a classmethod's and a staticmethod's callable is (compiled code reads it). */
extern const Py_ssize_t lu_classmethod_callable_offset;
/* The Python or kernel function a class's __new__ entry runs, called with the class first: 1 with *func, else 0. */
int lu_new_function(PyObject *new_, PyObject **func);
int lu_descr_set_native(PyObject *descr, PyObject *obj, PyObject *value);
/* obj.name = value (value NULL deletes) when C alone can do it: object's own
   __setattr__, then a native data descriptor or, for a name the type lacks,
   the instance's dict. Same results as lu_descr_set_native. */
int lu_setattr_native(PyObject *obj, PyObject *name, PyObject *value);
/* obj.name for an exact str name, answered when no Python code decides: a
   data member or getset of the type, then the instance's dict, then a plain
   value or function of the type (bound as a method). NULL, with no error, for
   everything else, which is the kernel's: a missing attribute, __getattr__,
   __getattribute__, properties and other descriptors. A new reference
   (runtime/interp/ceval.c, which LOAD_ATTR and the optimizing tier share). */
PyObject *lu_getattr_native(PyObject *owner, PyObject *name);
PyObject *lu_method_wrapper_new(PyObject *descr, PyObject *self);
/* The Python function the interpreter runs for a kernel callable, and the
   self it prepends (borrowed, or NULL). NULL when callable is none. Checks
   an unbound descriptor's first argument: -1 with TypeError set. */
int lu_kernel_target(PyObject *callable, PyObject *const *args, Py_ssize_t nargs,
                     PyObject **func, PyObject **self);
/* The Python function of a kernel callable, as lu_kernel_target finds it
   but without checking an argument (the optimizing tier knows it), and the
   self a builtin binds (borrowed, or NULL). NULL when callable is none. */
PyObject *lu_kernel_function_of(PyObject *callable, PyObject **self);
/* The twin of kernel function f for calls of nargs positional arguments
   and no keywords (the kernel_twin primitive), borrowed, or NULL. */
PyFunctionObject *lu_kernel_twin(PyFunctionObject *f, int nargs);
/* The kernel's kind(x, name) for every x of exact type t (prim/type.c): 1 or
   0, or -1 for a name it does not know or one ("kernel") that is not the
   type's. */
int lu_kind_of_type(PyTypeObject *t, const char *kind);
/* The kernel's field_get and field_set (object/descr.c): field i's offset
   in an instance of tp, or -1; and the field read (a new reference, None
   for none) and written at that offset, under the object's lock. */
Py_ssize_t lu_field_offset(PyTypeObject *tp, long i);
PyObject *lu_field_get(PyObject *obj, Py_ssize_t off);
void lu_field_set(PyObject *obj, Py_ssize_t off, PyObject *value);
/* The step next_item takes for an iterator of exact type t (next_missing_set),
   borrowed, or None. */
PyObject *lu_next_missing_of(PyObject *t);
/* An unbound slot wrapper's call (wrapper_descriptor.__call__) on the
   arguments as a vector, self first; kwargs a dict or NULL. */
PyObject *lu_wrapper_descr_call(PyObject *descr, PyObject *const *args, Py_ssize_t nargs, PyObject *kwargs);
/* Calls a callable from C (PyObject_Vectorcall): vectorcall, else tp_call
   (a slot wrapper's without a tuple of the arguments). */
PyObject *lu_call(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames);
/* Calls interp->kernel[id] from C, in a nested eval loop. */
PyObject *lu_call_kernel(enum lu_kernel_id id, PyObject *const *args, Py_ssize_t nargs);
/* What the interpreter answers in C without running the kernel, which the
   optimizing tier's helpers answer the same way (runtime/interp/ceval.c):
   container[sub] for exact lists, tuples, bytes, dicts and slices, a new
   reference or NULL (with an error for a bad slice or a key an exact dict
   lacks) when the kernel must; COMPARE_OP's oparg for ints and equality no Python code decides,
   1, 0 or -1 for the kernel; and the kernel function `target` called with
   plain arguments (dict.get, len, isinstance, ...), its result, or NULL
   (without an error) when the kernel must run. */
PyObject *lu_subscr_native(PyObject *container, PyObject *sub);
/* container[start:stop:step] as lu_subscr_native answers it for a slice. */
PyObject *lu_slice_native(PyObject *container, PyObject *start, PyObject *stop, PyObject *step);
/* list[lo:hi] = seq for a tuple or list seq (prim/storage.c's list_splice),
   the bounds clamped under the list's lock: 0, -1 with an error, or 1 when
   the list's length is no longer expect (>= 0) and nothing was done. */
int lu_list_splice(PyListObject *l, Py_ssize_t lo, Py_ssize_t hi, PyObject *seq, Py_ssize_t expect);
/* container[start:stop] = v without Python: int or None bounds, and an
   exact list with a tuple or list v, or an exact bytearray with a bytes,
   bytearray or memoryview v. 0, -1 with an error, or 1 when the kernel
   must do it. */
int lu_store_slice_native(PyObject *container, PyObject *start, PyObject *stop, PyObject *v);
/* container[sub] = v for a slice sub without a step, as lu_store_slice_native. */
int lu_setitem_slice_native(PyObject *container, PyObject *sub, PyObject *v);
/* Type tp's number slot at offset in PyNumberMethods (types/slots.c): 1 for
   an extension's C function, 0 for none, -1 for the runtime's (a kernel
   type's or a class's, which runs the kernel). */
int lu_nb_slot_kind(PyTypeObject *tp, size_t offset);
/* BINARY_OP's oparg (not NB_SUBSCR) on a and b by their extension types'
   number slots, as CPython's binary_op1 and binary_iop1: the result, or
   NULL (with an error when a slot raised) when it is the kernel's to
   decide, because a slot is the runtime's or every slot declined. */
PyObject *lu_binary_foreign(PyObject *a, PyObject *b, int op);
/* LOAD_SPECIAL's method `which` (0 __enter__, 1 __exit__, 2 __aenter__,
   3 __aexit__) on owner's type when it is a function or a method
   descriptor, which a call with owner first runs as the kernel's
   load_special binds it: a new reference, or NULL (no error) for the
   kernel to look it up. */
PyObject *lu_special_function(PyObject *owner, int which);
/* float.__pow__ of a float and a float or int (either side), without a
   modulus: the result, or NULL with the error (runs no Python). */
PyObject *lu_float_pow(PyObject *a, PyObject *b);
/* a + b for two exact lists or two exact tuples. */
PyObject *lu_seq_concat(PyObject *a, PyObject *b);
/* sub in s for two strs (no Python, no error). */
int lu_str_contains(PyObject *s, PyObject *sub);
/* a + b for two exact bytes. */
PyObject *lu_bytes_concat(PyObject *a, PyObject *b);
/* Whether lu_binary_foreign takes the operator for operands of these types. */
int lu_binary_is_foreign(PyTypeObject *ta, PyTypeObject *tb, int op);
/* container[start:stop] for an exact list, tuple, str or bytes by machine-int
   bounds, either None as `none` says (bit 0 the start, bit 1 the stop): a new
   reference, or NULL without an error for another type. */
PyObject *lu_slice_native_raw(PyObject *container, int64_t start, int64_t stop, int none);
int lu_compare_native(PyObject *a, PyObject *b, int oparg);
PyObject *lu_native_call(PyThreadState *ts, PyObject *target, PyObject *const *args, Py_ssize_t npos);
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
   it from `from`, 3 installs PyObject_HashNotImplemented (tp_hash only), 4 installs
   the repeat of a builtin sequence over its __mul__ (sq_repeat only). */
int lu_set_slot(PyTypeObject *tp, PyObject *name, int how, PyTypeObject *from);
/* type.__text_signature__: the signature tp_doc starts with, or None. */
PyObject *lu_type_text_signature(PyTypeObject *tp);
/* Whether f is one of the slot thunks, which look the dunder up by name. */
int lu_is_slot_thunk(void *f);
/* Calls special method `name` of self's type, as vectorcall_method does. */
PyObject *lu_call_special(PyObject *self, PyObject *name, PyObject *const *args, Py_ssize_t nargs);

/* The one path that tells watchers about a dict change (FOUNDATIONS 2.4):
   the namespace primitives and the kernel's dict primitives call it before
   they change `mp`. key and value are borrowed. */
void lu_dict_notify(PyDictObject *mp, PyDict_WatchEvent event, PyObject *key, PyObject *value);
/* The version of mp's keys table as it stands (1..65535 for a caller that
   keeps it in a cache word), assigned on first ask and cleared by any change
   to the table's keys; 0 when none can be given. */
uint32_t lu_dict_keys_version(PyDictObject *mp);

/* Function versions (FOUNDATIONS 2.4): MAKE_FUNCTION sets func_version from
   the code's co_version; changing __code__, __defaults__ or __kwdefaults__
   clears it. lu_func_notify tells function watchers. */
void lu_function_set_version(PyFunctionObject *func);
/* SET_FUNCTION_ATTRIBUTE: stores attr (a reference it takes) in the field
   `which` (a MAKE_FUNCTION_* flag) names. */
void lu_function_set_attribute(PyFunctionObject *func, int which, PyObject *attr);
void lu_func_notify(PyFunction_WatchEvent event, PyFunctionObject *func, PyObject *new_value);
/* Tells code watchers that co was created or is being destroyed. */
void lu_code_notify(PyCodeEvent event, PyCodeObject *co);

/* The watcher tables (runtime/object/watchers.c), set up with the interpreter's
   version counters. Add puts callback in the
   first free id from `first` and returns it, or raises RuntimeError. Clear
   frees an id; Check raises ValueError, formatted with the id from `invalid`
   or `unset`, unless the id has a callback. -1 on error. */
void lu_watchers_init(struct _is *interp);

/* Facts compiled code relies on between polls (runtime/jit/deps.c): a
   type's version (obj: the version), a dict's keys (obj: the dict, which
   dict watcher id 0 marks), the value of one key (obj: the dict, sub: its
   value word's offset in the keys table), a function's code and defaults
   (obj: the function), and the callees a call site has met (obj: its
   table, runtime/jit/jit.c), which only grows. A
   writer calls lu_jit_invalidate after its change: the units that rely on
   it are dead, and every task leaves compiled code at its next poll, the
   running one's code at its next check. */
enum lu_dep_kind { LU_DEP_TYPE, LU_DEP_KEYS, LU_DEP_VALUE, LU_DEP_FUNC, LU_DEP_CALLEES };
void lu_jit_invalidate(int kind, uintptr_t obj, uint32_t sub);
int lu_watcher_add(lu_watcher *table, int first, lu_watcher callback, const char *kind);
int lu_watcher_check(lu_watcher *table, int id, const char *invalid, const char *unset);

/* Frees replaced storage (a list's array, a dict's keys table) once no
   lock-free reader can still hold it (runtime/prim/storage.c). */
void lu_qsbr_retire(void *p);

/* Appends to a list (borrowed item), growing its storage: a list whose lock
   the caller holds, or that no other thread sees (runtime/prim/storage.c). */
int lu_list_append(PyObject *list, PyObject *item);

/* A list's ob_item points into a block that starts with its capacity
   (_PyListArray), so that a reader holding only the pointer can bound its
   index by the array it loaded, whichever size it read before. The slots are
   zeroed, so a slot past the size reads as NULL, not as garbage. */
struct lu_list_array {
    Py_ssize_t allocated;
    PyObject *ob_item[];
};

#define LU_LIST_ARRAY(items) ((struct lu_list_array *)((char *)(items) - offsetof(struct lu_list_array, ob_item)))

/* A zeroed array of room for capacity items (its ob_item), NULL on failure. */
static inline PyObject **
lu_list_array_new(Py_ssize_t capacity)
{
    struct lu_list_array *a = PyMem_Calloc(1, sizeof(*a) + capacity * sizeof(PyObject *));
    if (a == NULL) {
        return NULL;
    }
    a->allocated = capacity;
    return a->ob_item;
}

static inline void
lu_list_array_free(PyObject **items)
{
    if (items != NULL) {
        PyMem_Free(LU_LIST_ARRAY(items));
    }
}

/* Frees an array a lock-free reader may still hold. */
static inline void
lu_list_array_retire(PyObject **items)
{
    if (items != NULL) {
        lu_qsbr_retire(LU_LIST_ARRAY(items));
    }
}

/* l[i] as a new reference, read without the list's lock; NULL when i is out
   of range or the item changed under the read (lu_list_item_ref then asks
   again under the lock). */
static inline PyObject *
lu_list_try_item_ref(PyListObject *l, Py_ssize_t i)
{
    Py_ssize_t size = _Py_atomic_load_ssize_relaxed(&((PyVarObject *)l)->ob_size);
    PyObject **items = _Py_atomic_load_ptr(&l->ob_item);
    if ((size_t)i >= (size_t)size || items == NULL || i >= LU_LIST_ARRAY(items)->allocated) {
        return NULL;
    }
    return lu_try_xget_ref(&items[i]);
}

/* l[i] as a new reference, NULL when i is out of range (runtime/prim/storage.c). */
PyObject *lu_list_item_ref_locked(PyListObject *l, Py_ssize_t i);

/* n >= 0 copies of the items of a tuple or list, as a new tuple (as_tuple)
   or list; NULL with MemoryError when that is too large (runtime/prim/storage.c). */
PyObject *lu_seq_repeat(PyObject *seq, Py_ssize_t n, int as_tuple);

static inline PyObject *
lu_list_item_ref(PyListObject *l, Py_ssize_t i)
{
    PyObject *item = lu_list_try_item_ref(l, i);
    return item != NULL ? item : lu_list_item_ref_locked(l, i);
}

/* The tuple, list, reverse list and sequence-protocol iterators' layout
   (runtime/prim/storage.c); it_seq is NULL once exhausted. */
typedef struct {
    PyObject_HEAD
    Py_ssize_t it_index;
    PyObject *it_seq;
} seqiterobject;
/* range_iterator (runtime/prim/storage.c): the values as C longs, which
   compiled code steps in place (runtime/jit). */
typedef struct {
    PyObject_HEAD
    long start, step, len;
} rangeiterobject;
/* GET_ITER of the types the loop iterates natively (runtime/interp/ceval.c);
   NULL without an error for every other iterable, which the kernel handles. */
PyObject *lu_get_iter_exact(PyObject *v);
/* The next item of a tuple, list or list_reverseiterator, or a range_iterator's
   next int; NULL when exhausted. */
PyObject *lu_seqiter_next(PyObject *iter);
/* The next key, value or (key, value) of a dict iterator, LU_MISSING when it
   is exhausted, NULL with RuntimeError when the dict changed. */
PyObject *lu_dictiter_next(PyObject *it);
/* The key, value or item iterator of an exact dict or dict view; NULL (no error) for another object. */
PyObject *lu_dict_iter(PyObject *obj);
/* A new dict_keys, dict_values or dict_items view (by its type) of a dict. */
PyObject *lu_dict_view(PyObject *dict, PyTypeObject *view_type);
PyObject *lu_rangeiter_next(PyObject *iter);
/* The count items of an exact tuple or list from start by step, as a new tuple or list. */
PyObject *lu_seq_slice(PyObject *seq, Py_ssize_t start, Py_ssize_t step, Py_ssize_t count, int as_tuple);
int lu_enum_next(PyObject *e, PyObject **v);
/* A new range_iterator or longrange_iterator over the range r, or NULL with an error. */
PyObject *lu_range_iter(PyObject *r);

/* Exceptions raised natively (runtime/capi/errors.c), before and beneath the
   kernel's exception classes. */
void lu_err_format(PyObject *type, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void lu_err_format_from_cause(PyObject *type, const char *format, const char *name);

/* Inline bodies of exported C-API functions on the runtime's hot paths. The
   runtime's own calls of those names compile to these; the exported
   functions (the ABI extensions bind to) wrap the same bodies, in the files
   that define them after an #undef. */
static inline PyThreadState *
lu_tstate(void)
{
    struct lu_task *t = lu_current_task();
    return t != NULL && t->ts.state == _Py_THREAD_ATTACHED ? &t->ts : NULL;
}
_Noreturn void lu_tstate_missing(void);        /* PyThreadState_Get's fatal error */
static inline PyThreadState *
lu_tstate_get(void)
{
    PyThreadState *ts = lu_tstate();
    if (ts == NULL) {
        lu_tstate_missing();
    }
    return ts;
}
static inline PyInterpreterState *
lu_interp(void)
{
    return lu_tstate_get()->interp;
}
static inline PyObject *
lu_err_occurred(void)
{
    PyThreadState *ts = lu_tstate();
    return ts != NULL && ts->current_exception != NULL ? (PyObject *)Py_TYPE(ts->current_exception) : NULL;
}
static inline int
lu_type_is_subtype(PyTypeObject *a, PyTypeObject *b)
{
    PyObject *mro = a->tp_mro;
    if (mro == NULL) {
        /* a type not yet ready: its base chain */
        for (; a != NULL; a = a->tp_base) {
            if (a == b) {
                return 1;
            }
        }
        return b == &PyBaseObject_Type;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); i++) {
        if (PyTuple_GET_ITEM(mro, i) == (PyObject *)b) {
            return 1;
        }
    }
    return 0;
}
static inline int
lu_type_check(PyObject *ob, PyTypeObject *type)
{
    return Py_IS_TYPE(ob, type) || lu_type_is_subtype(Py_TYPE(ob), type);
}
static inline vectorcallfunc
lu_vectorcall_function(PyObject *callable)
{
    PyTypeObject *tp = Py_TYPE(callable);
    if (!PyType_HasFeature(tp, Py_TPFLAGS_HAVE_VECTORCALL)) {
        return NULL;
    }
    vectorcallfunc f;
    memcpy(&f, (char *)callable + tp->tp_vectorcall_offset, sizeof f);
    return f;
}
#ifndef LU_STUBS
#define PyThreadState_GetUnchecked lu_tstate
#define PyThreadState_Get lu_tstate_get
#define PyInterpreterState_Get lu_interp
#define PyErr_Occurred lu_err_occurred
#define PyType_IsSubtype lu_type_is_subtype
#define PyVectorcall_Function lu_vectorcall_function
#undef PyObject_TypeCheck
#define PyObject_TypeCheck(ob, type) lu_type_check(_PyObject_CAST(ob), (type))
#endif

#endif
