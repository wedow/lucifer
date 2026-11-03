/* PEP 669 through the C API, as an unmodified multi-phase extension module
   (tests/capi/capi_monitoring_run.py drives it): a scope's state array follows
   the interpreter's monitoring version (PyMonitoring_EnterScope), and the Fire
   functions call the callbacks of the tools that watch the event. */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

static uint64_t version;
static PyMonitoringState states[3];
static const uint8_t event_types[3] = {
    PY_MONITORING_EVENT_PY_START, PY_MONITORING_EVENT_CALL, PY_MONITORING_EVENT_LINE,
};

/* fire(kind, code, offset, arg): enters the scope and fires when a tool watches. */
static PyObject *
fire(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError, "fire(kind, code, offset, arg)");
        return NULL;
    }
    Py_ssize_t kind = PyLong_AsSsize_t(args[0]), offset = PyLong_AsSsize_t(args[2]);
    Py_ssize_t arg = PyLong_AsSsize_t(args[3]);
    PyObject *code = args[1];
    if (PyErr_Occurred()) {
        return NULL;
    }
    if (PyMonitoring_EnterScope(states, &version, event_types, 3) < 0) {
        return NULL;
    }
    PyMonitoringState *state = &states[kind];
    if (!state->active) {
        return PyLong_FromLong(0);
    }
    int err = 0;
    if (kind == 0) {
        err = _PyMonitoring_FirePyStartEvent(state, code, (int32_t)offset);
    }
    else if (kind == 1) {
        PyObject *callable = PyLong_FromSsize_t(arg);
        err = _PyMonitoring_FireCallEvent(state, code, (int32_t)offset, callable, Py_None);
        Py_DECREF(callable);
    }
    else {
        err = _PyMonitoring_FireLineEvent(state, code, (int32_t)offset, (int)arg);
    }
    PyMonitoring_ExitScope();
    if (err < 0) {
        return NULL;
    }
    return PyLong_FromLong(1);
}

static PyMethodDef methods[] = {
    { "fire", (PyCFunction)(void (*)(void))fire, METH_FASTCALL, "fire(kind, code, offset, arg)" },
    { NULL, NULL, 0, NULL },
};


static PyModuleDef_Slot slots[] = {
    { Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED },
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

static PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "capi_monitoring", "PEP 669 events fired from C", 0, methods, slots,
    NULL, NULL, NULL,
};

PyMODINIT_FUNC
PyInit_capi_monitoring(void)
{
    return PyModuleDef_Init(&module);
}
