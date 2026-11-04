/* The import API (Python/import.c): imports go through builtins.__import__,
   the kernel's, which runs importlib; sys.modules is the interpreter's. */

#include "lucifer.h"

PyObject *
PyImport_GetModuleDict(void)
{
    return PyInterpreterState_Get()->modules;
}

PyObject *
PyImport_GetModule(PyObject *name)
{
    PyObject *m = PyObject_GetItem(PyImport_GetModuleDict(), name);
    if (m == NULL) {
        if (PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_Clear();
        }
        return NULL;
    }
    if (m == Py_None) {
        Py_DECREF(m);
        return NULL;
    }
    return m;
}

PyObject *
PyImport_AddModuleRef(const char *name)
{
    PyObject *modules = PyImport_GetModuleDict(), *key = PyUnicode_FromString(name);
    if (key == NULL) {
        return NULL;
    }
    PyObject *m = PyImport_GetModule(key);
    if (m == NULL && !PyErr_Occurred()) {
        m = PyModule_NewObject(key);
        if (m != NULL && PyObject_SetItem(modules, key, m) < 0) {
            Py_CLEAR(m);
        }
    }
    Py_DECREF(key);
    return m;
}

PyObject *
PyImport_ImportModuleLevelObject(PyObject *name, PyObject *globals, PyObject *locals,
                                 PyObject *fromlist, int level)
{
    if (name == NULL || !PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_TypeError, "module name must be a string");
        return NULL;
    }
    if (level < 0) {
        PyErr_SetString(PyExc_ValueError, "level must be >= 0");
        return NULL;
    }
    if (PyUnicode_GET_LENGTH(name) == 0 && level == 0) {
        PyErr_SetString(PyExc_ValueError, "Empty module name");
        return NULL;
    }
    PyObject *empty = fromlist == NULL ? PyTuple_New(0) : NULL;
    PyObject *args[5] = { name, globals != NULL ? globals : Py_None, locals != NULL ? locals : Py_None,
                          fromlist != NULL ? fromlist : empty, PyLong_FromLong(level) };
    PyObject *result = NULL;
    if (args[3] != NULL && args[4] != NULL) {
        result = lu_call(PyInterpreterState_Get()->import_func, args, 5, NULL);
    }
    Py_XDECREF(empty);
    Py_XDECREF(args[4]);
    return result;
}

PyObject *
PyImport_Import(PyObject *name)
{
    PyObject *globals = PyDict_New(), *fromlist = PyList_New(0), *item = PyUnicode_FromString("__doc__");
    PyObject *result = NULL;
    if (globals != NULL && fromlist != NULL && item != NULL && PyList_Append(fromlist, item) == 0) {
        PyObject *mod = PyImport_ImportModuleLevelObject(name, globals, globals, fromlist, 0);
        if (mod != NULL) {
            Py_DECREF(mod);
            result = PyImport_GetModule(name);
            if (result == NULL && !PyErr_Occurred()) {
                PyErr_Format(PyExc_KeyError, "%U not in sys.modules as expected", name);
            }
        }
    }
    Py_XDECREF(globals);
    Py_XDECREF(fromlist);
    Py_XDECREF(item);
    return result;
}

PyObject *
PyImport_ImportModule(const char *name)
{
    PyObject *pname = PyUnicode_FromString(name);
    if (pname == NULL) {
        return NULL;
    }
    PyObject *result = PyImport_Import(pname);
    Py_DECREF(pname);
    return result;
}

PyObject *
PySys_GetObject(const char *name)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *v = PyDict_GetItemString(PyInterpreterState_Get()->sysdict, name);
    PyErr_SetRaisedException(exc);
    return v;
}

PyObject *
PyEval_GetBuiltins(void)
{
    return PyInterpreterState_Get()->builtins;
}
