/* Weak references (Objects/weakrefobject.c, layout half): the list of
   references on a referent, found through its type's tp_weaklistoffset (for
   a heap type the managed slot before the object), the clearing of that list
   when the referent dies, and the primitives lib/kernel/weakref.py builds the
   ReferenceType methods on.

   A list is guarded by the striped mutex of its referent's address. A
   reference reaches its referent only through lu_weakref_get, a try-incref
   under that mutex, so a thread never takes a reference to an object whose
   owner is freeing it. A referent and its references are marked maybe-weakref
   when linked, which keeps their shared count nonzero. */

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
            __atomic_store_n(&ref->wr_object, Py_None, __ATOMIC_RELEASE);
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
    /* The proxies are weak references too, of their own types. */
    PyTypeObject *const proxies[] = { &_PyWeakref_ProxyType, &_PyWeakref_CallableProxyType };
    for (int i = 0; i < 2; i++) {
        proxies[i]->tp_basicsize = sizeof(PyWeakReference);
        proxies[i]->tp_flags |= Py_TPFLAGS_HAVE_GC;
        proxies[i]->tp_dealloc = weakref_dealloc;
        proxies[i]->tp_traverse = weakref_traverse;
        proxies[i]->tp_clear = weakref_clear;
    }
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
        __atomic_store_n(&ref->wr_object, Py_None, __ATOMIC_RELEASE);
        if (ref->wr_callback != NULL && calls != NULL && lu_try_incref((PyObject *)ref)) {
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

/* PyUnstable_Object_ClearWeakRefsNoCallbacks: every reference to ob goes
   dead and no callback runs (Objects/weakrefobject.c). */
void
PyUnstable_Object_ClearWeakRefsNoCallbacks(PyObject *ob)
{
    PyWeakReference **head = list_of(ob);
    if (head == NULL) {
        return;
    }
    PyMutex *m = stripe(ob);
    PyMutex_Lock(m);
    while (*head != NULL) {
        PyWeakReference *ref = *head;
        unlink_ref(ref);
        __atomic_store_n(&ref->wr_object, Py_None, __ATOMIC_RELEASE);
    }
    PyMutex_Unlock(m);
}

static int
is_proxy_type(PyTypeObject *type)
{
    return type == &_PyWeakref_ProxyType || type == &_PyWeakref_CallableProxyType;
}

/* get_basic_refs: the list's callback-less reference of exactly
   ReferenceType, its head when there is one, and its callback-less proxy,
   which follows it (or heads the list). The caller holds the stripe. */
static void
basic_refs(PyWeakReference *head, PyWeakReference **ref, PyWeakReference **proxy)
{
    *ref = *proxy = NULL;
    if (head != NULL && head->wr_callback == NULL && Py_TYPE(head) == &_PyWeakref_RefType) {
        *ref = head;
        head = head->wr_next;
    }
    if (head != NULL && head->wr_callback == NULL && is_proxy_type(Py_TYPE(head))) {
        *proxy = head;
    }
}

/* Links ref after prev, or at the head when prev is NULL. */
static void
link_after(PyWeakReference **head, PyWeakReference *prev, PyWeakReference *ref)
{
    if (prev == NULL) {
        ref->wr_next = *head;
        if (*head != NULL) {
            (*head)->wr_prev = ref;
        }
        *head = ref;
        return;
    }
    ref->wr_prev = prev;
    ref->wr_next = prev->wr_next;
    if (prev->wr_next != NULL) {
        prev->wr_next->wr_prev = ref;
    }
    prev->wr_next = ref;
}

/* A reference of `type` (ReferenceType, a subtype of it, or a proxy type)
   to ob, as Objects/weakrefobject.c makes them: without a callback,
   ReferenceType and the proxies reuse the one ob already has; the list keeps
   the basic reference first, then the basic proxy, then the rest. A new
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
    PyWeakReference *ref, *proxy;
    int basic = callback == NULL && (type == &_PyWeakref_RefType || is_proxy_type(type));
    if (basic) {
        PyMutex_Lock(lock);
        basic_refs(*head, &ref, &proxy);
        PyWeakReference *reuse = type == &_PyWeakref_RefType ? ref : proxy;
        if (reuse != NULL && lu_try_incref((PyObject *)reuse)) {
            PyMutex_Unlock(lock);
            return (PyObject *)reuse;
        }
        PyMutex_Unlock(lock);
    }
    PyWeakReference *new = (PyWeakReference *)(type->tp_alloc != NULL ? type->tp_alloc : PyType_GenericAlloc)(type, 0);
    if (new == NULL) {
        return NULL;
    }
    new->wr_object = ob;
    new->wr_callback = Py_XNewRef(callback);
    new->hash = -1;
    lu_set_maybe_weakref(ob);
    lu_set_maybe_weakref((PyObject *)new);
    PyMutex_Lock(lock);
    basic_refs(*head, &ref, &proxy);
    PyWeakReference *prev = type == &_PyWeakref_RefType && basic ? NULL
                          : basic ? ref
                          : proxy != NULL ? proxy : ref;
    link_after(head, prev, new);
    PyMutex_Unlock(lock);
    return (PyObject *)new;
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

#define REF(i) (nargs == 1 && PyWeakref_Check(args[0]) \
    ? (PyWeakReference *)args[0] : (lu_err_format(PyExc_TypeError, "a weak reference is needed"), NULL))

/* A new reference to the referent of the weak reference `ref`, NULL once it is dead
   (_PyWeakref_GET_REF): under the referent's stripe, a try-incref, which fails once its
   last strong reference has gone. The owner of a referent whose count is zero is
   deallocating it, so a try-incref would revive it: that is dead too. */
PyObject *
lu_weakref_get(PyObject *ref)
{
    PyObject *ob = __atomic_load_n(&((PyWeakReference *)ref)->wr_object, __ATOMIC_ACQUIRE);
    if (ob == Py_None) {
        return NULL;
    }
    PyMutex *m = stripe(ob);
    PyMutex_Lock(m);
    PyObject *result = NULL;
    if (((PyWeakReference *)ref)->wr_object != Py_None
        && !(_Py_IsOwnedByCurrentThread(ob) && ob->ob_ref_local == 0) && lu_try_incref(ob)) {
        result = ob;
    }
    PyMutex_Unlock(m);
    return result;
}

/* For native hashing and comparison, which may neither block nor run code
   (dict.c): the referent of ref, borrowed, NULL once it has died. While the
   referent's stripe is held (*held, taken by a try-lock unless `already`
   is that stripe) it cannot finish dying, since clearing its references
   takes the stripe. 0, holding nothing, when the stripe is busy or the
   referent is dying: the caller declines. */
int
lu_weakref_peek(PyObject *ref, PyMutex *already, PyObject **ob, PyMutex **held)
{
    PyWeakReference *r = (PyWeakReference *)ref;
    PyObject *o = __atomic_load_n(&r->wr_object, __ATOMIC_ACQUIRE);
    *held = NULL;
    *ob = NULL;
    if (o == Py_None) {
        return 1;
    }
    PyMutex *m = stripe(o);
    if (m != already) {
        uint8_t unlocked = _Py_UNLOCKED;
        if (!_Py_atomic_compare_exchange_uint8(&m->_bits, &unlocked, _Py_LOCKED)) {
            return 0;
        }
        *held = m;
    }
    if (r->wr_object != o || Py_REFCNT(o) == 0) {
        lu_weakref_unpeek(*held);
        *held = NULL;
        return 0;
    }
    *ob = o;
    return 1;
}

void
lu_weakref_unpeek(PyMutex *held)
{
    if (held != NULL) {
        PyMutex_Unlock(held);
    }
}

/* weakref_get(ref): the referent, or None once it is dead. */
static PyObject *
prim_weakref_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyWeakReference *ref = REF(0);
    if (ref == NULL) {
        return NULL;
    }
    PyObject *ob = lu_weakref_get((PyObject *)ref);
    return ob != NULL ? ob : Py_NewRef(Py_None);
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
        if (lu_try_incref((PyObject *)ref)) {
            lu_list_append(list, (PyObject *)ref);
            Py_DECREF(ref);
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
