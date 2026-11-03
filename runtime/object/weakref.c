/* Weak references (Objects/weakrefobject.c, layout half): the list of
   references on a referent, found through its type's tp_weaklistoffset (for
   a heap type the managed slot before the object), the clearing of that list
   when the referent dies, and the primitives lib/kernel/weakref.py builds the
   ReferenceType methods on.

   A list is guarded by the striped mutex of its referent's address. A
   reference reaches its referent only through weakref_get, which counts it
   alive while its refcount is above zero; that is exact until threads start,
   and then wants the free-threaded try-incref of the biased counts. */

#include "interp/exceptions.h"
#include "prim/prim.h"

#include <string.h>

#define LU_WEAKREF_STRIPES 64

static PyMutex *
stripe(PyObject *ob)
{
    return &_PyRuntime.weakref_locks[((uintptr_t)ob >> 4) % LU_WEAKREF_STRIPES];
}

static PyWeakReference **
list_of(PyObject *ob)
{
    Py_ssize_t offset = Py_TYPE(ob)->tp_weaklistoffset;
    return offset != 0 ? (PyWeakReference **)((char *)ob + offset) : NULL;
}

/* Unlinks `ref` from its referent's list, which the caller has locked. */
static void
unlink_ref(PyWeakReference *ref)
{
    PyWeakReference **head = list_of(ref->wr_object);
    if (ref->wr_prev != NULL) {
        ref->wr_prev->wr_next = ref->wr_next;
    }
    else {
        *head = ref->wr_next;
    }
    if (ref->wr_next != NULL) {
        ref->wr_next->wr_prev = ref->wr_prev;
    }
    ref->wr_prev = ref->wr_next = NULL;
}

/* _PyWeakref_ClearRef: the reference goes dead, keeping its callback. */
void
_PyWeakref_ClearRef(PyWeakReference *ref)
{
    for (;;) {
        PyObject *ob = __atomic_load_n(&ref->wr_object, __ATOMIC_ACQUIRE);
        if (ob == Py_None || ob == NULL) {
            return;
        }
        PyMutex *m = stripe(ob);
        PyMutex_Lock(m);
        if (ref->wr_object == ob) {
            unlink_ref(ref);
            ref->wr_object = Py_None;
            PyMutex_Unlock(m);
            return;
        }
        PyMutex_Unlock(m);
    }
}

/* The references to ob, or NULL if its type has no weak reference list. */
PyWeakReference **
lu_weakref_list(PyObject *ob)
{
    return list_of(ob);
}

static void
weakref_dealloc(PyObject *self)
{
    PyWeakReference *ref = (PyWeakReference *)self;
    _PyWeakref_ClearRef(ref);
    Py_CLEAR(ref->wr_callback);
    lu_object_free(self);
}

static int
weakref_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(((PyWeakReference *)self)->wr_callback);
    return 0;
}

static int
weakref_clear(PyObject *self)
{
    _PyWeakref_ClearRef((PyWeakReference *)self);
    Py_CLEAR(((PyWeakReference *)self)->wr_callback);
    return 0;
}

void
lu_weakref_init(void)
{
    _PyWeakref_RefType.tp_basicsize = sizeof(PyWeakReference);
    _PyWeakref_RefType.tp_flags |= Py_TPFLAGS_HAVE_GC;
    _PyWeakref_RefType.tp_dealloc = weakref_dealloc;
    _PyWeakref_RefType.tp_traverse = weakref_traverse;
    _PyWeakref_RefType.tp_clear = weakref_clear;
}

/* PyObject_ClearWeakRefs: the referent is dying. Every reference becomes
   dead, and each callback of a reference still alive runs, newest first. */
void
lu_clear_weakrefs(PyObject *ob)
{
    PyWeakReference **head = list_of(ob);
    if (head == NULL || *head == NULL) {
        return;
    }
    PyObject *pending = PyErr_GetRaisedException();    /* callbacks run beside it */
    PyMutex *m = stripe(ob);
    PyMutex_Lock(m);
    PyObject *calls = PyList_New(0);
    for (PyWeakReference *ref = *head; ref != NULL; ref = ref->wr_next) {
        ref->wr_object = Py_None;
        if (ref->wr_callback != NULL && calls != NULL && Py_REFCNT(ref) > 0) {
            Py_INCREF(ref);
            lu_list_append(calls, (PyObject *)ref);
            Py_DECREF(ref);
        }
    }
    *head = NULL;
    PyMutex_Unlock(m);
    for (Py_ssize_t i = 0; calls != NULL && i < PyList_GET_SIZE(calls); i++) {
        PyWeakReference *ref = (PyWeakReference *)PyList_GET_ITEM(calls, i);
        PyObject *callback = ref->wr_callback;
        ref->wr_callback = NULL;
        PyObject *arg = (PyObject *)ref;
        PyObject *result = lu_call(callback, &arg, 1, NULL);
        if (result == NULL) {
            lu_err_unraisable("calling weakref callback", callback);
        }
        Py_XDECREF(result);
        Py_DECREF(callback);
    }
    Py_XDECREF(calls);
    PyErr_SetRaisedException(pending);
}

/* A reference of `type` (a subtype of ReferenceType) to ob. Without a
   callback, ReferenceType itself reuses the reference ob already has. A new
   reference, or NULL with an exception set. */
PyObject *
lu_weakref_new(PyTypeObject *type, PyObject *ob, PyObject *callback)
{
    PyWeakReference **head = list_of(ob);
    if (head == NULL) {
        lu_err_format(PyExc_TypeError, "cannot create weak reference to '%s' object", Py_TYPE(ob)->tp_name);
        return NULL;
    }
    PyMutex *lock = stripe(ob);
    if (callback == NULL && type == &_PyWeakref_RefType) {
        PyMutex_Lock(lock);
        PyWeakReference *basic = *head;
        if (basic != NULL && basic->wr_callback == NULL && Py_TYPE(basic) == &_PyWeakref_RefType
            && Py_REFCNT(basic) > 0) {
            Py_INCREF(basic);
            PyMutex_Unlock(lock);
            return (PyObject *)basic;
        }
        PyMutex_Unlock(lock);
    }
    PyWeakReference *ref = (PyWeakReference *)(type->tp_alloc != NULL ? type->tp_alloc : PyType_GenericAlloc)(type, 0);
    if (ref == NULL) {
        return NULL;
    }
    ref->wr_object = ob;
    ref->wr_callback = Py_XNewRef(callback);
    ref->hash = -1;
    PyMutex_Lock(lock);
    PyWeakReference *first = *head;
    int basic = callback == NULL && type == &_PyWeakref_RefType;
    if (first != NULL && !basic && first->wr_callback == NULL && Py_TYPE(first) == &_PyWeakref_RefType) {
        /* after the basic reference, which stays the head */
        ref->wr_prev = first;
        ref->wr_next = first->wr_next;
        if (first->wr_next != NULL) {
            first->wr_next->wr_prev = ref;
        }
        first->wr_next = ref;
    }
    else {
        ref->wr_next = first;
        if (first != NULL) {
            first->wr_prev = ref;
        }
        *head = ref;
    }
    PyMutex_Unlock(lock);
    return (PyObject *)ref;
}

/* weakref_new(type, ob, callback) */
static PyObject *
prim_weakref_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3 || !PyType_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "weakref_new(type, ob, callback)");
        return NULL;
    }
    return lu_weakref_new((PyTypeObject *)args[0], args[1], args[2] == Py_None ? NULL : args[2]);
}

#define REF(i) (nargs == 1 && PyObject_TypeCheck(args[0], &_PyWeakref_RefType) \
    ? (PyWeakReference *)args[0] : (lu_err_format(PyExc_TypeError, "a weak reference is needed"), NULL))

/* weakref_get(ref): the referent, or None once it is dead. */
static PyObject *
prim_weakref_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyWeakReference *ref = REF(0);
    if (ref == NULL) {
        return NULL;
    }
    PyObject *ob = __atomic_load_n(&ref->wr_object, __ATOMIC_ACQUIRE);
    if (ob == Py_None) {
        return Py_NewRef(Py_None);
    }
    PyMutex *m2 = stripe(ob);
    PyMutex_Lock(m2);
    ob = ref->wr_object;
    PyObject *result = ob != Py_None && Py_REFCNT(ob) > 0 ? Py_NewRef(ob) : Py_NewRef(Py_None);
    PyMutex_Unlock(m2);
    return result;
}

/* weakref_callback(ref): the callback, or None. */
static PyObject *
prim_weakref_callback(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyWeakReference *ref = REF(0);
    return ref == NULL ? NULL : Py_NewRef(ref->wr_callback != NULL ? ref->wr_callback : Py_None);
}

/* weakref_hash(ref): the cached hash, or -1 if there is none. */
static PyObject *
prim_weakref_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyWeakReference *ref = REF(0);
    return ref == NULL ? NULL : PyLong_FromLongLong(ref->hash);
}

/* weakref_set_hash(ref, hash) */
static PyObject *
prim_weakref_set_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long h;
    if (nargs != 2 || !PyObject_TypeCheck(args[0], &_PyWeakref_RefType) || lu_long_as_long(args[1], &h) < 0) {
        lu_err_format(PyExc_TypeError, "weakref_set_hash(ref, hash)");
        return NULL;
    }
    ((PyWeakReference *)args[0])->hash = h;
    return Py_NewRef(Py_None);
}

/* weakrefs_of(ob): a list of the references to ob, newest callbacks first
   after the basic one. */
static PyObject *
prim_weakrefs_of(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *list = PyList_New(0);
    PyWeakReference **head = nargs == 1 && list != NULL ? list_of(args[0]) : NULL;
    if (head == NULL) {
        return list;
    }
    PyMutex *lock = stripe(args[0]);
    PyMutex_Lock(lock);
    for (PyWeakReference *ref = *head; ref != NULL; ref = ref->wr_next) {
        if (Py_REFCNT(ref) > 0) {
            lu_list_append(list, (PyObject *)ref);
        }
    }
    PyMutex_Unlock(lock);
    return list;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_weakref_methods[] = {
    FAST(weakref_new), FAST(weakref_get), FAST(weakref_callback), FAST(weakref_hash),
    FAST(weakref_set_hash), FAST(weakrefs_of),
    { NULL, NULL, 0, NULL },
};
