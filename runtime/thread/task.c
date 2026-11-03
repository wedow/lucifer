/* Tasks (DESIGN 5.3): a task is a PyThreadState plus Lucifer's tail. The
   scheduler is degenerate: a task is bound to the carrier that creates it,
   for life, which is cp314t's 1:1 threads. Python/pystate.c's lifecycle
   arrives with K1p; this is the record and its one binding. */

#define _GNU_SOURCE
#include "lucifer.h"

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
    ts->state = 1;                              /* _Py_THREAD_ATTACHED */
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
    ts->id = ++interp->next_task_id;
    ts->_whence = 1;                            /* _PyThreadState_WHENCE_INIT */
    ts->_status.initialized = 1;
    ts->py_recursion_limit = LU_DEFAULT_RECURSION_LIMIT;
    ts->py_recursion_remaining = LU_DEFAULT_RECURSION_LIMIT;
    ts->what_event = -1;
    ts->exc_info = &ts->exc_state;
    ts->context_ver = 1;
    /* The interpreter's task list takes a lock once a second task exists
       (K1p). */
    ts->next = interp->tasks;
    if (ts->next != NULL) {
        ts->next->prev = ts;
    }
    interp->tasks = ts;
    t->pinned_for_life = 1;
    bind(lu_carrier, t);
    return t;
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
