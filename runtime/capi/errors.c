/* The raised exception (Python/errors.c): one object in the task's
   current_exception (FOUNDATIONS 2.8). Native raising builds a
   BaseException-layout instance directly, as BaseException.__new__ does,
   because C must be able to raise before the kernel's exception classes
   exist; chaining and the subclass layouts arrive with K1k. */

#include "lucifer.h"

#include <stdarg.h>

extern PyTypeObject *const lu_exception_types[];

static void
exc_dealloc(PyObject *op)
{
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)op;
    Py_XDECREF(e->dict);
    Py_XDECREF(e->args);
    Py_XDECREF(e->notes);
    Py_XDECREF(e->traceback);
    Py_XDECREF(e->context);
    Py_XDECREF(e->cause);
    lu_object_free(op);
}

void
lu_exc_init(void)
{
    for (PyTypeObject *const *tp = lu_exception_types; *tp != NULL; tp++) {
        (*tp)->tp_basicsize = sizeof(PyBaseExceptionObject);
        (*tp)->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        (*tp)->tp_dealloc = exc_dealloc;
    }
}

/* An instance of `type` with the given args tuple (stolen). */
static PyObject *
exc_new(PyObject *type, PyObject *args)
{
    if (args == NULL) {
        return NULL;
    }
    PyBaseExceptionObject *e = (PyBaseExceptionObject *)lu_object_new(
        (PyTypeObject *)type, sizeof(PyBaseExceptionObject));
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
    if (value != NULL && PyExceptionInstance_Check(value)
        && PyErr_GivenExceptionMatches((PyObject *)Py_TYPE(value), type)) {
        PyErr_SetRaisedException(Py_NewRef(value));
        return;
    }
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
    PyObject *exc = exc_new(type, args);
    if (exc != NULL) {
        PyErr_SetRaisedException(exc);
    }
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
    /* The static types' bases are filled in by the kernel; until then each
       matches only itself. */
    for (PyTypeObject *tp = (PyTypeObject *)given; tp != NULL; tp = tp->tp_base) {
        if ((PyObject *)tp == exc) {
            return 1;
        }
    }
    return 0;
}

int
PyErr_ExceptionMatches(PyObject *exc)
{
    return PyErr_GivenExceptionMatches(PyErr_Occurred(), exc);
}

/* Without the faulthandler dump and the finalization CPython attempts. */
void
_Py_FatalErrorFunc(const char *func, const char *msg)
{
    fprintf(stderr, "Fatal Python error: %s: %s\n", func, msg);
    abort();
}
