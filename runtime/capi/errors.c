/* The raised exception (Python/errors.c): one object in the task's
   current_exception (FOUNDATIONS 2.8). Native raising builds a
   BaseException-layout instance directly, as BaseException.__new__ does,
   because C must be able to raise before the kernel's exception classes
   exist. Attributes beyond args stay unset, as when the class is not
   initialised. */

#include "lucifer.h"
#include "interp/exceptions.h"
#include "interp/frame.h"

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
    if (PyType_IsSubtype(tp, (PyTypeObject *)PyExc_SyntaxError)) {
        /* its __init__ reads msg, filename, lineno... out of the args */
        PyObject *exc = PyObject_Call(type, args, NULL);
        Py_DECREF(args);
        return exc;
    }
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

/* An instance of `type` made from `value`, as _PyErr_CreateException: the
   tuple is the arguments, None none, anything else the one argument. */
static PyObject *
create_exception(PyObject *type, PyObject *value)
{
    /* The class call runs Python at whatever depth the raise itself came
       from. At the recursion limit there is no room for it (and the limit's
       own RecursionError must not recurse back through here), so the
       instance is built directly, as before the kernel exists. */
    PyThreadState *ts = PyThreadState_Get();
    if (ts->py_recursion_remaining <= 0) {
        PyObject *args = value == NULL || value == Py_None ? PyTuple_New(0)
            : PyTuple_Check(value) ? Py_NewRef(value) : PyTuple_Pack(1, value);
        return args != NULL ? exc_new(type, args) : NULL;
    }
    PyObject *exc = value == NULL || value == Py_None
        ? PyObject_CallNoArgs(type)
        : PyTuple_Check(value) ? PyObject_Call(type, value, NULL) : PyObject_CallOneArg(type, value);
    return exc;
}

/* The exception raised while creating one becomes the raised exception, with
   a note of the creation that failed (Python/errors.c
   get_normalization_failure_note). */
static void
normalization_failure(PyObject *type, PyObject *value)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL || !PyExceptionInstance_Check(exc)) {
        Py_XDECREF(exc);
        return;
    }
    PyObject *args = PyObject_Repr(value);
    if (args == NULL) {
        PyErr_Clear();
        args = PyUnicode_FromString("<unknown>");
    }
    PyObject *note = args != NULL
        ? PyUnicode_FromFormat("Normalization failed: type=%s args=%S",
                               ((PyTypeObject *)type)->tp_name, args)
        : NULL;
    Py_XDECREF(args);
    if (note != NULL) {
        PyObject *r = PyObject_CallMethod(exc, "add_note", "O", note);
        Py_XDECREF(r);
        PyErr_Clear();          /* a note that failed is not raised either */
        Py_DECREF(note);
    }
    PyErr_SetRaisedException(exc);
}

void
PyErr_SetObject(PyObject *type, PyObject *value)
{
    if (type != NULL && !PyExceptionClass_Check(type)) {
        lu_err_format(PyExc_SystemError, "PyErr_SetObject: exception is not a BaseException subclass");
        return;
    }
    if (!PyInterpreterState_Get()->kernel_booted) {
        /* the kernel's classes cannot be called yet: build the instance */
        PyErr_Clear();
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
        if (args == NULL) {
            return;
        }
        PyObject *exc = exc_new(type, args);
        if (exc != NULL) {
            lu_exc_chain_context(exc);
            PyErr_SetRaisedException(exc);
        }
        return;
    }
    int is_subclass = 0;
    if (value != NULL && PyExceptionInstance_Check(value)) {
        is_subclass = PyObject_IsSubclass((PyObject *)Py_TYPE(value), type);
        if (is_subclass < 0) {
            return;
        }
    }
    if (is_subclass) {
        Py_INCREF(value);
    }
    else {
        /* Normalize the value now; the pending exception is cleared first,
           and one that fails to normalize becomes the raised exception
           (Python/errors.c _PyErr_SetObject). */
        PyErr_Clear();
        PyObject *fixed = create_exception(type, value);
        if (fixed == NULL) {
            normalization_failure(type, value);
            return;
        }
        value = fixed;
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

/* raising MemoryError must not need memory (Objects/exceptions.c): when
   no fresh instance can be made, the one made at boot is raised, as
   CPython's last_resort_memory_error is. Making the fresh one can itself
   run out and call back here, which takes the last resort at once. */
void
lu_errors_init(PyInterpreterState *interp)
{
    PyObject *args = PyTuple_New(0);
    interp->last_resort_memory_error = args != NULL ? exc_new(PyExc_MemoryError, args) : NULL;
    if (interp->last_resort_memory_error == NULL) {
        PyErr_Clear();
    }
}

PyObject *
PyErr_NoMemory(void)
{
    static _Thread_local int raising;
    if (raising) {
        PyErr_SetRaisedException(Py_XNewRef(_PyRuntime.main_interp.last_resort_memory_error));
        return NULL;
    }
    raising = 1;
    PyObject *args = PyTuple_New(0);
    PyObject *exc = args != NULL ? exc_new(PyExc_MemoryError, args) : NULL;
    raising = 0;
    if (exc == NULL) {
        PyErr_Clear();
        exc = Py_XNewRef(_PyRuntime.main_interp.last_resort_memory_error);
    }
    PyErr_SetRaisedException(exc);
    return NULL;
}

int
PyErr_BadArgument(void)
{
    PyErr_SetString(PyExc_TypeError, "bad argument type for built-in operation");
    return 0;
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
        int err = PyException_SetTraceback(exc, traceback);
        Py_DECREF(traceback);
        if (err < 0) {
            Py_DECREF(exc);
            return;             /* the TypeError of a bad traceback stays */
        }
    }
    PyErr_SetRaisedException(exc);
}

/* The errno error as an instance of `exc`, with the filenames when there are
   any (Python/errors.c PyErr_SetFromErrnoWithFilenameObjects). Calling `exc`
   lets OSError map errno to its subclasses. */
PyObject *
PyErr_SetFromErrnoWithFilenameObjects(PyObject *exc, PyObject *filenameObject, PyObject *filenameObject2)
{
    int err = errno;
    PyObject *message = PyUnicode_FromString(err ? strerror(err) : "Error");
    PyObject *args = NULL;
    if (message != NULL) {
        args = filenameObject == NULL
            ? Py_BuildValue("(iO)", err, message)
            : filenameObject2 != NULL
                ? Py_BuildValue("(iOOiO)", err, message, filenameObject, 0, filenameObject2)
                : Py_BuildValue("(iOO)", err, message, filenameObject);
    }
    Py_XDECREF(message);
    PyObject *v = args != NULL ? PyObject_Call(exc, args, NULL) : NULL;
    Py_XDECREF(args);
    if (v != NULL) {
        PyErr_SetObject((PyObject *)Py_TYPE(v), v);
        Py_DECREF(v);
    }
    return NULL;
}

PyObject *
PyErr_SetFromErrno(PyObject *exc)
{
    return PyErr_SetFromErrnoWithFilenameObjects(exc, NULL, NULL);
}

PyObject *
PyErr_SetFromErrnoWithFilenameObject(PyObject *exc, PyObject *filenameObject)
{
    return PyErr_SetFromErrnoWithFilenameObjects(exc, filenameObject, NULL);
}

PyObject *
PyErr_SetFromErrnoWithFilename(PyObject *exc, const char *filename)
{
    PyObject *name = filename != NULL ? PyUnicode_DecodeFSDefault(filename) : NULL;
    PyObject *r = PyErr_SetFromErrnoWithFilenameObjects(exc, name, NULL);
    Py_XDECREF(name);
    return r;
}

/* A warning the warnings module cannot decide, as during finalization:
   printed as _warnings.c's show_warning does, from the frame stack_level
   levels up (Python/_warnings.c setup_context). */
static void
warn_native(PyObject *category, const char *text, Py_ssize_t stack_level)
{
    struct lu_frame *f = lu_frame_user((struct lu_frame *)PyThreadState_Get()->current_frame,
                                       stack_level > 1 ? stack_level - 1 : 0);
    const char *file = "<sys>";
    int line = 0;
    if (f != NULL && f->code != NULL) {
        file = PyUnicode_AsUTF8(f->code->co_filename);
        line = PyCode_Addr2Line(f->code, (int)(lu_frame_lasti(f) * sizeof(lu_codeunit)));
    }
    char buf[512];
    int n = snprintf(buf, sizeof buf, "%s:%d: %s: %s\n", file, line > 0 ? line : 0,
                     category != NULL ? ((PyTypeObject *)category)->tp_name : "RuntimeWarning", text);
    if (n > 0) {
        lu_stderr_write(buf, (size_t)n);
    }
}

/* warnings.warn(message, category, stacklevel). */
int
PyErr_WarnEx(PyObject *category, const char *text, Py_ssize_t stack_level)
{
    PyObject *message = PyUnicode_FromString(text);
    if (message == NULL) {
        return -1;
    }
    PyObject *warnings = PyImport_ImportModule("warnings");
    int r = 0;
    if (warnings != NULL) {
        PyObject *result = PyObject_CallMethod(warnings, "warn", "OOn", message,
                                               category != NULL ? category : PyExc_RuntimeWarning,
                                               stack_level);
        Py_DECREF(warnings);
        r = result == NULL ? -1 : 0;
        Py_XDECREF(result);
    }
    else {
        PyErr_Clear();          /* the module cannot be imported: print natively */
        warn_native(category, text, stack_level);
    }
    Py_DECREF(message);
    return r;
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

/* The current exception handed to sys.unraisablehook (Python/errors.c
   format_unraisable_v): the object it gets carries exc_type, exc_value,
   exc_traceback, err_msg and object, with a traceback of the calling frame
   synthesized when the exception has none. Without a hook it is printed as
   write_unraisable_exc does. The exception is fetched before the message is
   formatted, so a formatting failure clears nothing of it. */
static void
format_unraisable_v(const char *format, va_list va, PyObject *obj)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL) {
        return;
    }
    if (((PyBaseExceptionObject *)exc)->traceback == NULL) {
        struct lu_frame *f = lu_frame_user((struct lu_frame *)PyThreadState_Get()->current_frame, 0);
        if (f != NULL) {
            PyErr_SetRaisedException(exc);
            lu_traceback_here(f, lu_frame_lasti(f));
            exc = PyErr_GetRaisedException();
        }
    }
    PyObject *err_msg = NULL;
    if (format != NULL) {
        err_msg = PyUnicode_FromFormatV(format, va);
        if (err_msg == NULL) {
            PyErr_Clear();     /* e.g. an undecodable format string: no message */
        }
    }
    /* The lookup itself may run out of memory; that failure is dropped, not
       reported as another unraisable error. */
    PyObject *hook = NULL;
    if (PyDict_GetItemStringRef(PyInterpreterState_Get()->sysdict, "unraisablehook", &hook) < 0) {
        PyErr_Clear();
    }
    if (hook != NULL && hook != Py_None) {
        PyObject *types_mod = PyImport_ImportModule("types");
        PyObject *factory = types_mod != NULL
            ? PyObject_GetAttrString(types_mod, "SimpleNamespace") : NULL;
        Py_XDECREF(types_mod);
        PyObject *kwargs = factory != NULL ? PyDict_New() : NULL;
        PyObject *noargs = kwargs != NULL ? PyTuple_New(0) : NULL;
        PyObject *tb = PyException_GetTraceback(exc);
        if (noargs != NULL
            && PyDict_SetItemString(kwargs, "exc_type", (PyObject *)Py_TYPE(exc)) == 0
            && PyDict_SetItemString(kwargs, "exc_value", exc) == 0
            && PyDict_SetItemString(kwargs, "exc_traceback", tb != NULL ? tb : Py_None) == 0
            && PyDict_SetItemString(kwargs, "err_msg", err_msg != NULL ? err_msg : Py_None) == 0
            && PyDict_SetItemString(kwargs, "object", obj != NULL ? obj : Py_None) == 0) {
            PyObject *unraisable_exc = PyObject_Call(factory, noargs, kwargs);
            if (unraisable_exc != NULL) {
                PyObject *r = PyObject_CallOneArg(hook, unraisable_exc);
                Py_DECREF(unraisable_exc);
                Py_XDECREF(r);
            }
        }
        Py_XDECREF(noargs);
        PyErr_Clear();     /* an error the hook made is not raisable here either */
        Py_XDECREF(tb);
        Py_XDECREF(kwargs);
        Py_XDECREF(factory);
    }
    else {
        /* write_unraisable_exc: a header naming the object or message, then
           the exception with its traceback. */
        PyObject *repr = obj != NULL ? PyObject_Repr(obj) : NULL;
        const char *text = repr != NULL ? PyUnicode_AsUTF8(repr) : NULL;
        char head[512];
        int n;
        if (obj != NULL && err_msg != NULL) {
            n = snprintf(head, sizeof head, "%s: %s\n", PyUnicode_AsUTF8(err_msg),
                         text != NULL ? text : "<object repr() failed>");
        }
        else if (obj != NULL) {
            n = snprintf(head, sizeof head, "Exception ignored in: %s\n",
                         text != NULL ? text : "<object repr() failed>");
        }
        else if (err_msg != NULL) {
            n = snprintf(head, sizeof head, "%s:\n", PyUnicode_AsUTF8(err_msg));
        }
        else {
            n = 0;
        }
        PyErr_Clear();
        if (n > 0) {
            lu_stderr_write(head, (size_t)n);
        }
        lu_err_display(exc);
        Py_XDECREF(repr);
    }
    Py_XDECREF(hook);
    Py_XDECREF(err_msg);
    Py_DECREF(exc);
}

void
PyErr_WriteUnraisable(PyObject *obj)
{
    format_unraisable_v(NULL, (va_list){0}, obj);
}

void
PyErr_FormatUnraisable(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    format_unraisable_v(format, ap, NULL);
    va_end(ap);
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

/* lu_err_format with the exception that was set as the new one's __cause__
   and __context__ (_PyErr_FormatFromCause). */
void
lu_err_format_from_cause(PyObject *type, const char *format, const char *name)
{
    PyObject *cause = PyErr_GetRaisedException();
    lu_err_format(type, format, name);
    PyObject *exc = PyErr_GetRaisedException();
    if (exc != NULL && cause != NULL) {
        PyException_SetCause(exc, Py_NewRef(cause));
        PyException_SetContext(exc, cause);
        cause = NULL;
    }
    Py_XDECREF(cause);
    PyErr_SetRaisedException(exc);
}
