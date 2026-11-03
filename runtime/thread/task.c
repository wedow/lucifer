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
    ts->id = ++interp->next_task_id;
    ts->next = interp->tasks;
    if (ts->next != NULL) {
        ts->next->prev = ts;
    }
    interp->tasks = ts;
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
    t->carrier = lu_carrier;
    ts->thread_id = (unsigned long)pthread_self();
    ts->native_thread_id = (unsigned long)gettid();
    if (e->nbound++ == 0) {
        lu_brc_register(e);
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
   the poll word and its mirror, with the reasons targeted at `t` raised. */
static void
install(struct lu_carrier *c, struct lu_task *t)
{
    struct lu_task *old = c->task;
    uintptr_t carried = c->poll & LU_POLL_EVENTS_MASK & ~(old != NULL ? old->pending : 0);
    c->poll = t->ts.interp->monitoring_version | carried | t->pending;
    t->ts.eval_breaker = c->poll;
    c->task = t;
}

/* _PyThreadState_Attach */
void
lu_task_attach(PyThreadState *ts)
{
    if (TASK(ts)->carrier != lu_carrier) {
        Py_FatalError("PyEval_RestoreThread: the thread state is bound to another thread");
    }
    if (lu_carrier->task != TASK(ts)) {
        install(lu_carrier, TASK(ts));
    }
    lu_syscall_end(ts);
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
    ts->_status.active = 0;
    __atomic_store_n(&ts->state, _Py_THREAD_DETACHED, __ATOMIC_SEQ_CST);
    return ts;
}

/* _PyThreadState_Attach's second half. With stop-the-world (K1q) it waits
   here while the world is stopped. */
void
lu_syscall_end(PyThreadState *ts)
{
    if (ts == NULL) {
        return;
    }
    int err = errno;
    __atomic_store_n(&ts->state, _Py_THREAD_ATTACHED, __ATOMIC_SEQ_CST);
    ts->_status.active = 1;
    if (ts->critical_section != 0) {
        _PyCriticalSection_Resume(ts);
    }
    errno = err;
}

/* tstate_delete_common's unlink, unbind and free. The task is not attached,
   or is the thread's current one: unbinding merges the owner record's queue,
   which frees objects and so needs a tstate. */
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
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
}

/* unbind_gilstate_tstate and unbind_tstate */
static void
unbind(struct lu_task *t)
{
    struct lu_carrier *c = t->carrier;
    if (c == NULL) {
        return;
    }
    if (t->ts._status.bound_gilstate) {
        c->gilstate = NULL;
        t->ts._status.bound_gilstate = 0;
    }
    struct lu_entry *e = lu_entry_of(c, t->ts.interp);
    if (--e->nbound == 0) {
        lu_brc_retire(e);
    }
    t->carrier = NULL;
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
    PyMem_RawFree(t);
}

/* Detaches `t` from its carrier's slot once it is unbound. */
static void
release_slot(struct lu_carrier *c, struct lu_task *t)
{
    if (c->task == t) {
        c->poll &= ~t->pending;
        c->task = NULL;
    }
}

void
lu_task_delete(struct lu_task *t)
{
    struct lu_carrier *c = t->carrier;
    if (c != NULL && c != lu_carrier) {
        Py_FatalError("PyThreadState_Delete: the thread state is bound to another thread");
    }
    unlink_task(t);
    unbind(t);
    if (c != NULL) {
        release_slot(c, t);
    }
    t->ts.state = _Py_THREAD_DETACHED;
    t->ts._status.active = 0;
    t->ts._status.finalized = 1;
    free_task(t);
}

/* The thread is exiting with tasks still bound to it (a foreign thread that
   never released, or the main task). They are unbound while the last one
   is still the carrier's task, which merging needs; their Python state is
   not cleared. */
void
lu_task_carrier_exit(struct lu_carrier *c)
{
    PyThreadState *done = NULL;
    for (struct lu_entry *e = c->entries; e != NULL; e = e->next) {
        for (;;) {
            struct lu_task *t = NULL;
            pthread_mutex_lock(&e->interp->tasks_lock.mutex);
            for (PyThreadState *ts = e->interp->tasks; ts != NULL && t == NULL; ts = ts->next) {
                t = TASK(ts)->carrier == c ? TASK(ts) : NULL;
            }
            pthread_mutex_unlock(&e->interp->tasks_lock.mutex);
            if (t == NULL) {
                break;
            }
            unlink_task(t);
            unbind(t);
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
