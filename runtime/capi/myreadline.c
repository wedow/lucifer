/* The readline the tokenizer and input() use (Parser/myreadline.c, CPython
   3.14): the embedder's PyOS_ReadlineFunctionPointer, or fgets when stdin or
   stdout is no terminal, with the thread detached while it waits. */

#include "lucifer.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

/* The thread state reading, which a readline function may attach; the
   readline extension module reads it. */
LU_EXPORT PyThreadState *_PyOS_ReadlineTState;
static PyMutex readline_lock;

/* fgets, restarted after EINTR unless a signal handler raised. 0 for a
   line, -1 at EOF, 1 for an interrupt, -2 for an error. */
static int
my_fgets(PyThreadState *tstate, char *buf, int len, FILE *fp)
{
    for (;;) {
        if (PyOS_InputHook != NULL) {
            (void)PyOS_InputHook();
        }
        errno = 0;
        clearerr(fp);
        if (fgets(buf, len, fp) != NULL) {
            return 0;
        }
        int err = errno;
        if (feof(fp)) {
            clearerr(fp);
            return -1;
        }
        if (err != EINTR) {
            return -2;
        }
        PyEval_RestoreThread(tstate);
        int s = PyErr_CheckSignals();
        PyEval_SaveThread();
        if (s < 0) {
            return 1;
        }
    }
}

/* Writes the prompt to stderr and reads a line, in PyMem_Raw memory: ""
   at EOF, NULL with an exception after an interrupt. */
char *
PyOS_StdioReadline(FILE *sys_stdin, FILE *sys_stdout, const char *prompt)
{
    PyThreadState *tstate = _PyOS_ReadlineTState;
    fflush(sys_stdout);
    if (prompt) {
        fprintf(stderr, "%s", prompt);
    }
    fflush(stderr);
    size_t n = 0;
    char *p = NULL;
    do {
        size_t incr = n > 0 ? n + 2 : 100;
        char *pr = incr > INT_MAX ? NULL : PyMem_RawRealloc(p, n + incr);
        if (pr == NULL) {
            PyMem_RawFree(p);
            PyEval_RestoreThread(tstate);
            if (incr > INT_MAX) {
                PyErr_SetString(PyExc_OverflowError, "input line too long");
            }
            else {
                PyErr_NoMemory();
            }
            PyEval_SaveThread();
            return NULL;
        }
        p = pr;
        int err = my_fgets(tstate, p + n, (int)incr, sys_stdin);
        if (err == 1) {
            PyMem_RawFree(p);
            return NULL;
        }
        if (err != 0) {
            p[n] = '\0';
            break;
        }
        n += strlen(p + n);
    } while (n == 0 || p[n - 1] != '\n');
    char *pr = PyMem_RawRealloc(p, n + 1);
    if (pr == NULL) {
        PyMem_RawFree(p);
        PyEval_RestoreThread(tstate);
        PyErr_NoMemory();
        PyEval_SaveThread();
    }
    return pr;
}

char *
PyOS_Readline(FILE *sys_stdin, FILE *sys_stdout, const char *prompt)
{
    PyThreadState *tstate = PyThreadState_Get();
    if (__atomic_load_n(&_PyOS_ReadlineTState, __ATOMIC_RELAXED) == tstate) {
        PyErr_SetString(PyExc_RuntimeError, "can't re-enter readline");
        return NULL;
    }
    char *rv;
    Py_BEGIN_ALLOW_THREADS
    PyMutex_Lock(&readline_lock);
    __atomic_store_n(&_PyOS_ReadlineTState, tstate, __ATOMIC_RELAXED);
    if (PyOS_ReadlineFunctionPointer == NULL) {
        PyOS_ReadlineFunctionPointer = PyOS_StdioReadline;
    }
    /* Not a terminal (python -i < file, say): no line editing. */
    if (!isatty(fileno(sys_stdin)) || !isatty(fileno(sys_stdout))) {
        rv = PyOS_StdioReadline(sys_stdin, sys_stdout, prompt);
    }
    else {
        rv = PyOS_ReadlineFunctionPointer(sys_stdin, sys_stdout, prompt);
    }
    __atomic_store_n(&_PyOS_ReadlineTState, NULL, __ATOMIC_RELAXED);
    PyMutex_Unlock(&readline_lock);
    Py_END_ALLOW_THREADS
    if (rv == NULL) {
        return NULL;
    }
    size_t len = strlen(rv) + 1;
    char *res = PyMem_Malloc(len);
    if (res != NULL) {
        memcpy(res, rv, len);
    }
    else {
        PyErr_NoMemory();
    }
    PyMem_RawFree(rv);
    return res;
}
