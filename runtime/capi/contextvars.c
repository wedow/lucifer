/* The context variable API (Python/context.c), over the kernel's
   _contextvars classes. */

#include "lucifer.h"

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
