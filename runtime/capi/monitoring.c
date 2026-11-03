/* The monitoring C-API (PEP 669 as C extensions use it; the last part of
   Python/instrumentation.c of CPython 3.14): a state array per scope holds the
   tools active for each event, refreshed when the interpreter's monitoring
   version moves, and the Fire functions call those tools' callbacks. */

#include "interp/instrument.h"

static int
most_significant_bit(uint8_t bits)
{
    return 31 - __builtin_clz((unsigned)bits);
}

static int
capi_call_instrumentation(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                          PyObject **args, Py_ssize_t nargs, int event)
{
    PyThreadState *ts = PyThreadState_Get();
    PyInterpreterState *interp = ts->interp;
    uint8_t tools = state->active;
    args[1] = codelike;
    if (offset < 0) {
        PyErr_SetString(PyExc_ValueError, "offset must be non-negative");
        return -1;
    }
    if (event != PY_MONITORING_EVENT_LINE) {
        PyObject *offset_obj = PyLong_FromLong(offset);
        if (offset_obj == NULL) {
            return -1;
        }
        args[2] = offset_obj;
    }
    size_t nargsf = (size_t)nargs | PY_VECTORCALL_ARGUMENTS_OFFSET;
    PyObject **callargs = &args[1];
    int err = 0;
    while (tools) {
        int tool = most_significant_bit(tools);
        tools ^= (1 << tool);
        int res = lu_call_one_instrument(interp, ts, callargs, nargsf, tool, event);
        if (res < 0) {
            err = -1;
            break;
        }
        if (res > 0) {
            if (!PY_MONITORING_IS_INSTRUMENTED_EVENT(event)) {
                PyErr_Format(PyExc_ValueError, "Cannot disable %s events. Callback removed.",
                             lu_monitoring_event_name(event));
                Py_CLEAR(interp->monitoring_callables[tool][event]);
                err = -1;
                break;
            }
            state->active &= ~(1 << tool);
        }
    }
    Py_XDECREF(args[2]);
    return err;
}

int
PyMonitoring_EnterScope(PyMonitoringState *state_array, uint64_t *version, const uint8_t *event_types,
                        Py_ssize_t length)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    uint32_t current = (uint32_t)interp->monitoring_version;
    if (current == *version) {
        return 0;
    }
    for (Py_ssize_t i = 0; i < length; i++) {
        state_array[i].active = interp->monitors.tools[event_types[i]];
    }
    *version = current;
    return 0;
}

int
PyMonitoring_ExitScope(void)
{
    return 0;
}

int
_PyMonitoring_FirePyStartEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset)
{
    PyObject *args[3] = { NULL, NULL, NULL };
    return capi_call_instrumentation(state, codelike, offset, args, 2, PY_MONITORING_EVENT_PY_START);
}

int
_PyMonitoring_FirePyResumeEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset)
{
    PyObject *args[3] = { NULL, NULL, NULL };
    return capi_call_instrumentation(state, codelike, offset, args, 2, PY_MONITORING_EVENT_PY_RESUME);
}

int
_PyMonitoring_FirePyReturnEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                                PyObject *retval)
{
    PyObject *args[4] = { NULL, NULL, NULL, retval };
    return capi_call_instrumentation(state, codelike, offset, args, 3, PY_MONITORING_EVENT_PY_RETURN);
}

int
_PyMonitoring_FirePyYieldEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                               PyObject *retval)
{
    PyObject *args[4] = { NULL, NULL, NULL, retval };
    return capi_call_instrumentation(state, codelike, offset, args, 3, PY_MONITORING_EVENT_PY_YIELD);
}

int
_PyMonitoring_FireCallEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                            PyObject *callable, PyObject *arg0)
{
    PyObject *args[5] = { NULL, NULL, NULL, callable, arg0 };
    return capi_call_instrumentation(state, codelike, offset, args, 4, PY_MONITORING_EVENT_CALL);
}

int
_PyMonitoring_FireLineEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset, int lineno)
{
    PyObject *lno = PyLong_FromLong(lineno);
    if (lno == NULL) {
        return -1;
    }
    PyObject *args[3] = { NULL, NULL, lno };
    int res = capi_call_instrumentation(state, codelike, offset, args, 2, PY_MONITORING_EVENT_LINE);
    Py_DECREF(lno);
    return res;
}

int
_PyMonitoring_FireJumpEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                            PyObject *target_offset)
{
    PyObject *args[4] = { NULL, NULL, NULL, target_offset };
    return capi_call_instrumentation(state, codelike, offset, args, 3, PY_MONITORING_EVENT_JUMP);
}

/* The deprecated BRANCH fires as BRANCH_RIGHT. */
int
_PyMonitoring_FireBranchEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                              PyObject *target_offset)
{
    PyObject *args[4] = { NULL, NULL, NULL, target_offset };
    return capi_call_instrumentation(state, codelike, offset, args, 3, PY_MONITORING_EVENT_BRANCH_RIGHT);
}

int
_PyMonitoring_FireBranchRightEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                                   PyObject *target_offset)
{
    PyObject *args[4] = { NULL, NULL, NULL, target_offset };
    return capi_call_instrumentation(state, codelike, offset, args, 3, PY_MONITORING_EVENT_BRANCH_RIGHT);
}

int
_PyMonitoring_FireBranchLeftEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                                  PyObject *target_offset)
{
    PyObject *args[4] = { NULL, NULL, NULL, target_offset };
    return capi_call_instrumentation(state, codelike, offset, args, 3, PY_MONITORING_EVENT_BRANCH_LEFT);
}

int
_PyMonitoring_FireCReturnEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                               PyObject *retval)
{
    PyObject *args[4] = { NULL, NULL, NULL, retval };
    return capi_call_instrumentation(state, codelike, offset, args, 3, PY_MONITORING_EVENT_C_RETURN);
}

/* The exception events fire with the exception in flight, which a callback
   that raises replaces. */
static int
fire_exception_event(PyMonitoringState *state, PyObject *codelike, int32_t offset, int event, int keep)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL) {
        PyErr_Format(PyExc_ValueError, "Firing event %d with no exception set", event);
        return -1;
    }
    PyObject *args[4] = { NULL, NULL, NULL, exc };
    int err = capi_call_instrumentation(state, codelike, offset, args, 3, event);
    if (err == 0 && keep) {
        PyErr_SetRaisedException(exc);
    }
    else {
        Py_XDECREF(exc);
    }
    return err;
}

#define EXCEPTION_EVENT(fn, event) \
    int fn(PyMonitoringState *state, PyObject *codelike, int32_t offset) \
    { \
        return fire_exception_event(state, codelike, offset, event, 1); \
    }

EXCEPTION_EVENT(_PyMonitoring_FirePyThrowEvent, PY_MONITORING_EVENT_PY_THROW)
EXCEPTION_EVENT(_PyMonitoring_FireRaiseEvent, PY_MONITORING_EVENT_RAISE)
EXCEPTION_EVENT(_PyMonitoring_FireCRaiseEvent, PY_MONITORING_EVENT_C_RAISE)
EXCEPTION_EVENT(_PyMonitoring_FireReraiseEvent, PY_MONITORING_EVENT_RERAISE)
EXCEPTION_EVENT(_PyMonitoring_FireExceptionHandledEvent, PY_MONITORING_EVENT_EXCEPTION_HANDLED)
EXCEPTION_EVENT(_PyMonitoring_FirePyUnwindEvent, PY_MONITORING_EVENT_PY_UNWIND)

int
_PyMonitoring_FireStopIterationEvent(PyMonitoringState *state, PyObject *codelike, int32_t offset,
                                     PyObject *value)
{
    PyErr_SetObject(PyExc_StopIteration, value);
    return fire_exception_event(state, codelike, offset, PY_MONITORING_EVENT_STOP_ITERATION, 0);
}
