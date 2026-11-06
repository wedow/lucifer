/* Services of the sys module, threads-local storage, the code runner and
   frame globals, for C callers (Python/sysmodule.c, thread_pthread.h,
   pythonrun.c, ceval.c). */

#include "lucifer.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

/* sys */

/* text written to sys.<name>, or to the C stream when that fails; no
   exception escapes (Python/sysmodule.c sys_pyfile_write_unicode). */
static void
write_text(const char *name, FILE *fallback, PyObject *text)
{
    PyObject *file = PySys_GetObject(name);
    PyObject *r = file != NULL ? PyObject_CallMethod(file, "write", "(O)", text) : NULL;
    if (r == NULL) {
        PyErr_Clear();
        const char *utf8 = PyUnicode_AsUTF8(text);
        if (utf8 != NULL) {
            fputs(utf8, fallback);
        }
        PyErr_Clear();
    }
    Py_XDECREF(r);
}

/* sys_write: printf formatting, at most 1000 bytes, then "... truncated". */
static void
write_sys(const char *name, FILE *fallback, const char *format, va_list va)
{
    char buffer[1001];
    PyObject *exc = PyErr_GetRaisedException();
    int written = PyOS_vsnprintf(buffer, sizeof buffer, format, va);
    const char *parts[2] = { buffer, written < 0 || (size_t)written >= sizeof buffer ? "... truncated" : NULL };
    for (int i = 0; i < 2 && parts[i] != NULL; i++) {
        PyObject *text = PyUnicode_FromString(parts[i]);
        if (text != NULL) {
            write_text(name, fallback, text);
            Py_DECREF(text);
        }
        else {
            PyErr_Clear();
            fputs(parts[i], fallback);
        }
    }
    PyErr_SetRaisedException(exc);
}

/* sys_format: PyUnicode_FromFormat, unlimited. */
static void
format_sys(const char *name, FILE *fallback, const char *format, va_list va)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *message = PyUnicode_FromFormatV(format, va);
    if (message != NULL) {
        write_text(name, fallback, message);
        Py_DECREF(message);
    }
    PyErr_SetRaisedException(exc);
}

void
PySys_WriteStdout(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    write_sys("stdout", stdout, format, ap);
    va_end(ap);
}

void
PySys_WriteStderr(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    write_sys("stderr", stderr, format, ap);
    va_end(ap);
}

void
PySys_FormatStdout(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    format_sys("stdout", stdout, format, ap);
    va_end(ap);
}

void
PySys_FormatStderr(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    format_sys("stderr", stderr, format, ap);
    va_end(ap);
}

/* sys._xoptions, replaced by a new dict when it is missing or not a dict
   (get_xoptions); borrowed. */
PyObject *
PySys_GetXOptions(void)
{
    PyObject *xoptions = PySys_GetObject("_xoptions");
    if (xoptions == NULL || !PyDict_Check(xoptions)) {
        xoptions = PyDict_New();
        if (xoptions == NULL || PySys_SetObject("_xoptions", xoptions) < 0) {
            Py_XDECREF(xoptions);
            return NULL;
        }
        Py_DECREF(xoptions);    /* sys holds it */
    }
    return xoptions;
}

int
PySys_SetObject(const char *name, PyObject *v)
{
    PyObject *sysdict = PyInterpreterState_Get()->sysdict;
    if (v == NULL) {
        int r = PyDict_DelItemString(sysdict, name);
        if (r < 0 && PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_Clear();
            return 0;
        }
        return r;
    }
    return PyDict_SetItemString(sysdict, name, v);
}

int
PySys_AuditTuple(const char *event, PyObject *args)
{
    PyObject *sys_audit = PySys_GetObject("audit"), *name = PyUnicode_FromString(event);
    PyObject *call = name != NULL && sys_audit != NULL ? PyTuple_New(1) : NULL;
    int r = -1;
    if (call != NULL) {
        PyTuple_SET_ITEM(call, 0, name);
        name = NULL;
        PyObject *all = args != NULL ? PySequence_Concat(call, args) : Py_NewRef(call);
        PyObject *result = all != NULL ? PyObject_CallObject(sys_audit, all) : NULL;
        r = result != NULL ? 0 : -1;
        Py_XDECREF(result);
        Py_XDECREF(all);
    }
    Py_XDECREF(name);
    Py_XDECREF(call);
    return r;
}

int
PySys_Audit(const char *event, const char *argformat, ...)
{
    PyObject *args = NULL;
    if (argformat != NULL && *argformat) {
        va_list ap;
        va_start(ap, argformat);
        args = Py_VaBuildValue(argformat, ap);
        va_end(ap);
        if (args == NULL) {
            return -1;
        }
        if (!PyTuple_Check(args)) {
            PyObject *one = PyTuple_Pack(1, args);
            Py_DECREF(args);
            args = one;
        }
    }
    int r = PySys_AuditTuple(event, args);
    Py_XDECREF(args);
    return r;
}

/* Interpreter build information (Python/getversion.c, getplatform.c,
   getcopyright.c, getcompiler.c, Modules/getbuildinfo.c). The strings match
   lib/kernel/sys.py, which reports them as sys.version, sys.platform and
   sys.copyright. */

const char *
Py_GetVersion(void)
{
    return "3.14.10 free-threading build (lucifer) [lucifer]";
}

const char *
Py_GetPlatform(void)
{
    return "linux";
}

const char *
Py_GetCopyright(void)
{
    return "Copyright (c) 2001 Python Software Foundation.\nAll Rights Reserved.";
}

const char *
Py_GetCompiler(void)
{
    return "[lucifer]";
}

const char *
Py_GetBuildInfo(void)
{
    return "lucifer";
}

/* Exported for _testlimitedcapi only (Modules/getbuildinfo.c). */
LU_EXPORT int
_Py_GetBuiltWithAssert(void)
{
#ifdef NDEBUG
    return 0;
#else
    return 1;
#endif
}

/* Thread identity and thread-specific storage. */

unsigned long
PyThread_get_thread_ident(void)
{
    return (unsigned long)pthread_self();
}

unsigned long
PyThread_get_thread_native_id(void)
{
    return (unsigned long)gettid();
}

LU_EXPORT unsigned long long
PyThread_get_thread_ident_ex(void)
{
    return (unsigned long long)pthread_self();
}

Py_tss_t *
PyThread_tss_alloc(void)
{
    Py_tss_t *key = PyMem_RawCalloc(1, sizeof *key);
    return key;
}

void
PyThread_tss_free(Py_tss_t *key)
{
    if (key != NULL) {
        PyThread_tss_delete(key);
        PyMem_RawFree(key);
    }
}

int
PyThread_tss_is_created(Py_tss_t *key)
{
    return key->_is_initialized;
}

int
PyThread_tss_create(Py_tss_t *key)
{
    if (key->_is_initialized) {
        return 0;
    }
    if (pthread_key_create(&key->_key, NULL) != 0) {
        return -1;
    }
    key->_is_initialized = 1;
    return 0;
}

void
PyThread_tss_delete(Py_tss_t *key)
{
    if (key->_is_initialized) {
        pthread_key_delete(key->_key);
        key->_is_initialized = 0;
    }
}

int
PyThread_tss_set(Py_tss_t *key, void *value)
{
    return pthread_setspecific(key->_key, value) != 0 ? -1 : 0;
}

void *
PyThread_tss_get(Py_tss_t *key)
{
    return pthread_getspecific(key->_key);
}

/* Running code: PyRun_* and Py_CompileString* are in runtime/compiler/pythonrun.c. */

PyObject *
PyEval_GetGlobals(void)
{
    return lu_caller_globals(PyThreadState_Get());
}

PyObject *
PyEval_GetFrameGlobals(void)
{
    return Py_XNewRef(lu_caller_globals(PyThreadState_Get()));
}

/* Python/ceval.c: the caller's frame's builtins, else the interpreter's. Borrowed. */
PyObject *
PyEval_GetBuiltins(void)
{
    PyFrameObject *frame = PyEval_GetFrame();
    PyObject *b = frame != NULL ? PyFrame_GetBuiltins(frame) : NULL;
    if (b == NULL || b == Py_None) {
        return PyInterpreterState_Get()->builtins;
    }
    Py_DECREF(b);                   /* borrowed, as the C-API returns it */
    return b;
}

PyObject *
PyEval_GetFrameBuiltins(void)
{
    return Py_NewRef(PyEval_GetBuiltins());
}

PyObject *
PyEval_GetFrameLocals(void)
{
    PyFrameObject *frame = PyEval_GetFrame();
    return frame != NULL ? PyFrame_GetLocals(frame) : NULL;
}

/* Python/ceval.c PyEval_GetFuncName: what a traceback says called it. */
const char *
PyEval_GetFuncName(PyObject *func)
{
    if (PyMethod_Check(func)) {
        return PyEval_GetFuncName(PyMethod_GET_FUNCTION(func));
    }
    if (PyFunction_Check(func)) {
        return PyUnicode_AsUTF8(((PyFunctionObject *)func)->func_name);
    }
    if (PyCFunction_Check(func)) {
        return ((PyCFunctionObject *)func)->m_ml->ml_name;
    }
    return Py_TYPE(func)->tp_name;
}

const char *
PyEval_GetFuncDesc(PyObject *func)
{
    if (PyMethod_Check(func) || PyFunction_Check(func) || PyCFunction_Check(func)) {
        return "()";
    }
    return " object";
}

PyObject *
PyOS_FSPath(PyObject *path)
{
    PyObject *os = PyImport_ImportModule("os");
    PyObject *r = os != NULL ? PyObject_CallMethod(os, "fspath", "(O)", path) : NULL;
    Py_XDECREF(os);
    return r;
}

int
PyContextVar_Reset(PyObject *var, PyObject *token)
{
    PyObject *r = PyObject_CallMethod(var, "reset", "(O)", token);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* The clock queries (Python/pytime.c): nanoseconds since the epoch of each
   clock. Raw and checked forms read the same clock here. */

#include <time.h>

static PyTime_t
clock_ns(clockid_t id)
{
    struct timespec ts;
    clock_gettime(id, &ts);
    return (PyTime_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int
PyTime_Monotonic(PyTime_t *result)
{
    *result = clock_ns(CLOCK_MONOTONIC);
    return 0;
}

int
PyTime_MonotonicRaw(PyTime_t *result)
{
    return PyTime_Monotonic(result);
}

int
PyTime_PerfCounter(PyTime_t *result)
{
    *result = clock_ns(CLOCK_MONOTONIC);
    return 0;
}

int
PyTime_PerfCounterRaw(PyTime_t *result)
{
    return PyTime_PerfCounter(result);
}

int
PyTime_Time(PyTime_t *result)
{
    *result = clock_ns(CLOCK_REALTIME);
    return 0;
}

int
PyTime_TimeRaw(PyTime_t *result)
{
    return PyTime_Time(result);
}

double
PyTime_AsSecondsDouble(PyTime_t t)
{
    /* Whole seconds convert exactly; volatile keeps the rounding of the
       division what CPython's is. */
    volatile double d;
    if (t % 1000000000 == 0) {
        d = (double)(t / 1000000000);
    }
    else {
        d = (double)t;
        d /= 1e9;
    }
    return d;
}
