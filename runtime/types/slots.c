/* C slot thunks (Objects/typeobject.c slot_tp_* and friends): the type
   slots of kernel types and classes, for C callers. Each looks the special
   method up on the type and runs it in a nested eval loop, which pins the
   task (DESIGN 5.4); the interpreter calls the same kernel Python directly.

   The slotdefs table itself, which dunders feed which slot, is kernel Python
   (lib/kernel/_boot.py); this file has the slot table it installs from:
   each slot's place and its thunk. A slot without a thunk yet is left as
   it is (the sequence slots arrive with their types). */

#include "interp/exceptions.h"
#include "interp/frame.h"
#include "prim/prim.h"

#include <string.h>

/* Runs interp->kernel[id] on args from C. */
PyObject *
lu_call_kernel(enum lu_kernel_id id, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *func = PyInterpreterState_Get()->kernel[id];
    if (func == NULL) {
        lu_err_format(PyExc_SystemError, "kernel function %d is not registered yet", (int)id);
        return NULL;
    }
    return lu_call(func, args, nargs, NULL);
}

/* vectorcall_method/lookup_maybe_method: the special method of self's
   type, called with self first when it is a plain function or a kernel
   method descriptor, else bound through its __get__ if it has one (a
   builtin function has none: it gets no self). */
static PyObject *
call_special_in(PyTypeObject *tp, PyObject *self, PyObject *name, PyObject *const *args,
                Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *descr = _PyType_LookupRef(tp, name);
    if (descr == NULL) {
        lu_err_format(PyExc_AttributeError, "'%.100s' object has no attribute '%s'",
                      Py_TYPE(self)->tp_name, PyUnicode_AsUTF8(name));
        return NULL;
    }
    Py_ssize_t total = nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    PyObject *small[8];
    PyObject **stack = total + 1 <= 8 ? small : PyMem_Malloc((total + 1) * sizeof(PyObject *));
    if (stack == NULL) {
        Py_DECREF(descr);
        return PyErr_NoMemory();
    }
    PyObject *func, *bound_self, *result;
    int target = PyFunction_Check(descr) ? 1
        : (Py_TYPE(descr)->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR) ? lu_kernel_target(descr, &self, 1, &func, &bound_self)
        : 0;
    if (target < 0) {
        PyErr_Clear();  /* self fails the descriptor's check: __get__ decides */
    }
    int unbound = target > 0;
    if (unbound) {
        stack[0] = self;
        memcpy(stack + 1, args, total * sizeof(PyObject *));
        result = lu_call(descr, stack, nargs + 1, kwnames);
    }
    else {
        /* A descriptor: bind it through __get__ (kernel Python), then call. */
        PyObject *get = _PyType_LookupRef(Py_TYPE(descr), LU_ID(__get__));
        PyObject *bound;
        if (get == NULL) {
            bound = Py_NewRef(descr);
        }
        else {
            PyObject *get_args[3] = { descr, self, (PyObject *)Py_TYPE(self) };
            bound = lu_call(get, get_args, 3, NULL);
            Py_DECREF(get);
        }
        result = bound != NULL ? lu_call(bound, args, nargs, kwnames) : NULL;
        Py_XDECREF(bound);
    }
    if (stack != small) {
        PyMem_Free(stack);
    }
    Py_DECREF(descr);
    return result;
}

static PyObject *
call_special_kw(PyObject *self, PyObject *name, PyObject *const *args, Py_ssize_t nargs,
                PyObject *kwnames)
{
    return call_special_in(Py_TYPE(self), self, name, args, nargs, kwnames);
}

PyObject *
lu_call_special(PyObject *self, PyObject *name, PyObject *const *args, Py_ssize_t nargs)
{
    return call_special_kw(self, name, args, nargs, NULL);
}

/* A tuple and dict call as vectorcall operands; *kwnames new or NULL. */
static PyObject **
tuple_dict_args(PyObject *args, PyObject *kwargs, Py_ssize_t *nargs, PyObject **kwnames)
{
    return lu_unpack_call(args, kwargs != NULL && ((PyDictObject *)kwargs)->ma_used ? kwargs : NULL,
                          nargs, kwnames);
}

static void
release_args(PyObject **stack, Py_ssize_t nargs, PyObject *kwnames)
{
    Py_ssize_t total = nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_DECREF(stack[i]);
    }
    Py_XDECREF(kwnames);
    PyMem_Free(stack - 1);
}

static PyObject *
call_special_tuple(PyObject *self, PyObject *name, PyObject *args, PyObject *kwargs)
{
    Py_ssize_t nargs;
    PyObject *kwnames;
    PyObject **stack = tuple_dict_args(args, kwargs, &nargs, &kwnames);
    if (stack == NULL) {
        return NULL;
    }
    PyObject *result = call_special_kw(self, name, stack, nargs, kwnames);
    release_args(stack, nargs, kwnames);
    return result;
}

/* The thunks. */

PyObject *
lu_slot_tp_getattro(PyObject *self, PyObject *name)
{
    PyObject *args[2] = { self, name };
    return lu_call_kernel(LU_K_getattr, args, 2);
}

int
lu_slot_tp_setattro(PyObject *self, PyObject *name, PyObject *value)
{
    PyObject *args[3] = { self, name, value };
    PyObject *r = value != NULL ? lu_call_kernel(LU_K_setattr, args, 3)
                                : lu_call_kernel(LU_K_delattr, args, 2);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

PyObject *
lu_slot_tp_call(PyObject *self, PyObject *args, PyObject *kwargs)
{
    if (Py_EnterRecursiveCall(" while calling a Python object")) {
        return NULL;
    }
    PyObject *result = call_special_tuple(self, LU_ID(__call__), args, kwargs);
    Py_LeaveRecursiveCall();
    return result;
}

static PyObject *
string_result(PyObject *r, const char *name)
{
    if (r != NULL && !PyUnicode_Check(r)) {
        lu_err_format(PyExc_TypeError, "%s returned non-string (type %.200s)", name,
                      Py_TYPE(r)->tp_name);
        Py_CLEAR(r);
    }
    return r;
}

static PyObject *
slot_tp_repr(PyObject *self)
{
    return string_result(lu_call_special(self, LU_ID(__repr__), NULL, 0), "__repr__");
}

static PyObject *
slot_tp_str(PyObject *self)
{
    return string_result(lu_call_special(self, LU_ID(__str__), NULL, 0), "__str__");
}

/* An int result as a C integer; -1 with an error when it is not one. */
static int
int_result(PyObject *r, const char *message, long *value)
{
    if (r == NULL) {
        return -1;
    }
    int err = 0;
    if (!PyLong_Check(r)) {
        lu_err_format(PyExc_TypeError, message, Py_TYPE(r)->tp_name);
        err = -1;
    }
    else {
        err = lu_long_as_long(r, value);
    }
    Py_DECREF(r);
    return err;
}

static Py_hash_t
slot_tp_hash(PyObject *self)
{
    long h;
    if (int_result(lu_call_special(self, LU_ID(__hash__), NULL, 0),
                   "__hash__ method should return an integer", &h) < 0) {
        return -1;
    }
    return h == -1 ? -2 : (Py_hash_t)h;
}

static PyObject *
slot_tp_richcompare(PyObject *self, PyObject *other, int op)
{
    PyObject *const names[] = { LU_ID(__lt__), LU_ID(__le__), LU_ID(__eq__), LU_ID(__ne__),
                                LU_ID(__gt__), LU_ID(__ge__) };
    if (_PyType_Lookup(Py_TYPE(self), names[op]) == NULL) {
        return Py_NewRef(Py_NotImplemented);
    }
    return lu_call_special(self, names[op], &other, 1);
}

/* object's own slots run object's methods, not the receiver type's: a type that
   delegates to PyBaseObject_Type.tp_repr (or _str, _richcompare) would recurse
   into itself through the lookup. */
static PyObject *
object_tp_repr(PyObject *self)
{
    return string_result(call_special_in(&PyBaseObject_Type, self, LU_ID(__repr__), NULL, 0, NULL),
                         "__repr__");
}

static PyObject *
object_tp_str(PyObject *self)
{
    return string_result(call_special_in(&PyBaseObject_Type, self, LU_ID(__str__), NULL, 0, NULL),
                         "__str__");
}

static PyObject *
object_tp_richcompare(PyObject *self, PyObject *other, int op)
{
    PyObject *const names[] = { LU_ID(__lt__), LU_ID(__le__), LU_ID(__eq__), LU_ID(__ne__),
                                LU_ID(__gt__), LU_ID(__ge__) };
    if (_PyType_Lookup(&PyBaseObject_Type, names[op]) == NULL) {
        return Py_NewRef(Py_NotImplemented);
    }
    return call_special_in(&PyBaseObject_Type, self, names[op], &other, 1, NULL);
}

/* The attribute protocol word (runtime/lucifer.h). */

/* Whether d, which tp finds under `name`, is the C slot wrapper of `name`
   that wraps object's generic function, made for a base of tp (for any
   type when tp is NULL): slot_tp_getattr_hook's test, which runs such a
   wrapper as generic access. */
static int
generic_wrapper(PyObject *d, PyObject *name, PyTypeObject *tp)
{
    PyWrapperDescrObject *w = (PyWrapperDescrObject *)d;
    return d != NULL && Py_IS_TYPE(d, &PyWrapperDescr_Type)
        && (w->d_wrapped == (void *)PyObject_GenericGetAttr || w->d_wrapped == (void *)PyObject_GenericSetAttr)
        && PyUnicode_EqualToUTF8(name, w->d_base->name) && (tp == NULL || PyType_IsSubtype(tp, PyDescr_TYPE(w)));
}

/* Whether d is what `owner` itself has under `name` (object's, type's or
   module's own dunder). */
static int
owners(PyObject *d, PyTypeObject *owner, PyObject *name)
{
    return d != NULL && d == _PyType_Lookup(owner, name);
}

/* What the kernel's hook (lib/kernel/object.py getattr_hook, setattr_hook,
   delattr_hook) does for instances of tp when tp's slot is the thunk. */
static int
thunk_way(PyTypeObject *tp, int field)
{
    if (field == LU_ATTR_READ) {
        PyObject *get = _PyType_Lookup(tp, LU_ID(__getattribute__));
        int fallback = _PyType_Lookup(tp, LU_ID(__getattr__)) != NULL;
        if (owners(get, &PyBaseObject_Type, LU_ID(__getattribute__))
            || generic_wrapper(get, LU_ID(__getattribute__), fallback ? NULL : tp)) {
            return fallback ? LU_ATTR_GENERIC_GETATTR : LU_ATTR_GENERIC;
        }
        return fallback ? LU_ATTR_CUSTOM
            : owners(get, &PyType_Type, LU_ID(__getattribute__)) && PyType_IsSubtype(tp, &PyType_Type) ? LU_ATTR_TYPE
            : owners(get, &PyModule_Type, LU_ID(__getattribute__)) && PyType_IsSubtype(tp, &PyModule_Type) ? LU_ATTR_MODULE
            : LU_ATTR_CUSTOM;
    }
    PyObject *name = field == LU_ATTR_STORE ? LU_ID(__setattr__) : LU_ID(__delattr__);
    PyObject *set = _PyType_Lookup(tp, name);
    return owners(set, &PyBaseObject_Type, name) || generic_wrapper(set, name, tp) ? LU_ATTR_GENERIC
        : owners(set, &PyType_Type, name) && PyType_IsSubtype(tp, &PyType_Type) ? LU_ATTR_TYPE
        : LU_ATTR_CUSTOM;
}

int
lu_attr_classify(PyTypeObject *tp, int field)
{
    void *slot = field == LU_ATTR_READ ? (void *)tp->tp_getattro : (void *)tp->tp_setattro;
    return slot == (void *)PyObject_GenericGetAttr || slot == (void *)PyObject_GenericSetAttr ? LU_ATTR_GENERIC
        : slot == (void *)lu_slot_tp_getattro || slot == (void *)lu_slot_tp_setattro ? thunk_way(tp, field)
        : LU_ATTR_SLOT;
}

/* The slot a class gets for an attribute thunk, as update_one_slot decides
   it: object's own function where the hook would run object's algorithm
   (object itself keeps its thunk, which static types inherit: an extension
   type that sets PyObject_GenericGetAttr itself still differs from its base
   and gets its own wrapper, as in CPython); the C function of a base's slot
   wrapper the class finds (a class over a C type with its own getattro),
   except for __getattribute__ under a __getattr__; the thunk otherwise. */
static void *
attribute_thunk(PyTypeObject *tp, void *thunk)
{
    if (thunk == (void *)lu_slot_tp_getattro) {
        int way = thunk_way(tp, LU_ATTR_READ);
        void *specific = way == LU_ATTR_CUSTOM && _PyType_Lookup(tp, LU_ID(__getattr__)) == NULL
            ? lu_wrapper_specific(tp, LU_ID(__getattribute__)) : NULL;
        return way == LU_ATTR_GENERIC ? (void *)PyObject_GenericGetAttr : specific != NULL ? specific : thunk;
    }
    if (thunk == (void *)lu_slot_tp_setattro) {
        if (thunk_way(tp, LU_ATTR_STORE) == LU_ATTR_GENERIC && thunk_way(tp, LU_ATTR_DELETE) == LU_ATTR_GENERIC) {
            return (void *)PyObject_GenericSetAttr;
        }
        void *specific = lu_wrapper_specific(tp, LU_ID(__setattr__));
        return specific != NULL && specific == lu_wrapper_specific(tp, LU_ID(__delattr__)) ? specific : thunk;
    }
    return thunk;
}

/* Serializes every decision of an attribute slot and every write of the
   word: the last writer, which runs after its own dict change, reads every
   change before it. */
static PyMutex attr_lock;

static unsigned long
attr_word(PyTypeObject *tp)
{
    return (unsigned long)lu_attr_classify(tp, LU_ATTR_READ) << LU_ATTR_READ
        | (unsigned long)lu_attr_classify(tp, LU_ATTR_STORE) << LU_ATTR_STORE
        | (unsigned long)lu_attr_classify(tp, LU_ATTR_DELETE) << LU_ATTR_DELETE;
}

/* Whether tp's attribute slot (LU_TPFLAGS_GET_DECIDED or _SET_DECIDED) is
   the one attribute_thunk decided, as lu_set_slot records it; a slot C
   gave the type stays as C set it. */
static int
decided_here(PyTypeObject *tp, unsigned long which)
{
    return (__atomic_load_n(&tp->tp_flags, __ATOMIC_RELAXED) & which) != 0;
}

/* Stores the word, after the slots it reads; the caller holds attr_lock.
   Until the kernel has booted nothing is stored: static types' dicts are
   still being filled, and the accessor decides as it goes. */
static void
store_word(PyTypeObject *tp)
{
    if (!PyInterpreterState_Get()->kernel_booted) {
        return;
    }
    unsigned long word = attr_word(tp), old = __atomic_load_n(&tp->tp_flags, __ATOMIC_RELAXED);
    while (!__atomic_compare_exchange_n(&tp->tp_flags, &old, (old & ~LU_TPFLAGS_ATTR) | word, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
    }
}

/* tp's word from its slots; when `decide`, a heap type's attribute slots
   decided first, where they are the ones attribute_thunk decides. */
void
lu_attr_update(PyTypeObject *tp, int decide)
{
    PyMutex_Lock(&attr_lock);
    if (decide && decided_here(tp, LU_TPFLAGS_GET_DECIDED)) {
        tp->tp_getattro = (getattrofunc)attribute_thunk(tp, (void *)lu_slot_tp_getattro);
    }
    if (decide && decided_here(tp, LU_TPFLAGS_SET_DECIDED)) {
        tp->tp_setattro = (setattrofunc)attribute_thunk(tp, (void *)lu_slot_tp_setattro);
    }
    store_word(tp);
    PyMutex_Unlock(&attr_lock);
}

/* Whether tp's word and slots are what a refresh would make them now; not
   while a writer is between its dict change and its slot rewrite. The
   caller holds attr_lock. */
static int
attr_current(PyTypeObject *tp)
{
    unsigned long word = __atomic_load_n(&tp->tp_flags, __ATOMIC_RELAXED) & LU_TPFLAGS_ATTR;
    return word != 0 && word == attr_word(tp)
        && (!decided_here(tp, LU_TPFLAGS_GET_DECIDED)
            || (void *)tp->tp_getattro == attribute_thunk(tp, (void *)lu_slot_tp_getattro))
        && (!decided_here(tp, LU_TPFLAGS_SET_DECIDED)
            || (void *)tp->tp_setattro == attribute_thunk(tp, (void *)lu_slot_tp_setattro));
}

static int
has_gap(PyTypeObject *tp, int gap)
{
    PyObject *names[3] = { LU_ID(__getattribute__), LU_ID(__setattr__), LU_ID(__delattr__) };
    for (int i = 0; (gap & LU_ATTR_GAP_WRAPPER) && i < 3; i++) {
        if (generic_wrapper(_PyType_Lookup(tp, names[i]), names[i], NULL)) {
            return 1;
        }
    }
    return (gap & LU_ATTR_GAP_THUNK)
        && (tp->tp_getattro == lu_slot_tp_getattro || tp->tp_setattro == lu_slot_tp_setattro);
}

/* A site and the word disagree: unless the kernel is still booting or the
   type has the site's gap, the site's predicate, the word, the gap and then
   whether the word is current are evaluated again under attr_lock, which
   holds the word and slots still (a writer that changed the dict before
   the lookups and has not rewritten the slots shows as a word that is not
   current); the disagreement is the site's only if it is still there,
   and the type's tag, taken under the lock and non-zero, is the same at
   the end: a dict store clears it, and stores into a class's dict do not
   take attr_lock. The tag, with the lock held; 0 for no check. */
uint32_t
lu_attr_check_begin(PyTypeObject *tp, int gap)
{
    if (!PyInterpreterState_Get()->kernel_booted || has_gap(tp, gap)) {
        return 0;
    }
    PyMutex_Lock(&attr_lock);
    uint32_t tag = __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE);
    if (tag == 0) {
        PyMutex_Unlock(&attr_lock);
    }
    return tag;
}

void
lu_attr_check_end(const char *site, PyTypeObject *tp, int field, int gap, int differ, int legacy, uint32_t tag)
{
    int fatal = differ && !has_gap(tp, gap) && attr_current(tp)
        && __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) == tag;
    PyMutex_Unlock(&attr_lock);
    if (!fatal) {
        return;
    }
    char message[300];
    unsigned long flags = __atomic_load_n(&tp->tp_flags, __ATOMIC_RELAXED);
    snprintf(message, sizeof message, "attribute word check at %s: type %.100s, field %d, word %lu/%lu/%lu, "
             "decided %d, legacy %d", site, tp->tp_name, field, flags >> LU_ATTR_READ & 7,
             flags >> LU_ATTR_STORE & 7, flags >> LU_ATTR_DELETE & 7, lu_attr_classify(tp, field), legacy);
    Py_FatalError(message);
}

static void *
object_thunk(void *thunk)
{
    return thunk == (void *)slot_tp_hash ? (void *)PyObject_GenericHash
         : thunk == (void *)slot_tp_repr ? (void *)object_tp_repr
         : thunk == (void *)slot_tp_str ? (void *)object_tp_str
         : thunk == (void *)slot_tp_richcompare ? (void *)object_tp_richcompare
         : thunk;
}

static PyObject *
slot_tp_iter(PyObject *self)
{
    return lu_call_special(self, LU_ID(__iter__), NULL, 0);
}

static PyObject *
slot_tp_iternext(PyObject *self)
{
    return lu_call_special(self, LU_ID(__next__), NULL, 0);
}

static PyObject *
slot_tp_descr_get(PyObject *self, PyObject *obj, PyObject *type)
{
    PyObject *args[2] = { obj != NULL ? obj : Py_None, type != NULL ? type : Py_None };
    return lu_call_special(self, LU_ID(__get__), args, 2);
}

static int
slot_tp_descr_set(PyObject *self, PyObject *obj, PyObject *value)
{
    PyObject *args[2] = { obj, value };
    PyObject *r = value != NULL ? lu_call_special(self, LU_ID(__set__), args, 2)
                                : lu_call_special(self, LU_ID(__delete__), args, 1);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

static int
slot_tp_init(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *r = call_special_tuple(self, LU_ID(__init__), args, kwargs);
    if (r == NULL) {
        return -1;
    }
    if (r != Py_None) {
        lu_err_format(PyExc_TypeError, "__init__() should return None, not '%.200s'",
                      Py_TYPE(r)->tp_name);
        Py_DECREF(r);
        return -1;
    }
    Py_DECREF(r);
    return 0;
}

/* slot_tp_finalize: __del__, run on a dying object with the pending
   exception set aside; an error in it is reported, not raised. */
static void
slot_tp_finalize(PyObject *self)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *r = lu_call_special(self, LU_ID(__del__), NULL, 0);
    if (r == NULL) {
        PyObject *err = PyErr_GetRaisedException();
        PyObject *del = PyObject_GetAttr((PyObject *)Py_TYPE(self), LU_ID(__del__));
        PyErr_Clear();
        PyErr_SetRaisedException(err);
        lu_err_unraisable_text("Exception ignored while calling deallocator", del);
        Py_XDECREF(del);
    }
    Py_XDECREF(r);
    PyErr_SetRaisedException(exc);
}

/* slot_tp_new: type.__new__ as an attribute of the type, called with the
   type first. */
static PyObject *
slot_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *func = PyObject_GetAttr((PyObject *)type, LU_ID(__new__));
    if (func == NULL) {
        return NULL;
    }
    Py_ssize_t nargs;
    PyObject *kwnames;
    PyObject **stack = lu_unpack_call(args, kwargs != NULL && ((PyDictObject *)kwargs)->ma_used
                                      ? kwargs : NULL, &nargs, &kwnames);
    PyObject *result = NULL;
    if (stack != NULL) {
        stack[-1] = (PyObject *)type;
        result = lu_call(func, stack - 1, nargs + 1, kwnames);
        release_args(stack, nargs, kwnames);
    }
    Py_DECREF(func);
    return result;
}

/* tp_new of a static builtin type whose __new__ is kernel Python (float, int,
   str, ...). C code calls it with the type it wants an instance of, often a
   subtype with a __new__ of its own that has called it: the __new__ to run is
   the builtin's, found in the first builtin of the subtype's MRO that has this
   slot, never the subtype's. */
static PyObject *
slot_tp_new_builtin(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    PyObject *mro = type->tp_mro;
    PyTypeObject *owner = NULL;
    for (Py_ssize_t i = 0; mro != NULL && i < PyTuple_GET_SIZE(mro) && owner == NULL; i++) {
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        if ((base->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN) && base->tp_new == slot_tp_new_builtin) {
            owner = base;
        }
    }
    PyObject *func = owner != NULL ? lu_ns_lookup(lu_type_dict(owner), LU_ID(__new__)) : NULL;
    if (func == NULL) {
        return slot_tp_new(type, args, kwargs);
    }
    Py_ssize_t nargs;
    PyObject *kwnames;
    PyObject **stack = lu_unpack_call(args, kwargs != NULL && ((PyDictObject *)kwargs)->ma_used
                                      ? kwargs : NULL, &nargs, &kwnames);
    PyObject *result = NULL;
    if (stack != NULL) {
        stack[-1] = (PyObject *)type;
        lu_new_check_skip(1);
        result = lu_call(func, stack - 1, nargs + 1, kwnames);
        lu_new_check_skip(0);
        release_args(stack, nargs, kwnames);
    }
    return result;
}

static PyObject *
slot_mp_subscript(PyObject *self, PyObject *key)
{
    return lu_call_special(self, LU_ID(__getitem__), &key, 1);
}

/* sq_item and sq_ass_item: the index as an int in __getitem__, __setitem__
   or __delitem__ (PySequence_GetItem has already added the length to a
   negative index). */
static PyObject *
slot_sq_item(PyObject *self, Py_ssize_t i)
{
    PyObject *key = PyLong_FromLong(i);
    if (key == NULL) {
        return NULL;
    }
    PyObject *r = lu_call_special(self, LU_ID(__getitem__), &key, 1);
    Py_DECREF(key);
    return r;
}

static int
slot_sq_ass_item(PyObject *self, Py_ssize_t i, PyObject *value)
{
    PyObject *args[2] = { PyLong_FromLong(i), value };
    if (args[0] == NULL) {
        return -1;
    }
    PyObject *r = value != NULL ? lu_call_special(self, LU_ID(__setitem__), args, 2)
                                : lu_call_special(self, LU_ID(__delitem__), args, 1);
    Py_DECREF(args[0]);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

static Py_ssize_t
slot_length(PyObject *self)
{
    long n;
    if (int_result(lu_call_special(self, LU_ID(__len__), NULL, 0),
                   "'%.200s' object cannot be interpreted as an integer", &n) < 0) {
        return -1;
    }
    if (n < 0) {
        lu_err_format(PyExc_ValueError, "__len__() should return >= 0");
        return -1;
    }
    return n;
}

/* The truth of a special method's result; consumes r. */
static int
truth_result(PyObject *r)
{
    if (r == NULL) {
        return -1;
    }
    int t = lu_is_true(r);
    Py_DECREF(r);
    return t;
}

static int
slot_sq_contains(PyObject *self, PyObject *value)
{
    return truth_result(lu_call_special(self, LU_ID(__contains__), &value, 1));
}

static int
slot_nb_bool(PyObject *self)
{
    PyObject *r = lu_call_special(self, LU_ID(__bool__), NULL, 0);
    if (r != NULL && !PyBool_Check(r)) {
        lu_err_format(PyExc_TypeError, "__bool__ should return bool, returned %s",
                      Py_TYPE(r)->tp_name);
        Py_DECREF(r);
        return -1;
    }
    return truth_result(r);
}

static int
slot_mp_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
    PyObject *args[2] = { key, value };
    PyObject *r = value != NULL ? lu_call_special(self, LU_ID(__setitem__), args, 2)
                                : lu_call_special(self, LU_ID(__delitem__), args, 1);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* A special method called when the type has it, else NotImplemented
   (vectorcall_maybe). */
static PyObject *
call_maybe(PyObject *self, PyObject *name, PyObject *const *args, Py_ssize_t nargs)
{
    if (_PyType_Lookup(Py_TYPE(self), name) == NULL) {
        return Py_NewRef(Py_NotImplemented);
    }
    return lu_call_special(self, name, args, nargs);
}

/* method_is_overloaded: other's type defines `name` differently from self's. */
static int
overloaded(PyObject *self, PyObject *other, PyObject *name)
{
    PyObject *b = _PyType_Lookup(Py_TYPE(other), name);
    return b != NULL && b != _PyType_Lookup(Py_TYPE(self), name);
}

/* SLOT1BINFULL: the binary slots of kernel types and classes. `slot` is the
   thunk itself, so a type counts when it inherits the thunk; the kernel's
   binary_op does the same for the interpreter, in Python. */
static PyObject *
binary_slot(PyObject *self, PyObject *other, int offset, void *slot, PyObject *dunder,
            PyObject *rdunder)
{
#define SLOT_OF(t) ((t)->tp_as_number != NULL ? *(void **)((char *)(t)->tp_as_number + offset) : NULL)
    int do_other = Py_TYPE(self) != Py_TYPE(other) && SLOT_OF(Py_TYPE(other)) == slot;
    if (SLOT_OF(Py_TYPE(self)) == slot) {
        PyObject *r;
        if (do_other && PyType_IsSubtype(Py_TYPE(other), Py_TYPE(self))
            && overloaded(self, other, rdunder)) {
            PyObject *swapped[2] = { self, NULL };
            r = call_maybe(other, rdunder, swapped, 1);
            if (r != Py_NotImplemented) {
                return r;
            }
            Py_DECREF(r);
            do_other = 0;
        }
        r = call_maybe(self, dunder, &other, 1);
        if (r != Py_NotImplemented || Py_TYPE(other) == Py_TYPE(self)) {
            return r;
        }
        Py_DECREF(r);
    }
    if (do_other) {
        return call_maybe(other, rdunder, &self, 1);
    }
    return Py_NewRef(Py_NotImplemented);
#undef SLOT_OF
}

#define BINARY_THUNK(fn, field, dunder, rdunder) \
    static PyObject *fn(PyObject *a, PyObject *b) \
    { return binary_slot(a, b, offsetof(PyNumberMethods, field), (void *)fn, \
                         LU_ID(dunder), LU_ID(rdunder)); }
BINARY_THUNK(slot_nb_add, nb_add, __add__, __radd__)
BINARY_THUNK(slot_nb_subtract, nb_subtract, __sub__, __rsub__)
BINARY_THUNK(slot_nb_multiply, nb_multiply, __mul__, __rmul__)
BINARY_THUNK(slot_nb_remainder, nb_remainder, __mod__, __rmod__)
BINARY_THUNK(slot_nb_divmod, nb_divmod, __divmod__, __rdivmod__)
BINARY_THUNK(slot_nb_lshift, nb_lshift, __lshift__, __rlshift__)
BINARY_THUNK(slot_nb_rshift, nb_rshift, __rshift__, __rrshift__)
BINARY_THUNK(slot_nb_and, nb_and, __and__, __rand__)
BINARY_THUNK(slot_nb_xor, nb_xor, __xor__, __rxor__)
BINARY_THUNK(slot_nb_or, nb_or, __or__, __ror__)
BINARY_THUNK(slot_nb_floor_divide, nb_floor_divide, __floordiv__, __rfloordiv__)
BINARY_THUNK(slot_nb_true_divide, nb_true_divide, __truediv__, __rtruediv__)
BINARY_THUNK(slot_nb_matrix_multiply, nb_matrix_multiply, __matmul__, __rmatmul__)

/* pow(): the two-argument form reflects; the three-argument form only calls
   self.__pow__, as ternary_op may offer another type's slot. */
static PyObject *
slot_nb_power(PyObject *self, PyObject *other, PyObject *modulus)
{
    if (modulus == Py_None) {
        return binary_slot(self, other, offsetof(PyNumberMethods, nb_power),
                           (void *)slot_nb_power, LU_ID(__pow__), LU_ID(__rpow__));
    }
    if (Py_TYPE(self)->tp_as_number != NULL && Py_TYPE(self)->tp_as_number->nb_power == slot_nb_power) {
        PyObject *args[2] = { other, modulus };
        return lu_call_special(self, LU_ID(__pow__), args, 2);
    }
    return Py_NewRef(Py_NotImplemented);
}

/* The in-place forms and the unary slots call one special method. */
#define INPLACE_THUNK(fn, dunder) \
    static PyObject *fn(PyObject *a, PyObject *b) \
    { return lu_call_special(a, LU_ID(dunder), &b, 1); }
INPLACE_THUNK(slot_nb_inplace_add, __iadd__)
INPLACE_THUNK(slot_nb_inplace_subtract, __isub__)
INPLACE_THUNK(slot_nb_inplace_multiply, __imul__)
INPLACE_THUNK(slot_nb_inplace_remainder, __imod__)
INPLACE_THUNK(slot_nb_inplace_lshift, __ilshift__)
INPLACE_THUNK(slot_nb_inplace_rshift, __irshift__)
INPLACE_THUNK(slot_nb_inplace_and, __iand__)
INPLACE_THUNK(slot_nb_inplace_xor, __ixor__)
INPLACE_THUNK(slot_nb_inplace_or, __ior__)
INPLACE_THUNK(slot_nb_inplace_floor_divide, __ifloordiv__)
INPLACE_THUNK(slot_nb_inplace_true_divide, __itruediv__)
INPLACE_THUNK(slot_nb_inplace_matrix_multiply, __imatmul__)

static PyObject *
slot_nb_inplace_power(PyObject *self, PyObject *other, PyObject *modulus)
{
    return lu_call_special(self, LU_ID(__ipow__), &other, 1);
}

#define UNARY_THUNK(fn, dunder) \
    static PyObject *fn(PyObject *a) { return lu_call_special(a, LU_ID(dunder), NULL, 0); }
UNARY_THUNK(slot_nb_negative, __neg__)
UNARY_THUNK(slot_nb_positive, __pos__)
UNARY_THUNK(slot_nb_absolute, __abs__)
UNARY_THUNK(slot_nb_invert, __invert__)
UNARY_THUNK(slot_nb_int, __int__)
UNARY_THUNK(slot_nb_float, __float__)
UNARY_THUNK(slot_nb_index, __index__)

static void slot_bf_releasebuffer(PyObject *self, Py_buffer *view);

/* bf_getbuffer of a class that defines __buffer__ (PEP 688): the memoryview it
   returns exports the buffer, wrapped so that its release reaches the class. */
static int
slot_bf_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    PyObject *name = PyUnicode_InternFromString("__buffer__");
    PyObject *arg = PyLong_FromLong(flags);
    PyObject *mv = name != NULL && arg != NULL ? lu_call_special(self, name, &arg, 1) : NULL;
    Py_XDECREF(name);
    Py_XDECREF(arg);
    if (mv == NULL) {
        return -1;
    }
    if (!PyMemoryView_Check(mv)) {
        lu_err_format(PyExc_TypeError, "__buffer__ returned non-memoryview object");
        Py_DECREF(mv);
        return -1;
    }
    if (PyObject_GetBuffer(mv, view, flags) < 0) {
        Py_DECREF(mv);
        return -1;
    }
    lu_buffer_wrapper *w = (lu_buffer_wrapper *)lu_object_new(&lu_buffer_wrapper_type, sizeof *w);
    if (w == NULL) {
        PyBuffer_Release(view);
        Py_DECREF(mv);
        return -1;
    }
    w->mv = mv;
    w->obj = Py_NewRef(self);
    Py_SETREF(view->obj, (PyObject *)w);
    return 0;
}

/* The wrapper's release: the memoryview's export goes, then the class's
   __release_buffer__ runs with that memoryview, an exception in it unraisable
   and one in flight kept (releasebuffer_call_python). */
static void
bufwrap_release(PyObject *op, Py_buffer *view)
{
    lu_buffer_wrapper *w = (lu_buffer_wrapper *)op;
    PyObject *mv = w->mv, *obj = w->obj;
    if (mv == NULL || obj == NULL) {
        return;                         /* already released */
    }
    w->mv = w->obj = NULL;
    PyBufferProcs *mb = Py_TYPE(mv)->tp_as_buffer;
    if (mb != NULL && mb->bf_releasebuffer != NULL) {
        mb->bf_releasebuffer(mv, view);
    }
    PyBufferProcs *pb = Py_TYPE(obj)->tp_as_buffer;
    if (pb != NULL && pb->bf_releasebuffer == slot_bf_releasebuffer) {
        PyObject *exc = PyErr_GetRaisedException();
        PyObject *name = PyUnicode_InternFromString("__release_buffer__");
        PyObject *r = name != NULL ? lu_call_special(obj, name, &mv, 1) : NULL;
        Py_XDECREF(name);
        if (r == NULL) {
            lu_err_unraisable_text("Exception ignored in __release_buffer__", obj);
        }
        Py_XDECREF(r);
        PyErr_SetRaisedException(exc);
    }
    Py_DECREF(mv);
    Py_DECREF(obj);
}

PyBufferProcs lu_buffer_wrapper_procs = { .bf_releasebuffer = bufwrap_release };

/* bf_releasebuffer of a class that defines __release_buffer__. The wrapper
   its own __buffer__ exports through calls the method; a buffer a native
   base exported is released by that base (releasebuffer_maybe_call_super). */
static void
slot_bf_releasebuffer(PyObject *self, Py_buffer *view)
{
    PyObject *mro = Py_TYPE(self)->tp_mro;
    for (Py_ssize_t i = 1; mro != NULL && i < PyTuple_GET_SIZE(mro); i++) {
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        PyBufferProcs *pb = base->tp_as_buffer;
        if (pb != NULL && pb->bf_releasebuffer != NULL && pb->bf_releasebuffer != slot_bf_releasebuffer) {
            pb->bf_releasebuffer(self, view);
            return;
        }
    }
}

/* sq_repeat of a builtin sequence: its __mul__. Heap types get none, as in CPython. */
static PyObject *
slot_sq_repeat(PyObject *self, Py_ssize_t n)
{
    PyObject *count = PyLong_FromSsize_t(n);
    PyObject *r = count != NULL ? lu_call_special(self, LU_ID(__mul__), &count, 1) : NULL;
    Py_XDECREF(count);
    return r;
}

/* The slot table: where each slot lives and its thunk. */
enum { IN_TYPE, IN_NUMBER, IN_MAPPING, IN_SEQUENCE, IN_ASYNC, IN_BUFFER };

struct slot {
    const char *name;
    uint8_t where;
    uint16_t offset;
    void *thunk;
};

#define TP(f, t) { #f, IN_TYPE, offsetof(PyTypeObject, f), (void *)(t) }
#define NB(f, t) { #f, IN_NUMBER, offsetof(PyNumberMethods, f), (void *)(t) }
#define MP(f, t) { #f, IN_MAPPING, offsetof(PyMappingMethods, f), (void *)(t) }
#define SQ(f, t) { #f, IN_SEQUENCE, offsetof(PySequenceMethods, f), (void *)(t) }
#define AM(f) { #f, IN_ASYNC, offsetof(PyAsyncMethods, f), NULL }
#define BF(f, t) { #f, IN_BUFFER, offsetof(PyBufferProcs, f), (void *)(t) }
#define NB_SLOTS(X) \
    X(nb_add, slot_nb_add) \
    X(nb_subtract, slot_nb_subtract) \
    X(nb_multiply, slot_nb_multiply) \
    X(nb_remainder, slot_nb_remainder) \
    X(nb_divmod, slot_nb_divmod) \
    X(nb_power, slot_nb_power) \
    X(nb_negative, slot_nb_negative) \
    X(nb_positive, slot_nb_positive) \
    X(nb_absolute, slot_nb_absolute) \
    X(nb_bool, slot_nb_bool) \
    X(nb_invert, slot_nb_invert) \
    X(nb_lshift, slot_nb_lshift) \
    X(nb_rshift, slot_nb_rshift) \
    X(nb_and, slot_nb_and) \
    X(nb_xor, slot_nb_xor) \
    X(nb_or, slot_nb_or) \
    X(nb_int, slot_nb_int) \
    X(nb_float, slot_nb_float) \
    X(nb_inplace_add, slot_nb_inplace_add) \
    X(nb_inplace_subtract, slot_nb_inplace_subtract) \
    X(nb_inplace_multiply, slot_nb_inplace_multiply) \
    X(nb_inplace_remainder, slot_nb_inplace_remainder) \
    X(nb_inplace_power, slot_nb_inplace_power) \
    X(nb_inplace_lshift, slot_nb_inplace_lshift) \
    X(nb_inplace_rshift, slot_nb_inplace_rshift) \
    X(nb_inplace_and, slot_nb_inplace_and) \
    X(nb_inplace_xor, slot_nb_inplace_xor) \
    X(nb_inplace_or, slot_nb_inplace_or) \
    X(nb_floor_divide, slot_nb_floor_divide) \
    X(nb_true_divide, slot_nb_true_divide) \
    X(nb_inplace_floor_divide, slot_nb_inplace_floor_divide) \
    X(nb_inplace_true_divide, slot_nb_inplace_true_divide) \
    X(nb_index, slot_nb_index) \
    X(nb_matrix_multiply, slot_nb_matrix_multiply) \
    X(nb_inplace_matrix_multiply, slot_nb_inplace_matrix_multiply) \

#define NB_ENTRY(f, t) NB(f, t),
static const struct slot slots[] = {
    TP(tp_getattr, NULL), TP(tp_setattr, NULL),
    TP(tp_getattro, lu_slot_tp_getattro), TP(tp_setattro, lu_slot_tp_setattro),
    TP(tp_repr, slot_tp_repr), TP(tp_hash, slot_tp_hash), TP(tp_call, lu_slot_tp_call),
    TP(tp_str, slot_tp_str), TP(tp_richcompare, slot_tp_richcompare),
    TP(tp_iter, slot_tp_iter), TP(tp_iternext, slot_tp_iternext),
    TP(tp_descr_get, slot_tp_descr_get), TP(tp_descr_set, slot_tp_descr_set),
    TP(tp_init, slot_tp_init), TP(tp_new, slot_tp_new), TP(tp_finalize, slot_tp_finalize),
    BF(bf_getbuffer, slot_bf_getbuffer), BF(bf_releasebuffer, slot_bf_releasebuffer), AM(am_await), AM(am_aiter), AM(am_anext),
    NB_SLOTS(NB_ENTRY)
    MP(mp_length, slot_length), MP(mp_subscript, slot_mp_subscript),
    MP(mp_ass_subscript, slot_mp_ass_subscript),
    SQ(sq_length, slot_length), SQ(sq_concat, NULL), SQ(sq_repeat, NULL), SQ(sq_item, slot_sq_item),
    SQ(sq_ass_item, slot_sq_ass_item), SQ(sq_contains, slot_sq_contains), SQ(sq_inplace_concat, NULL),
    SQ(sq_inplace_repeat, NULL),
};

int
lu_is_slot_thunk(void *f)
{
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++) {
        if (slots[i].thunk == f) {
            return 1;
        }
    }
    return 0;
}

/* The sub-structure a slot lives in, allocated for a static type on first
   use (heap types embed theirs). */
static char *
slot_base(PyTypeObject *tp, int where, int create)
{
    void **sub;
    size_t size;
    switch (where) {
    case IN_TYPE: return (char *)tp;
    case IN_NUMBER: sub = (void **)&tp->tp_as_number; size = sizeof(PyNumberMethods); break;
    case IN_MAPPING: sub = (void **)&tp->tp_as_mapping; size = sizeof(PyMappingMethods); break;
    case IN_SEQUENCE: sub = (void **)&tp->tp_as_sequence; size = sizeof(PySequenceMethods); break;
    case IN_ASYNC: sub = (void **)&tp->tp_as_async; size = sizeof(PyAsyncMethods); break;
    default: sub = (void **)&tp->tp_as_buffer; size = sizeof(PyBufferProcs); break;
    }
    if (*sub == NULL && create) {
        /* Process-wide, like the type: never freed. */
        *sub = PyMem_RawCalloc(1, size);
    }
    return *sub;
}

/* The thunk of each PyNumberMethods field, by its index. */
#define NB_THUNK(f, t) [offsetof(PyNumberMethods, f) / sizeof(void *)] = (void *)(t),
static void *const nb_thunks[sizeof(PyNumberMethods) / sizeof(void *)] = { NB_SLOTS(NB_THUNK) };

int
lu_nb_slot_kind(PyTypeObject *tp, size_t offset)
{
    void *fn = tp->tp_as_number != NULL ? *(void **)((char *)tp->tp_as_number + offset) : NULL;
    void *thunk = nb_thunks[offset / sizeof(void *)];
    return fn == NULL ? 0 : thunk == NULL || fn == thunk ? -1 : 1;
}

#define NSLOTS (int)(sizeof slots / sizeof slots[0])

static int set_slot(PyTypeObject *tp, const struct slot *s, int how, PyTypeObject *from);

int
lu_set_slot(PyTypeObject *tp, int index, int how, PyTypeObject *from)
{
    if (index < 0 || index >= NSLOTS) {
        lu_err_format(PyExc_SystemError, "set_slot: no slot %d", index);
        return -1;
    }
    const struct slot *s = &slots[index];
    /* An attribute slot is decided, written and its word stored in one
       step: see attr_lock. */
    int attribute = s->where == IN_TYPE
        && (s->offset == offsetof(PyTypeObject, tp_getattro) || s->offset == offsetof(PyTypeObject, tp_setattro));
    if (attribute) {
        PyMutex_Lock(&attr_lock);
    }
    int err = set_slot(tp, s, how, from);
    if (attribute && err == 0) {
        unsigned long which = s->offset == offsetof(PyTypeObject, tp_getattro) ? LU_TPFLAGS_GET_DECIDED
            : LU_TPFLAGS_SET_DECIDED;
        if (how == 1 && tp != &PyBaseObject_Type) {
            __atomic_fetch_or(&tp->tp_flags, which, __ATOMIC_RELAXED);
        }
        else {
            __atomic_fetch_and(&tp->tp_flags, ~which, __ATOMIC_RELAXED);
        }
    }
    if (attribute) {
        store_word(tp);
        PyMutex_Unlock(&attr_lock);
    }
    if (err == 0 && s->where == IN_TYPE) {
        lu_type_refresh_plain(tp);      /* tp_finalize, tp_del or tp_dealloc may be what changed */
    }
    return err;
}

static int
set_slot(PyTypeObject *tp, const struct slot *s, int how, PyTypeObject *from)
{
    void *value;
    switch (how) {
    case 0: value = NULL; break;
    case 1:
        value = s->thunk == (void *)slot_tp_new && (tp->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN)
            ? (void *)slot_tp_new_builtin
            : tp == &PyBaseObject_Type ? object_thunk(s->thunk) : attribute_thunk(tp, s->thunk);
        break;
    case 2: {
        char *src = from != NULL ? slot_base(from, s->where, 0) : NULL;
        value = src != NULL ? *(void **)(src + s->offset) : NULL;
        break;
    }
    case 3: value = (void *)PyObject_HashNotImplemented; break;
    case 4: value = (void *)slot_sq_repeat; break;
    default:
        lu_err_format(PyExc_SystemError, "set_slot: bad how %d", how);
        return -1;
    }
    if (how == 1 && value == NULL) {
        return 0;                       /* no thunk yet: the slot stays as it is */
    }
    char *base = slot_base(tp, s->where, value != NULL);
    if (base == NULL) {
        if (value == NULL) {
            return 0;
        }
        PyErr_NoMemory();
        return -1;
    }
    *(void **)(base + s->offset) = value;
    return 0;
}

/* Whether f is a tp_new that runs a kernel __new__ (a class's or builtin's), which a
   wrapper in the type's dict must not call back. */
int
lu_tp_new_is_thunk(newfunc f)
{
    return f == (newfunc)slot_tp_new || f == (newfunc)slot_tp_new_builtin;
}

Py_hash_t
PyObject_HashNotImplemented(PyObject *self)
{
    PyErr_Format(PyExc_TypeError, "unhashable type: '%.200s'", Py_TYPE(self)->tp_name);
    return -1;
}

/* slot_index(name): the index set_slot takes for the slot named so. */
static PyObject *
prim_slot_index(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const char *n = nargs == 1 && PyUnicode_Check(args[0]) ? PyUnicode_AsUTF8(args[0]) : NULL;
    for (int i = 0; n != NULL && i < NSLOTS; i++) {
        if (!strcmp(slots[i].name, n)) {
            return PyLong_FromLong(i);
        }
    }
    return PyErr_Occurred() ? NULL : (lu_err_format(PyExc_SystemError, "slot_index: no such slot"), NULL);
}

/* set_slot(type, index, how, from): lu_set_slot; `from` a type or None. */
static PyObject *
prim_set_slot(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long index, how;
    if (nargs != 4 || !PyType_Check(args[0]) || !PyLong_CheckExact(args[1]) || lu_long_as_long(args[1], &index) < 0
        || !PyLong_CheckExact(args[2]) || lu_long_as_long(args[2], &how) < 0
        || (args[3] != Py_None && !PyType_Check(args[3]))) {
        lu_err_format(PyExc_TypeError, "set_slot(type, index, how, from)");
        return NULL;
    }
    PyTypeObject *from = args[3] != Py_None ? (PyTypeObject *)args[3] : NULL;
    return lu_set_slot((PyTypeObject *)args[0], (int)index, (int)how, from) < 0
        ? NULL : Py_NewRef(Py_None);
}

/* update_slots(type, own_only, slotdefs): fixup_slot_dispatchers over
   slotdefs, _boot.SLOTS's (index, dunders) pairs. Each slot gets its thunk
   when one of its dunders is defined (in the type's own dict when own_only,
   for a static type, anywhere in its MRO for a class) and is inherited from
   tp_base otherwise; __hash__ = None makes the type unhashable. */
static PyObject *
prim_update_slots(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3 || !PyType_Check(args[0]) || !PyTuple_CheckExact(args[2])) {
        lu_err_format(PyExc_TypeError, "update_slots(type, own_only, slotdefs)");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[0];
    int own_only = PyObject_IsTrue(args[1]);
    PyObject *dict = lu_type_dict(tp);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(args[2]); i++) {
        PyObject *def = PyTuple_GET_ITEM(args[2], i), *names = PyTuple_GET_ITEM(def, 1);
        int index = (int)PyLong_AsLong(PyTuple_GET_ITEM(def, 0));
        PyObject *found = NULL;         /* the last dunder found decides, as in slotdefs order */
        for (Py_ssize_t j = 0; j < PyTuple_GET_SIZE(names); j++) {
            PyObject *name = PyTuple_GET_ITEM(names, j);
            PyObject *v = own_only ? lu_ns_get(dict, name) : _PyType_LookupRef(tp, name);
            if (v != NULL) {
                Py_XSETREF(found, v);
            }
            else if (PyErr_Occurred()) {
                Py_XDECREF(found);
                return NULL;
            }
        }
        int how = found == NULL ? (own_only ? -1 : 2)
            : found == Py_None && slots[index].where == IN_TYPE && slots[index].offset == offsetof(PyTypeObject, tp_hash) ? 3
            : 1;
        Py_XDECREF(found);
        if (how >= 0 && lu_set_slot(tp, index, how, tp->tp_base) < 0) {
            return NULL;
        }
    }
    return Py_NewRef(Py_None);
}

PyMethodDef lu_prim_slot_methods[] = {
    { "set_slot", (PyCFunction)(void (*)(void))prim_set_slot, METH_FASTCALL, NULL },
    { "slot_index", (PyCFunction)(void (*)(void))prim_slot_index, METH_FASTCALL, NULL },
    { "update_slots", (PyCFunction)(void (*)(void))prim_update_slots, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
