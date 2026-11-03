/* The poll handler (_Py_HandlePending of Python/ceval_gil.c, FOUNDATIONS
   2.5): every reason in a carrier's poll word is handled here, for the
   interpreter and later the JIT. A reason is raised by setting its bit, and
   the handler clears the bit before acting on it, so that a reason raised
   meanwhile is seen at the next poll.

   Also here: pending calls (Py_AddPendingCall) and PyThreadState_SetAsyncExc,
   the two reasons whose producers are other threads' calls. */

#include "lucifer.h"

#define TASK(ts) ((struct lu_task *)(ts))

/* A reason aimed at the polling task: cleared from the task and the word. */
static void
unset_targeted(struct lu_task *t, uintptr_t bits)
{
    __atomic_fetch_and(&t->pending, ~bits, __ATOMIC_SEQ_CST);
    lu_poll_clear(t->carrier, bits);
}

/* Reasons with no producer yet: the GIL is never requested, there is no JIT
   or scheduler policy to preempt for, and the remote debugger arrives later. */
#define LU_POLL_NO_PRODUCER (LU_POLL_GIL_DROP_REQUEST | LU_POLL_JIT_INVALIDATE_COLD \
    | LU_POLL_PREEMPT | LU_POLL_DEOPT_SELF | LU_POLL_REMOTE_EXEC)

int
lu_poll_slow(PyThreadState *ts)
{
    struct lu_task *t = TASK(ts);
    struct lu_carrier *c = t->carrier;
    uintptr_t breaker = __atomic_load_n(&c->poll, __ATOMIC_ACQUIRE);

    /* Stop-the-world. The attach inside blocks until the stop is over. */
    if (breaker & LU_POLL_PLEASE_STOP) {
        lu_poll_clear(c, LU_POLL_PLEASE_STOP);
        lu_stw_acknowledge(t);
    }
    if (breaker & LU_POLL_SIGNALS_PENDING) {
        if (lu_signals_handle(ts) < 0) {
            return -1;
        }
    }
    if (breaker & LU_POLL_CALLS_TO_DO) {
        if (lu_pending_calls_handle(ts) < 0) {
            return -1;
        }
    }
    /* Objects with refcounts to merge, and storage retired to QSBR. */
    if (breaker & LU_POLL_EXPLICIT_MERGE) {
        lu_poll_clear(c, LU_POLL_EXPLICIT_MERGE);
        lu_brc_merge(lu_entry_of(c, ts->interp));
        if (c->qsbr.should_process) {
            lu_qsbr_process(c);
        }
    }
    /* Collection runs only here. */
    if (breaker & LU_POLL_GC_SCHEDULED) {
        lu_poll_clear(c, LU_POLL_GC_SCHEDULED);
        lu_gc_run(ts);
    }
    /* The stress tick (FOUNDATIONS 2.7) becomes a collection with M0.2. */
    if (breaker & LU_POLL_STRESS_TICK) {
        lu_poll_clear(c, LU_POLL_STRESS_TICK);
    }
    if (breaker & LU_POLL_NO_PRODUCER) {
        unset_targeted(t, breaker & LU_POLL_NO_PRODUCER);
    }
    /* An asynchronous exception, set by PyThreadState_SetAsyncExc. */
    if (breaker & LU_POLL_ASYNC_EXCEPTION) {
        unset_targeted(t, LU_POLL_ASYNC_EXCEPTION);
        PyObject *exc = __atomic_exchange_n(&ts->async_exc, NULL, __ATOMIC_SEQ_CST);
        if (exc != NULL) {
            PyErr_SetNone(exc);
            Py_DECREF(exc);
            return -1;
        }
    }
    return 0;
}

LU_EXPORT int
_Py_HandlePending(PyThreadState *ts)
{
    return lu_poll_slow(ts);
}

/* PyThreadState_SetAsyncExc: the task whose thread id is `id` raises `exc`
   at its next poll. Returns the number of tasks changed. The task list lock
   keeps the task and its carrier alive while the reason is raised. */
int
PyThreadState_SetAsyncExc(unsigned long id, PyObject *exc)
{
    struct _is *interp = PyThreadState_Get()->interp;
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        if (ts->thread_id != id) {
            continue;
        }
        Py_XINCREF(exc);
        PyObject *old = __atomic_exchange_n(&ts->async_exc, exc, __ATOMIC_SEQ_CST);
        lu_task_signal(TASK(ts), LU_POLL_ASYNC_EXCEPTION);
        pthread_mutex_unlock(&interp->tasks_lock.mutex);
        Py_XDECREF(old);
        return 1;
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    return 0;
}

/* Pending calls (Python/ceval_gil.c). A call added for the main thread runs
   there; the others run in whichever task polls first. */

#define PENDING_MAINTHREADONLY 1        /* _Py_PENDING_MAINTHREADONLY */
#define PENDING_RAWFREE 2               /* _Py_PENDING_RAWFREE */

LU_EXPORT int
_PyEval_AddPendingCall(PyInterpreterState *interp, int (*func)(void *), void *arg, int flags)
{
    struct lu_pending *p = &interp->pending;
    struct lu_pending_call *call = calloc(1, sizeof *call);
    if (call == NULL) {
        return -1;
    }
    call->func = func;
    call->arg = arg;
    call->main_only = (flags & PENDING_MAINTHREADONLY) != 0;
    call->rawfree = (flags & PENDING_RAWFREE) != 0;
    PyMutex_Lock(&p->mutex);
    if (p->tail == NULL) {
        p->tail = &p->head;
    }
    *p->tail = call;
    p->tail = &call->next;
    PyMutex_Unlock(&p->mutex);

    if (call->main_only) {
        struct lu_carrier *main = __atomic_load_n(&_PyRuntime.main_carrier, __ATOMIC_ACQUIRE);
        if (main != NULL) {
            lu_poll_set(main, LU_POLL_CALLS_TO_DO);
        }
    }
    else {
        pthread_mutex_lock(&interp->tasks_lock.mutex);
        for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
            struct lu_carrier *c = TASK(ts)->carrier;
            if (c != NULL) {
                lu_poll_set(c, LU_POLL_CALLS_TO_DO);
            }
        }
        pthread_mutex_unlock(&interp->tasks_lock.mutex);
    }
    return 0;
}

int
Py_AddPendingCall(int (*func)(void *), void *arg)
{
    return _PyEval_AddPendingCall(&_PyRuntime.main_interp, func, arg, PENDING_MAINTHREADONLY);
}

/* How many queued calls this task may run. */
static int
count_calls(struct lu_pending *p, int main_thread)
{
    int n = 0;
    PyMutex_Lock(&p->mutex);
    for (struct lu_pending_call *call = p->head; call != NULL; call = call->next) {
        n += main_thread || !call->main_only;
    }
    PyMutex_Unlock(&p->mutex);
    return n;
}

/* The first queued call this task may run, unlinked, or NULL. */
static struct lu_pending_call *
pop_call(struct lu_pending *p, int main_thread)
{
    struct lu_pending_call *found = NULL, **link = &p->head;
    PyMutex_Lock(&p->mutex);
    for (; *link != NULL; link = &(*link)->next) {
        if (main_thread || !(*link)->main_only) {
            found = *link;
            *link = found->next;
            if (p->tail == &found->next) {
                p->tail = link;
            }
            break;
        }
    }
    PyMutex_Unlock(&p->mutex);
    return found;
}

/* make_pending_calls. The reason is cleared before the calls run, so that a
   call added meanwhile raises it again. A task already running the queue
   (a call that polls) leaves the rest to its outer loop, and a loop runs
   only as many calls as were queued when it began, so that calls that keep
   adding calls cannot starve the task: it polls again for the rest. */
int
lu_pending_calls_handle(PyThreadState *ts)
{
    struct lu_task *t = TASK(ts);
    struct lu_pending *p = &ts->interp->pending;
    lu_poll_clear(t->carrier, LU_POLL_CALLS_TO_DO);
    if (t->in_pending_calls) {
        return 0;
    }
    int main_thread = lu_thread_can_handle_signals();
    int budget = count_calls(p, main_thread);
    int res = 0;
    t->in_pending_calls = 1;
    while (budget-- > 0 && res == 0) {
        struct lu_pending_call *call = pop_call(p, main_thread);
        if (call == NULL) {
            break;
        }
        res = call->func(call->arg) != 0 ? -1 : 0;
        if (call->rawfree && call->arg != NULL) {
            PyMem_RawFree(call->arg);
        }
        free(call);
    }
    t->in_pending_calls = 0;
    if (res != 0 || count_calls(p, main_thread) > 0) {
        lu_poll_set(t->carrier, LU_POLL_CALLS_TO_DO);
    }
    return res;
}

LU_EXPORT int
_PyEval_MakePendingCalls(PyThreadState *ts)
{
    if (lu_thread_can_handle_signals() && lu_signals_handle(ts) < 0) {
        return -1;
    }
    return lu_pending_calls_handle(ts);
}

int
Py_MakePendingCalls(void)
{
    return _PyEval_MakePendingCalls(PyThreadState_Get());
}
