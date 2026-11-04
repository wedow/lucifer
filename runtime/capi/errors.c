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

/* The three-part form (type, value, traceback) the exception used to be
   kept in; value is the exception instance. */

void
PyErr_Fetch(PyObject **type, PyObject **value, PyObject **traceback)
{
    PyObject *exc = PyErr_GetRaisedException();
    *value = exc;
    *type = exc != NULL ? Py_NewRef((PyObject *)Py_TYPE(exc)) : NULL;
    *traceback = exc != NULL ? PyException_GetTraceback(exc) : NULL;
}

/* An instance of `type` made from `value`, as _PyErr_CreateException: the
   tuple is the arguments, None none, anything else the one argument. */
static PyObject *
create_exception(PyObject *type, PyObject *value)
{
    if (value == NULL || value == Py_None) {
        return PyObject_CallNoArgs(type);
    }
    return PyTuple_Check(value) ? PyObject_Call(type, value, NULL) : PyObject_CallOneArg(type, value);
}

/* The exception that (type, value) stand for, new; value stays as it was. */
static PyObject *
normalized(PyObject *type, PyObject *value)
{
    if (value != NULL && PyExceptionInstance_Check(value)
        && PyObject_IsSubclass((PyObject *)Py_TYPE(value), type) > 0) {
        return Py_NewRef(value);
    }
    PyObject *exc = create_exception(type, value);
    if (exc != NULL && !PyExceptionInstance_Check(exc)) {
        PyErr_Format(PyExc_TypeError, "calling %R should have returned an instance of BaseException, "
                     "not %s", type, Py_TYPE(exc)->tp_name);
        Py_CLEAR(exc);
    }
    return exc;
}

void
PyErr_NormalizeException(PyObject **type, PyObject **value, PyObject **traceback)
{
    if (*type == NULL) {
        return;
    }
    PyObject *exc = normalized(*type, *value);
    if (exc == NULL) {
        /* the error raised while normalizing replaces the pending one */
        PyObject *err = PyErr_GetRaisedException();
        Py_CLEAR(*type);
        Py_CLEAR(*value);
        Py_CLEAR(*traceback);
        *type = Py_NewRef((PyObject *)Py_TYPE(err));
        *value = err;
        *traceback = PyException_GetTraceback(err);
        return;
    }
    Py_XSETREF(*value, exc);
    Py_XSETREF(*type, Py_NewRef((PyObject *)Py_TYPE(exc)));
    if (*traceback != NULL) {
        PyException_SetTraceback(exc, *traceback);
    }
    else {
        *traceback = PyException_GetTraceback(exc);
    }
}

void
PyErr_Restore(PyObject *type, PyObject *value, PyObject *traceback)
{
    if (type == NULL) {
        Py_XDECREF(value);
        Py_XDECREF(traceback);
        PyErr_Clear();
        return;
    }
    PyObject *exc = normalized(type, value);
    Py_DECREF(type);
    Py_XDECREF(value);
    if (exc == NULL) {
        Py_XDECREF(traceback);
        return;
    }
    if (traceback != NULL) {
        PyException_SetTraceback(exc, traceback);
        Py_DECREF(traceback);
    }
    PyErr_SetRaisedException(exc);
}

PyObject *
PyErr_SetFromErrno(PyObject *exc)
{
    int err = errno;
    PyObject *message = PyUnicode_FromString(err ? strerror(err) : "Error");
    PyObject *args = message != NULL ? Py_BuildValue("(iO)", err, message) : NULL;
    PyObject *v = args != NULL ? PyObject_Call(exc, args, NULL) : NULL;
    if (v != NULL) {
        PyErr_SetObject((PyObject *)Py_TYPE(v), v);
        Py_DECREF(v);
    }
    Py_XDECREF(message);
    Py_XDECREF(args);
    return NULL;
}

/* warnings.warn(message, category, stacklevel). */
int
PyErr_WarnEx(PyObject *category, const char *text, Py_ssize_t stack_level)
{
    PyObject *message = PyUnicode_FromString(text);
    PyObject *warnings = message != NULL ? PyImport_ImportModule("warnings") : NULL;
    PyObject *r = NULL;
    if (warnings != NULL) {
        r = PyObject_CallMethod(warnings, "warn", "OOn", message, category != NULL ? category : PyExc_RuntimeWarning,
                                stack_level);
    }
    Py_XDECREF(message);
    Py_XDECREF(warnings);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PyErr_WarnFormat(PyObject *category, Py_ssize_t stack_level, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyObject *message = PyUnicode_FromFormatV(format, ap);
    va_end(ap);
    if (message == NULL) {
        return -1;
    }
    const char *text = PyUnicode_AsUTF8(message);
    int r = text != NULL ? PyErr_WarnEx(category, text, stack_level) : -1;
    Py_DECREF(message);
    return r;
}

void
PyErr_WriteUnraisable(PyObject *obj)
{
    lu_err_unraisable_text(NULL, obj);
}

/* PyErr_Print: SystemExit leaves the process; anything else goes to
   sys.excepthook, else the native renderer. */
void
PyErr_Print(void)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL) {
        return;
    }
    if (PyErr_GivenExceptionMatches(exc, PyExc_SystemExit)) {
        PyObject *code = PyObject_GetAttrString(exc, "code");
        long status = code == NULL || code == Py_None ? 0 : PyLong_Check(code) ? PyLong_AsLong(code) : 1;
        fflush(stdout);
        exit((int)status);
    }
    PyObject *hook = PySys_GetObject("excepthook");
    PyObject *tb = PyException_GetTraceback(exc);
    PyObject *args[3] = { (PyObject *)Py_TYPE(exc), exc, tb != NULL ? tb : Py_None };
    PyObject *r = hook != NULL ? lu_call(hook, args, 3, NULL) : NULL;
    if (r == NULL) {
        PyErr_Clear();
        lu_err_display(exc);
    }
    Py_XDECREF(r);
    Py_XDECREF(tb);
    Py_DECREF(exc);
}

int
PyArg_ValidateKeywordArguments(PyObject *kwargs)
{
    if (!PyDict_Check(kwargs)) {
        PyErr_BadInternalCall();
        return 0;
    }
    PyObject *key;
    for (Py_ssize_t pos = 0; PyDict_Next(kwargs, &pos, &key, NULL); ) {
        if (!PyUnicode_Check(key)) {
            PyErr_SetString(PyExc_TypeError, "keywords must be strings");
            return 0;
        }
    }
    return 1;
}
