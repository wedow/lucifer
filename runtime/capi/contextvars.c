/* The context variable API (Python/context.c), over the kernel's
   _contextvars classes. */

#include "lucifer.h"
#include "prim/prim.h"

PyObject *
PyContextVar_New(const char *name, PyObject *def)
{
    PyObject *module = PyImport_ImportModule("_contextvars");
    PyObject *cls = module != NULL ? PyObject_GetAttrString(module, "ContextVar") : NULL;
    PyObject *args = cls != NULL ? Py_BuildValue("(s)", name) : NULL;
    PyObject *kwargs = args != NULL && def != NULL ? Py_BuildValue("{s:O}", "default", def) : NULL;
    PyObject *var = args != NULL && (def == NULL || kwargs != NULL) ? PyObject_Call(cls, args, kwargs) : NULL;
    Py_XDECREF(module);
    Py_XDECREF(cls);
    Py_XDECREF(args);
    Py_XDECREF(kwargs);
    return var;
}

/* *val is the variable's value in the current context, else default_value,
   else the variable's own default, else NULL. */
int
PyContextVar_Get(PyObject *var, PyObject *default_value, PyObject **val)
{
    PyObject *r = default_value != NULL ? PyObject_CallMethod(var, "get", "O", default_value)
                                        : PyObject_CallMethod(var, "get", NULL);
    if (r == NULL && PyErr_ExceptionMatches(PyExc_LookupError)) {
        PyErr_Clear();
        *val = NULL;
        return 0;
    }
    *val = r;
    return r == NULL ? -1 : 0;
}

PyObject *
PyContextVar_Set(PyObject *var, PyObject *value)
{
    return PyObject_CallMethod(var, "set", "O", value);
}

/* Context watchers (Python/context.c): told of each switch of the thread's
   current context, which the kernel's Context.run reports. */

int
PyContext_AddWatcher(PyContext_WatchCallback callback)
{
    return lu_watcher_add(PyInterpreterState_Get()->context_watchers, 0, (lu_watcher)callback, "context");
}

int
PyContext_ClearWatcher(int watcher_id)
{
    lu_watcher *table = PyInterpreterState_Get()->context_watchers;
    if (lu_watcher_check(table, watcher_id, "Invalid context watcher ID %d",
                         "No context watcher set for ID %d") < 0) {
        return -1;
    }
    table[watcher_id] = NULL;
    return 0;
}

/* context_switched(ctx): the thread's current context is now ctx (None
   for none); each watcher is told, a failing one reported unraisable. */
static PyObject *
prim_context_switched(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1) {
        lu_err_format(PyExc_TypeError, "context_switched(ctx)");
        return NULL;
    }
    lu_watcher *table = PyInterpreterState_Get()->context_watchers;
    PyObject *ctx = args[0] == Py_None ? NULL : args[0];
    for (int i = 0; i < LU_MAX_WATCHERS; i++) {
        PyContext_WatchCallback cb = (PyContext_WatchCallback)table[i];
        if (cb != NULL && cb(Py_CONTEXT_SWITCHED, ctx) < 0) {
            PyErr_FormatUnraisable("Exception ignored in %s watcher callback for %R",
                                   "Py_CONTEXT_SWITCHED", args[0]);
        }
    }
    Py_RETURN_NONE;
}

PyMethodDef lu_prim_context_methods[] = {
    { "context_switched", (PyCFunction)(void (*)(void))prim_context_switched, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};
