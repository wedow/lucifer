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
   type, bound unless it is a plain function or a kernel descriptor, then
   called with self first. */
static PyObject *
call_special_kw(PyObject *self, PyObject *name, PyObject *const *args, Py_ssize_t nargs,
                PyObject *kwnames)
{
    PyObject *descr = _PyType_Lookup(Py_TYPE(self), name);
    if (descr == NULL) {
        lu_err_format(PyExc_AttributeError, "'%.100s' object has no attribute '%s'",
                      Py_TYPE(self)->tp_name, PyUnicode_AsUTF8(name));
        return NULL;
    }
    Py_ssize_t total = nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    PyObject *small[8];
    PyObject **stack = total + 1 <= 8 ? small : PyMem_Malloc((total + 1) * sizeof(PyObject *));
    if (stack == NULL) {
        return PyErr_NoMemory();
    }
    PyObject *func, *bound_self, *result;
    int unbound = PyFunction_Check(descr) || lu_kernel_target(descr, &self, 1, &func, &bound_self) > 0;
    PyErr_Clear();
    if (unbound) {
        stack[0] = self;
        memcpy(stack + 1, args, total * sizeof(PyObject *));
        result = lu_call(descr, stack, nargs + 1, kwnames);
    }
    else {
        /* A descriptor: bind it through __get__ (kernel Python), then call. */
        PyObject *get = _PyType_Lookup(Py_TYPE(descr), LU_ID(__get__));
        PyObject *bound;
        if (get == NULL) {
            bound = Py_NewRef(descr);
        }
        else {
            PyObject *get_args[3] = { descr, self, (PyObject *)Py_TYPE(self) };
            bound = lu_call(get, get_args, 3, NULL);
        }
        result = bound != NULL ? lu_call(bound, args, nargs, kwnames) : NULL;
        Py_XDECREF(bound);
    }
    if (stack != small) {
        PyMem_Free(stack);
    }
    return result;
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
        PyObject *del = PyObject_GetAttr((PyObject *)Py_TYPE(self), LU_ID(__del__));
        PyErr_Clear();
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

/* bf_getbuffer of a class that defines __buffer__ (PEP 688): the memoryview it
   returns exports the buffer, and the caller releases through the view. */
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
    int r = PyObject_GetBuffer(mv, view, flags);
    Py_DECREF(mv);
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
static const struct slot slots[] = {
    TP(tp_getattr, NULL), TP(tp_setattr, NULL),
    TP(tp_getattro, lu_slot_tp_getattro), TP(tp_setattro, lu_slot_tp_setattro),
    TP(tp_repr, slot_tp_repr), TP(tp_hash, slot_tp_hash), TP(tp_call, lu_slot_tp_call),
    TP(tp_str, slot_tp_str), TP(tp_richcompare, slot_tp_richcompare),
    TP(tp_iter, slot_tp_iter), TP(tp_iternext, slot_tp_iternext),
    TP(tp_descr_get, slot_tp_descr_get), TP(tp_descr_set, slot_tp_descr_set),
    TP(tp_init, slot_tp_init), TP(tp_new, slot_tp_new), TP(tp_finalize, slot_tp_finalize),
    BF(bf_getbuffer, slot_bf_getbuffer), BF(bf_releasebuffer, NULL), AM(am_await), AM(am_aiter), AM(am_anext),
    NB(nb_add, slot_nb_add), NB(nb_subtract, slot_nb_subtract),
    NB(nb_multiply, slot_nb_multiply), NB(nb_remainder, slot_nb_remainder),
    NB(nb_divmod, slot_nb_divmod), NB(nb_power, slot_nb_power),
    NB(nb_negative, slot_nb_negative), NB(nb_positive, slot_nb_positive),
    NB(nb_absolute, slot_nb_absolute), NB(nb_bool, slot_nb_bool),
    NB(nb_invert, slot_nb_invert), NB(nb_lshift, slot_nb_lshift), NB(nb_rshift, slot_nb_rshift),
    NB(nb_and, slot_nb_and), NB(nb_xor, slot_nb_xor), NB(nb_or, slot_nb_or),
    NB(nb_int, slot_nb_int), NB(nb_float, slot_nb_float),
    NB(nb_inplace_add, slot_nb_inplace_add), NB(nb_inplace_subtract, slot_nb_inplace_subtract),
    NB(nb_inplace_multiply, slot_nb_inplace_multiply),
    NB(nb_inplace_remainder, slot_nb_inplace_remainder),
    NB(nb_inplace_power, slot_nb_inplace_power), NB(nb_inplace_lshift, slot_nb_inplace_lshift),
    NB(nb_inplace_rshift, slot_nb_inplace_rshift), NB(nb_inplace_and, slot_nb_inplace_and),
    NB(nb_inplace_xor, slot_nb_inplace_xor), NB(nb_inplace_or, slot_nb_inplace_or),
    NB(nb_floor_divide, slot_nb_floor_divide), NB(nb_true_divide, slot_nb_true_divide),
    NB(nb_inplace_floor_divide, slot_nb_inplace_floor_divide),
    NB(nb_inplace_true_divide, slot_nb_inplace_true_divide), NB(nb_index, slot_nb_index),
    NB(nb_matrix_multiply, slot_nb_matrix_multiply),
    NB(nb_inplace_matrix_multiply, slot_nb_inplace_matrix_multiply),
    MP(mp_length, slot_length), MP(mp_subscript, slot_mp_subscript),
    MP(mp_ass_subscript, slot_mp_ass_subscript),
    SQ(sq_length, slot_length), SQ(sq_concat, NULL), SQ(sq_repeat, NULL), SQ(sq_item, slot_sq_item),
    SQ(sq_ass_item, slot_sq_ass_item), SQ(sq_contains, slot_sq_contains), SQ(sq_inplace_concat, NULL),
    SQ(sq_inplace_repeat, NULL),
};

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

int
lu_set_slot(PyTypeObject *tp, PyObject *name, int how, PyTypeObject *from)
{
    const char *n = PyUnicode_AsUTF8(name);
    const struct slot *s = NULL;
    for (size_t i = 0; n != NULL && i < sizeof slots / sizeof slots[0]; i++) {
        if (!strcmp(slots[i].name, n)) {
            s = &slots[i];
        }
    }
    if (s == NULL) {
        lu_err_format(PyExc_SystemError, "set_slot: no slot '%s'", n != NULL ? n : "?");
        return -1;
    }
    void *value;
    switch (how) {
    case 0: value = NULL; break;
    case 1: value = s->thunk; break;
    case 2: {
        char *src = from != NULL ? slot_base(from, s->where, 0) : NULL;
        value = src != NULL ? *(void **)(src + s->offset) : NULL;
        break;
    }
    case 3: value = (void *)PyObject_HashNotImplemented; break;
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

Py_hash_t
PyObject_HashNotImplemented(PyObject *self)
{
    lu_err_format(PyExc_TypeError, "unhashable type: '%.200s'", Py_TYPE(self)->tp_name);
    return -1;
}

/* set_slot(type, slot, how, from): lu_set_slot; `from` a type or None. */
static PyObject *
prim_set_slot(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long how;
    if (nargs != 4 || !PyType_Check(args[0]) || !PyUnicode_Check(args[1])
        || !PyLong_CheckExact(args[2]) || lu_long_as_long(args[2], &how) < 0
        || (args[3] != Py_None && !PyType_Check(args[3]))) {
        lu_err_format(PyExc_TypeError, "set_slot(type, name, how, from)");
        return NULL;
    }
    PyTypeObject *from = args[3] != Py_None ? (PyTypeObject *)args[3] : NULL;
    return lu_set_slot((PyTypeObject *)args[0], args[1], (int)how, from) < 0
        ? NULL : Py_NewRef(Py_None);
}

PyMethodDef lu_prim_slot_methods[] = {
    { "set_slot", (PyCFunction)(void (*)(void))prim_set_slot, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
