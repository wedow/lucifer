/* The _lucifer primitives under the _thread port (Modules/_threadmodule.c):
   mutexes kept in the byte of a bytearray, and thread ids. */

#define _GNU_SOURCE
#include "prim/prim.h"
#include "thread/lock.h"
#include "interp/exceptions.h"

#include <pthread.h>
#include <unistd.h>

#define LU_DEFAULT_THREAD_STACK ((size_t)8 << 20)

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)

static PyMutex *
mutex_of(PyObject *o)
{
    if (!PyByteArray_Check(o) || PyByteArray_Size(o) != 1) {
        lu_err_format(PyExc_TypeError, "a mutex is a one-byte bytearray");
        return NULL;
    }
    return (PyMutex *)PyByteArray_AsString(o);
}

/* mutex_new(): an unlocked mutex. */
static PyObject *
prim_mutex_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    return PyByteArray_FromStringAndSize("", 1);
}

/* mutex_acquire(mutex, timeout_ns): waits at most timeout_ns, for ever if it
   is negative and not at all if 0. True when the mutex is taken. Detached
   while it waits. A signal handler that raises ends the wait with its
   exception; one that returns lets it go on. */
static PyObject *
prim_mutex_acquire(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyMutex *mutex = mutex_of(args[0]);
    long timeout;
    if (mutex == NULL || !PyLong_Check(args[1]) || lu_long_as_long(args[1], &timeout) < 0) {
        return NULL;
    }
    PyTime_t deadline = timeout > 0 ? lu_monotonic_ns() + timeout : 0;
    PyTime_t left = timeout > 0 ? deadline - lu_monotonic_ns() : timeout;
    PyLockStatus status = _PyMutex_LockTimed(mutex, left > 0 || timeout <= 0 ? left : 0,
                                             _PY_LOCK_DETACH | _PY_LOCK_HANDLE_SIGNALS);
    if (status == PY_LOCK_INTR) {
        return NULL;
    }
    return Py_NewRef(status == PY_LOCK_ACQUIRED ? Py_True : Py_False);
}

/* mutex_release(mutex): False when it was not locked. */
static PyObject *
prim_mutex_release(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyMutex *mutex = mutex_of(args[0]);
    if (mutex == NULL) {
        return NULL;
    }
    return Py_NewRef(_PyMutex_TryUnlock(mutex) == 0 ? Py_True : Py_False);
}

static PyObject *
prim_mutex_locked(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyMutex *mutex = mutex_of(args[0]);
    if (mutex == NULL) {
        return NULL;
    }
    return Py_NewRef(PyMutex_IsLocked(mutex) ? Py_True : Py_False);
}

static PyObject *
prim_thread_ident(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    return PyLong_FromLongLong((long long)PyThreadState_Get()->thread_id);
}

static PyObject *
prim_thread_native_id(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    return PyLong_FromLongLong((long long)lu_carrier->native_id);
}

/* thread_start(callable, stack_size): runs callable() in a new OS thread with its own
   task (bound to that carrier for life, DESIGN 5.7), and answers the thread's identifier,
   which is the pthread_t the task's ident is taken from. A stack_size of 0 is the default.
   The thread is detached; the Python side keeps the lock a join waits on. Whatever
   callable raises is reported as an unraisable exception. */
static void *
thread_main(void *arg)
{
    PyObject *callable = arg;
    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject *result = lu_call(callable, NULL, 0, NULL);
    if (result == NULL) {
        lu_err_unraisable_text("Exception ignored in thread started by", callable);
    }
    Py_XDECREF(result);
    Py_DECREF(callable);
    PyGILState_Release(gstate);
    return NULL;
}

static PyObject *
prim_thread_start(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    long stack_size;
    if (!PyLong_Check(args[1]) || lu_long_as_long(args[1], &stack_size) < 0) {
        return NULL;
    }
    if (_PyRuntime.main_interp.finalizing) {
        lu_err_format(PyExc_RuntimeError, "can't create new thread at interpreter shutdown");
        return NULL;
    }
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack_size > 0 ? (size_t)stack_size : LU_DEFAULT_THREAD_STACK);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    Py_INCREF(args[0]);
    int err = pthread_create(&thread, &attr, thread_main, args[0]);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        Py_DECREF(args[0]);
        lu_err_format(PyExc_RuntimeError, "can't start new thread");
        return NULL;
    }
    return PyLong_FromLongLong((long long)thread);
}

/* main_thread_ident(): the ident of the main task, from any thread. */
static PyObject *
prim_main_thread_ident(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    return PyLong_FromLongLong((long long)_PyRuntime.main_thread);
}

/* thread_dict(): the current task's dict, made on first use. It dies with the task
   (PyThreadState_Clear), which is what makes _thread._local per task. */
static PyObject *
prim_thread_dict(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    PyThreadState *ts = PyThreadState_Get();
    if (ts->dict == NULL && (ts->dict = PyDict_New()) == NULL) {
        return NULL;
    }
    return Py_NewRef(ts->dict);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_thread_methods[] = {
    FAST(mutex_new), FAST(mutex_acquire), FAST(mutex_release), FAST(mutex_locked),
    FAST(thread_ident), FAST(thread_native_id), FAST(thread_start), FAST(main_thread_ident),
    FAST(thread_dict),
    { NULL, NULL, 0, NULL },
};
