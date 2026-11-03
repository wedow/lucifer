/* The frozen kernel loader. tools/freeze writes build/frozen.c: the
   lib/kernel modules as marshal blobs, in boot order. Each runs in its own
   namespace named after the module it implements, and exports its __all__
   names into builtins. */

#include "interp/frozen.h"

#include "marshal.h"

static int
export_all(PyObject *ns, PyObject *builtins)
{
    PyObject *names = lu_ns_lookup(ns, LU_ID(__all__));
    if (names == NULL) {
        return 0;
    }
    if (!PyTuple_CheckExact(names)) {
        lu_err_format(PyExc_SystemError, "a kernel module's __all__ must be a tuple");
        return -1;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(names); i++) {
        PyObject *name = PyTuple_GET_ITEM(names, i);
        PyObject *value = PyUnicode_CheckExact(name) ? lu_ns_lookup(ns, name) : NULL;
        if (value == NULL) {
            lu_err_format(PyExc_SystemError, "kernel __all__ names a missing global");
            return -1;
        }
        if (lu_ns_insert(builtins, name, value) < 0) {
            return -1;
        }
    }
    return 0;
}

int
lu_kernel_boot(PyInterpreterState *interp)
{
    PyObject *builtins_module = lu_ns_lookup(interp->modules, LU_ID(builtins));
    for (const struct lu_frozen *m = lu_frozen_kernel; m->name != NULL; m++) {
        PyObject *code = PyMarshal_ReadObjectFromString((const char *)m->data, m->size);
        if (code == NULL) {
            return -1;
        }
        PyObject *module = lu_module_new(m->name);
        if (module == NULL) {
            Py_DECREF(code);
            return -1;
        }
        PyObject *ns = PyModule_GetDict(module);
        PyObject *result = NULL;
        if (lu_ns_insert(ns, LU_ID(__builtins__), builtins_module) == 0) {
            result = PyEval_EvalCode(code, ns, ns);
        }
        Py_DECREF(code);
        int err = result == NULL || export_all(ns, interp->builtins) < 0;
        Py_XDECREF(result);
        Py_DECREF(module);
        if (err) {
            return -1;
        }
    }
    return 0;
}
