/* The PyThread lock API (Python/thread_pthread.h): a lock any thread may
   release, on a PyMutex. */

#include "thread/lock.h"

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
