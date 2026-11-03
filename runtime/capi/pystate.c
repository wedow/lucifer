/* The thread-state API over tasks (Python/pystate.c): current, new, clear,
   delete, swap, the GILState calls and the save/restore bracket. The record
   and its binding are runtime/thread/task.c.

   A thread's tstate is the task attached to its carrier, and is NULL while
   the thread is detached, as in CPython. */

#include "lucifer.h"

#define TASK(ts) ((struct lu_task *)(ts))

PyThreadState *
PyThreadState_GetUnchecked(void)
{
    struct lu_task *t = lu_current_task();
    return t != NULL && t->ts.state == _Py_THREAD_ATTACHED ? &t->ts : NULL;
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

PyInterpreterState *
PyInterpreterState_Main(void)
{
    return &_PyRuntime.main_interp;
}

PyInterpreterState *
PyThreadState_GetInterpreter(PyThreadState *ts)
{
    return ts->interp;
}

PyThreadState *
PyThreadState_New(PyInterpreterState *interp)
{
    struct lu_task *t = lu_task_new(interp, LU_WHENCE_UNKNOWN);
    if (t != NULL) {
        lu_task_bind(t);
    }
    return t != NULL ? &t->ts : NULL;
}

void
PyThreadState_Clear(PyThreadState *ts)
{
    ts->_status.finalizing = 1;
    Py_CLEAR(ts->threading_local_key);
    Py_CLEAR(ts->threading_local_sentinel);
    Py_CLEAR(ts->dict);
    Py_CLEAR(ts->async_exc);
    Py_CLEAR(ts->current_exception);
    Py_CLEAR(ts->exc_state.exc_value);
    Py_CLEAR(ts->c_profileobj);
    Py_CLEAR(ts->c_traceobj);
    Py_CLEAR(ts->async_gen_firstiter);
    Py_CLEAR(ts->async_gen_finalizer);
    Py_CLEAR(ts->context);
    ts->_status.cleared = 1;
}

void
PyThreadState_Delete(PyThreadState *ts)
{
    if (ts == PyThreadState_GetUnchecked()) {
        Py_FatalError("PyThreadState_Delete: tstate is still current");
    }
    lu_task_delete(TASK(ts));
}

void
PyThreadState_DeleteCurrent(void)
{
    lu_task_delete(TASK(PyThreadState_Get()));
}

PyThreadState *
PyThreadState_Swap(PyThreadState *newts)
{
    PyThreadState *oldts = PyThreadState_GetUnchecked();
    if (oldts != NULL) {
        lu_syscall_begin();
    }
    if (newts != NULL) {
        lu_task_attach(newts);
    }
    return oldts;
}

PyThreadState *
PyEval_SaveThread(void)
{
    PyThreadState *ts = lu_syscall_begin();
    if (ts == NULL) {
        Py_FatalError("PyEval_SaveThread: the function must be called with the GIL held");
    }
    return ts;
}

void
PyEval_RestoreThread(PyThreadState *ts)
{
    if (ts == NULL) {
        Py_FatalError("PyEval_RestoreThread: NULL tstate");
    }
    lu_task_attach(ts);
}

PyThreadState *
PyGILState_GetThisThreadState(void)
{
    return lu_carrier != NULL ? (PyThreadState *)lu_carrier->gilstate : NULL;
}

int
PyGILState_Check(void)
{
    PyThreadState *ts = PyThreadState_GetUnchecked();
    return ts != NULL && ts == PyGILState_GetThisThreadState();
}

PyGILState_STATE
PyGILState_Ensure(void)
{
    PyThreadState *tcur = PyGILState_GetThisThreadState();
    int has_gil;
    if (tcur == NULL) {
        struct lu_task *t = lu_task_new(&_PyRuntime.main_interp, LU_WHENCE_GILSTATE);
        if (t == NULL) {
            Py_FatalError("Couldn't create thread-state for new thread");
        }
        lu_task_bind(t);        /* the thread has no gilstate tstate: this becomes it */
        tcur = &t->ts;
        tcur->gilstate_counter = 0;
        has_gil = 0;
    }
    else {
        has_gil = tcur == PyThreadState_GetUnchecked();
    }
    if (!has_gil) {
        PyEval_RestoreThread(tcur);
    }
    ++tcur->gilstate_counter;
    return has_gil ? PyGILState_LOCKED : PyGILState_UNLOCKED;
}

void
PyGILState_Release(PyGILState_STATE oldstate)
{
    PyThreadState *tstate = PyGILState_GetThisThreadState();
    if (tstate == NULL) {
        Py_FatalError("auto-releasing thread-state, but no thread-state for this thread");
    }
    if (tstate != PyThreadState_GetUnchecked()) {
        Py_FatalError("PyGILState_Release: thread state must be current when releasing");
    }
    if (--tstate->gilstate_counter == 0) {
        /* Clearing may run destructors that use PyGILState_Ensure/Release
           themselves, so the counter is not zero while it runs. */
        ++tstate->gilstate_counter;
        PyThreadState_Clear(tstate);
        --tstate->gilstate_counter;
        PyThreadState_DeleteCurrent();
    }
    else if (oldstate == PyGILState_UNLOCKED) {
        PyEval_SaveThread();
    }
}
