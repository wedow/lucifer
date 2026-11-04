/* The raised exception (Python/errors.c): one object in the task's
   current_exception (FOUNDATIONS 2.8). Native raising builds a
   BaseException-layout instance directly, as BaseException.__new__ does,
   because C must be able to raise before the kernel's exception classes
   exist. Attributes beyond args stay unset, as when the class is not
   initialised. */

#include "lucifer.h"
#include "interp/exceptions.h"

#include <stdarg.h>
#include <string.h>

/* An instance of `type` with the given args tuple (stolen). */
static PyObject *
exc_new(PyObject *type, PyObject *args)
{
    if (args == NULL) {
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)type;
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)(tp->tp_alloc != NULL
        ? tp->tp_alloc(tp, 0) : lu_object_new(tp, tp->tp_basicsize));
    if (e == NULL) {
        Py_DECREF(args);
        return NULL;
    }
    e->args = args;
    return (PyObject *)e;
}

void
PyErr_SetRaisedException(PyObject *exc)
{
    PyThreadState *ts = PyThreadState_Get();
    PyObject *old = ts->current_exception;
    ts->current_exception = exc;
    Py_XDECREF(old);
}

PyObject *
PyErr_GetRaisedException(void)
{
    PyThreadState *ts = PyThreadState_Get();
    PyObject *exc = ts->current_exception;
    ts->current_exception = NULL;
    return exc;
}

void
PyErr_Clear(void)
{
    PyErr_SetRaisedException(NULL);
}

PyObject *
PyErr_Occurred(void)
{
    PyThreadState *ts = PyThreadState_GetUnchecked();
    return ts != NULL && ts->current_exception != NULL
        ? (PyObject *)Py_TYPE(ts->current_exception) : NULL;
}

void
PyErr_SetObject(PyObject *type, PyObject *value)
{
    if (type != NULL && !PyExceptionClass_Check(type)) {
        lu_err_format(PyExc_SystemError, "PyErr_SetObject: exception is not a BaseException subclass");
        return;
    }
    if (value != NULL && PyExceptionInstance_Check(value)
        && PyErr_GivenExceptionMatches((PyObject *)Py_TYPE(value), type)) {
        Py_INCREF(value);
    }
    else {
        PyObject *args;
        if (value == NULL || value == Py_None) {
            args = PyTuple_New(0);
        }
        else if (PyTuple_CheckExact(value)) {
            args = Py_NewRef(value);
        }
        else {
            args = PyTuple_New(1);
            if (args != NULL) {
                PyTuple_SET_ITEM(args, 0, Py_NewRef(value));
            }
        }
        value = exc_new(type, args);
        if (value == NULL) {
            return;
        }
    }
    lu_exc_chain_context(value);
    PyErr_SetRaisedException(value);
}

void
PyErr_SetNone(PyObject *type)
{
    PyErr_SetObject(type, NULL);
}

void
PyErr_SetString(PyObject *type, const char *message)
{
    PyObject *value = PyUnicode_FromString(message);
    if (value != NULL) {
        PyErr_SetObject(type, value);
        Py_DECREF(value);
    }
}

/* A message formatted with printf conversions (not PyUnicode_FromFormat's). */
void
lu_err_format(PyObject *type, const char *format, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof buf, format, ap);
    va_end(ap);
    PyErr_SetString(type, buf);
}

PyObject *
PyErr_NoMemory(void)
{
    /* MemoryError carries no message, so raising it allocates only the instance. */
    PyErr_SetRaisedException(exc_new(PyExc_MemoryError, PyTuple_New(0)));
    return NULL;
}

int
PyErr_GivenExceptionMatches(PyObject *given, PyObject *exc)
{
    if (given == NULL || exc == NULL) {
        return 0;
    }
    if (PyTuple_Check(exc)) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(exc); i++) {
            if (PyErr_GivenExceptionMatches(given, PyTuple_GET_ITEM(exc, i))) {
                return 1;
            }
        }
        return 0;
    }
    if (PyExceptionInstance_Check(given)) {
        given = (PyObject *)Py_TYPE(given);
    }
    return PyType_Check(given) && PyType_Check(exc)
        && PyType_IsSubtype((PyTypeObject *)given, (PyTypeObject *)exc);
}

int
PyErr_ExceptionMatches(PyObject *exc)
{
    return PyErr_GivenExceptionMatches(PyErr_Occurred(), exc);
}

/* A new exception class, a real class over base (or Exception) whose dict
   carries __module__ from the name's prefix (Python/errors.c
   PyErr_NewException). */
PyObject *
PyErr_NewException(const char *name, PyObject *base, PyObject *dict)
{
    PyObject *mydict = NULL, *modulename = NULL, *bases = NULL, *result = NULL;
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        PyErr_SetString(PyExc_SystemError, "PyErr_NewException: name must be module.class");
        return NULL;
    }
    if (base == NULL) {
        base = PyExc_Exception;
    }
    if (dict == NULL) {
        dict = mydict = PyDict_New();
        if (dict == NULL) {
            return NULL;
        }
    }
    if (PyDict_GetItemString(dict, "__module__") == NULL) {
        modulename = PyUnicode_FromStringAndSize(name, dot - name);
        if (modulename == NULL || PyDict_SetItemString(dict, "__module__", modulename) < 0) {
            goto done;
        }
    }
    if (PyTuple_Check(base)) {
        bases = Py_NewRef(base);
    }
    else {
        bases = PyTuple_Pack(1, base);
        if (bases == NULL) {
            goto done;
        }
    }
    PyObject *clsname = PyUnicode_FromString(dot + 1);
    PyObject *args[3] = { clsname, bases, dict };
    result = clsname == NULL ? NULL : lu_call((PyObject *)&PyType_Type, args, 3, NULL);
    Py_XDECREF(clsname);
done:
    Py_XDECREF(bases);
    Py_XDECREF(mydict);
    Py_XDECREF(modulename);
    return result;
}

PyObject *
PyErr_NewExceptionWithDoc(const char *name, const char *doc, PyObject *base, PyObject *dict)
{
    PyObject *mydict = NULL;
    if (dict == NULL) {
        dict = mydict = PyDict_New();
        if (dict == NULL) {
            return NULL;
        }
    }
    if (doc != NULL) {
        PyObject *docobj = PyUnicode_FromString(doc);
        int err = docobj == NULL || PyDict_SetItemString(dict, "__doc__", docobj) < 0;
        Py_XDECREF(docobj);
        if (err) {
            Py_DECREF(mydict);
            return NULL;
        }
    }
    PyObject *result = PyErr_NewException(name, base, dict);
    Py_XDECREF(mydict);
    return result;
}

/* Without the faulthandler dump and the finalization CPython attempts. */
void
_Py_FatalErrorFunc(const char *func, const char *msg)
{
    lu_fh_fatal(func, msg);
}
