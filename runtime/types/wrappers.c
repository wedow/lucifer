/* Slot wrappers (Objects/typeobject.c slotdefs and wrap_*): for a type with
   C slots of its own, the dunder methods that call them. PyType_Ready and
   PyType_FromSpec add these to the type's dict, so the kernel's special-method
   lookups (`a + b`, `len(a)`, `a < b`) find the C code. Copied from CPython's
   v3.14.10 typeobject.c, without the slot_* thunks, which kernel Python
   replaces for classes, and without the __buffer__ wrappers. */

#include "lucifer.h"

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <stddef.h>

static int
check_num_args(PyObject *ob, int n)
{
    if (!PyTuple_CheckExact(ob)) {
        PyErr_SetString(PyExc_SystemError,
            "PyArg_UnpackTuple() argument list is not a tuple");
        return 0;
    }
    if (n == PyTuple_GET_SIZE(ob))
        return 1;
    PyErr_Format(
        PyExc_TypeError,
        "expected %d argument%s, got %zd", n, n == 1 ? "" : "s", PyTuple_GET_SIZE(ob));
    return 0;
}

static Py_ssize_t
check_pow_args(PyObject *ob)
{
    // Returns the argument count on success or `-1` on error.
    int min = 1;
    int max = 2;
    if (!PyTuple_CheckExact(ob)) {
        PyErr_SetString(PyExc_SystemError,
            "PyArg_UnpackTuple() argument list is not a tuple");
        return -1;
    }
    Py_ssize_t size = PyTuple_GET_SIZE(ob);
    if (size >= min && size <= max) {
        return size;
    }
    PyErr_Format(
        PyExc_TypeError,
        "expected %d or %d arguments, got %zd", min, max, PyTuple_GET_SIZE(ob));
    return -1;
}

static PyObject *
wrap_lenfunc(PyObject *self, PyObject *args, void *wrapped)
{
    lenfunc func = (lenfunc)wrapped;
    Py_ssize_t res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyLong_FromSsize_t(res);
}

static PyObject *
wrap_inquirypred(PyObject *self, PyObject *args, void *wrapped)
{
    inquiry func = (inquiry)wrapped;
    int res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyBool_FromLong((long)res);
}

static PyObject *
wrap_binaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_l(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other);
}

static PyObject *
wrap_binaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
    binaryfunc func = (binaryfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(other, self);
}

static PyObject *
wrap_ternaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    ternaryfunc func = (ternaryfunc)wrapped;
    PyObject *other;
    PyObject *third = Py_None;

    /* Note: This wrapper only works for __pow__() */

    Py_ssize_t size = check_pow_args(args);
    if (size == -1) {
        return NULL;
    }
    other = PyTuple_GET_ITEM(args, 0);
    if (size == 2) {
       third = PyTuple_GET_ITEM(args, 1);
    }

    return (*func)(self, other, third);
}

static PyObject *
wrap_ternaryfunc_r(PyObject *self, PyObject *args, void *wrapped)
{
    ternaryfunc func = (ternaryfunc)wrapped;
    PyObject *other;
    PyObject *third = Py_None;

    /* Note: This wrapper only works for __rpow__() */

    Py_ssize_t size = check_pow_args(args);
    if (size == -1) {
        return NULL;
    }
    other = PyTuple_GET_ITEM(args, 0);
    if (size == 2) {
       third = PyTuple_GET_ITEM(args, 1);
    }

    return (*func)(other, self, third);
}

static PyObject *
wrap_unaryfunc(PyObject *self, PyObject *args, void *wrapped)
{
    unaryfunc func = (unaryfunc)wrapped;

    if (!check_num_args(args, 0))
        return NULL;
    return (*func)(self);
}

static PyObject *
wrap_indexargfunc(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeargfunc func = (ssizeargfunc)wrapped;
    PyObject* o;
    Py_ssize_t i;

    if (!check_num_args(args, 1))
        return NULL;
    o = PyTuple_GET_ITEM(args, 0);
    i = PyNumber_AsSsize_t(o, PyExc_OverflowError);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    return (*func)(self, i);
}

static Py_ssize_t
getindex(PyObject *self, PyObject *arg)
{
    Py_ssize_t i;

    i = PyNumber_AsSsize_t(arg, PyExc_OverflowError);
    if (i == -1 && PyErr_Occurred())
        return -1;
    if (i < 0) {
        PySequenceMethods *sq = Py_TYPE(self)->tp_as_sequence;
        if (sq && sq->sq_length) {
            Py_ssize_t n = (*sq->sq_length)(self);
            if (n < 0) {
                assert(PyErr_Occurred());
                return -1;
            }
            i += n;
        }
    }
    return i;
}

static PyObject *
wrap_sq_item(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeargfunc func = (ssizeargfunc)wrapped;
    PyObject *arg;
    Py_ssize_t i;

    if (PyTuple_GET_SIZE(args) == 1) {
        arg = PyTuple_GET_ITEM(args, 0);
        i = getindex(self, arg);
        if (i == -1 && PyErr_Occurred())
            return NULL;
        return (*func)(self, i);
    }
    check_num_args(args, 1);
    assert(PyErr_Occurred());
    return NULL;
}

static PyObject *
wrap_sq_setitem(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeobjargproc func = (ssizeobjargproc)wrapped;
    Py_ssize_t i;
    int res;
    PyObject *arg, *value;

    if (!PyArg_UnpackTuple(args, "__setitem__", 2, 2, &arg, &value))
        return NULL;
    i = getindex(self, arg);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    res = (*func)(self, i, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_sq_delitem(PyObject *self, PyObject *args, void *wrapped)
{
    ssizeobjargproc func = (ssizeobjargproc)wrapped;
    Py_ssize_t i;
    int res;
    PyObject *arg;

    if (!check_num_args(args, 1))
        return NULL;
    arg = PyTuple_GET_ITEM(args, 0);
    i = getindex(self, arg);
    if (i == -1 && PyErr_Occurred())
        return NULL;
    res = (*func)(self, i, NULL);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

/* XXX objobjproc is a misnomer; should be objargpred */
static PyObject *
wrap_objobjproc(PyObject *self, PyObject *args, void *wrapped)
{
    objobjproc func = (objobjproc)wrapped;
    int res;
    PyObject *value;

    if (!check_num_args(args, 1))
        return NULL;
    value = PyTuple_GET_ITEM(args, 0);
    res = (*func)(self, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    else
        return PyBool_FromLong(res);
}

static PyObject *
wrap_objobjargproc(PyObject *self, PyObject *args, void *wrapped)
{
    objobjargproc func = (objobjargproc)wrapped;
    int res;
    PyObject *key, *value;

    if (!PyArg_UnpackTuple(args, "__setitem__", 2, 2, &key, &value))
        return NULL;
    res = (*func)(self, key, value);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_delitem(PyObject *self, PyObject *args, void *wrapped)
{
    objobjargproc func = (objobjargproc)wrapped;
    int res;
    PyObject *key;

    if (!check_num_args(args, 1))
        return NULL;
    key = PyTuple_GET_ITEM(args, 0);
    res = (*func)(self, key, NULL);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_setattr(PyObject *self, PyObject *args, void *wrapped)
{
    setattrofunc func = (setattrofunc)wrapped;
    PyObject *name, *value;

    if (!PyArg_UnpackTuple(args, "__setattr__", 2, 2, &name, &value))
        return NULL;
    if (func(self, name, value) < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_delattr(PyObject *self, PyObject *args, void *wrapped)
{
    setattrofunc func = (setattrofunc)wrapped;

    if (!check_num_args(args, 1))
        return NULL;
    if (func(self, PyTuple_GET_ITEM(args, 0), NULL) < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_hashfunc(PyObject *self, PyObject *args, void *wrapped)
{
    hashfunc func = (hashfunc)wrapped;
    Py_hash_t res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == -1 && PyErr_Occurred())
        return NULL;
    return PyLong_FromSsize_t(res);
}

static PyObject *
wrap_call(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
    ternaryfunc func = (ternaryfunc)wrapped;

    return (*func)(self, args, kwds);
}

static PyObject *
wrap_del(PyObject *self, PyObject *args, void *wrapped)
{
    destructor func = (destructor)wrapped;

    if (!check_num_args(args, 0))
        return NULL;

    (*func)(self);
    Py_RETURN_NONE;
}

static PyObject *
wrap_richcmpfunc(PyObject *self, PyObject *args, void *wrapped, int op)
{
    richcmpfunc func = (richcmpfunc)wrapped;
    PyObject *other;

    if (!check_num_args(args, 1))
        return NULL;
    other = PyTuple_GET_ITEM(args, 0);
    return (*func)(self, other, op);
}

#undef RICHCMP_WRAPPER
#define RICHCMP_WRAPPER(NAME, OP) \
static PyObject * \
richcmp_##NAME(PyObject *self, PyObject *args, void *wrapped) \
{ \
    return wrap_richcmpfunc(self, args, wrapped, OP); \
}

RICHCMP_WRAPPER(lt, Py_LT)
RICHCMP_WRAPPER(le, Py_LE)
RICHCMP_WRAPPER(eq, Py_EQ)
RICHCMP_WRAPPER(ne, Py_NE)
RICHCMP_WRAPPER(gt, Py_GT)
RICHCMP_WRAPPER(ge, Py_GE)

static PyObject *
wrap_next(PyObject *self, PyObject *args, void *wrapped)
{
    unaryfunc func = (unaryfunc)wrapped;
    PyObject *res;

    if (!check_num_args(args, 0))
        return NULL;
    res = (*func)(self);
    if (res == NULL && !PyErr_Occurred())
        PyErr_SetNone(PyExc_StopIteration);
    return res;
}

static PyObject *
wrap_descr_get(PyObject *self, PyObject *args, void *wrapped)
{
    descrgetfunc func = (descrgetfunc)wrapped;
    PyObject *obj;
    PyObject *type = NULL;

    if (!PyArg_UnpackTuple(args, "__get__", 1, 2, &obj, &type))
        return NULL;
    if (obj == Py_None)
        obj = NULL;
    if (type == Py_None)
        type = NULL;
    if (type == NULL && obj == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "__get__(None, None) is invalid");
        return NULL;
    }
    return (*func)(self, obj, type);
}

static PyObject *
wrap_descr_set(PyObject *self, PyObject *args, void *wrapped)
{
    descrsetfunc func = (descrsetfunc)wrapped;
    PyObject *obj, *value;
    int ret;

    if (!PyArg_UnpackTuple(args, "__set__", 2, 2, &obj, &value))
        return NULL;
    ret = (*func)(self, obj, value);
    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_descr_delete(PyObject *self, PyObject *args, void *wrapped)
{
    descrsetfunc func = (descrsetfunc)wrapped;
    PyObject *obj;
    int ret;

    if (!check_num_args(args, 1))
        return NULL;
    obj = PyTuple_GET_ITEM(args, 0);
    ret = (*func)(self, obj, NULL);
    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

static PyObject *
wrap_init(PyObject *self, PyObject *args, void *wrapped, PyObject *kwds)
{
    initproc func = (initproc)wrapped;

    if (func(self, args, kwds) < 0)
        return NULL;
    Py_RETURN_NONE;
}

/* The table. Offsets are into PyHeapTypeObject, whose number, mapping and
   sequence tables follow the type object. */

#define TPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    {#NAME, offsetof(PyTypeObject, SLOT), NULL, WRAPPER, DOC}
#define FLSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC, FLAGS) \
    {#NAME, offsetof(PyTypeObject, SLOT), NULL, WRAPPER, DOC, FLAGS}
#define ETSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    {#NAME, offsetof(PyHeapTypeObject, SLOT), NULL, WRAPPER, DOC}
#define BUFSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_buffer.SLOT, FUNCTION, NULL, DOC)
#define AMSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_async.SLOT, FUNCTION, WRAPPER, DOC)
#define SQSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_sequence.SLOT, FUNCTION, WRAPPER, DOC)
#define MPSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_mapping.SLOT, FUNCTION, WRAPPER, DOC)
#define NBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, DOC)
#define UNSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
           #NAME "($self, /)\n--\n\n" DOC)
#define IBSLOT(NAME, SLOT, FUNCTION, WRAPPER, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, WRAPPER, \
           #NAME "($self, value, /)\n--\n\nReturn self" DOC "value.")
#define BINSLOT(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
           #NAME "($self, value, /)\n--\n\nReturn self" DOC "value.")
#define RBINSLOT(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
           #NAME "($self, value, /)\n--\n\nReturn value" DOC "self.")
#define BINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_l, \
           #NAME "($self, value, /)\n--\n\n" DOC)
#define RBINSLOTNOTINFIX(NAME, SLOT, FUNCTION, DOC) \
    ETSLOT(NAME, as_number.SLOT, FUNCTION, wrap_binaryfunc_r, \
           #NAME "($self, value, /)\n--\n\n" DOC)

static struct wrapperbase slotdefs[] = {
    TPSLOT(__getattribute__, tp_getattr, NULL, NULL, ""),
    TPSLOT(__getattr__, tp_getattr, NULL, NULL, ""),
    TPSLOT(__setattr__, tp_setattr, NULL, NULL, ""),
    TPSLOT(__delattr__, tp_setattr, NULL, NULL, ""),
    TPSLOT(__repr__, tp_repr, slot_tp_repr, wrap_unaryfunc,
           "__repr__($self, /)\n--\n\nReturn repr(self)."),
    TPSLOT(__hash__, tp_hash, slot_tp_hash, wrap_hashfunc,
           "__hash__($self, /)\n--\n\nReturn hash(self)."),
    FLSLOT(__call__, tp_call, slot_tp_call,
           (wrapperfunc)(void (*)(void))wrap_call,
           "__call__($self, /, *args, **kwargs)\n--\n\nCall self as a function.",
           PyWrapperFlag_KEYWORDS),
    TPSLOT(__str__, tp_str, slot_tp_str, wrap_unaryfunc,
           "__str__($self, /)\n--\n\nReturn str(self)."),
    TPSLOT(__getattribute__, tp_getattro, _Py_slot_tp_getattr_hook,
           wrap_binaryfunc,
           "__getattribute__($self, name, /)\n--\n\nReturn getattr(self, name)."),
    TPSLOT(__getattr__, tp_getattro, _Py_slot_tp_getattr_hook, NULL,
           "__getattr__($self, name, /)\n--\n\nImplement getattr(self, name)."),
    TPSLOT(__setattr__, tp_setattro, slot_tp_setattro, wrap_setattr,
           "__setattr__($self, name, value, /)\n--\n\nImplement setattr(self, name, value)."),
    TPSLOT(__delattr__, tp_setattro, slot_tp_setattro, wrap_delattr,
           "__delattr__($self, name, /)\n--\n\nImplement delattr(self, name)."),
    TPSLOT(__lt__, tp_richcompare, slot_tp_richcompare, richcmp_lt,
           "__lt__($self, value, /)\n--\n\nReturn self<value."),
    TPSLOT(__le__, tp_richcompare, slot_tp_richcompare, richcmp_le,
           "__le__($self, value, /)\n--\n\nReturn self<=value."),
    TPSLOT(__eq__, tp_richcompare, slot_tp_richcompare, richcmp_eq,
           "__eq__($self, value, /)\n--\n\nReturn self==value."),
    TPSLOT(__ne__, tp_richcompare, slot_tp_richcompare, richcmp_ne,
           "__ne__($self, value, /)\n--\n\nReturn self!=value."),
    TPSLOT(__gt__, tp_richcompare, slot_tp_richcompare, richcmp_gt,
           "__gt__($self, value, /)\n--\n\nReturn self>value."),
    TPSLOT(__ge__, tp_richcompare, slot_tp_richcompare, richcmp_ge,
           "__ge__($self, value, /)\n--\n\nReturn self>=value."),
    TPSLOT(__iter__, tp_iter, slot_tp_iter, wrap_unaryfunc,
           "__iter__($self, /)\n--\n\nImplement iter(self)."),
    TPSLOT(__next__, tp_iternext, slot_tp_iternext, wrap_next,
           "__next__($self, /)\n--\n\nImplement next(self)."),
    TPSLOT(__get__, tp_descr_get, slot_tp_descr_get, wrap_descr_get,
           "__get__($self, instance, owner=None, /)\n--\n\nReturn an attribute of instance, which is of type owner."),
    TPSLOT(__set__, tp_descr_set, slot_tp_descr_set, wrap_descr_set,
           "__set__($self, instance, value, /)\n--\n\nSet an attribute of instance to value."),
    TPSLOT(__delete__, tp_descr_set, slot_tp_descr_set,
           wrap_descr_delete,
           "__delete__($self, instance, /)\n--\n\nDelete an attribute of instance."),
    FLSLOT(__init__, tp_init, slot_tp_init,
           (wrapperfunc)(void (*)(void))wrap_init,
           "__init__($self, /, *args, **kwargs)\n--\n\n"
           "Initialize self.  See help(type(self)) for accurate signature.",
           PyWrapperFlag_KEYWORDS),
    TPSLOT(__new__, tp_new, slot_tp_new, NULL,
           "__new__(type, /, *args, **kwargs)\n--\n\n"
           "Create and return new object.  See help(type) for accurate signature."),
    TPSLOT(__del__, tp_finalize, slot_tp_finalize, (wrapperfunc)wrap_del,
           "__del__($self, /)\n--\n\n"
           "Called when the instance is about to be destroyed."),

    BUFSLOT(__buffer__, bf_getbuffer, slot_bf_getbuffer, wrap_buffer,
            "__buffer__($self, flags, /)\n--\n\n"
            "Return a buffer object that exposes the underlying memory of the object."),
    BUFSLOT(__release_buffer__, bf_releasebuffer, slot_bf_releasebuffer, wrap_releasebuffer,
            "__release_buffer__($self, buffer, /)\n--\n\n"
            "Release the buffer object that exposes the underlying memory of the object."),

    AMSLOT(__await__, am_await, slot_am_await, wrap_unaryfunc,
           "__await__($self, /)\n--\n\nReturn an iterator to be used in await expression."),
    AMSLOT(__aiter__, am_aiter, slot_am_aiter, wrap_unaryfunc,
           "__aiter__($self, /)\n--\n\nReturn an awaitable, that resolves in asynchronous iterator."),
    AMSLOT(__anext__, am_anext, slot_am_anext, wrap_unaryfunc,
           "__anext__($self, /)\n--\n\nReturn a value or raise StopAsyncIteration."),

    BINSLOT(__add__, nb_add, slot_nb_add,
           "+"),
    RBINSLOT(__radd__, nb_add, slot_nb_add,
           "+"),
    BINSLOT(__sub__, nb_subtract, slot_nb_subtract,
           "-"),
    RBINSLOT(__rsub__, nb_subtract, slot_nb_subtract,
           "-"),
    BINSLOT(__mul__, nb_multiply, slot_nb_multiply,
           "*"),
    RBINSLOT(__rmul__, nb_multiply, slot_nb_multiply,
           "*"),
    BINSLOT(__mod__, nb_remainder, slot_nb_remainder,
           "%"),
    RBINSLOT(__rmod__, nb_remainder, slot_nb_remainder,
           "%"),
    BINSLOTNOTINFIX(__divmod__, nb_divmod, slot_nb_divmod,
           "Return divmod(self, value)."),
    RBINSLOTNOTINFIX(__rdivmod__, nb_divmod, slot_nb_divmod,
           "Return divmod(value, self)."),
    NBSLOT(__pow__, nb_power, slot_nb_power, wrap_ternaryfunc,
           "__pow__($self, value, mod=None, /)\n--\n\nReturn pow(self, value, mod)."),
    NBSLOT(__rpow__, nb_power, slot_nb_power, wrap_ternaryfunc_r,
           "__rpow__($self, value, mod=None, /)\n--\n\nReturn pow(value, self, mod)."),
    UNSLOT(__neg__, nb_negative, slot_nb_negative, wrap_unaryfunc, "-self"),
    UNSLOT(__pos__, nb_positive, slot_nb_positive, wrap_unaryfunc, "+self"),
    UNSLOT(__abs__, nb_absolute, slot_nb_absolute, wrap_unaryfunc,
           "abs(self)"),
    UNSLOT(__bool__, nb_bool, slot_nb_bool, wrap_inquirypred,
           "True if self else False"),
    UNSLOT(__invert__, nb_invert, slot_nb_invert, wrap_unaryfunc, "~self"),
    BINSLOT(__lshift__, nb_lshift, slot_nb_lshift, "<<"),
    RBINSLOT(__rlshift__, nb_lshift, slot_nb_lshift, "<<"),
    BINSLOT(__rshift__, nb_rshift, slot_nb_rshift, ">>"),
    RBINSLOT(__rrshift__, nb_rshift, slot_nb_rshift, ">>"),
    BINSLOT(__and__, nb_and, slot_nb_and, "&"),
    RBINSLOT(__rand__, nb_and, slot_nb_and, "&"),
    BINSLOT(__xor__, nb_xor, slot_nb_xor, "^"),
    RBINSLOT(__rxor__, nb_xor, slot_nb_xor, "^"),
    BINSLOT(__or__, nb_or, slot_nb_or, "|"),
    RBINSLOT(__ror__, nb_or, slot_nb_or, "|"),
    UNSLOT(__int__, nb_int, slot_nb_int, wrap_unaryfunc,
           "int(self)"),
    UNSLOT(__float__, nb_float, slot_nb_float, wrap_unaryfunc,
           "float(self)"),
    IBSLOT(__iadd__, nb_inplace_add, slot_nb_inplace_add,
           wrap_binaryfunc, "+="),
    IBSLOT(__isub__, nb_inplace_subtract, slot_nb_inplace_subtract,
           wrap_binaryfunc, "-="),
    IBSLOT(__imul__, nb_inplace_multiply, slot_nb_inplace_multiply,
           wrap_binaryfunc, "*="),
    IBSLOT(__imod__, nb_inplace_remainder, slot_nb_inplace_remainder,
           wrap_binaryfunc, "%="),
    IBSLOT(__ipow__, nb_inplace_power, slot_nb_inplace_power,
           wrap_ternaryfunc, "**="),
    IBSLOT(__ilshift__, nb_inplace_lshift, slot_nb_inplace_lshift,
           wrap_binaryfunc, "<<="),
    IBSLOT(__irshift__, nb_inplace_rshift, slot_nb_inplace_rshift,
           wrap_binaryfunc, ">>="),
    IBSLOT(__iand__, nb_inplace_and, slot_nb_inplace_and,
           wrap_binaryfunc, "&="),
    IBSLOT(__ixor__, nb_inplace_xor, slot_nb_inplace_xor,
           wrap_binaryfunc, "^="),
    IBSLOT(__ior__, nb_inplace_or, slot_nb_inplace_or,
           wrap_binaryfunc, "|="),
    BINSLOT(__floordiv__, nb_floor_divide, slot_nb_floor_divide, "//"),
    RBINSLOT(__rfloordiv__, nb_floor_divide, slot_nb_floor_divide, "//"),
    BINSLOT(__truediv__, nb_true_divide, slot_nb_true_divide, "/"),
    RBINSLOT(__rtruediv__, nb_true_divide, slot_nb_true_divide, "/"),
    IBSLOT(__ifloordiv__, nb_inplace_floor_divide,
           slot_nb_inplace_floor_divide, wrap_binaryfunc, "//="),
    IBSLOT(__itruediv__, nb_inplace_true_divide,
           slot_nb_inplace_true_divide, wrap_binaryfunc, "/="),
    NBSLOT(__index__, nb_index, slot_nb_index, wrap_unaryfunc,
           "__index__($self, /)\n--\n\n"
           "Return self converted to an integer, if self is suitable "
           "for use as an index into a list."),
    BINSLOT(__matmul__, nb_matrix_multiply, slot_nb_matrix_multiply,
            "@"),
    RBINSLOT(__rmatmul__, nb_matrix_multiply, slot_nb_matrix_multiply,
             "@"),
    IBSLOT(__imatmul__, nb_inplace_matrix_multiply, slot_nb_inplace_matrix_multiply,
           wrap_binaryfunc, "@="),
    MPSLOT(__len__, mp_length, slot_mp_length, wrap_lenfunc,
           "__len__($self, /)\n--\n\nReturn len(self)."),
    MPSLOT(__getitem__, mp_subscript, slot_mp_subscript,
           wrap_binaryfunc,
           "__getitem__($self, key, /)\n--\n\nReturn self[key]."),
    MPSLOT(__setitem__, mp_ass_subscript, slot_mp_ass_subscript,
           wrap_objobjargproc,
           "__setitem__($self, key, value, /)\n--\n\nSet self[key] to value."),
    MPSLOT(__delitem__, mp_ass_subscript, slot_mp_ass_subscript,
           wrap_delitem,
           "__delitem__($self, key, /)\n--\n\nDelete self[key]."),

    SQSLOT(__len__, sq_length, slot_sq_length, wrap_lenfunc,
           "__len__($self, /)\n--\n\nReturn len(self)."),
    /* Heap types defining __add__/__mul__ have sq_concat/sq_repeat == NULL.
       The logic in abstract.c always falls back to nb_add/nb_multiply in
       this case.  Defining both the nb_* and the sq_* slots to call the
       user-defined methods has unexpected side-effects, as shown by
       test_descr.notimplemented() */
    SQSLOT(__add__, sq_concat, NULL, wrap_binaryfunc,
           "__add__($self, value, /)\n--\n\nReturn self+value."),
    SQSLOT(__mul__, sq_repeat, NULL, wrap_indexargfunc,
           "__mul__($self, value, /)\n--\n\nReturn self*value."),
    SQSLOT(__rmul__, sq_repeat, NULL, wrap_indexargfunc,
           "__rmul__($self, value, /)\n--\n\nReturn value*self."),
    SQSLOT(__getitem__, sq_item, slot_sq_item, wrap_sq_item,
           "__getitem__($self, key, /)\n--\n\nReturn self[key]."),
    SQSLOT(__setitem__, sq_ass_item, slot_sq_ass_item, wrap_sq_setitem,
           "__setitem__($self, key, value, /)\n--\n\nSet self[key] to value."),
    SQSLOT(__delitem__, sq_ass_item, slot_sq_ass_item, wrap_sq_delitem,
           "__delitem__($self, key, /)\n--\n\nDelete self[key]."),
    SQSLOT(__contains__, sq_contains, slot_sq_contains, wrap_objobjproc,
           "__contains__($self, key, /)\n--\n\nReturn bool(key in self)."),
    SQSLOT(__iadd__, sq_inplace_concat, NULL,
           wrap_binaryfunc,
           "__iadd__($self, value, /)\n--\n\nImplement self+=value."),
    SQSLOT(__imul__, sq_inplace_repeat, NULL,
           wrap_indexargfunc,
           "__imul__($self, value, /)\n--\n\nImplement self*=value."),

    {NULL}
};

/* The slot an offset into PyHeapTypeObject names, through the type's own tables. */
static void **
slotptr(PyTypeObject *type, int ioffset)
{
    char *ptr;
    long offset = ioffset;

    /* Note: this depends on the order of the members of PyHeapTypeObject! */
    if ((size_t)offset >= offsetof(PyHeapTypeObject, as_buffer)) {
        ptr = (char *)type->tp_as_buffer;
        offset -= offsetof(PyHeapTypeObject, as_buffer);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_sequence)) {
        ptr = (char *)type->tp_as_sequence;
        offset -= offsetof(PyHeapTypeObject, as_sequence);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_mapping)) {
        ptr = (char *)type->tp_as_mapping;
        offset -= offsetof(PyHeapTypeObject, as_mapping);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_number)) {
        ptr = (char *)type->tp_as_number;
        offset -= offsetof(PyHeapTypeObject, as_number);
    }
    else if ((size_t)offset >= offsetof(PyHeapTypeObject, as_async)) {
        ptr = (char *)type->tp_as_async;
        offset -= offsetof(PyHeapTypeObject, as_async);
    }
    else {
        ptr = (char *)type;
    }
    if (ptr != NULL)
        ptr += offset;
    return (void **)ptr;
}

/* Adds a wrapper descriptor to tp's dict for each slot tp has of its own
   (one its base does not have too) whose dunder the dict lacks, as
   add_operators does. */
int
lu_type_add_operators(PyTypeObject *tp)
{
    PyObject *dict = lu_type_dict(tp);
    for (struct wrapperbase *p = slotdefs; p->name != NULL; p++) {
        if (p->wrapper == NULL) {
            continue;
        }
        void **ptr = slotptr(tp, p->offset);
        if (ptr == NULL || *ptr == NULL) {
            continue;
        }
        void **base = tp->tp_base != NULL ? slotptr(tp->tp_base, p->offset) : NULL;
        if (base != NULL && *base == *ptr) {
            continue;
        }
        PyObject *name = PyUnicode_InternFromString(p->name);
        if (name == NULL) {
            return -1;
        }
        int err = 0;
        if (lu_ns_lookup(dict, name) == NULL) {
            PyObject *descr = *ptr == (void *)PyObject_HashNotImplemented
                ? Py_NewRef(Py_None) : PyDescr_NewWrapper(tp, p, *ptr);
            err = descr == NULL || lu_ns_insert(dict, name, descr) < 0;
            Py_XDECREF(descr);
        }
        Py_DECREF(name);
        if (err) {
            return -1;
        }
    }
    return 0;
}
