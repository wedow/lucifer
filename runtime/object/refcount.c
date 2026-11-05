/* Biased reference counting, the out-of-line half of Include/refcount.h
   (Objects/object.c). The owning thread (ob_tid) counts in ob_ref_local
   without atomics; every other thread counts in ob_ref_shared atomically,
   which may go negative until the owner merges the two. */

#include "lucifer.h"

void
Py_IncRef(PyObject *o)
{
    Py_XINCREF(o);
}

void
Py_DecRef(PyObject *o)
{
    Py_XDECREF(o);
}

void
_Py_IncRef(PyObject *o)
{
    Py_INCREF(o);
}

void
_Py_DecRef(PyObject *o)
{
    Py_DECREF(o);
}

/* The only birth path (Objects/object.c new_reference). */
void
_Py_NewReference(PyObject *op)
{
    op->ob_flags = 0;
    op->ob_mutex = (PyMutex){ 0 };
    op->ob_tid = _Py_ThreadId();
    op->ob_gc_bits = 0;
    op->ob_ref_local = 1;
    op->ob_ref_shared = 0;
    PyRefTracer tracer = _PyRuntime.ref_tracer.func;
    if (tracer != NULL) {
        tracer(op, PyRefTracer_CREATE, _PyRuntime.ref_tracer.data);
    }
}

void
_Py_NewReferenceNoTotal(PyObject *op)
{
    _Py_NewReference(op);
}

/* The only death path, with the trashcan of Include/internal/pycore_object.h:
   a death inside LU_TRASH_DEPTH nested deaths is queued, and the outermost
   death runs the queue, so freeing a structure nested a million deep does not
   take a million C frames. */
#define LU_TRASH_DEPTH 100

static _Thread_local struct {
    int depth;
    PyObject **queue;
    size_t length, capacity;
} trash;

static void
dealloc_now(PyObject *op)
{
    destructor dealloc = Py_TYPE(op)->tp_dealloc;
    PyRefTracer tracer = _PyRuntime.ref_tracer.func;
    if (tracer != NULL) {
        tracer(op, PyRefTracer_DESTROY, _PyRuntime.ref_tracer.data);
    }
    dealloc(op);
}

void
_Py_Dealloc(PyObject *op)
{
    /* A dying object is the collector's no longer, before any code of its
       dealloc runs: the collector must not find a zero count over live fields,
       or an object parked in the trash queue. */
    op->ob_gc_bits &= ~LU_GC_TRACKED;
    if (trash.depth >= LU_TRASH_DEPTH) {
        if (trash.length == trash.capacity) {
            size_t capacity = trash.capacity ? trash.capacity * 2 : 64;
            PyObject **queue = realloc(trash.queue, capacity * sizeof *queue);
            if (queue != NULL) {
                trash.queue = queue;
                trash.capacity = capacity;
            }
        }
        if (trash.length < trash.capacity) {
            trash.queue[trash.length++] = op;
            return;
        }
    }
    trash.depth++;
    dealloc_now(op);
    if (--trash.depth == 0) {
        while (trash.length > 0) {
            trash.depth++;
            dealloc_now(trash.queue[--trash.length]);
            trash.depth--;
        }
    }
}

int
PyRefTracer_SetTracer(PyRefTracer tracer, void *data)
{
    _PyRuntime.ref_tracer.data = data;
    _PyRuntime.ref_tracer.func = tracer;
    return 0;
}

PyRefTracer
PyRefTracer_GetTracer(void **data)
{
    if (data != NULL) {
        *data = _PyRuntime.ref_tracer.data;
    }
    return _PyRuntime.ref_tracer.func;
}

/* A non-owner's decref. If the shared count was zero, the object goes to the
   owner's merge queue, and the queue keeps the reference being dropped. */
void
_Py_DecRefSharedDebug(PyObject *o, const char *filename, int lineno)
{
    int should_queue;
    Py_ssize_t new_shared;
    Py_ssize_t shared = _Py_atomic_load_ssize_relaxed(&o->ob_ref_shared);
    do {
        should_queue = (shared == 0 || shared == _Py_REF_MAYBE_WEAKREF);
        new_shared = should_queue ? _Py_REF_QUEUED : shared - (1 << _Py_REF_SHARED_SHIFT);
    } while (!_Py_atomic_compare_exchange_ssize(&o->ob_ref_shared, &shared, new_shared));

    if (should_queue) {
        lu_brc_queue_object(o);
    }
    else if (new_shared == _Py_REF_MERGED) {
        _Py_Dealloc(o);
    }
}

void
_Py_DecRefShared(PyObject *o)
{
    _Py_DecRefSharedDebug(o, NULL, 0);
}

/* The owner's local count reached zero: give up ownership. */
void
_Py_MergeZeroLocalRefcount(PyObject *op)
{
    Py_ssize_t shared = _Py_atomic_load_ssize_acquire(&op->ob_ref_shared);
    if (shared == 0) {
        _Py_Dealloc(op);
        return;
    }
    /* Clear ob_tid before marking the count merged, so that a zero ob_tid
       always implies a merged refcount (gh-121794). */
    _Py_atomic_store_uintptr_relaxed(&op->ob_tid, 0);
    Py_ssize_t new_shared;
    do {
        new_shared = (shared & ~_Py_REF_SHARED_FLAG_MASK) | _Py_REF_MERGED;
    } while (!_Py_atomic_compare_exchange_ssize(&op->ob_ref_shared, &shared, new_shared));
    if (new_shared == _Py_REF_MERGED) {
        _Py_Dealloc(op);
    }
}

Py_ssize_t
lu_explicit_merge_refcount(PyObject *op, Py_ssize_t extra)
{
    /* Write ob_ref_local and ob_tid before merging (gh-119999). */
    Py_ssize_t local = (Py_ssize_t)op->ob_ref_local;
    _Py_atomic_store_uint32_relaxed(&op->ob_ref_local, 0);
    _Py_atomic_store_uintptr_relaxed(&op->ob_tid, 0);

    Py_ssize_t refcnt;
    Py_ssize_t new_shared;
    Py_ssize_t shared = _Py_atomic_load_ssize_relaxed(&op->ob_ref_shared);
    do {
        refcnt = Py_ARITHMETIC_RIGHT_SHIFT(Py_ssize_t, shared, _Py_REF_SHARED_SHIFT);
        refcnt += local + extra;
        new_shared = _Py_REF_SHARED(refcnt, _Py_REF_MERGED);
    } while (!_Py_atomic_compare_exchange_ssize(&op->ob_ref_shared, &shared, new_shared));
    return refcnt;
}

int
PyUnstable_IsImmortal(PyObject *op)
{
    return _Py_IsImmortal(op);
}

/* _Py_SetImmortal: the object is never freed and owned by no thread. */
void
lu_set_immortal(PyObject *op)
{
    op->ob_tid = 0;
    op->ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL;
    op->ob_ref_shared = 0;
    op->ob_gc_bits |= LU_GC_DEFERRED;
}

void
lu_set_deferred(PyObject *op)
{
    op->ob_gc_bits |= LU_GC_DEFERRED;
    op->ob_ref_shared = _Py_REF_SHARED(LU_REF_DEFERRED, 0);
}

/* Finalizers from dealloc (Objects/object.c): tp_finalize runs once, on the
   object resurrected for the call, which then dies unless the finalizer
   kept it. The finalized bit is _PyGC_BITS_FINALIZED. */
void
PyObject_CallFinalizer(PyObject *self)
{
    PyTypeObject *tp = Py_TYPE(self);
    if (tp->tp_finalize == NULL || (self->ob_gc_bits & LU_GC_FINALIZED)) {
        return;
    }
    tp->tp_finalize(self);
    self->ob_gc_bits |= LU_GC_FINALIZED;
}

/* _PyObject_ResurrectStart / _PyObject_ResurrectEnd: a dying object, given
   one reference for the call to something that may look at it. End returns 0
   when the object is dead again and -1 when the call kept it. */
void
lu_resurrect_start(PyObject *self)
{
    _Py_atomic_store_uintptr_relaxed(&self->ob_tid, _Py_ThreadId());
    _Py_atomic_store_uint32_relaxed(&self->ob_ref_local, 1);
    _Py_atomic_store_ssize_relaxed(&self->ob_ref_shared, 0);
}

int
lu_resurrect_end(PyObject *self)
{
    uint32_t local = _Py_atomic_load_uint32_relaxed(&self->ob_ref_local);
    Py_ssize_t shared = _Py_atomic_load_ssize_acquire(&self->ob_ref_shared);
    if (_Py_IsOwnedByCurrentThread(self) && local == 1 && shared == 0) {
        _Py_atomic_store_uint32_relaxed(&self->ob_ref_local, 0);
        return 0;
    }
    if (_Py_IsOwnedByCurrentThread(self)) {
        return lu_explicit_merge_refcount(self, -1) == 0 ? 0 : -1;
    }
    /* Another thread owns it now: drop the resurrection's shared reference. */
    Py_ssize_t new_shared;
    do {
        new_shared = shared - (1 << _Py_REF_SHARED_SHIFT);
    } while (!_Py_atomic_compare_exchange_ssize(&self->ob_ref_shared, &shared, new_shared));
    return new_shared == _Py_REF_MERGED ? 0 : -1;
}

int
PyObject_CallFinalizerFromDealloc(PyObject *self)
{
    /* The finalizer sees a tracked object: it may be found by gc calls, or
       resurrect it, and then it must stay tracked. */
    int gc = (Py_TYPE(self)->tp_flags & Py_TPFLAGS_HAVE_GC) != 0;
    if (gc) {
        self->ob_gc_bits |= LU_GC_TRACKED;
    }
    lu_resurrect_start(self);
    PyObject_CallFinalizer(self);
    int resurrected = lu_resurrect_end(self);
    if (gc && resurrected == 0) {
        self->ob_gc_bits &= ~LU_GC_TRACKED;
    }
    return resurrected;
}

/* The refcount test of _PyObject_IsUniquelyReferenced. */
int
PyUnstable_Object_IsUniquelyReferenced(PyObject *op)
{
    return _Py_IsOwnedByCurrentThread(op) && op->ob_ref_local == 1
        && _Py_atomic_load_ssize_relaxed(&op->ob_ref_shared) == 0;
}

/* The runtime keeps no temporaries on a value stack C can see: the API's
   "no". */
int
PyUnstable_Object_IsUniqueReferencedTemporary(PyObject *op)
{
    return 0;
}

/* Deferred reference counting for an object others may already hold: only
   a tracked GC object, since only a collection frees it from now on. */
int
PyUnstable_Object_EnableDeferredRefcount(PyObject *op)
{
    if (!lu_is_gc(op) || !lu_gc_is_tracked(op)) {
        return 0;
    }
    uint8_t bits = __atomic_load_n(&op->ob_gc_bits, __ATOMIC_RELAXED);
    if ((bits & LU_GC_DEFERRED)
        || !__atomic_compare_exchange_n(&op->ob_gc_bits, &bits, (uint8_t)(bits | LU_GC_DEFERRED), 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        return 0;
    }
    __atomic_fetch_add(&op->ob_ref_shared, _Py_REF_SHARED(LU_REF_DEFERRED, 0), __ATOMIC_RELAXED);
    return 1;
}

LU_EXPORT void
_Py_SetImmortal(PyObject *op)
{
    lu_set_immortal(op);
}
