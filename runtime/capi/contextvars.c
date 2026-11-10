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
    /* A value the current context holds, read as ContextVar.get reads it
       (its _data dict) without calling it; the rest is the method's. */
    PyObject *ctx = PyThreadState_Get()->context;
    PyObject *data = ctx != NULL ? lu_getattr_native(ctx, LU_ID(_data)) : NULL;
    if (data != NULL) {
        int found = PyDict_CheckExact(data) ? PyDict_GetItemRef(data, var, val) : 0;
        Py_DECREF(data);
        if (found != 0) {
            return found < 0 ? -1 : 0;
        }
    }
    else if (ctx != NULL && PyErr_Occurred()) {
        PyErr_Clear();          /* the method answers, or raises, instead */
    }
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

/* The task's current context lives in its thread state (ts->context), as
   in CPython, so a new thread starts with none whatever ran before it.

   context_get(): the current context, or None.
   context_set(ctx, switched): makes ctx (None for none) current and answers
   the one it replaces; when switched, each watcher is told (Context.run's
   enter and exit), a failing one reported unraisable. */
static PyObject *
prim_context_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 0) {
        lu_err_format(PyExc_TypeError, "context_get()");
        return NULL;
    }
    PyObject *ctx = PyThreadState_Get()->context;
    return Py_NewRef(ctx != NULL ? ctx : Py_None);
}

static PyObject *
prim_context_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2) {
        lu_err_format(PyExc_TypeError, "context_set(ctx, switched)");
        return NULL;
    }
    PyThreadState *ts = PyThreadState_Get();
    PyObject *ctx = args[0] == Py_None ? NULL : args[0];
    PyObject *prev = ts->context;
    ts->context = Py_XNewRef(ctx);
    ts->context_ver++;
    if (args[1] == Py_True) {
        lu_watcher *table = ts->interp->context_watchers;
        for (int i = 0; i < LU_MAX_WATCHERS; i++) {
            PyContext_WatchCallback cb = (PyContext_WatchCallback)table[i];
            if (cb != NULL && cb(Py_CONTEXT_SWITCHED, ctx) < 0) {
                PyErr_FormatUnraisable("Exception ignored in %s watcher callback for %R",
                                       "Py_CONTEXT_SWITCHED", args[0]);
            }
        }
    }
    return prev != NULL ? prev : Py_NewRef(Py_None);
}

PyMethodDef lu_prim_context_methods[] = {
    { "context_get", (PyCFunction)(void (*)(void))prim_context_get, METH_FASTCALL, NULL },
    { "context_set", (PyCFunction)(void (*)(void))prim_context_set, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};

/* _PyContext_NewHamtForTests (Python/context.c): a bare HAMT for the
   _testinternalcapi.hamt() tests. lucifer's contexts are dicts and it has no
   HAMT (Python/hamt.c), so the hook fails cleanly instead of aborting. */
LU_EXPORT PyObject *
_PyContext_NewHamtForTests(void)
{
    PyErr_SetString(PyExc_NotImplementedError, "lucifer has no HAMT: contexts hold their variables in dicts");
    return NULL;
}
