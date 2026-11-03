/* PyMutex (Python/lock.c of CPython 3.14): a one-byte lock whose slow path
   parks in the parking lot, and PyEvent. The other lock kinds of that file (_PyRawMutex, _PyRecursiveMutex, _PyRWMutex, _PySeqLock, _PyOnceFlag)
   arrive with their first users. */

#include "thread/lock.h"

#include <sched.h>

// If a thread waits on a lock for longer than TIME_TO_BE_FAIR_NS (1 ms), then
// the unlocking thread directly hands off ownership of the lock. This avoids
// starvation.
static const PyTime_t TIME_TO_BE_FAIR_NS = 1000*1000;

// Spin for a bit before parking the thread.
static const int MAX_SPIN_COUNT = 40;

struct mutex_entry {
    // The time after which the unlocking thread should hand off lock ownership
    // directly to the waiting thread. Written by the waiting thread.
    PyTime_t time_to_be_fair;

    // Set to 1 if the lock was handed off. Written by the unlocking thread.
    int handed_off;
};

/* _PyTime_Add: saturating. */
static PyTime_t
time_add(PyTime_t t, PyTime_t d)
{
    return t > INT64_MAX - d ? INT64_MAX : t + d;
}

LU_EXPORT PyLockStatus
_PyMutex_LockTimed(PyMutex *m, PyTime_t timeout, _PyLockFlags flags)
{
    uint8_t v = _Py_atomic_load_uint8_relaxed(&m->_bits);
    if ((v & _Py_LOCKED) == 0) {
        if (_Py_atomic_compare_exchange_uint8(&m->_bits, &v, v|_Py_LOCKED)) {
            return PY_LOCK_ACQUIRED;
        }
    }
    if (timeout == 0) {
        return PY_LOCK_FAILURE;
    }

    PyTime_t now = lu_monotonic_ns();
    PyTime_t endtime = 0;
    if (timeout > 0) {
        endtime = time_add(now, timeout);
    }

    struct mutex_entry entry = {
        .time_to_be_fair = now + TIME_TO_BE_FAIR_NS,
        .handed_off = 0,
    };

    Py_ssize_t spin_count = 0;
    for (;;) {
        if ((v & _Py_LOCKED) == 0) {
            // The lock is unlocked. Try to grab it.
            if (_Py_atomic_compare_exchange_uint8(&m->_bits, &v, v|_Py_LOCKED)) {
                return PY_LOCK_ACQUIRED;
            }
            continue;
        }

        if (!(v & _Py_HAS_PARKED) && spin_count < MAX_SPIN_COUNT) {
            // Spin for a bit.
            sched_yield();
            spin_count++;
            continue;
        }

        if (timeout == 0) {
            return PY_LOCK_FAILURE;
        }

        uint8_t newv = v;
        if (!(v & _Py_HAS_PARKED)) {
            // We are the first waiter. Set the _Py_HAS_PARKED flag.
            newv = v | _Py_HAS_PARKED;
            if (!_Py_atomic_compare_exchange_uint8(&m->_bits, &v, newv)) {
                continue;
            }
        }

        int ret = _PyParkingLot_Park(&m->_bits, &newv, sizeof(newv), timeout,
                                     &entry, (flags & _PY_LOCK_DETACH) != 0);
        if (ret == Py_PARK_OK) {
            if (entry.handed_off) {
                // We own the lock now.
                assert(_Py_atomic_load_uint8_relaxed(&m->_bits) & _Py_LOCKED);
                return PY_LOCK_ACQUIRED;
            }
        }
        else if (ret == Py_PARK_INTR && (flags & _PY_LOCK_HANDLE_SIGNALS)) {
            if (Py_MakePendingCalls() < 0) {
                return PY_LOCK_INTR;
            }
        }
        else if (ret == Py_PARK_TIMEOUT) {
            assert(timeout >= 0);
            return PY_LOCK_FAILURE;
        }

        if (timeout > 0) {
            timeout = endtime - lu_monotonic_ns();
            if (timeout <= 0) {
                // Avoid negative values because those mean block forever.
                timeout = 0;
            }
        }

        v = _Py_atomic_load_uint8_relaxed(&m->_bits);
    }
}

static void
mutex_unpark(void *arg, void *park_arg, int has_more_waiters)
{
    PyMutex *m = (PyMutex*)arg;
    struct mutex_entry *entry = (struct mutex_entry*)park_arg;
    uint8_t v = 0;
    if (entry) {
        int should_be_fair = lu_monotonic_ns() > entry->time_to_be_fair;

        entry->handed_off = should_be_fair;
        if (should_be_fair) {
            v |= _Py_LOCKED;
        }
        if (has_more_waiters) {
            v |= _Py_HAS_PARKED;
        }
    }
    _Py_atomic_store_uint8(&m->_bits, v);
}

int
_PyMutex_TryUnlock(PyMutex *m)
{
    uint8_t v = _Py_atomic_load_uint8(&m->_bits);
    for (;;) {
        if ((v & _Py_LOCKED) == 0) {
            // error: the mutex is not locked
            return -1;
        }
        else if ((v & _Py_HAS_PARKED)) {
            // wake up a single thread
            _PyParkingLot_Unpark(&m->_bits, mutex_unpark, m);
            return 0;
        }
        else if (_Py_atomic_compare_exchange_uint8(&m->_bits, &v, _Py_UNLOCKED)) {
            // fast-path: no waiters
            return 0;
        }
    }
}

#undef PyMutex_Lock
void
PyMutex_Lock(PyMutex *m)
{
    _PyMutex_LockTimed(m, -1, _PY_LOCK_DETACH);
}

#undef PyMutex_Unlock
void
PyMutex_Unlock(PyMutex *m)
{
    if (_PyMutex_TryUnlock(m) < 0) {
        Py_FatalError("unlocking mutex that is not locked");
    }
}

#undef PyMutex_IsLocked
int
PyMutex_IsLocked(PyMutex *m)
{
    return _PyMutex_IsLocked(m);
}

/* PyEvent: a one-time notification. The byte is unlocked while unset and
   unwatched, has-parked while a waiter sleeps, and locked once set. */

LU_EXPORT int
_PyEvent_IsSet(PyEvent *evt)
{
    return _Py_atomic_load_uint8(&evt->v) == _Py_LOCKED;
}

LU_EXPORT void
_PyEvent_Notify(PyEvent *evt)
{
    uintptr_t v = _Py_atomic_exchange_uint8(&evt->v, _Py_LOCKED);
    if (v == _Py_HAS_PARKED) {
        _PyParkingLot_UnparkAll(&evt->v);
    }
}

LU_EXPORT void
PyEvent_Wait(PyEvent *evt)
{
    while (!PyEvent_WaitTimed(evt, -1, /*detach=*/1)) {
    }
}

LU_EXPORT int
PyEvent_WaitTimed(PyEvent *evt, PyTime_t timeout_ns, int detach)
{
    for (;;) {
        uint8_t v = _Py_atomic_load_uint8(&evt->v);
        if (v == _Py_LOCKED) {
            return 1;
        }
        if (v == _Py_UNLOCKED
            && !_Py_atomic_compare_exchange_uint8(&evt->v, &v, _Py_HAS_PARKED)) {
            continue;
        }
        uint8_t expected = _Py_HAS_PARKED;
        (void)_PyParkingLot_Park(&evt->v, &expected, sizeof(evt->v), timeout_ns, NULL, detach);
        return _Py_atomic_load_uint8(&evt->v) == _Py_LOCKED;
    }
}
