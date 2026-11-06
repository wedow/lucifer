/* The internal side of PyMutex, the parking lot and critical sections
   (Include/internal/pycore_lock.h, pycore_parking_lot.h and
   pycore_critical_section.h of CPython 3.14). */
#ifndef LU_LOCK_H
#define LU_LOCK_H

#include "lucifer.h"

#define _Py_HAS_PARKED 2
#define _Py_ONCE_INITIALIZED 4

/* The raw-thread calls of pycore_pythread.h: an identity that stays put for
   the thread's life, and startable, joinable threads. */
typedef unsigned long long PyThread_ident_t;
typedef uintptr_t PyThread_handle_t;
PyThread_ident_t PyThread_get_thread_ident_ex(void);
int PyThread_start_joinable_thread(void (*func)(void *), void *arg,
                                   PyThread_ident_t *ident, PyThread_handle_t *handle);
int PyThread_join_thread(PyThread_handle_t th);
int PyThread_detach_thread(PyThread_handle_t th);

/* A recursive mutex and a readers-writer lock (pycore_lock.h). */
typedef struct {
    PyMutex mutex;
    unsigned long long thread;  /* PyThread_get_thread_ident_ex() of the owner */
    size_t level;
} _PyRecursiveMutex;

typedef struct {
    uintptr_t bits;
} _PyRWMutex;

int _PyRecursiveMutex_IsLockedByCurrentThread(_PyRecursiveMutex *m);
void _PyRecursiveMutex_Lock(_PyRecursiveMutex *m);
void _PyRecursiveMutex_Unlock(_PyRecursiveMutex *m);
int _PyRecursiveMutex_TryUnlock(_PyRecursiveMutex *m);
void _PyRWMutex_RLock(_PyRWMutex *rwmutex);
void _PyRWMutex_RUnlock(_PyRWMutex *rwmutex);
void _PyRWMutex_Lock(_PyRWMutex *rwmutex);
void _PyRWMutex_Unlock(_PyRWMutex *rwmutex);

typedef int _Py_once_fn_t(void *arg);
int _PyOnceFlag_CallOnceSlow(_PyOnceFlag *flag, _Py_once_fn_t *fn, void *arg);

typedef enum _PyLockFlags {
    _Py_LOCK_DONT_DETACH = 0,           /* stay attached while waiting */
    _PY_LOCK_DETACH = 1,                /* detach while waiting */
    _PY_LOCK_HANDLE_SIGNALS = 2,        /* run pending calls when interrupted */
} _PyLockFlags;

PyLockStatus _PyMutex_LockTimed(PyMutex *m, PyTime_t timeout_ns, _PyLockFlags flags);
int _PyMutex_TryUnlock(PyMutex *m);

/* PyEvent (Python/lock.c); the type is in lucifer.h. */
int _PyEvent_IsSet(PyEvent *evt);
void _PyEvent_Notify(PyEvent *evt);
void PyEvent_Wait(PyEvent *evt);
int PyEvent_WaitTimed(PyEvent *evt, PyTime_t timeout_ns, int detach);

/* The parking lot: compare-and-sleep on an address, and wake. */
enum {
    Py_PARK_OK = LU_PARK_OK,            /* unparked by another thread */
    Py_PARK_AGAIN = -1,                 /* *address did not match *expected */
    Py_PARK_TIMEOUT = LU_PARK_TIMEOUT,
    Py_PARK_INTR = LU_PARK_INTR,
};

typedef void _Py_unpark_fn_t(void *arg, void *park_arg, int has_more_waiters);

int _PyParkingLot_Park(const void *address, const void *expected, size_t address_size,
                       PyTime_t timeout_ns, void *park_arg, int detach);
void _PyParkingLot_Unpark(const void *address, _Py_unpark_fn_t *fn, void *arg);
void _PyParkingLot_UnparkAll(const void *address);

/* A tstate's critical_section word is a tagged pointer to its top section. */
#define _Py_CRITICAL_SECTION_INACTIVE       0x1
#define _Py_CRITICAL_SECTION_TWO_MUTEXES    0x2
#define _Py_CRITICAL_SECTION_MASK           0x3

/* Called by the syscall bracket (lu_syscall_begin and lu_syscall_end). */
void _PyCriticalSection_SuspendAll(PyThreadState *tstate);
void _PyCriticalSection_Resume(PyThreadState *tstate);

#endif
