/* Object initialization, calls and the attribute and item protocols from C
   (Objects/object.c, call.c, abstract.c). */

#include "interp/frame.h"

#include <stdarg.h>

PyObject *
PyObject_Init(PyObject *op, PyTypeObject *tp)
{
    if (op == NULL) {
        return PyErr_NoMemory();
    }
    Py_SET_TYPE(op, tp);
    if (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        Py_INCREF(tp);
    }
    _Py_NewReference(op);
    return op;
}

/* Calls from C (Objects/call.c): vectorcall where the type has it, else
   tp_call with a tuple and a dict. */

LU_EXPORT PyObject *
_PyObject_MakeTpCall(PyThreadState *ts, PyObject *callable, PyObject *const *args,
                     Py_ssize_t nargs, PyObject *kwnames)
{
    ternaryfunc call = Py_TYPE(callable)->tp_call;
    if (call == NULL) {
        lu_err_format(PyExc_TypeError, "'%.200s' object is not callable", Py_TYPE(callable)->tp_name);
        return NULL;
    }
    PyObject *tuple = PyTuple_New(nargs);
    if (tuple == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < nargs; i++) {
        PyTuple_SET_ITEM(tuple, i, Py_NewRef(args[i]));
    }
    PyObject *kwargs = NULL;
    Py_ssize_t nkw = kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0;
    if (nkw) {
        kwargs = PyDict_New();
        for (Py_ssize_t i = 0; kwargs != NULL && i < nkw; i++) {
            if (lu_ns_insert(kwargs, PyTuple_GET_ITEM(kwnames, i), args[nargs + i]) < 0) {
                Py_CLEAR(kwargs);
            }
        }
        if (kwargs == NULL) {
            Py_DECREF(tuple);
            return NULL;
        }
    }
    PyObject *result = call(callable, tuple, kwargs);
    Py_DECREF(tuple);
    Py_XDECREF(kwargs);
    return result;
}

PyObject *
lu_call(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    vectorcallfunc vector = PyVectorcall_Function(callable);
    if (vector != NULL) {
        return vector(callable, args, nargsf, kwnames);
    }
    return _PyObject_MakeTpCall(PyThreadState_Get(), callable, args, PyVectorcall_NARGS(nargsf),
                                kwnames);
}

PyObject *
PyObject_Vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    return lu_call(callable, args, nargsf, kwnames);
}

PyObject *
PyObject_CallOneArg(PyObject *callable, PyObject *arg)
{
    return lu_call(callable, &arg, 1, NULL);
}

PyObject *
PyObject_CallNoArgs(PyObject *callable)
{
    return lu_call(callable, NULL, 0, NULL);
}

PyObject *
PyObject_Call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    vectorcallfunc vector = PyVectorcall_Function(callable);
    if (vector == NULL) {
        ternaryfunc call = Py_TYPE(callable)->tp_call;
        if (call == NULL) {
            lu_err_format(PyExc_TypeError, "'%.200s' object is not callable",
                          Py_TYPE(callable)->tp_name);
            return NULL;
        }
        return call(callable, args, kwargs);
    }
    Py_ssize_t nargs;
    PyObject *kwnames;
    PyObject **stack = lu_unpack_call(args, kwargs != NULL && ((PyDictObject *)kwargs)->ma_used
                                      ? kwargs : NULL, &nargs, &kwnames);
    if (stack == NULL) {
        return NULL;
    }
    PyObject *result = vector(callable, stack, nargs, kwnames);
    Py_ssize_t total = nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_DECREF(stack[i]);
    }
    Py_XDECREF(kwnames);
    PyMem_Free(stack - 1);
    return result;
}

PyObject *
PyVectorcall_Call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    return PyObject_Call(callable, args, kwargs);
}

PyObject *
PyObject_CallFunctionObjArgs(PyObject *callable, ...)
{
    PyObject *stack[16];
    Py_ssize_t n = 0;
    va_list ap;
    va_start(ap, callable);
    for (PyObject *arg; (arg = va_arg(ap, PyObject *)) != NULL; ) {
        if (n == 16) {
            va_end(ap);
            lu_err_format(PyExc_SystemError, "PyObject_CallFunctionObjArgs: more than 16 arguments");
            return NULL;
        }
        stack[n++] = arg;
    }
    va_end(ap);
    return lu_call(callable, stack, n, NULL);
}

/* The attribute and item protocols (Objects/object.c, abstract.c): the
   type's slot, which for kernel types and classes runs kernel Python. */

PyObject *
PyObject_GetAttr(PyObject *v, PyObject *name)
{
    if (!PyUnicode_Check(name)) {
        lu_err_format(PyExc_TypeError, "attribute name must be string, not '%.200s'",
                      Py_TYPE(name)->tp_name);
        return NULL;
    }
    getattrofunc get = Py_TYPE(v)->tp_getattro;
    if (get == NULL) {
        lu_err_format(PyExc_AttributeError, "'%.100s' object has no attribute '%s'",
                      Py_TYPE(v)->tp_name, PyUnicode_AsUTF8(name));
        return NULL;
    }
    return get(v, name);
}

PyObject *
PyObject_GetAttrString(PyObject *v, const char *name)
{
    PyObject *s = PyUnicode_FromString(name);
    if (s == NULL) {
        return NULL;
    }
    PyObject *r = PyObject_GetAttr(v, s);
    Py_DECREF(s);
    return r;
}

int
PyObject_SetAttr(PyObject *v, PyObject *name, PyObject *value)
{
    if (!PyUnicode_Check(name)) {
        lu_err_format(PyExc_TypeError, "attribute name must be string, not '%.200s'",
                      Py_TYPE(name)->tp_name);
        return -1;
    }
    setattrofunc set = Py_TYPE(v)->tp_setattro;
    if (set == NULL) {
        lu_err_format(PyExc_TypeError, "'%.100s' object has no attributes (%s .%s)",
                      Py_TYPE(v)->tp_name, value == NULL ? "del" : "assign to",
                      PyUnicode_AsUTF8(name));
        return -1;
    }
    return set(v, name, value);
}

int
PyObject_DelAttr(PyObject *v, PyObject *name)
{
    return PyObject_SetAttr(v, name, NULL);
}

int
PyObject_SetAttrString(PyObject *v, const char *name, PyObject *value)
{
    PyObject *s = PyUnicode_FromString(name);
    if (s == NULL) {
        return -1;
    }
    int r = PyObject_SetAttr(v, s, value);
    Py_DECREF(s);
    return r;
}

PyObject *
PyObject_GenericGetAttr(PyObject *obj, PyObject *name)
{
    PyObject *args[2] = { obj, name };
    return lu_call_kernel(LU_K_generic_getattr, args, 2);
}

int
PyObject_GenericSetAttr(PyObject *obj, PyObject *name, PyObject *value)
{
    PyObject *args[3] = { obj, name, value != NULL ? value : LU_MISSING };
    PyObject *r = lu_call_kernel(LU_K_generic_setattr, args, 3);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

PyObject *
PyObject_GetItem(PyObject *o, PyObject *key)
{
    PyMappingMethods *m = Py_TYPE(o)->tp_as_mapping;
    if (m != NULL && m->mp_subscript != NULL) {
        return m->mp_subscript(o, key);
    }
    lu_err_format(PyExc_TypeError, "'%.200s' object is not subscriptable", Py_TYPE(o)->tp_name);
    return NULL;
}
