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

void
_Py_NewReference(PyObject *op)
{
    op->ob_flags = 0;
    op->ob_mutex = (PyMutex){ 0 };
    op->ob_tid = _Py_ThreadId();
    op->ob_gc_bits = 0;
    op->ob_ref_local = 1;
    op->ob_ref_shared = 0;
}

void
_Py_NewReferenceNoTotal(PyObject *op)
{
    _Py_NewReference(op);
}

void
_Py_Dealloc(PyObject *op)
{
    Py_TYPE(op)->tp_dealloc(op);
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
