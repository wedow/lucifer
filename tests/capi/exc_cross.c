/* Exceptions crossing the C boundary in both directions, as an unmodified
   multi-phase extension module (tests/capi/exc_cross.py drives it). */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

/* call(f, *args): f(*args); its exception passes through unchanged. */
static PyObject *
exc_call(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs < 1) {
        PyErr_SetString(PyExc_TypeError, "call(f, *args)");
        return NULL;
    }
    return PyObject_Vectorcall(args[0], args + 1, nargs - 1, NULL);
}

static int
check_nargs(const char *name, Py_ssize_t nargs, Py_ssize_t want)
{
    if (nargs != want) {
        PyErr_Format(PyExc_TypeError, "%s expects %zd arguments, got %zd", name, want, nargs);
        return -1;
    }
    return 0;
}

/* catch(f, exc_type): f(), a raise of exc_type is cleared and returned as the
   instance; any other propagates. */
static PyObject *
exc_catch(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (check_nargs("catch", nargs, 2) < 0) {
        return NULL;
    }
    PyObject *f = args[0], *type = args[1];
    PyObject *r = PyObject_CallNoArgs(f);
    if (r != NULL) {
        return r;
    }
    if (!PyErr_ExceptionMatches(type)) {
        return NULL;
    }
    PyObject *exc = PyErr_GetRaisedException();
    if (PyErr_Occurred() != NULL) {
        Py_DECREF(exc);
        PyErr_SetString(PyExc_SystemError, "error indicator still set after Get");
        return NULL;
    }
    return exc;
}

/* roundtrip(f): f(); on a raise, takes the exception and sets it again. */
static PyObject *
exc_roundtrip(PyObject *self, PyObject *f)
{
    PyObject *r = PyObject_CallNoArgs(f);
    if (r != NULL) {
        return r;
    }
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *again = PyErr_GetRaisedException();
    if (exc == NULL || again != NULL) {
        Py_XDECREF(again);
        Py_XDECREF(exc);
        PyErr_SetString(PyExc_SystemError, "Get did not clear the error indicator");
        return NULL;
    }
    PyErr_SetRaisedException(exc);
    return NULL;
}

/* raise_(type, message): PyErr_SetString. */
static PyObject *
exc_raise(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    const char *message = check_nargs("raise_", nargs, 2) < 0 ? NULL : PyUnicode_AsUTF8(args[1]);
    if (message == NULL) {
        return NULL;
    }
    PyErr_SetString(args[0], message);
    return NULL;
}

/* raise_fmt(type, text, n, obj): PyErr_Format's conversions. */
static PyObject *
exc_raise_fmt(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (check_nargs("raise_fmt", nargs, 4) < 0) {
        return NULL;
    }
    const char *text = PyUnicode_AsUTF8(args[1]);
    Py_ssize_t n = PyLong_AsSsize_t(args[2]);
    PyObject *type = args[0], *obj = args[3];
    if (text == NULL || (n == -1 && PyErr_Occurred())) {
        return NULL;
    }
    PyErr_Format(type, "%s: %zd items, %d%%, %.3s, %U, %S, %R, %c", text, n, 42, "abcdef", obj, obj, obj, 'x');
    return NULL;
}

/* raise_instance(exc): PyErr_SetObject with an instance. */
static PyObject *
exc_raise_instance(PyObject *self, PyObject *exc)
{
    PyErr_SetObject((PyObject *)Py_TYPE(exc), exc);
    return NULL;
}

/* occurred(): whether the error indicator is set (never, between calls). */
static PyObject *
exc_occurred(PyObject *self, PyObject *unused)
{
    if (PyErr_Occurred() != NULL) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

/* swallow(f): f(), and an exception it raises is cleared; returns whether one was. */
static PyObject *
exc_swallow(PyObject *self, PyObject *f)
{
    PyObject *r = PyObject_CallNoArgs(f);
    if (r != NULL) {
        Py_DECREF(r);
        Py_RETURN_FALSE;
    }
    PyErr_Clear();
    Py_RETURN_TRUE;
}

/* nest(f, depth): C calls Python calls C ... depth times, then f(). */
static PyObject *
exc_nest(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *f = args[0];
    Py_ssize_t depth = check_nargs("nest", nargs, 2) < 0 ? -1 : PyLong_AsSsize_t(args[1]);
    if (depth == -1 && PyErr_Occurred()) {
        return NULL;
    }
    if (depth == 0) {
        return PyObject_CallNoArgs(f);
    }
    PyObject *nest = PyObject_GetAttrString(self, "nest");
    PyObject *d = PyLong_FromSsize_t(depth - 1);
    PyObject *r = nest != NULL && d != NULL ? PyObject_CallFunctionObjArgs(nest, f, d, NULL) : NULL;
    Py_XDECREF(nest);
    Py_XDECREF(d);
    return r;
}

static PyMethodDef methods[] = {
    { "call", (PyCFunction)(void (*)(void))exc_call, METH_FASTCALL, NULL },
    { "catch", (PyCFunction)(void (*)(void))exc_catch, METH_FASTCALL, NULL },
    { "roundtrip", exc_roundtrip, METH_O, NULL },
    { "raise_", (PyCFunction)(void (*)(void))exc_raise, METH_FASTCALL, NULL },
    { "raise_fmt", (PyCFunction)(void (*)(void))exc_raise_fmt, METH_FASTCALL, NULL },
    { "raise_instance", exc_raise_instance, METH_O, NULL },
    { "occurred", exc_occurred, METH_NOARGS, NULL },
    { "swallow", exc_swallow, METH_O, NULL },
    { "nest", (PyCFunction)(void (*)(void))exc_nest, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};

static PyModuleDef_Slot slots[] = {
    { Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED },
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

static PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "exc_cross", "exceptions across C", 0, methods, slots, NULL, NULL, NULL,
};

PyMODINIT_FUNC
PyInit_exc_cross(void)
{
    return PyModuleDef_Init(&module);
}
