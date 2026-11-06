/* The PyThread lock API (Python/thread_pthread.h): a lock any thread may
   release, on a PyMutex, and the thread-start calls. */

#include "thread/lock.h"

#include <pthread.h>
#include <stdlib.h>

/* bpo-33015: the wrapper casts "void func(void *)" to pthread's required
   "void *func(void *)", and frees its argument record. */
typedef struct {
    void (*func)(void *);
    void *arg;
} pythread_callback;

static void *
pythread_wrapper(void *arg)
{
    pythread_callback callback = *(pythread_callback *)arg;
    free(arg);
    callback.func(callback.arg);
    return NULL;
}

static int
start_thread(void (*func)(void *), void *arg, pthread_t *out)
{
    pythread_callback *callback = malloc(sizeof *callback);
    if (callback == NULL) {
        return -1;
    }
    callback->func = func;
    callback->arg = arg;
    if (pthread_create(out, NULL, pythread_wrapper, callback) != 0) {
        free(callback);
        return -1;
    }
    return 0;
}

/* Cast through an integer of the same size: POSIX allows pthread_t to be a
   non-arithmetic type (musl makes it a pointer). */
static unsigned long
pthread_ident(pthread_t value)
{
    return (unsigned long)(uintptr_t)value;
}

unsigned long
PyThread_start_new_thread(void (*func)(void *), void *arg)
{
    pthread_t th;
    if (start_thread(func, arg, &th) != 0) {
        return PYTHREAD_INVALID_THREAD_ID;
    }
    pthread_detach(th);
    return pthread_ident(th);
}

LU_EXPORT int
PyThread_start_joinable_thread(void (*func)(void *), void *arg,
                               PyThread_ident_t *ident, PyThread_handle_t *handle)
{
    pthread_t th;
    if (start_thread(func, arg, &th) != 0) {
        return -1;
    }
    *ident = pthread_ident(th);
    *handle = (PyThread_handle_t)th;
    return 0;
}

LU_EXPORT int
PyThread_join_thread(PyThread_handle_t th)
{
    return lu_join_thread((pthread_t)th);
}

LU_EXPORT int
PyThread_detach_thread(PyThread_handle_t th)
{
    return pthread_detach((pthread_t)th);
}

void
PyThread_exit_thread(void)
{
    pthread_exit(NULL);
}

PyThread_type_lock
PyThread_allocate_lock(void)
{
    PyMutex *m = PyMem_RawCalloc(1, sizeof *m);
    if (m == NULL) {
        PyErr_NoMemory();
    }
    return m;
}

void
PyThread_free_lock(PyThread_type_lock lock)
{
    PyMem_RawFree(lock);
}

PyLockStatus
PyThread_acquire_lock_timed(PyThread_type_lock lock, PY_TIMEOUT_T microseconds, int intr_flag)
{
    PyTime_t timeout = microseconds < 0 ? -1 : microseconds * 1000;
    return _PyMutex_LockTimed(lock, timeout, _PY_LOCK_DETACH | (intr_flag ? _PY_LOCK_HANDLE_SIGNALS : 0));
}

int
PyThread_acquire_lock(PyThread_type_lock lock, int waitflag)
{
    return PyThread_acquire_lock_timed(lock, waitflag ? -1 : 0, 0) == PY_LOCK_ACQUIRED;
}

void
PyThread_release_lock(PyThread_type_lock lock)
{
    PyMutex_Unlock(lock);
}
