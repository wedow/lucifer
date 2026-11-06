/* Services of the sys module, threads-local storage, the code runner and
   frame globals, for C callers (Python/sysmodule.c, thread_pthread.h,
   pythonrun.c, ceval.c). */

#include "lucifer.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#undef Py_CompileString
#undef Py_CompileStringFlags
#undef PyRun_String

/* sys */

static void
write_sys(const char *name, FILE *fallback, const char *format, va_list va)
{
    char buffer[1001];
    PyObject *exc = PyErr_GetRaisedException();
    int n = vsnprintf(buffer, sizeof buffer, format, va);
    PyObject *file = PySys_GetObject(name);
    PyObject *text = n >= 0 ? PyUnicode_DecodeUTF8(buffer, n < (int)sizeof buffer ? n : (int)sizeof buffer - 1, NULL) : NULL;
    PyObject *r = file != NULL && text != NULL ? PyObject_CallMethod(file, "write", "(O)", text) : NULL;
    if (r == NULL) {
        PyErr_Clear();
        fputs(buffer, fallback);
    }
    Py_XDECREF(r);
    Py_XDECREF(text);
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

/* Running code. */

PyObject *
Py_CompileStringObject(const char *str, PyObject *filename, int start, PyCompilerFlags *flags, int optimize)
{
    /* compile() with the C caller's flags and nothing inherited. The source
       is bytes, so a coding cookie decides its decoding, unless the flags
       say it is already UTF-8 text (Python/pythonrun.c). */
    const char *mode = start == Py_eval_input ? "eval" : start == Py_single_input ? "single" : "exec";
    int cf = flags != NULL ? flags->cf_flags : 0;
    PyObject *args[6] = {
        (cf & PyCF_IGNORE_COOKIE) ? PyUnicode_FromString(str) : PyBytes_FromString(str),
        filename, PyUnicode_FromString(mode),
        PyLong_FromLong(cf & ~(PyCF_IGNORE_COOKIE | PyCF_SOURCE_IS_UTF8)), Py_True,
        PyLong_FromLong(optimize),
    };
    PyObject *code = NULL;
    if (args[0] != NULL && args[2] != NULL && args[3] != NULL && args[5] != NULL) {
        code = lu_call(PyDict_GetItemString(PyInterpreterState_Get()->builtins, "compile"), args, 6, NULL);
    }
    Py_XDECREF(args[0]);
    Py_XDECREF(args[2]);
    Py_XDECREF(args[3]);
    Py_XDECREF(args[5]);
    return code;
}

PyObject *
Py_CompileStringExFlags(const char *str, const char *filename, int start, PyCompilerFlags *flags, int optimize)
{
    PyObject *name = PyUnicode_DecodeFSDefault(filename);
    PyObject *code = name != NULL ? Py_CompileStringObject(str, name, start, flags, optimize) : NULL;
    Py_XDECREF(name);
    return code;
}

PyObject *
Py_CompileStringFlags(const char *str, const char *filename, int start, PyCompilerFlags *flags)
{
    return Py_CompileStringExFlags(str, filename, start, flags, -1);
}

PyObject *
Py_CompileString(const char *str, const char *filename, int start)
{
    return Py_CompileStringExFlags(str, filename, start, NULL, -1);
}

PyObject *
PyRun_StringFlags(const char *str, int start, PyObject *globals, PyObject *locals, PyCompilerFlags *flags)
{
    PyObject *code = Py_CompileStringExFlags(str, "<string>", start, flags, -1);
    PyObject *r = code != NULL ? PyEval_EvalCode(code, globals, locals) : NULL;
    Py_XDECREF(code);
    return r;
}

PyObject *
PyRun_String(const char *str, int start, PyObject *globals, PyObject *locals)
{
    return PyRun_StringFlags(str, start, globals, locals, NULL);
}

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
