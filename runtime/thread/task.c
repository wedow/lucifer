/* Tasks (DESIGN 5.3): a task is a PyThreadState plus Lucifer's tail. The
   scheduler is degenerate: a task is bound to the thread that creates it,
   for life, which is cp314t's 1:1 threads. The lifecycle follows
   Python/pystate.c; the public API over it is runtime/capi/pystate.c.

   A carrier's `task` is the task last attached to it, and the thread's
   tstate while that task is attached. */

#define _GNU_SOURCE
#include "thread/lock.h"

#include <errno.h>
#include <unistd.h>

#define LU_DEFAULT_RECURSION_LIMIT 1000         /* Py_DEFAULT_RECURSION_LIMIT */

#define TASK(ts) ((struct lu_task *)(ts))

struct lu_task *
lu_task_new(struct _is *interp, int whence)
{
    struct lu_task *t = PyMem_RawCalloc(1, sizeof *t);
    if (t == NULL) {
        return NULL;
    }
    PyThreadState *ts = &t->ts;
    ts->interp = interp;
    ts->_whence = whence;
    ts->_status.initialized = 1;
    ts->gilstate_counter = 1;
    ts->py_recursion_limit = LU_DEFAULT_RECURSION_LIMIT;
    ts->py_recursion_remaining = LU_DEFAULT_RECURSION_LIMIT;
    ts->what_event = -1;
    ts->exc_info = &ts->exc_state;
    ts->context_ver = 1;
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    lu_stw_task_new(interp, t);
    ts->id = ++interp->next_task_id;
    ts->next = interp->tasks;
    if (ts->next != NULL) {
        ts->next->prev = ts;
    }
    interp->tasks = ts;
    if (interp->tasks_closed) {
        ts->state = _Py_THREAD_SHUTTING_DOWN;
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    t->pinned_for_life = 1;
    return t;
}

/* bind_tstate. The thread's first allocation or task creates its carrier, and
   registering the owner record comes before the task's first refcount
   operation. */
static void
bind_tstate(struct lu_task *t)
{
    PyThreadState *ts = &t->ts;
    lu_current_entry();
    struct lu_entry *e = lu_entry_of(lu_carrier, ts->interp);
    __atomic_store_n(&t->carrier, lu_carrier, __ATOMIC_SEQ_CST);
    if (ts->thread_id == 0) {
        ts->thread_id = (unsigned long)pthread_self();      /* the task's ident, for life */
    }
    ts->native_thread_id = lu_carrier->native_id;
    e->gc_alloc_count += t->gc_alloc_count;
    t->gc_alloc_count = 0;
    e->nbound++;
    if (e->brc.tid == 0) {
        lu_brc_register(e);
    }
    else if (e->brc.idle) {
        lu_brc_set_idle(e, 0);
    }
    ts->_status.bound = 1;
}

/* bind_gilstate_tstate */
static void
bind_gilstate_tstate(struct lu_task *t)
{
    struct lu_task *old = lu_carrier->gilstate;
    if (old != NULL) {
        old->ts._status.bound_gilstate = 0;
    }
    lu_carrier->gilstate = t;
    t->ts._status.bound_gilstate = 1;
}

void
lu_task_bind(struct lu_task *t)
{
    bind_tstate(t);
    if (lu_carrier->gilstate == NULL) {
        bind_gilstate_tstate(t);
    }
}

/* Makes `t` the task of the carrier, rewriting the fields the carrier owns:
   the poll word and its mirror. The word keeps the carrier's own reasons and
   the monitoring version of `t`'s interpreter; the reasons targeted at the
   old task leave with it and those targeted at `t` arrive. Other threads
   raise reasons in the word meanwhile, so it is rewritten by compare and
   exchange, reading the pending reasons afresh each time: a reason raised
   after the read finds the word changed, one raised before is in the read. */
static void
install(struct lu_carrier *c, struct lu_task *t)
{
    struct lu_task *old = c->task;
    uintptr_t version = t->ts.interp->monitoring_version;
    uintptr_t cur = __atomic_load_n(&c->poll, __ATOMIC_RELAXED), next;
    do {
        uintptr_t old_pending = old != NULL ? __atomic_load_n(&old->pending, __ATOMIC_SEQ_CST) : 0;
        next = version | (cur & LU_POLL_EVENTS_MASK & ~old_pending)
            | __atomic_load_n(&t->pending, __ATOMIC_SEQ_CST);
    } while (!__atomic_compare_exchange_n(&c->poll, &cur, next, 0,
                                          __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
    t->ts.eval_breaker = next;
    c->task = t;
}

void
lu_task_unsignal(struct lu_task *t, uintptr_t bits)
{
    __atomic_fetch_and(&t->pending, ~bits, __ATOMIC_SEQ_CST);
    lu_poll_clear(t->carrier, bits);
}

void
lu_task_signal(struct lu_task *t, uintptr_t bits)
{
    __atomic_fetch_or(&t->pending, bits, __ATOMIC_SEQ_CST);
    struct lu_carrier *c = __atomic_load_n(&t->carrier, __ATOMIC_ACQUIRE);
    if (c != NULL) {
        lu_poll_set(c, bits);
    }
}

/* _PyThreadState_Attach. */
void
lu_task_attach(PyThreadState *ts)
{
    if (TASK(ts)->carrier != lu_carrier) {
        Py_FatalError("PyEval_RestoreThread: the thread state is bound to another thread");
    }
    if (PyThreadState_GetUnchecked() != NULL) {
        Py_FatalError("non-NULL old thread state");
    }
    if (lu_carrier->task != TASK(ts)) {
        install(lu_carrier, TASK(ts));
    }
    lu_syscall_end(ts);
}

/* detach_thread: the task stops using objects, so its critical sections
   release their locks, its carrier goes offline for QSBR, and its state
   becomes `state`: detached, or suspended for a stop-the-world. */
void
lu_task_detach(struct lu_task *t, int state)
{
    PyThreadState *ts = &t->ts;
    if (ts->critical_section != 0) {
        _PyCriticalSection_SuspendAll(ts);
    }
    lu_qsbr_detach(t->carrier);
    ts->_status.active = 0;
    __atomic_store_n(&ts->state, state, __ATOMIC_SEQ_CST);
}

/* _PyThreadState_Detach. */
PyThreadState *
lu_syscall_begin(void)
{
    struct lu_task *t = lu_current_task();
    if (t == NULL || t->ts.state != _Py_THREAD_ATTACHED) {
        return NULL;
    }
    lu_task_detach(t, _Py_THREAD_DETACHED);
    return &t->ts;
}

/* _PyThreadState_Attach's second half, tstate_wait_attach: a task that a
   stop-the-world has suspended waits here until the world restarts, then
   takes its detached state back and attaches. */
void
lu_syscall_end(PyThreadState *ts)
{
    if (ts == NULL) {
        return;
    }
    int err = errno;
    for (;;) {
        int state = _Py_THREAD_DETACHED;
        if (__atomic_compare_exchange_n(&ts->state, &state, _Py_THREAD_ATTACHED, 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            break;
        }
        if (state == _Py_THREAD_SUSPENDED || state == _Py_THREAD_SHUTTING_DOWN) {
            _PyParkingLot_Park(&ts->state, &state, sizeof state, -1, NULL, 0);
        }
        else {
            Py_FatalError("attaching a thread state that is attached");
        }
    }
    lu_qsbr_attach(TASK(ts)->carrier);
    ts->_status.active = 1;
    if (ts->critical_section != 0) {
        _PyCriticalSection_Resume(ts);
    }
    errno = err;
}

/* tstate_delete_common's unlink. A stop-the-world in progress stops
   waiting for a task that goes. */
static void
unlink_task(struct lu_task *t)
{
    struct _is *interp = t->ts.interp;
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    if (t->ts.prev != NULL) {
        t->ts.prev->next = t->ts.next;
    }
    else {
        interp->tasks = t->ts.next;
    }
    if (t->ts.next != NULL) {
        t->ts.next->prev = t->ts.prev;
    }
    lu_stw_task_delete(interp, t);
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
}

/* unbind_gilstate_tstate and unbind_tstate. The task is not attached, or is
   the thread's current one: unbinding merges the owner record's queue, which
   frees objects and so needs a tstate. The task is still linked in the
   interpreter's list while it runs destructors, so that a stop-the-world
   sees it, and keeps its carrier until the caller has unlinked it. */
static void
unbind(struct lu_task *t)
{
    struct lu_carrier *c = t->carrier;
    if (c == NULL) {
        return;
    }
    /* The merge may run destructors that use PyGILState_Ensure, so the
       gilstate tstate stays bound through it. */
    struct lu_entry *e = lu_entry_of(c, t->ts.interp);
    /* A scheduler carrier outlives its tasks and keeps owning objects, so
       its record stays registered until the carrier exits. */
    if (--e->nbound == 0 && !c->scheduled) {
        lu_brc_retire(e);
    }
    if (t->ts._status.bound_gilstate) {
        c->gilstate = NULL;
        t->ts._status.bound_gilstate = 0;
    }
    t->ts._status.unbound = 1;
}

static void
free_task(struct lu_task *t)
{
    PyThreadState *ts = &t->ts;
    for (_PyStackChunk *chunk = ts->datastack_chunk; chunk != NULL;) {
        _PyStackChunk *previous = chunk->previous;
        PyMem_RawFree(chunk);
        chunk = previous;
    }
    PyMem_RawFree(ts->datastack_cached_chunk);
    PyMem_RawFree(t->base_entry);
    PyMem_RawFree(t);
}

/* Detaches `t` from its carrier's slot once it is unlinked: no thread can
   raise a reason at it any more, so its reasons leave the word. */
static void
release_slot(struct lu_carrier *c, struct lu_task *t)
{
    if (c->task == t) {
        lu_poll_clear(c, __atomic_load_n(&t->pending, __ATOMIC_SEQ_CST));
        c->task = NULL;
    }
}

void
lu_task_leave(struct lu_task *t)
{
    struct lu_carrier *c = t->carrier;
    lu_task_detach(t, _Py_THREAD_DETACHED);
    struct lu_entry *e = lu_entry_of(c, t->ts.interp);
    /* The collector's trigger counts a task's allocations as one thread's:
       the carrier's buffer follows the task, or a task that moves every few
       hundred allocations would never fill either buffer. */
    t->gc_alloc_count = e->gc_alloc_count;
    e->gc_alloc_count = 0;
    /* A collection those allocations scheduled is the task's to run, at its
       next poll wherever that is. */
    if (__atomic_fetch_and(&c->poll, ~(uintptr_t)LU_POLL_GC_SCHEDULED, __ATOMIC_SEQ_CST)
        & LU_POLL_GC_SCHEDULED) {
        __atomic_fetch_or(&t->pending, LU_POLL_GC_SCHEDULED, __ATOMIC_SEQ_CST);
    }
    if (--e->nbound == 0) {
        lu_brc_set_idle(e, 1);          /* the carrier's last refcount operation is behind it */
    }
    if (t->ts._status.bound_gilstate) {
        c->gilstate = NULL;
        t->ts._status.bound_gilstate = 0;
    }
    t->ts._status.bound = 0;
    release_slot(c, t);
    __atomic_store_n(&t->carrier, NULL, __ATOMIC_RELEASE);
}

void
lu_task_delete(struct lu_task *t)
{
    struct lu_carrier *c = t->carrier;
    if (c != NULL && c != lu_carrier) {
        Py_FatalError("PyThreadState_Delete: the thread state is bound to another thread");
    }
    unbind(t);
    unlink_task(t);
    if (_PyRuntime.main_task == t) {
        __atomic_store_n(&_PyRuntime.main_task, NULL, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&t->carrier, NULL, __ATOMIC_RELEASE);
    if (c != NULL) {
        release_slot(c, t);
        if (t->ts.state == _Py_THREAD_ATTACHED) {
            lu_qsbr_detach(c);          /* the thread leaves the interpreter with it */
        }
    }
    t->ts.state = _Py_THREAD_DETACHED;
    t->ts._status.active = 0;
    t->ts._status.finalized = 1;
    free_task(t);
}

/* A task of `c` still linked in `interp`, the gilstate task only when no
   other is left. */
static struct lu_task *
bound_task(struct _is *interp, struct lu_carrier *c)
{
    struct lu_task *found = NULL;
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        if (TASK(ts)->carrier == c && (found == NULL || found->ts._status.bound_gilstate)) {
            found = TASK(ts);
        }
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    return found;
}

/* The thread is exiting with tasks still bound to it (a foreign thread that
   never released, or the main task). Each is attached while it is unbound,
   which merging needs, and stays the carrier's task until all are; their
   Python state is not cleared. The gilstate task goes last: the merges may
   run destructors that call PyGILState_Ensure. */
void
lu_task_carrier_exit(struct lu_carrier *c)
{
    PyThreadState *done = NULL;
    for (struct lu_entry *e = c->entries; e != NULL; e = e->next) {
        struct lu_task *t;
        while ((t = bound_task(e->interp, c)) != NULL) {
            PyThreadState_Swap(&t->ts);         /* unbind's merge may run destructors */
            unbind(t);
            unlink_task(t);
            t->ts.next = done;
            done = &t->ts;
        }
    }
    c->task = NULL;
    c->gilstate = NULL;
    while (done != NULL) {
        PyThreadState *next = done->next;
        free_task(TASK(done));
        done = next;
    }
}

int
lu_task_shutdown_others(struct _is *interp)
{
    lu_stw_stop(interp);
    struct lu_task *self = lu_current_task();
    /* Every other task is suspended now. It leaves the list, so no later stop waits
       for it and no collection scans its frames, and it is never resumed: its thread
       hangs in lu_syscall_end. Tasks created from here on start in that state. */
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    interp->tasks_closed = 1;
    int stopped = 0;
    PyThreadState *next;
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = next) {
        next = ts->next;
        if (TASK(ts) != self) {
            if (ts->prev != NULL) {
                ts->prev->next = ts->next;
            }
            else {
                interp->tasks = ts->next;
            }
            if (ts->next != NULL) {
                ts->next->prev = ts->prev;
            }
            __atomic_store_n(&ts->state, _Py_THREAD_SHUTTING_DOWN, __ATOMIC_SEQ_CST);
            stopped++;
        }
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    lu_stw_start(interp);
    return stopped;
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
