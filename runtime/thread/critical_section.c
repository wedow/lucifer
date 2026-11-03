/* Critical sections (Python/critical_section.c and the inline functions of
   Include/internal/pycore_critical_section.h, CPython 3.14): per-object
   PyMutex locks kept as a stack in the tstate. A section is suspended, its
   locks released, whenever the task detaches (the syscall bracket, or a
   park inside PyMutex_Lock), and the top one is resumed on attach. */

#include "thread/lock.h"

static_assert(_Alignof(PyCriticalSection) >= 4,
              "critical section must be aligned to at least 4 bytes");

static PyCriticalSection *
untag_critical_section(uintptr_t tag)
{
    return (PyCriticalSection *)(tag & ~_Py_CRITICAL_SECTION_MASK);
}

static int
is_active(uintptr_t tag)
{
    return tag != 0 && (tag & _Py_CRITICAL_SECTION_INACTIVE) == 0;
}

static int
lock_fast(PyMutex *m)
{
    uint8_t expected = _Py_UNLOCKED;
    return _Py_atomic_compare_exchange_uint8(&m->_bits, &expected, _Py_LOCKED);
}

/* While the world is stopped no other task runs, so a section takes no lock
   (and could deadlock on one held by a task that is stopped). */
LU_EXPORT void
_PyCriticalSection_BeginSlow(PyCriticalSection *c, PyMutex *m)
{
    PyThreadState *tstate = PyThreadState_GetUnchecked();
    // As an optimisation for locking the same object recursively, skip
    // locking if the mutex is currently locked by the top-most critical
    // section.
    // If the top-most critical section is a two-mutex critical section,
    // then locking is skipped if either mutex is m.
    if (tstate->critical_section) {
        PyCriticalSection *prev = untag_critical_section(tstate->critical_section);
        if (prev->_cs_mutex == m) {
            c->_cs_mutex = NULL;
            c->_cs_prev = 0;
            return;
        }
        if (tstate->critical_section & _Py_CRITICAL_SECTION_TWO_MUTEXES) {
            PyCriticalSection2 *prev2 = (PyCriticalSection2 *)
                untag_critical_section(tstate->critical_section);
            if (prev2->_cs_mutex2 == m) {
                c->_cs_mutex = NULL;
                c->_cs_prev = 0;
                return;
            }
        }
    }
    if (lu_world_stopped(tstate->interp)) {
        c->_cs_mutex = NULL;
        c->_cs_prev = 0;
        return;
    }
    c->_cs_mutex = NULL;
    c->_cs_prev = (uintptr_t)tstate->critical_section;
    tstate->critical_section = (uintptr_t)c;

    PyMutex_Lock(m);
    c->_cs_mutex = m;
}

LU_EXPORT void
_PyCriticalSection2_BeginSlow(PyCriticalSection2 *c, PyMutex *m1, PyMutex *m2,
                              int is_m1_locked)
{
    PyThreadState *tstate = PyThreadState_GetUnchecked();
    if (lu_world_stopped(tstate->interp)) {
        c->_cs_base._cs_mutex = NULL;
        c->_cs_mutex2 = NULL;
        c->_cs_base._cs_prev = 0;
        return;
    }
    // Same optimization as in _PyCriticalSection_BeginSlow: skip locking when
    // recursively acquiring the same locks.
    if (tstate->critical_section &&
        tstate->critical_section & _Py_CRITICAL_SECTION_TWO_MUTEXES) {
        PyCriticalSection2 *prev2 = (PyCriticalSection2 *)
            untag_critical_section(tstate->critical_section);
        assert((uintptr_t)m1 < (uintptr_t)m2);
        assert((uintptr_t)prev2->_cs_base._cs_mutex <
            (uintptr_t)prev2->_cs_mutex2);
        if (prev2->_cs_base._cs_mutex == m1 && prev2->_cs_mutex2 == m2) {
            c->_cs_base._cs_mutex = NULL;
            c->_cs_mutex2 = NULL;
            c->_cs_base._cs_prev = 0;
            return;
        }
    }
    c->_cs_base._cs_mutex = NULL;
    c->_cs_mutex2 = NULL;
    c->_cs_base._cs_prev = tstate->critical_section;
    tstate->critical_section = (uintptr_t)c | _Py_CRITICAL_SECTION_TWO_MUTEXES;

    if (!is_m1_locked) {
        PyMutex_Lock(m1);
    }
    PyMutex_Lock(m2);
    c->_cs_base._cs_mutex = m1;
    c->_cs_mutex2 = m2;
}

// Release all locks held by critical sections. This is called by
// lu_syscall_begin.
LU_EXPORT void
_PyCriticalSection_SuspendAll(PyThreadState *tstate)
{
    uintptr_t *tagptr = &tstate->critical_section;
    while (is_active(*tagptr)) {
        PyCriticalSection *c = untag_critical_section(*tagptr);

        if (c->_cs_mutex) {
            PyMutex_Unlock(c->_cs_mutex);
            if ((*tagptr & _Py_CRITICAL_SECTION_TWO_MUTEXES)) {
                PyCriticalSection2 *c2 = (PyCriticalSection2 *)c;
                if (c2->_cs_mutex2) {
                    PyMutex_Unlock(c2->_cs_mutex2);
                }
            }
        }

        *tagptr |= _Py_CRITICAL_SECTION_INACTIVE;
        tagptr = &c->_cs_prev;
    }
}

LU_EXPORT void
_PyCriticalSection_Resume(PyThreadState *tstate)
{
    uintptr_t p = tstate->critical_section;
    PyCriticalSection *c = untag_critical_section(p);
    assert(!is_active(p));

    PyMutex *m1 = c->_cs_mutex;
    c->_cs_mutex = NULL;

    PyMutex *m2 = NULL;
    PyCriticalSection2 *c2 = NULL;
    if ((p & _Py_CRITICAL_SECTION_TWO_MUTEXES)) {
        c2 = (PyCriticalSection2 *)c;
        m2 = c2->_cs_mutex2;
        c2->_cs_mutex2 = NULL;
    }

    if (m1) {
        PyMutex_Lock(m1);
    }
    if (m2) {
        PyMutex_Lock(m2);
    }

    c->_cs_mutex = m1;
    if (m2) {
        c2->_cs_mutex2 = m2;
    }

    tstate->critical_section &= ~_Py_CRITICAL_SECTION_INACTIVE;
}

void
PyCriticalSection_BeginMutex(PyCriticalSection *c, PyMutex *m)
{
    if (lock_fast(m)) {
        PyThreadState *tstate = PyThreadState_GetUnchecked();
        c->_cs_mutex = m;
        c->_cs_prev = tstate->critical_section;
        tstate->critical_section = (uintptr_t)c;
    }
    else {
        _PyCriticalSection_BeginSlow(c, m);
    }
}

void
PyCriticalSection_Begin(PyCriticalSection *c, PyObject *op)
{
    PyCriticalSection_BeginMutex(c, &op->ob_mutex);
}

// Removes the top-most critical section from the thread's stack of critical
// sections. If the new top-most critical section is inactive, then it is
// resumed.
static void
pop(PyCriticalSection *c)
{
    PyThreadState *tstate = PyThreadState_GetUnchecked();
    uintptr_t prev = c->_cs_prev;
    tstate->critical_section = prev;

    if ((prev & _Py_CRITICAL_SECTION_INACTIVE) != 0) {
        _PyCriticalSection_Resume(tstate);
    }
}

void
PyCriticalSection_End(PyCriticalSection *c)
{
    // If the mutex is NULL, we used the fast path in
    // _PyCriticalSection_BeginSlow for locks already held in the top-most
    // critical section, and we shouldn't unlock or pop this critical section.
    if (c->_cs_mutex == NULL) {
        return;
    }
    PyMutex_Unlock(c->_cs_mutex);
    pop(c);
}

void
PyCriticalSection2_BeginMutex(PyCriticalSection2 *c, PyMutex *m1, PyMutex *m2)
{
    if (m1 == m2) {
        // If the two mutex arguments are the same, treat this as a critical
        // section with a single mutex.
        c->_cs_mutex2 = NULL;
        PyCriticalSection_BeginMutex(&c->_cs_base, m1);
        return;
    }

    if ((uintptr_t)m2 < (uintptr_t)m1) {
        // Sort the mutexes so that the lower address is locked first.
        // The exact order does not matter, but we need to acquire the mutexes
        // in a consistent order to avoid lock ordering deadlocks.
        PyMutex *tmp = m1;
        m1 = m2;
        m2 = tmp;
    }

    if (lock_fast(m1)) {
        if (lock_fast(m2)) {
            PyThreadState *tstate = PyThreadState_GetUnchecked();
            c->_cs_base._cs_mutex = m1;
            c->_cs_mutex2 = m2;
            c->_cs_base._cs_prev = tstate->critical_section;

            uintptr_t p = (uintptr_t)c | _Py_CRITICAL_SECTION_TWO_MUTEXES;
            tstate->critical_section = p;
        }
        else {
            _PyCriticalSection2_BeginSlow(c, m1, m2, 1);
        }
    }
    else {
        _PyCriticalSection2_BeginSlow(c, m1, m2, 0);
    }
}

void
PyCriticalSection2_Begin(PyCriticalSection2 *c, PyObject *a, PyObject *b)
{
    PyCriticalSection2_BeginMutex(c, &a->ob_mutex, &b->ob_mutex);
}

void
PyCriticalSection2_End(PyCriticalSection2 *c)
{
    // if mutex1 is NULL, we used the fast path in either
    // _PyCriticalSection_BeginSlow or _PyCriticalSection2_BeginSlow for mutexes
    // that are already held, and mutex2 should also be NULL.
    if (c->_cs_base._cs_mutex == NULL) {
        assert(c->_cs_mutex2 == NULL);
        return;
    }
    if (c->_cs_mutex2) {
        PyMutex_Unlock(c->_cs_mutex2);
    }
    PyMutex_Unlock(c->_cs_base._cs_mutex);
    pop(&c->_cs_base);
}
