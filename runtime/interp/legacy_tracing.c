/* sys.settrace, sys.setprofile and PyEval_SetTrace/SetProfile on top of PEP 669
   (Python/legacy_tracing.c and the trampolines of Python/sysmodule.c, CPython
   3.14). One event source: each legacy event is a monitoring callback of the
   reserved tools SYS_TRACE and SYS_PROFILE, which look at the task's own
   trace function, so tracing is per task while the events are global. */

#include "interp/instrument.h"
#include "interp/code.h"
#include "interp/exceptions.h"

#define TASK_LIST_LOCK(interp) pthread_mutex_lock(&(interp)->tasks_lock.mutex)
#define TASK_LIST_UNLOCK(interp) pthread_mutex_unlock(&(interp)->tasks_lock.mutex)

/* The handlers are PyCFunctions whose self is the legacy event number. */
#define HANDLER(name) \
    static PyObject *name(PyObject *self, PyObject *const *args, Py_ssize_t nargs)

static int
legacy_event(PyObject *self)
{
    return (int)lu_as_long(self);
}

static PyObject *
call_profile_func(PyObject *self, PyObject *arg)
{
    PyThreadState *ts = PyThreadState_Get();
    if (ts->c_profilefunc == NULL) {
        Py_RETURN_NONE;
    }
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == NULL) {
        PyErr_SetString(PyExc_SystemError, "Missing frame when calling profile function.");
        return NULL;
    }
    Py_INCREF(frame);
    int err = ts->c_profilefunc(ts->c_profileobj, frame, legacy_event(self), arg);
    Py_DECREF(frame);
    if (err) {
        return NULL;
    }
    Py_RETURN_NONE;
}

HANDLER(sys_profile_start) { return call_profile_func(self, Py_None); }
HANDLER(sys_profile_return) { return call_profile_func(self, args[2]); }
HANDLER(sys_profile_unwind) { return call_profile_func(self, NULL); }

HANDLER(sys_profile_call_or_return)
{
    PyObject *callable = args[2];
    if (PyCFunction_Check(callable)) {
        return call_profile_func(self, callable);
    }
    if (Py_TYPE(callable) == &PyMethodDescr_Type) {
        PyObject *self_arg = args[3];
        /* A builtin method, for backwards compatibility; no argument, no event. */
        if (self_arg == &lu_instrumentation_missing) {
            Py_RETURN_NONE;
        }
        PyObject *meth = Py_TYPE(callable)->tp_descr_get(callable, self_arg, (PyObject *)Py_TYPE(self_arg));
        if (meth == NULL) {
            return NULL;
        }
        PyObject *res = call_profile_func(self, meth);
        Py_DECREF(meth);
        return res;
    }
    Py_RETURN_NONE;
}

/* SYS_TRACE's local events on `code`. */
static lu_event_set
trace_local_events(PyCodeObject *code)
{
    lu_event_set events = 0;
    lu_monitoring *data = __atomic_load_n(&code->_co_monitoring, __ATOMIC_ACQUIRE);
    for (int e = 0; data != NULL && e < LU_MON_LOCAL_EVENTS; e++) {
        events |= (lu_event_set)((data->local_monitors.tools[e] >> LU_TOOL_SYS_TRACE) & 1) << e;
    }
    return events;
}

/* Whether `code` already has instruction events as `enable` asks. */
static int
opcode_trace_is(PyCodeObject *code, int enable)
{
    return !(trace_local_events(code) & (1 << PY_MONITORING_EVENT_INSTRUCTION)) == !enable;
}

static int
set_opcode_trace_world_stopped(PyCodeObject *code, int enable)
{
    if (opcode_trace_is(code, enable)) {
        return 0;
    }
    lu_event_set events = trace_local_events(code) ^ (1 << PY_MONITORING_EVENT_INSTRUCTION);
    return lu_monitoring_set_local_events(code, LU_TOOL_SYS_TRACE, events);
}

/* f_trace_opcodes: instruction events for this frame's code. Every trace
   event of such a frame asks again, so only a change stops the world. */
int
lu_set_opcode_trace(PyObject *frame, int enable)
{
    PyCodeObject *code = lu_frame_code((PyFrameObject *)frame);
    if (opcode_trace_is(code, enable)) {
        return 0;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    lu_stw_stop(interp);
    int res = set_opcode_trace_world_stopped(code, enable);
    lu_stw_start(interp);
    return res;
}

static PyObject *
call_trace_func(PyObject *self, PyObject *arg)
{
    PyThreadState *ts = PyThreadState_Get();
    if (ts->c_tracefunc == NULL) {
        Py_RETURN_NONE;
    }
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == NULL) {
        PyErr_SetString(PyExc_SystemError, "Missing frame when calling trace function.");
        return NULL;
    }
    if (lu_frame_trace_opcodes((PyObject *)frame) && lu_set_opcode_trace((PyObject *)frame, 1) != 0) {
        return NULL;
    }
    Py_INCREF(frame);
    int err = ts->c_tracefunc(ts->c_traceobj, frame, legacy_event(self), arg);
    Py_DECREF(frame);
    if (err) {
        return NULL;
    }
    Py_RETURN_NONE;
}

HANDLER(sys_trace_exception_func)
{
    PyObject *exc = args[2];
    PyObject *tb = PyException_GetTraceback(exc);
    if (tb == NULL) {
        tb = Py_NewRef(Py_None);
    }
    PyObject *tuple = PyTuple_Pack(3, (PyObject *)Py_TYPE(exc), exc, tb);
    Py_DECREF(tb);
    if (tuple == NULL) {
        return NULL;
    }
    PyObject *res = call_trace_func(self, tuple);
    Py_DECREF(tuple);
    return res;
}

HANDLER(sys_trace_start) { return call_trace_func(self, Py_None); }
HANDLER(sys_trace_unwind) { return call_trace_func(self, NULL); }
HANDLER(sys_trace_return) { return call_trace_func(self, args[2]); }

HANDLER(sys_trace_instruction_func)
{
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == NULL) {
        PyErr_SetString(PyExc_SystemError, "Missing frame when calling trace function.");
        return NULL;
    }
    PyThreadState *ts = PyThreadState_Get();
    if (!ts->c_tracefunc || !lu_frame_trace_opcodes((PyObject *)frame)) {
        if (lu_set_opcode_trace((PyObject *)frame, 0) != 0) {
            return NULL;
        }
        Py_RETURN_NONE;
    }
    Py_INCREF(frame);
    int err = ts->c_tracefunc(ts->c_traceobj, frame, legacy_event(self), Py_None);
    Py_DECREF(frame);
    if (err) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
trace_line(PyThreadState *ts, PyObject *self, PyFrameObject *frame, int line)
{
    if (!lu_frame_trace_lines((PyObject *)frame) || line < 0) {
        Py_RETURN_NONE;
    }
    Py_INCREF(frame);
    int err = ts->c_tracefunc(ts->c_traceobj, frame, legacy_event(self), Py_None);
    Py_DECREF(frame);
    if (err) {
        return NULL;
    }
    Py_RETURN_NONE;
}

HANDLER(sys_trace_line_func)
{
    PyThreadState *ts = PyThreadState_Get();
    if (ts->c_tracefunc == NULL) {
        Py_RETURN_NONE;
    }
    int line = (int)lu_as_long(args[1]);
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == NULL) {
        PyErr_SetString(PyExc_SystemError, "Missing frame when calling trace function.");
        return NULL;
    }
    return trace_line(ts, self, frame, line);
}

/* sys.settrace generates line events for all backward edges, even on the same line. */
HANDLER(sys_trace_jump_func)
{
    PyThreadState *ts = PyThreadState_Get();
    if (ts->c_tracefunc == NULL) {
        Py_RETURN_NONE;
    }
    int from = (int)(lu_as_long(args[1]) / sizeof(lu_codeunit));
    int to = (int)(lu_as_long(args[2]) / sizeof(lu_codeunit));
    if (to > from) {
        return Py_NewRef(&lu_instrumentation_disable);        /* a forward jump */
    }
    PyCodeObject *code = (PyCodeObject *)args[0];
    /* Tracing always sets line events, so the line table exists. */
    int to_line = lu_instrumentation_line(code, to);
    int from_line = lu_instrumentation_line(code, from);
    if (to_line != from_line) {
        return Py_NewRef(&lu_instrumentation_disable);        /* the target's INSTRUMENTED_LINE handles it */
    }
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == NULL) {
        PyErr_SetString(PyExc_SystemError, "Missing frame when calling trace function.");
        return NULL;
    }
    return trace_line(ts, self, frame, to_line);
}

#define FAST (PyCFunction)(void (*)(void))
static PyMethodDef handler_defs[] = {
    { "profile_start", FAST sys_profile_start, METH_FASTCALL, NULL },
    { "profile_return", FAST sys_profile_return, METH_FASTCALL, NULL },
    { "profile_unwind", FAST sys_profile_unwind, METH_FASTCALL, NULL },
    { "profile_call_or_return", FAST sys_profile_call_or_return, METH_FASTCALL, NULL },
    { "trace_exception", FAST sys_trace_exception_func, METH_FASTCALL, NULL },
    { "trace_start", FAST sys_trace_start, METH_FASTCALL, NULL },
    { "trace_unwind", FAST sys_trace_unwind, METH_FASTCALL, NULL },
    { "trace_return", FAST sys_trace_return, METH_FASTCALL, NULL },
    { "trace_instruction", FAST sys_trace_instruction_func, METH_FASTCALL, NULL },
    { "trace_line", FAST sys_trace_line_func, METH_FASTCALL, NULL },
    { "trace_jump", FAST sys_trace_jump_func, METH_FASTCALL, NULL },
};
#define H(name) (&handler_defs[name##_INDEX])
enum {
    profile_start_INDEX, profile_return_INDEX, profile_unwind_INDEX, profile_call_or_return_INDEX,
    trace_exception_INDEX, trace_start_INDEX, trace_unwind_INDEX, trace_return_INDEX,
    trace_instruction_INDEX, trace_line_INDEX, trace_jump_INDEX,
};

static int
set_callbacks(int tool, PyMethodDef *def, int legacy_event, int event1, int event2)
{
    PyObject *event = PyLong_FromLong(legacy_event);
    PyObject *callback = event != NULL ? PyCFunction_NewEx(def, event, NULL) : NULL;
    Py_XDECREF(event);
    if (callback == NULL) {
        return -1;
    }
    Py_XDECREF(lu_monitoring_register_callback(tool, event1, callback));
    if (event2 >= 0) {
        Py_XDECREF(lu_monitoring_register_callback(tool, event2, callback));
    }
    Py_DECREF(callback);
    return PyErr_Occurred() ? -1 : 0;
}

static int
setup_profile_callbacks(void)
{
    const int P = LU_TOOL_SYS_PROFILE;
    return set_callbacks(P, H(profile_start), PyTrace_CALL, PY_MONITORING_EVENT_PY_START,
                         PY_MONITORING_EVENT_PY_RESUME)
        || set_callbacks(P, H(profile_start), PyTrace_CALL, PY_MONITORING_EVENT_PY_THROW, -1)
        || set_callbacks(P, H(profile_return), PyTrace_RETURN, PY_MONITORING_EVENT_PY_RETURN,
                         PY_MONITORING_EVENT_PY_YIELD)
        || set_callbacks(P, H(profile_unwind), PyTrace_RETURN, PY_MONITORING_EVENT_PY_UNWIND, -1)
        || set_callbacks(P, H(profile_call_or_return), PyTrace_C_CALL, PY_MONITORING_EVENT_CALL, -1)
        || set_callbacks(P, H(profile_call_or_return), PyTrace_C_RETURN, PY_MONITORING_EVENT_C_RETURN, -1)
        || set_callbacks(P, H(profile_call_or_return), PyTrace_C_EXCEPTION, PY_MONITORING_EVENT_C_RAISE, -1);
}

static int
setup_trace_callbacks(void)
{
    const int T = LU_TOOL_SYS_TRACE;
    return set_callbacks(T, H(trace_start), PyTrace_CALL, PY_MONITORING_EVENT_PY_START,
                         PY_MONITORING_EVENT_PY_RESUME)
        || set_callbacks(T, H(trace_start), PyTrace_CALL, PY_MONITORING_EVENT_PY_THROW, -1)
        || set_callbacks(T, H(trace_return), PyTrace_RETURN, PY_MONITORING_EVENT_PY_RETURN, -1)
        || set_callbacks(T, H(trace_return), PyTrace_RETURN, PY_MONITORING_EVENT_PY_YIELD, -1)
        || set_callbacks(T, H(trace_exception), PyTrace_EXCEPTION, PY_MONITORING_EVENT_RAISE,
                         PY_MONITORING_EVENT_STOP_ITERATION)
        || set_callbacks(T, H(trace_line), PyTrace_LINE, PY_MONITORING_EVENT_LINE, -1)
        || set_callbacks(T, H(trace_unwind), PyTrace_RETURN, PY_MONITORING_EVENT_PY_UNWIND, -1)
        || set_callbacks(T, H(trace_jump), PyTrace_LINE, PY_MONITORING_EVENT_JUMP, -1)
        || set_callbacks(T, H(trace_instruction), PyTrace_OPCODE, PY_MONITORING_EVENT_INSTRUCTION, -1);
}

/* Runs setup once; the first caller to find it undone does it. */
static int
setup_once(PyInterpreterState *interp, int *done, int (*setup)(void))
{
    int err = 0;
    PyMutex_Lock(&interp->tracing_setup_lock);
    if (!*done) {
        err = setup();
        *done = err == 0;
    }
    PyMutex_Unlock(&interp->tracing_setup_lock);
    return err;
}

static PyObject *
swap_profile_func_arg(PyThreadState *ts, Py_tracefunc func, PyObject *arg)
{
    ts->interp->sys_profiling_threads += (func != NULL) - (ts->c_profilefunc != NULL);
    ts->c_profilefunc = func;
    PyObject *old = ts->c_profileobj;
    ts->c_profileobj = Py_XNewRef(arg);
    return old;
}

static PyObject *
swap_trace_func_arg(PyThreadState *ts, Py_tracefunc func, PyObject *arg)
{
    ts->interp->sys_tracing_threads += (func != NULL) - (ts->c_tracefunc != NULL);
    ts->c_tracefunc = func;
    PyObject *old = ts->c_traceobj;
    ts->c_traceobj = Py_XNewRef(arg);
    return old;
}

static int
set_monitoring_profile_events(PyInterpreterState *interp)
{
    uint32_t events = 0;
    if (interp->sys_profiling_threads) {
        events = (1 << PY_MONITORING_EVENT_PY_START) | (1 << PY_MONITORING_EVENT_PY_RESUME)
            | (1 << PY_MONITORING_EVENT_PY_RETURN) | (1 << PY_MONITORING_EVENT_PY_YIELD)
            | (1 << PY_MONITORING_EVENT_CALL) | (1 << PY_MONITORING_EVENT_PY_UNWIND)
            | (1 << PY_MONITORING_EVENT_PY_THROW);
    }
    return lu_monitoring_set_events(LU_TOOL_SYS_PROFILE, events);
}

static int
set_monitoring_trace_events(PyInterpreterState *interp)
{
    uint32_t events = 0;
    if (interp->sys_tracing_threads) {
        events = (1 << PY_MONITORING_EVENT_PY_START) | (1 << PY_MONITORING_EVENT_PY_RESUME)
            | (1 << PY_MONITORING_EVENT_PY_RETURN) | (1 << PY_MONITORING_EVENT_PY_YIELD)
            | (1 << PY_MONITORING_EVENT_RAISE) | (1 << PY_MONITORING_EVENT_LINE)
            | (1 << PY_MONITORING_EVENT_JUMP) | (1 << PY_MONITORING_EVENT_PY_UNWIND)
            | (1 << PY_MONITORING_EVENT_PY_THROW) | (1 << PY_MONITORING_EVENT_STOP_ITERATION);
    }
    return lu_monitoring_set_events(LU_TOOL_SYS_TRACE, events);
}

/* Instruction events for a thread whose current frame asked for opcode tracing. */
static int
maybe_set_opcode_trace(PyThreadState *ts)
{
    struct lu_frame *f = lu_frame_user((struct lu_frame *)ts->current_frame, 0);
    if (f == NULL || f->frame_obj == NULL || !lu_frame_trace_opcodes((PyObject *)f->frame_obj)) {
        return 0;
    }
    return set_opcode_trace_world_stopped(f->code, 1);
}

LU_EXPORT int
_PyEval_SetProfile(PyThreadState *ts, Py_tracefunc func, PyObject *arg)
{
    PyInterpreterState *interp = ts->interp;
    if (setup_once(interp, &interp->sys_profile_setup, setup_profile_callbacks) < 0) {
        return -1;
    }
    lu_stw_stop(interp);
    PyObject *old = swap_profile_func_arg(ts, func, arg);
    int ret = set_monitoring_profile_events(interp);
    lu_stw_start(interp);
    Py_XDECREF(old);        /* outside the stop */
    return ret;
}

/* All tasks of the interpreter take func and arg; their old objects come back as a tuple. */
static int
set_all_threads(PyInterpreterState *interp, Py_tracefunc func, PyObject *arg, int tracing)
{
    Py_ssize_t n = 0;
    TASK_LIST_LOCK(interp);
    for (PyThreadState *p = interp->tasks; p != NULL; p = p->next) {
        n++;
    }
    TASK_LIST_UNLOCK(interp);
    /* Tasks may come and go before the stop; the tuple is sized again below. */
    for (;;) {
        PyObject *old = PyTuple_New(n);
        if (old == NULL) {
            return -1;
        }
        lu_stw_stop(interp);
        TASK_LIST_LOCK(interp);
        Py_ssize_t now = 0;
        for (PyThreadState *p = interp->tasks; p != NULL; p = p->next) {
            now++;
        }
        if (now != n) {
            TASK_LIST_UNLOCK(interp);
            lu_stw_start(interp);
            Py_DECREF(old);
            n = now;
            continue;
        }
        Py_ssize_t i = n;
        for (PyThreadState *p = interp->tasks; p != NULL; p = p->next) {
            PyObject *prev = tracing ? swap_trace_func_arg(p, func, arg) : swap_profile_func_arg(p, func, arg);
            PyTuple_SET_ITEM(old, --i, prev);
        }
        int err = 0;
        if (tracing && interp->sys_tracing_threads) {
            for (PyThreadState *p = interp->tasks; p != NULL && !err; p = p->next) {
                err = maybe_set_opcode_trace(p);
            }
        }
        TASK_LIST_UNLOCK(interp);
        if (!err) {
            err = tracing ? set_monitoring_trace_events(interp) : set_monitoring_profile_events(interp);
        }
        lu_stw_start(interp);
        Py_DECREF(old);
        return err;
    }
}

int
_PyEval_SetProfileAllThreads(PyInterpreterState *interp, Py_tracefunc func, PyObject *arg)
{
    if (setup_once(interp, &interp->sys_profile_setup, setup_profile_callbacks) < 0) {
        return -1;
    }
    return set_all_threads(interp, func, arg, 0);
}

int
_PyEval_SetTrace(PyThreadState *ts, Py_tracefunc func, PyObject *arg)
{
    PyInterpreterState *interp = ts->interp;
    if (setup_once(interp, &interp->sys_trace_setup, setup_trace_callbacks) < 0) {
        return -1;
    }
    lu_stw_stop(interp);
    PyObject *old = swap_trace_func_arg(ts, func, arg);
    int err = set_monitoring_trace_events(interp);
    if (err == 0 && interp->sys_tracing_threads) {
        err = maybe_set_opcode_trace(ts);
    }
    lu_stw_start(interp);
    Py_XDECREF(old);
    return err;
}

int
_PyEval_SetTraceAllThreads(PyInterpreterState *interp, Py_tracefunc func, PyObject *arg)
{
    if (setup_once(interp, &interp->sys_trace_setup, setup_trace_callbacks) < 0) {
        return -1;
    }
    return set_all_threads(interp, func, arg, 1);
}

void
PyEval_SetProfile(Py_tracefunc func, PyObject *arg)
{
    if (_PyEval_SetProfile(PyThreadState_Get(), func, arg) < 0) {
        lu_err_unraisable("Exception ignored in sys tracing", Py_None);
    }
}

void
PyEval_SetProfileAllThreads(Py_tracefunc func, PyObject *arg)
{
    if (_PyEval_SetProfileAllThreads(PyInterpreterState_Get(), func, arg) < 0) {
        lu_err_unraisable("Exception ignored in sys tracing", Py_None);
    }
}

void
PyEval_SetTrace(Py_tracefunc func, PyObject *arg)
{
    if (_PyEval_SetTrace(PyThreadState_Get(), func, arg) < 0) {
        lu_err_unraisable("Exception ignored in sys tracing", Py_None);
    }
}

void
PyEval_SetTraceAllThreads(Py_tracefunc func, PyObject *arg)
{
    if (_PyEval_SetTraceAllThreads(PyInterpreterState_Get(), func, arg) < 0) {
        lu_err_unraisable("Exception ignored in sys tracing", Py_None);
    }
}

/* The sys functions */

static const char *const whatstrings[] = {
    "call", "exception", "line", "return", "c_call", "c_exception", "c_return", "opcode",
};

static PyObject *
call_trampoline(PyObject *callback, PyFrameObject *frame, int what, PyObject *arg)
{
    PyObject *args[3] = { (PyObject *)frame, NULL, arg != NULL ? arg : Py_None };
    args[1] = PyUnicode_InternFromString(whatstrings[what]);
    if (args[1] == NULL) {
        return NULL;
    }
    PyObject *result = PyObject_Vectorcall(callback, args, 3, NULL);
    Py_DECREF(args[1]);
    return result;
}

static int
profile_trampoline(PyObject *self, PyFrameObject *frame, int what, PyObject *arg)
{
    PyObject *result = call_trampoline(self, frame, what, arg);
    if (result == NULL) {
        _PyEval_SetProfile(PyThreadState_Get(), NULL, NULL);
        return -1;
    }
    Py_DECREF(result);
    return 0;
}

static int
trace_trampoline(PyObject *self, PyFrameObject *frame, int what, PyObject *arg)
{
    PyObject **trace = lu_frame_trace_slot((PyObject *)frame);
    PyObject *callback = what == PyTrace_CALL ? self : *trace;
    if (callback == NULL) {
        return 0;
    }
    Py_INCREF(callback);        /* the callback can reset f_trace */
    PyObject *result = call_trampoline(callback, frame, what, arg);
    Py_DECREF(callback);
    if (result == NULL) {
        _PyEval_SetTrace(PyThreadState_Get(), NULL, NULL);
        Py_CLEAR(*trace);
        return -1;
    }
    if (result != Py_None) {
        Py_XSETREF(*trace, result);
    }
    else {
        Py_DECREF(result);
    }
    return 0;
}

static PyObject *
sys_settrace(PyObject *m, PyObject *function)
{
    int err = function == Py_None ? _PyEval_SetTrace(PyThreadState_Get(), NULL, NULL)
                                  : _PyEval_SetTrace(PyThreadState_Get(), trace_trampoline, function);
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

static PyObject *
sys_settraceallthreads(PyObject *m, PyObject *function)
{
    int err = function == Py_None
        ? _PyEval_SetTraceAllThreads(PyInterpreterState_Get(), NULL, NULL)
        : _PyEval_SetTraceAllThreads(PyInterpreterState_Get(), trace_trampoline, function);
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

static PyObject *
sys_gettrace(PyObject *m, PyObject *unused)
{
    PyObject *obj = PyThreadState_Get()->c_traceobj;
    return Py_NewRef(obj != NULL ? obj : Py_None);
}

static PyObject *
sys_setprofile(PyObject *m, PyObject *function)
{
    int err = function == Py_None ? _PyEval_SetProfile(PyThreadState_Get(), NULL, NULL)
                                  : _PyEval_SetProfile(PyThreadState_Get(), profile_trampoline, function);
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

static PyObject *
sys_setprofileallthreads(PyObject *m, PyObject *function)
{
    int err = function == Py_None
        ? _PyEval_SetProfileAllThreads(PyInterpreterState_Get(), NULL, NULL)
        : _PyEval_SetProfileAllThreads(PyInterpreterState_Get(), profile_trampoline, function);
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

static PyObject *
sys_getprofile(PyObject *m, PyObject *unused)
{
    PyObject *obj = PyThreadState_Get()->c_profileobj;
    return Py_NewRef(obj != NULL ? obj : Py_None);
}

/* sys.call_tracing: func(*args) with tracing off in this task. */
static PyObject *
sys_call_tracing(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2) {
        PyErr_Format(PyExc_TypeError, "call_tracing expected 2 arguments, got %zd", nargs);
        return NULL;
    }
    if (!PyTuple_Check(args[1])) {
        PyErr_Format(PyExc_TypeError, "call_tracing() argument 2 must be tuple, not %.50s",
                     Py_TYPE(args[1])->tp_name);
        return NULL;
    }
    PyThreadState *ts = PyThreadState_Get();
    int save = ts->tracing;
    ts->tracing = 0;
    PyObject *result = PyObject_Call(args[0], args[1], NULL);
    ts->tracing = save;
    return result;
}

PyMethodDef lu_sys_tracing_methods[] = {
    { "settrace", sys_settrace, METH_O, NULL },
    { "gettrace", sys_gettrace, METH_NOARGS, NULL },
    { "setprofile", sys_setprofile, METH_O, NULL },
    { "getprofile", sys_getprofile, METH_NOARGS, NULL },
    { "_settraceallthreads", sys_settraceallthreads, METH_O, NULL },
    { "_setprofileallthreads", sys_setprofileallthreads, METH_O, NULL },
    { "call_tracing", FAST sys_call_tracing, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
