/* Tasks (DESIGN 5.3): a task is a PyThreadState plus Lucifer's tail. The
   scheduler is degenerate: a task is bound to the carrier that creates it,
   for life, which is cp314t's 1:1 threads. Python/pystate.c's lifecycle
   arrives with K1p; this is the record and its one binding. */

#define _GNU_SOURCE
#include "thread/lock.h"

#include <errno.h>
#include <unistd.h>

#define LU_DEFAULT_RECURSION_LIMIT 1000         /* Py_DEFAULT_RECURSION_LIMIT */

/* Binding rewrites the carrier-owned tstate fields and raises the task's
   pending reasons into the carrier's poll word. */
static void
bind(struct lu_carrier *c, struct lu_task *t)
{
    PyThreadState *ts = &t->ts;
    c->task = t;
    c->poll = ts->interp->monitoring_version | t->pending;
    ts->eval_breaker = c->poll;
    ts->thread_id = (unsigned long)pthread_self();
    ts->native_thread_id = (unsigned long)gettid();
    ts->_status.bound = 1;
    ts->_status.active = 1;
    ts->state = _Py_THREAD_ATTACHED;
}

struct lu_task *
lu_task_new(struct _is *interp)
{
    lu_current_entry();
    struct lu_task *t = PyMem_RawCalloc(1, sizeof *t);
    if (t == NULL) {
        return NULL;
    }
    PyThreadState *ts = &t->ts;
    ts->interp = interp;
    ts->_whence = 1;                            /* _PyThreadState_WHENCE_INIT */
    ts->_status.initialized = 1;
    ts->py_recursion_limit = LU_DEFAULT_RECURSION_LIMIT;
    ts->py_recursion_remaining = LU_DEFAULT_RECURSION_LIMIT;
    ts->what_event = -1;
    ts->exc_info = &ts->exc_state;
    ts->context_ver = 1;
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    ts->id = ++interp->next_task_id;
    ts->next = interp->tasks;
    if (ts->next != NULL) {
        ts->next->prev = ts;
    }
    interp->tasks = ts;
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    t->pinned_for_life = 1;
    bind(lu_carrier, t);
    return t;
}

/* _PyThreadState_Detach. Stop-the-world (K1q) counts a detached task as
   stopped. */
PyThreadState *
lu_syscall_begin(void)
{
    struct lu_task *t = lu_current_task();
    if (t == NULL || t->ts.state != _Py_THREAD_ATTACHED) {
        return NULL;
    }
    PyThreadState *ts = &t->ts;
    if (ts->critical_section != 0) {
        _PyCriticalSection_SuspendAll(ts);
    }
    __atomic_store_n(&ts->state, _Py_THREAD_DETACHED, __ATOMIC_SEQ_CST);
    return ts;
}

/* _PyThreadState_Attach. With stop-the-world (K1q) it waits here while the
   world is stopped. */
void
lu_syscall_end(PyThreadState *ts)
{
    if (ts == NULL) {
        return;
    }
    int err = errno;
    __atomic_store_n(&ts->state, _Py_THREAD_ATTACHED, __ATOMIC_SEQ_CST);
    if (ts->critical_section != 0) {
        _PyCriticalSection_Resume(ts);
    }
    errno = err;
}

/* The foreign-thread half of PyGILState_Ensure: a thread without a task gets
   one, pinned for life. Release and the gilstate counter arrive with the
   rest of the tstate API (K1p). */
PyGILState_STATE
PyGILState_Ensure(void)
{
    struct lu_task *t = lu_current_task();
    if (t == NULL) {
        if (lu_task_new(&_PyRuntime.main_interp) == NULL) {
            Py_FatalError("Couldn't create thread-state for new thread");
        }
        return PyGILState_UNLOCKED;
    }
    if (t->ts.state != _Py_THREAD_ATTACHED) {
        lu_syscall_end(&t->ts);
        return PyGILState_UNLOCKED;
    }
    return PyGILState_LOCKED;
}

PyThreadState *
PyThreadState_GetUnchecked(void)
{
    struct lu_task *t = lu_current_task();
    return t != NULL ? &t->ts : NULL;
}

PyThreadState *
PyThreadState_Get(void)
{
    PyThreadState *ts = PyThreadState_GetUnchecked();
    if (ts == NULL) {
        fprintf(stderr, "Fatal Python error: PyThreadState_Get: the function must be "
                "called with the GIL held, after Python initialization and before "
                "Python finalization, but the GIL is released (the current Python "
                "thread state is NULL)\n");
        abort();
    }
    return ts;
}

PyInterpreterState *
PyInterpreterState_Get(void)
{
    return PyThreadState_Get()->interp;
}

_PyFrameEvalFunction
_PyInterpreterState_GetEvalFrameFunc(PyInterpreterState *interp)
{
    return interp->eval_frame != NULL ? interp->eval_frame : _PyEval_EvalFrameDefault;
}

void
_PyInterpreterState_SetEvalFrameFunc(PyInterpreterState *interp, _PyFrameEvalFunction eval_frame)
{
    interp->eval_frame = eval_frame == _PyEval_EvalFrameDefault ? NULL : eval_frame;
}

int
lu_poll_slow(PyThreadState *ts)
{
    /* No reason is raised yet: each arrives with its owner (K1q). */
    return 0;
}
