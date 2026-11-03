/* The sys.monitoring module (the second half of Python/instrumentation.c).
   Every change of events or callbacks stops the world; reading them does not. */

#include "interp/instrument.h"
#include "interp/code.h"
#include "interp/exceptions.h"
#include "object/dict.h"

#define C_RETURN_EVENTS ((1 << PY_MONITORING_EVENT_C_RETURN) | (1 << PY_MONITORING_EVENT_C_RAISE))
#define C_CALL_EVENTS (C_RETURN_EVENTS | (1 << PY_MONITORING_EVENT_CALL))

const char *lu_monitoring_event_name(int event);        /* instrument.c */

static int
check_valid_tool(int tool_id)
{
    if (tool_id < 0 || tool_id >= LU_TOOL_SYS_PROFILE) {
        PyErr_Format(PyExc_ValueError, "invalid tool %d (must be between 0 and 5)", tool_id);
        return -1;
    }
    return 0;
}

/* The int arguments of a monitoring function; false with the error set. */
static int
int_args(const char *name, PyObject *const *args, Py_ssize_t nargs, Py_ssize_t want, int *out)
{
    if (nargs != want) {
        PyErr_Format(PyExc_TypeError, "%s expected %zd arguments, got %zd", name, want, nargs);
        return 0;
    }
    for (Py_ssize_t i = 0; i < want; i++) {
        if (!PyLong_Check(args[i])) {
            PyErr_Format(PyExc_TypeError, "%s: argument %zd must be int, not %.50s", name, i + 1,
                         Py_TYPE(args[i])->tp_name);
            return 0;
        }
        long v = lu_as_long(args[i]);
        if (v == -1 && PyErr_Occurred()) {
            return 0;
        }
        if (v > INT_MAX || v < INT_MIN) {
            PyErr_SetString(PyExc_OverflowError, "Python int too large to convert to C int");
            return 0;
        }
        out[i] = (int)v;
    }
    return 1;
}

static PyObject *
mon_use_tool_id(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2) {
        PyErr_Format(PyExc_TypeError, "use_tool_id expected 2 arguments, got %zd", nargs);
        return NULL;
    }
    int tool_id;
    if (!int_args("use_tool_id", args, 1, 1, &tool_id) || check_valid_tool(tool_id)) {
        return NULL;
    }
    if (!PyUnicode_Check(args[1])) {
        PyErr_SetString(PyExc_ValueError, "tool name must be a str");
        return NULL;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (interp->monitoring_tool_names[tool_id] != NULL) {
        PyErr_Format(PyExc_ValueError, "tool %d is already in use", tool_id);
        return NULL;
    }
    interp->monitoring_tool_names[tool_id] = Py_NewRef(args[1]);
    Py_RETURN_NONE;
}

static PyObject *
mon_clear_tool_id(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int tool_id;
    if (!int_args("clear_tool_id", args, nargs, 1, &tool_id) || check_valid_tool(tool_id)) {
        return NULL;
    }
    if (PyInterpreterState_Get()->monitoring_tool_names[tool_id] != NULL
        && lu_monitoring_clear_tool(tool_id) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
mon_free_tool_id(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int tool_id;
    if (!int_args("free_tool_id", args, nargs, 1, &tool_id) || check_valid_tool(tool_id)) {
        return NULL;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (interp->monitoring_tool_names[tool_id] != NULL && lu_monitoring_clear_tool(tool_id) < 0) {
        return NULL;
    }
    Py_CLEAR(interp->monitoring_tool_names[tool_id]);
    Py_RETURN_NONE;
}

static PyObject *
mon_get_tool(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int tool_id;
    if (!int_args("get_tool", args, nargs, 1, &tool_id) || check_valid_tool(tool_id)) {
        return NULL;
    }
    PyObject *name = PyInterpreterState_Get()->monitoring_tool_names[tool_id];
    return Py_NewRef(name != NULL ? name : Py_None);
}

static PyObject *
mon_register_callback(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int ints[2];
    if (nargs != 3) {
        PyErr_Format(PyExc_TypeError, "register_callback expected 3 arguments, got %zd", nargs);
        return NULL;
    }
    if (!int_args("register_callback", args, 2, 2, ints) || check_valid_tool(ints[0])) {
        return NULL;
    }
    int event = ints[1];
    if (event <= 0 || (event & (event - 1)) != 0) {
        PyErr_SetString(PyExc_ValueError, "The callback can only be set for one event at a time");
        return NULL;
    }
    int event_id = 31 - __builtin_clz((unsigned)event);
    if (event_id >= LU_MON_EVENTS) {
        PyErr_Format(PyExc_ValueError, "invalid event %d", event);
        return NULL;
    }
    PyObject *func = args[2] == Py_None ? NULL : args[2];
    PyObject *old = lu_monitoring_register_callback(ints[0], event_id, func);
    if (old == NULL) {
        return PyErr_Occurred() ? NULL : Py_NewRef(Py_None);
    }
    return old;
}

static PyObject *
mon_get_events(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int tool_id;
    if (!int_args("get_events", args, nargs, 1, &tool_id) || check_valid_tool(tool_id)) {
        return NULL;
    }
    lu_global_monitors *g = &PyInterpreterState_Get()->monitors;
    long set = 0;
    for (int e = 0; e < LU_MON_UNGROUPED_EVENTS; e++) {
        set |= (long)((g->tools[e] >> tool_id) & 1) << e;
    }
    return PyLong_FromLong(set);
}

/* The event set of set_events and set_local_events, with the derived events
   expanded; -1 with an error set. */
static int
normalize_events(int event_set, int limit, const char *what)
{
    if (event_set < 0 || event_set >= (1 << limit)) {
        PyErr_Format(PyExc_ValueError, "invalid %sevent set 0x%x", what, event_set);
        return -1;
    }
    if ((event_set & C_RETURN_EVENTS) && (event_set & C_CALL_EVENTS) != C_CALL_EVENTS) {
        PyErr_Format(PyExc_ValueError, "cannot set C_RETURN or C_RAISE events independently");
        return -1;
    }
    event_set &= ~C_RETURN_EVENTS;
    if (event_set & (1 << PY_MONITORING_EVENT_BRANCH)) {
        event_set &= ~(1 << PY_MONITORING_EVENT_BRANCH);
        event_set |= (1 << PY_MONITORING_EVENT_BRANCH_RIGHT) | (1 << PY_MONITORING_EVENT_BRANCH_LEFT);
    }
    return event_set;
}

static PyObject *
mon_set_events(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int ints[2];
    if (!int_args("set_events", args, nargs, 2, ints) || check_valid_tool(ints[0])) {
        return NULL;
    }
    int event_set = normalize_events(ints[1], LU_MON_EVENTS, "");
    if (event_set < 0) {
        return NULL;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    lu_stw_stop(interp);
    int err = lu_monitoring_set_events(ints[0], event_set);
    lu_stw_start(interp);
    if (err) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
mon_get_local_events(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2) {
        PyErr_Format(PyExc_TypeError, "get_local_events expected 2 arguments, got %zd", nargs);
        return NULL;
    }
    int tool_id;
    if (!PyCode_Check(args[1])) {
        PyErr_SetString(PyExc_TypeError, "code must be a code object");
        return NULL;
    }
    if (!int_args("get_local_events", args, 1, 1, &tool_id) || check_valid_tool(tool_id)) {
        return NULL;
    }
    long set = 0;
    lu_monitoring *data = ((PyCodeObject *)args[1])->_co_monitoring;
    if (data != NULL) {
        for (int e = 0; e < LU_MON_LOCAL_EVENTS; e++) {
            set |= (long)((data->local_monitors.tools[e] >> tool_id) & 1) << e;
        }
    }
    return PyLong_FromLong(set);
}

static PyObject *
mon_set_local_events(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3) {
        PyErr_Format(PyExc_TypeError, "set_local_events expected 3 arguments, got %zd", nargs);
        return NULL;
    }
    int ints[2];
    if (!PyCode_Check(args[1])) {
        PyErr_SetString(PyExc_TypeError, "code must be a code object");
        return NULL;
    }
    PyObject *pair[2] = { args[0], args[2] };
    if (!int_args("set_local_events", pair, 2, 2, ints) || check_valid_tool(ints[0])) {
        return NULL;
    }
    int event_set = normalize_events(ints[1], LU_MON_LOCAL_EVENTS, "local ");
    if (event_set < 0) {
        return NULL;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    lu_stw_stop(interp);
    int err = lu_monitoring_set_local_events((PyCodeObject *)args[1], ints[0], event_set);
    lu_stw_start(interp);
    if (err) {
        return NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
mon_restart_events(PyObject *m, PyObject *unused)
{
    int err = lu_monitoring_restart();
    return err ? NULL : Py_NewRef(Py_None);
}

static int
dict_set(PyObject *d, const char *name, PyObject *value)
{
    PyObject *key = PyUnicode_InternFromString(name);
    int err = key == NULL || lu_ns_insert(d, key, value) < 0;
    Py_XDECREF(key);
    return err ? -1 : 0;
}

static PyObject *
mon_all_events(PyObject *m, PyObject *unused)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyObject *res = PyDict_New();
    for (int e = 0; res != NULL && e < LU_MON_UNGROUPED_EVENTS; e++) {
        uint8_t tools = interp->monitors.tools[e];
        if (tools == 0) {
            continue;
        }
        PyObject *tools_obj = PyLong_FromLong(tools);
        int err = tools_obj == NULL || dict_set(res, lu_monitoring_event_name(e), tools_obj) < 0;
        Py_XDECREF(tools_obj);
        if (err) {
            Py_CLEAR(res);
        }
    }
    return res;
}

static PyMethodDef monitoring_methods[] = {
    { "use_tool_id", (PyCFunction)(void (*)(void))mon_use_tool_id, METH_FASTCALL, NULL },
    { "clear_tool_id", (PyCFunction)(void (*)(void))mon_clear_tool_id, METH_FASTCALL, NULL },
    { "free_tool_id", (PyCFunction)(void (*)(void))mon_free_tool_id, METH_FASTCALL, NULL },
    { "get_tool", (PyCFunction)(void (*)(void))mon_get_tool, METH_FASTCALL, NULL },
    { "register_callback", (PyCFunction)(void (*)(void))mon_register_callback, METH_FASTCALL, NULL },
    { "get_events", (PyCFunction)(void (*)(void))mon_get_events, METH_FASTCALL, NULL },
    { "set_events", (PyCFunction)(void (*)(void))mon_set_events, METH_FASTCALL, NULL },
    { "get_local_events", (PyCFunction)(void (*)(void))mon_get_local_events, METH_FASTCALL, NULL },
    { "set_local_events", (PyCFunction)(void (*)(void))mon_set_local_events, METH_FASTCALL, NULL },
    { "restart_events", mon_restart_events, METH_NOARGS, NULL },
    { "_all_events", mon_all_events, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL },
};

static int
set_attr(PyObject *module, const char *name, PyObject *value)
{
    PyObject *key = value != NULL ? PyUnicode_InternFromString(name) : NULL;
    int err = key == NULL || lu_ns_insert(PyModule_GetDict(module), key, value) < 0;
    Py_XDECREF(key);
    Py_XDECREF(value);
    return err ? -1 : 0;
}

PyObject *
lu_monitoring_module_new(void)
{
    PyObject *mod = lu_module_new("sys.monitoring");
    PyObject *events = lu_module_new("events");
    if (mod == NULL || events == NULL) {
        goto error;
    }
    for (PyMethodDef *ml = monitoring_methods; ml->ml_name != NULL; ml++) {
        if (set_attr(mod, ml->ml_name, PyCFunction_NewEx(ml, mod, NULL))) {
            goto error;
        }
    }
    if (set_attr(mod, "DISABLE", Py_NewRef(&lu_instrumentation_disable))
        || set_attr(mod, "MISSING", Py_NewRef(&lu_instrumentation_missing))
        || set_attr(mod, "DEBUGGER_ID", PyLong_FromLong(LU_TOOL_DEBUGGER))
        || set_attr(mod, "COVERAGE_ID", PyLong_FromLong(LU_TOOL_COVERAGE))
        || set_attr(mod, "PROFILER_ID", PyLong_FromLong(LU_TOOL_PROFILER))
        || set_attr(mod, "OPTIMIZER_ID", PyLong_FromLong(LU_TOOL_OPTIMIZER))
        || set_attr(events, "NO_EVENTS", PyLong_FromLong(0))) {
        goto error;
    }
    for (int i = 0; i < LU_MON_EVENTS; i++) {
        if (set_attr(events, lu_monitoring_event_name(i), PyLong_FromLong(1L << i))) {
            goto error;
        }
    }
    if (set_attr(mod, "events", events)) {
        events = NULL;
        goto error;
    }
    return mod;
error:
    Py_XDECREF(mod);
    Py_XDECREF(events);
    return NULL;
}
