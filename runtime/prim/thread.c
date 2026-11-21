/* The _lucifer primitives under the _thread port (Modules/_threadmodule.c):
   mutexes kept in a bytearray, and thread ids. */

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

/* A mutex is a bytearray holding the PyMutex and, while it is locked, the
   ident of the thread that locked it and how many times that thread holds
   it (an RLock's recursion count; 1 for a lock), which the lock and unlock
   record in the same primitive call as the PyMutex itself: a signal
   handler runs only at a poll between primitives, so none sees the mutex
   without its holder and count. Only the holder writes them. */
struct mutex {
    PyMutex lock;
    long long owner;
    long long count;
};

static struct mutex *
mutex_of(PyObject *o)
{
    if (!PyByteArray_Check(o) || PyByteArray_Size(o) != sizeof(struct mutex)) {
        lu_err_format(PyExc_TypeError, "a mutex is a %zu-byte bytearray", sizeof(struct mutex));
        return NULL;
    }
    return (struct mutex *)PyByteArray_AsString(o);
}

static long long
me(void)
{
    return (long long)PyThreadState_Get()->thread_id;
}

/* The calling thread's holds on the mutex: 0 when it does not hold it. */
static long long
mine(struct mutex *mutex)
{
    return __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == me() ? mutex->count : 0;
}

/* mutex_new(): an unlocked mutex. */
static PyObject *
prim_mutex_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    struct mutex init = { 0 };
    return PyByteArray_FromStringAndSize((const char *)&init, sizeof init);
}

/* Locks args[0], waiting at most args[1] nanoseconds, for ever if it is
   negative and not at all if 0, and records the caller holding it `count`
   times. Detached while it waits. A signal handler that raises ends the
   wait with its exception; one that returns lets it go on. A timeout of
   None waits for ever and leaves signals to the next poll, as CPython's
   PyMutex_Lock does for Condition's _acquire_restore: the lock is held
   again before any handler runs. */
static PyObject *
lock_timed(PyObject *const *args, long long count)
{
    struct mutex *mutex = mutex_of(args[0]);
    long timeout = -1;
    if (mutex == NULL
        || (args[1] != Py_None && (!PyLong_Check(args[1]) || lu_long_as_long(args[1], &timeout) < 0))) {
        return NULL;
    }
    _PyLockFlags flags = args[1] == Py_None ? _PY_LOCK_DETACH : _PY_LOCK_DETACH | _PY_LOCK_HANDLE_SIGNALS;
    PyTime_t deadline = timeout > 0 ? lu_monotonic_ns() + timeout : 0;
    PyTime_t left = timeout > 0 ? deadline - lu_monotonic_ns() : timeout;
    PyLockStatus status = _PyMutex_LockTimed(&mutex->lock, left > 0 || timeout <= 0 ? left : 0, flags);
    if (status == PY_LOCK_INTR) {
        return NULL;
    }
    if (status != PY_LOCK_ACQUIRED) {
        Py_RETURN_FALSE;
    }
    mutex->count = count;
    __atomic_store_n(&mutex->owner, me(), __ATOMIC_RELAXED);
    Py_RETURN_TRUE;
}

/* Unlocks the mutex: 0, or -1 when it was not locked. */
static int
unlock(struct mutex *mutex)
{
    __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELAXED);
    mutex->count = 0;
    return _PyMutex_TryUnlock(&mutex->lock);
}

/* mutex_acquire(mutex, timeout_ns): True when the mutex is taken (lock_timed). */
static PyObject *
prim_mutex_acquire(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    return lock_timed(args, 1);
}

/* mutex_release(mutex): False when it was not locked. */
static PyObject *
prim_mutex_release(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    struct mutex *mutex = mutex_of(args[0]);
    if (mutex == NULL) {
        return NULL;
    }
    return Py_NewRef(unlock(mutex) == 0 ? Py_True : Py_False);
}

/* rlock_acquire(mutex, timeout_ns, count): the caller holds the mutex
   `count` times more, at once when it holds it already, else as
   mutex_acquire. True when it does. */
static PyObject *
prim_rlock_acquire(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    struct mutex *mutex = mutex_of(args[0]);
    long count;
    if (mutex == NULL || !PyLong_Check(args[2]) || lu_long_as_long(args[2], &count) < 0) {
        return NULL;
    }
    if (mine(mutex)) {
        mutex->count += count;
        Py_RETURN_TRUE;
    }
    return lock_timed(args, count);
}

/* rlock_release(mutex, all): gives up one of the caller's holds, or all of
   them, unlocking the mutex with the last. The holds given up: 0 when the
   caller holds none. */
static PyObject *
prim_rlock_release(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    struct mutex *mutex = mutex_of(args[0]);
    if (mutex == NULL) {
        return NULL;
    }
    long long held = mine(mutex), n = PyObject_IsTrue(args[1]) ? held : held > 0;
    if (n > 0 && n == held) {
        unlock(mutex);
    }
    else if (n > 0) {
        mutex->count -= n;      /* only the holder writes the count */
    }
    return PyLong_FromLongLong(n);
}

static PyObject *
prim_mutex_locked(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    struct mutex *mutex = mutex_of(args[0]);
    if (mutex == NULL) {
        return NULL;
    }
    return Py_NewRef(PyMutex_IsLocked(&mutex->lock) ? Py_True : Py_False);
}

/* mutex_mine(mutex): the times the calling thread holds it: 0 when it does not. */
static PyObject *
prim_mutex_mine(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    struct mutex *mutex = mutex_of(args[0]);
    if (mutex == NULL) {
        return NULL;
    }
    return PyLong_FromLongLong(mine(mutex));
}

/* mutex_state(mutex): (holder's ident, its holds), (0, 0) when unlocked. */
static PyObject *
prim_mutex_state(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    struct mutex *mutex = mutex_of(args[0]);
    if (mutex == NULL) {
        return NULL;
    }
    long long owner = __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED);
    return Py_BuildValue("(LL)", owner, owner != 0 ? __atomic_load_n(&mutex->count, __ATOMIC_RELAXED) : 0LL);
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
    PyThreadState *ts = PyThreadState_Get();
    lu_fh_thread_enter(ts);
    PyObject *result = lu_call(callable, NULL, 0, NULL);
    if (result == NULL) {
        lu_err_unraisable_text("Exception ignored in thread started by", callable);
    }
    Py_XDECREF(result);
    Py_DECREF(callable);
    lu_fh_thread_exit(ts);
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
    if (__atomic_load_n(&_PyRuntime.finalizing, __ATOMIC_RELAXED)) {
        lu_err_format(PyExc_PythonFinalizationError, "can't create new thread at interpreter shutdown");
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
    FAST(mutex_new), FAST(mutex_acquire), FAST(mutex_release), FAST(mutex_locked), FAST(mutex_mine),
    FAST(mutex_state), FAST(rlock_acquire), FAST(rlock_release),
    FAST(thread_ident), FAST(thread_native_id), FAST(thread_start), FAST(main_thread_ident),
    FAST(thread_dict),
    { NULL, NULL, 0, NULL },
};
