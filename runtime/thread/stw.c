/* Stop-the-world (stop_the_world and start_the_world of Python/pystate.c,
   CPython 3.14t), over tasks (DESIGN 5.6).

   A request counts every other task in the interpreter. A detached task is
   stopped at once, by moving its state from detached to suspended; an
   attached task is asked, through its carrier's poll word, and stops at its
   next poll by suspending itself. Tasks do not acknowledge for their carrier:
   a running task is the one its carrier runs, so the cost is the number of
   attached carriers. A suspended task attaches only when the world restarts,
   which moves every task back to detached.

   The task list lock orders it all: it guards the request, the countdown and
   the walks, and creation and deletion of tasks tell a request in progress.
   The state field of each task is the only thing that says whether it is
   stopped. */

#include "thread/lock.h"

#define TASK(ts) ((struct lu_task *)(ts))
#define HEAD_LOCK(interp) pthread_mutex_lock(&(interp)->tasks_lock.mutex)
#define HEAD_UNLOCK(interp) pthread_mutex_unlock(&(interp)->tasks_lock.mutex)

/* Counts one task as stopped; the last one wakes the requester. */
static void
decrement_countdown(struct lu_stw *stw)
{
    if (--stw->countdown == 0) {
        _PyEvent_Notify(&stw->stop_event);
    }
}

/* Stops the detached tasks and asks the attached ones. True when this walk
   stopped the last. */
static int
park_detached_tasks(struct _is *interp)
{
    struct lu_stw *stw = &interp->stw;
    int num_parked = 0;
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        int state = __atomic_load_n(&ts->state, __ATOMIC_RELAXED);
        if (state == _Py_THREAD_DETACHED) {
            if (__atomic_compare_exchange_n(&ts->state, &state, _Py_THREAD_SUSPENDED, 0,
                                            __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
                num_parked++;
            }
        }
        else if (state == _Py_THREAD_ATTACHED && TASK(ts) != stw->requester
                 && TASK(ts)->carrier != NULL) {
            lu_poll_set(TASK(ts)->carrier, LU_POLL_PLEASE_STOP);
        }
    }
    stw->countdown -= num_parked;
    return num_parked > 0 && stw->countdown == 0;
}

void
lu_stw_stop(struct _is *interp)
{
    struct lu_stw *stw = &interp->stw;
    PyMutex_Lock(&stw->mutex);

    HEAD_LOCK(interp);
    stw->requested = 1;
    stw->countdown = 0;
    stw->stop_event.v = 0;
    struct lu_task *cur = lu_current_task();
    stw->requester = cur != NULL && cur->ts.state == _Py_THREAD_ATTACHED ? cur : NULL;
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        if (TASK(ts) != stw->requester) {
            stw->countdown++;
        }
    }
    if (stw->countdown == 0) {
        HEAD_UNLOCK(interp);
        __atomic_store_n(&stw->world_stopped, 1, __ATOMIC_RELEASE);
        return;
    }
    for (;;) {
        int stopped_all = park_detached_tasks(interp);
        HEAD_UNLOCK(interp);
        if (stopped_all) {
            break;
        }
        /* An attached task stops by itself, which notifies; one that detached
           since the walk is found by the next. */
        if (PyEvent_WaitTimed(&stw->stop_event, 1000 * 1000, 0)) {
            break;
        }
        HEAD_LOCK(interp);
    }
    __atomic_store_n(&stw->world_stopped, 1, __ATOMIC_RELEASE);
}

void
lu_stw_start(struct _is *interp)
{
    struct lu_stw *stw = &interp->stw;
    HEAD_LOCK(interp);
    stw->requested = 0;
    __atomic_store_n(&stw->world_stopped, 0, __ATOMIC_RELEASE);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        if (TASK(ts) != stw->requester) {
            int expected = _Py_THREAD_SUSPENDED;
            if (__atomic_compare_exchange_n(&ts->state, &expected, _Py_THREAD_DETACHED, 0,
                                            __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
                _PyParkingLot_UnparkAll(&ts->state);
            }
        }
    }
    stw->requester = NULL;
    HEAD_UNLOCK(interp);
    PyMutex_Unlock(&stw->mutex);
}

/* _PyThreadState_Suspend, then the attach that waits out the stop
   (tstate_wait_attach). A task found asking is attached: it stops itself
   and counts. A stale request, for a stop that is over, finds none and
   merely detaches and attaches. */
void
lu_stw_acknowledge(struct lu_task *t)
{
    struct _is *interp = t->ts.interp;
    HEAD_LOCK(interp);
    int requested = interp->stw.requested;
    HEAD_UNLOCK(interp);
    if (!requested) {
        lu_task_detach(t, _Py_THREAD_DETACHED);
    }
    else {
        lu_task_detach(t, _Py_THREAD_SUSPENDED);
        HEAD_LOCK(interp);
        decrement_countdown(&interp->stw);
        HEAD_UNLOCK(interp);
    }
    lu_syscall_end(&t->ts);
}

void
lu_stw_task_new(struct _is *interp, struct lu_task *t)
{
    if (interp->stw.requested) {
        t->ts.state = _Py_THREAD_SUSPENDED;
    }
}

void
lu_stw_task_delete(struct _is *interp, struct lu_task *t)
{
    if (t->ts.state != _Py_THREAD_SUSPENDED && interp->stw.requested) {
        decrement_countdown(&interp->stw);
    }
}

LU_EXPORT void
_PyEval_StopTheWorld(PyInterpreterState *interp)
{
    lu_stw_stop(interp);
}

LU_EXPORT void
_PyEval_StartTheWorld(PyInterpreterState *interp)
{
    lu_stw_start(interp);
}
