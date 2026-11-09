/* The thread-state API over tasks (Python/pystate.c): current, new, clear,
   delete, swap, the GILState calls and the save/restore bracket. The record
   and its binding are runtime/thread/task.c.

   A thread's tstate is the task attached to its carrier, and is NULL while
   the thread is detached, as in CPython. */

#include "lucifer.h"

#define TASK(ts) ((struct lu_task *)(ts))

/* The exported forms of lucifer.h's inline lu_tstate, lu_tstate_get and lu_interp. */
#undef PyThreadState_GetUnchecked
#undef PyThreadState_Get
#undef PyInterpreterState_Get

PyThreadState *
PyThreadState_GetUnchecked(void)
{
    return lu_tstate();
}

PyThreadState *
PyThreadState_Get(void)
{
    return lu_tstate_get();
}

void
lu_tstate_missing(void)
{
    fprintf(stderr, "Fatal Python error: PyThreadState_Get: the function must be "
            "called with an active thread state, after Python initialization and "
            "before Python finalization, but it was called without an active "
            "thread state. Are you trying to call the C API inside of a "
            "Py_BEGIN_ALLOW_THREADS block?\n");
    abort();
}

LU_EXPORT PyThreadState *
_PyThreadState_GetCurrent(void)
{
    return PyThreadState_GetUnchecked();
}

/* C recursion shares the Python recursion budget (Python/ceval.c). */
int
Py_EnterRecursiveCall(const char *where)
{
    PyThreadState *ts = PyThreadState_Get();
    if (ts->py_recursion_remaining-- <= 0) {
        ts->py_recursion_remaining++;
        PyErr_Format(PyExc_RecursionError, "maximum recursion depth exceeded%s", where);
        return -1;
    }
    return 0;
}

void
Py_LeaveRecursiveCall(void)
{
    PyThreadState_Get()->py_recursion_remaining++;
}

/* The internal headers' _Py_EnterRecursiveCall calls this once the C stack
   is past the soft limit of the bounds a thread recorded (Python/ceval.c). */
LU_EXPORT int
_Py_CheckRecursiveCall(PyThreadState *tstate, const char *where)
{
    char here;
    struct lu_task *t = (struct lu_task *)tstate;
    if ((uintptr_t)&here >= t->c_stack_soft_limit || tstate->recursion_headroom) {
        return 0;
    }
    tstate->recursion_headroom++;
    PyErr_Format(PyExc_RecursionError, "Stack overflow (used %d kB)%s",
                 (int)((t->c_stack_top - (uintptr_t)&here) / 1024), where);
    tstate->recursion_headroom--;
    return -1;
}

PyInterpreterState *
PyInterpreterState_Get(void)
{
    return lu_interp();
}

/* PyInterpreterState_GetDict: the interpreter's dict, made on first use
   (Python/pystate.c _PyInterpreterState_GetDictOrCreate). */

PyObject *
PyInterpreterState_GetDict(PyInterpreterState *interp)
{
    if (interp->dict == NULL) {
        interp->dict = PyDict_New();
        if (interp->dict == NULL) {
            PyErr_Clear();
        }
    }
    return interp->dict;        /* borrowed */
}

/* PyThreadState_EnterTracing / LeaveTracing (Python/ceval.c): a nesting
   count the tracing machinery reads. */

void
PyThreadState_EnterTracing(PyThreadState *tstate)
{
    tstate->tracing++;
}

void
PyThreadState_LeaveTracing(PyThreadState *tstate)
{
    tstate->tracing--;
}

/* The thread's dict, made on first use (Python/pystate.c). */

LU_EXPORT PyObject *
_PyThreadState_GetDict(PyThreadState *tstate)
{
    if (tstate->dict == NULL) {
        tstate->dict = PyDict_New();
        if (tstate->dict == NULL) {
            PyErr_Clear();
        }
    }
    return tstate->dict;        /* borrowed */
}

PyObject *
PyThreadState_GetDict(void)
{
    PyThreadState *ts = PyThreadState_GetUnchecked();
    return ts != NULL ? _PyThreadState_GetDict(ts) : NULL;
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

uint64_t
PyThreadState_GetID(PyThreadState *tstate)
{
    return tstate->id;
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
    /* Atomic: tasks exiting together clear at once; a stop-the-world that
       swaps functions has stopped this one. */
    if (ts->c_profilefunc != NULL) {
        __atomic_fetch_sub(&ts->interp->sys_profiling_threads, 1, __ATOMIC_RELAXED);
        ts->c_profilefunc = NULL;
    }
    if (ts->c_tracefunc != NULL) {
        __atomic_fetch_sub(&ts->interp->sys_tracing_threads, 1, __ATOMIC_RELAXED);
        ts->c_tracefunc = NULL;
    }
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
        /* Clearing, and the owner record's merge in deleting, may run
           destructors that use PyGILState_Ensure/Release themselves, so the
           counter is not zero while they run (gh-119585). */
        ++tstate->gilstate_counter;
        PyThreadState_Clear(tstate);
        PyThreadState_DeleteCurrent();
    }
    else if (oldstate == PyGILState_UNLOCKED) {
        PyEval_SaveThread();
    }
}

/* Single-phase modules, indexed in the interpreter for PyState_FindModule
   (Python/pystate.c). The index lives in the def, as PyModuleDef_Init
   assigned it; a def that reached us without one takes the next. */

static Py_ssize_t
module_index(PyModuleDef *def)
{
    Py_ssize_t index = def->m_base.m_index;
    if (index == 0) {
        index = __atomic_add_fetch(&_PyRuntime.next_module_index, 1, __ATOMIC_RELAXED);
        def->m_base.m_index = index;
    }
    return index;
}

int
PyState_AddModule(PyObject *module, PyModuleDef *def)
{
    if (def == NULL) {
        Py_FatalError("module definition is NULL");
        return -1;
    }
    if (def->m_slots != NULL) {
        PyErr_SetString(PyExc_SystemError, "PyState_AddModule called on module with slots");
        return -1;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *list = interp->modules_by_index;
    if (list == NULL) {
        interp->modules_by_index = list = PyList_New(0);
        if (list == NULL) {
            return -1;
        }
    }
    Py_ssize_t index = module_index(def);
    while (PyList_GET_SIZE(list) <= index) {
        if (PyList_Append(list, Py_None) < 0) {
            return -1;
        }
    }
    return PyList_SetItem(list, index, Py_NewRef(module));
}

PyObject *
PyState_FindModule(PyModuleDef *def)
{
    if (def->m_slots != NULL) {
        return NULL;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *list = interp->modules_by_index;
    Py_ssize_t index = def->m_base.m_index;
    if (list == NULL || index <= 0 || index >= PyList_GET_SIZE(list)) {
        return NULL;
    }
    PyObject *module = PyList_GET_ITEM(list, index);
    return PyModule_Check(module) ? module : NULL;     /* borrowed */
}

int
PyState_RemoveModule(PyModuleDef *def)
{
    if (def->m_slots != NULL) {
        PyErr_SetString(PyExc_SystemError, "PyState_RemoveModule called on module with slots");
        return -1;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *list = interp->modules_by_index;
    Py_ssize_t index = def->m_base.m_index;
    if (list != NULL && index > 0 && index < PyList_GET_SIZE(list)) {
        PyList_SetItem(list, index, Py_NewRef(Py_None));
    }
    return 0;
}

/* PyUnstable_ThreadState_SetStackProtection (Python/ceval.c): the C stack
   bounds a thread's recursion checks watch. The margins are CPython's
   (_PyOS_MIN_STACK_SIZE, _PyOS_STACK_MARGIN_BYTES of a release build). The
   interpreter's recursion limit is a counter; the bounds serve the C
   recursion checks extensions inline from the internal headers
   (_Py_EnterRecursiveCall), and no page is protected. */

#define LU_STACK_MARGIN_BYTES ((size_t)1 << 14)
#define LU_MIN_STACK_SIZE (LU_STACK_MARGIN_BYTES * 3)

static void
tstate_set_stack(PyThreadState *tstate, uintptr_t base, uintptr_t top)
{
    struct lu_task *t = (struct lu_task *)tstate;
    t->c_stack_top = top;
    t->c_stack_hard_limit = base + LU_STACK_MARGIN_BYTES;
    t->c_stack_soft_limit = base + LU_STACK_MARGIN_BYTES * 2;
}

/* The bounds of the C stack of the carrier a task is bound to, which
   ResetStackProtection restores; zeros (unknown) check nothing. */
void
lu_task_set_c_stack(struct lu_task *t, uintptr_t base, uintptr_t top)
{
    t->c_stack_init_base = base;
    t->c_stack_init_top = top;
    if (top != 0) {
        tstate_set_stack(&t->ts, base, top);
    }
    else {
        t->c_stack_top = t->c_stack_soft_limit = t->c_stack_hard_limit = 0;
    }
}

int
PyUnstable_ThreadState_SetStackProtection(PyThreadState *tstate, void *stack_start_addr,
                                           size_t stack_size)
{
    if (stack_size < LU_MIN_STACK_SIZE) {
        PyErr_Format(PyExc_ValueError, "stack_size must be at least %zu bytes",
                     LU_MIN_STACK_SIZE);
        return -1;
    }
    tstate_set_stack(tstate, (uintptr_t)stack_start_addr,
                     (uintptr_t)stack_start_addr + stack_size);
    return 0;
}

void
PyUnstable_ThreadState_ResetStackProtection(PyThreadState *tstate)
{
    struct lu_task *t = (struct lu_task *)tstate;
    if (t->c_stack_init_top != 0) {
        tstate_set_stack(tstate, t->c_stack_init_base, t->c_stack_init_top);
    }
    else {
        t->c_stack_top = t->c_stack_soft_limit = t->c_stack_hard_limit = 0;
    }
}
