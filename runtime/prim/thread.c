/* The _lucifer primitives under the _thread port (Modules/_threadmodule.c):
   mutexes kept in the byte of a bytearray, and thread ids. */

#define _GNU_SOURCE
#include "prim/prim.h"
#include "thread/lock.h"

#include <pthread.h>
#include <unistd.h>

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
    return PyLong_FromLongLong((long long)(uintptr_t)pthread_self());
}

static PyObject *
prim_thread_native_id(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    return PyLong_FromLongLong(gettid());
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_thread_methods[] = {
    FAST(mutex_new), FAST(mutex_acquire), FAST(mutex_release), FAST(mutex_locked),
    FAST(thread_ident), FAST(thread_native_id),
    { NULL, NULL, 0, NULL },
};
