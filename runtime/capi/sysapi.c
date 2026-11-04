/* Services of the sys module, threads-local storage, the code runner and
   frame globals, for C callers (Python/sysmodule.c, thread_pthread.h,
   pythonrun.c, ceval.c). */

#include "lucifer.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/syscall.h>
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

/* Thread identity and thread-specific storage. */

unsigned long
PyThread_get_thread_ident(void)
{
    return (unsigned long)pthread_self();
}

unsigned long
PyThread_get_thread_native_id(void)
{
    return (unsigned long)syscall(SYS_gettid);
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

PyObject *
PyThreadState_GetDict(void)
{
    PyThreadState *ts = PyThreadState_GetUnchecked();
    if (ts == NULL) {
        return NULL;
    }
    if (ts->dict == NULL) {
        ts->dict = PyDict_New();
        if (ts->dict == NULL) {
            PyErr_Clear();
        }
    }
    return ts->dict;
}

/* Running code. */

PyObject *
Py_CompileStringObject(const char *str, PyObject *filename, int start, PyCompilerFlags *flags, int optimize)
{
    const char *mode = start == Py_eval_input ? "eval" : start == Py_single_input ? "single" : "exec";
    PyObject *source = PyUnicode_FromString(str);
    PyObject *code = NULL;
    if (source != NULL) {
        PyObject *args[3] = { source, filename, PyUnicode_FromString(mode) };
        code = args[2] != NULL ? lu_call(PyDict_GetItemString(PyInterpreterState_Get()->builtins, "compile"), args, 3, NULL) : NULL;
        Py_XDECREF(args[2]);
        Py_DECREF(source);
    }
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
