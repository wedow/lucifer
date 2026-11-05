/* The abstract object protocols for C callers (Objects/abstract.c, object.c):
   comparison and truth through the kernel's implementations, and the sequence
   and number protocols through the type's slots. */

#include "lucifer.h"

PyObject *
PyObject_RichCompare(PyObject *v, PyObject *w, int op)
{
    if (op < Py_LT || op > Py_GE) {
        PyErr_BadInternalCall();
        return NULL;
    }
    PyObject *args[3] = { v, w, PyLong_FromLong(op << 5) };
    if (args[2] == NULL) {
        return NULL;
    }
    PyObject *r = lu_call_kernel(LU_K_compare, args, 3);
    Py_DECREF(args[2]);
    return r;
}

int
PyObject_RichCompareBool(PyObject *v, PyObject *w, int op)
{
    if (v == w) {
        if (op == Py_EQ) {
            return 1;
        }
        if (op == Py_NE) {
            return 0;
        }
    }
    PyObject *res = PyObject_RichCompare(v, w, op);
    if (res == NULL) {
        return -1;
    }
    int ok = PyBool_Check(res) ? res == Py_True : lu_is_true(res);
    Py_DECREF(res);
    return ok;
}

int
PyObject_IsTrue(PyObject *v)
{
    return lu_is_true(v);
}

/* A NULL is its own "<NULL>" text in repr, str and ascii (Objects/object.c). */
PyObject *
PyObject_Repr(PyObject *v)
{
    return v == NULL ? PyUnicode_FromString("<NULL>") : lu_call_kernel(LU_K_repr, &v, 1);
}

PyObject *
PyObject_Str(PyObject *v)
{
    return v == NULL ? PyUnicode_FromString("<NULL>") : lu_call_kernel(LU_K_str, &v, 1);
}

PyObject *
PyObject_ASCII(PyObject *v)
{
    return v == NULL ? PyUnicode_FromString("<NULL>") : lu_call_kernel(LU_K_ascii, &v, 1);
}

Py_ssize_t
PySequence_Size(PyObject *s)
{
    if (s == NULL) {
        PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        return -1;
    }
    PySequenceMethods *m = Py_TYPE(s)->tp_as_sequence;
    if (m != NULL && m->sq_length != NULL) {
        return m->sq_length(s);
    }
    if (Py_TYPE(s)->tp_as_mapping != NULL && Py_TYPE(s)->tp_as_mapping->mp_length != NULL) {
        PyErr_Format(PyExc_TypeError, "%.200s is not a sequence", Py_TYPE(s)->tp_name);
    }
    else {
        PyErr_Format(PyExc_TypeError, "object of type '%.200s' has no len()", Py_TYPE(s)->tp_name);
    }
    return -1;
}

int
PyIndex_Check(PyObject *obj)
{
    PyNumberMethods *nb = Py_TYPE(obj)->tp_as_number;
    return nb != NULL && nb->nb_index != NULL;
}

/* An exact int for anything with __index__. */
LU_EXPORT PyObject *
_PyNumber_Index(PyObject *item)
{
    if (item == NULL) {
        PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        return NULL;
    }
    if (PyLong_Check(item)) {
        return Py_NewRef(item);
    }
    if (!PyIndex_Check(item)) {
        PyErr_Format(PyExc_TypeError, "'%.200s' object cannot be interpreted as an integer",
                     Py_TYPE(item)->tp_name);
        return NULL;
    }
    PyObject *result = Py_TYPE(item)->tp_as_number->nb_index(item);
    if (result == NULL) {
        return NULL;
    }
    if (!PyLong_CheckExact(result)) {
        if (!PyLong_Check(result)) {
            PyErr_Format(PyExc_TypeError, "__index__ returned non-int (type %.200s)",
                         Py_TYPE(result)->tp_name);
            Py_DECREF(result);
            return NULL;
        }
        /* Issue #17576: warn if 'result' not of exact type int. */
        if (PyErr_WarnFormat(PyExc_DeprecationWarning, 1,
                "__index__ returned non-int (type %.200s).  "
                "The ability to return an instance of a strict subclass of int "
                "is deprecated, and may be removed in a future version of Python.",
                Py_TYPE(result)->tp_name)) {
            Py_DECREF(result);
            return NULL;
        }
    }
    return result;
}

PyObject *
PyNumber_Index(PyObject *item)
{
    PyObject *result = _PyNumber_Index(item);
    if (result != NULL && !PyLong_CheckExact(result)) {
        Py_SETREF(result, _PyLong_Copy((PyLongObject *)result));
    }
    return result;
}

Py_ssize_t
PyNumber_AsSsize_t(PyObject *item, PyObject *err)
{
    PyObject *value = _PyNumber_Index(item);
    if (value == NULL) {
        return -1;
    }
    Py_ssize_t result = PyLong_AsSsize_t(value);
    if (result != -1 || !PyErr_Occurred()) {
        Py_DECREF(value);
        return result;
    }
    if (!PyErr_ExceptionMatches(PyExc_OverflowError)) {
        Py_DECREF(value);
        return -1;
    }
    PyErr_Clear();
    if (err == NULL) {      /* clamp to the nearest bound */
        result = (((PyLongObject *)value)->long_value.lv_tag & _PyLong_SIGN_MASK) == 2 ? PY_SSIZE_T_MIN : PY_SSIZE_T_MAX;
    }
    else {
        PyErr_Format(err, "cannot fit '%.200s' into an index-sized integer", Py_TYPE(item)->tp_name);
        result = -1;
    }
    Py_DECREF(value);
    return result;
}

LU_EXPORT PyObject *
_PyObject_CallMethod(PyObject *obj, PyObject *name, const char *format, ...)
{
    PyObject *method = PyObject_GetAttr(obj, name);
    if (method == NULL) {
        return NULL;
    }
    PyObject *stack[16], *result = NULL;
    Py_ssize_t n = 0;
    va_list ap;
    va_start(ap, format);
    PyObject *args = format != NULL && *format ? Py_VaBuildValue(format, ap) : PyTuple_New(0);
    va_end(ap);
    if (args != NULL) {
        if (!PyTuple_Check(args)) {
            /* A single item is not wrapped: "O" gives the object itself. */
            stack[0] = args;
            n = 1;
            result = lu_call(method, stack, n, NULL);
        }
        else {
            result = PyObject_Call(method, args, NULL);
        }
        Py_DECREF(args);
    }
    Py_DECREF(method);
    return result;
}

int
PyCallable_Check(PyObject *x)
{
    return x != NULL && Py_TYPE(x)->tp_call != NULL;
}
