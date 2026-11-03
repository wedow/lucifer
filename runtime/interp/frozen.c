/* The frozen kernel loader. tools/freeze writes build/frozen.c: the
   lib/kernel modules as marshal blobs, in boot order. Each runs in its own
   namespace named after the module it implements, exports its __all__
   names into builtins, and is then _lucifer.NAME for the kernel modules
   after it. */

#include "interp/frozen.h"
#include "interp/code.h"

#include "marshal.h"

/* Marks a kernel code object and those nested in it, so that their frames
   are kernel-hidden and __build_class__ knows kernel class bodies. */
static void
mark_kernel(PyObject *code)
{
    PyCodeObject *co = (PyCodeObject *)code;
    co->co_flags |= LU_CO_KERNEL;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(co->co_consts); i++) {
        PyObject *c = PyTuple_GET_ITEM(co->co_consts, i);
        if (PyCode_Check(c)) {
            mark_kernel(c);
        }
    }
}

/* Marks the functions of a builtin module port, and those nested in them, so
   that their frames are hidden: the port stands for a C module. */
void
lu_hide_frames(PyObject *code)
{
    PyCodeObject *co = (PyCodeObject *)code;
    if (co->co_flags & CO_OPTIMIZED) {
        co->co_flags |= LU_CO_HIDDEN;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(co->co_consts); i++) {
        PyObject *c = PyTuple_GET_ITEM(co->co_consts, i);
        if (PyCode_Check(c)) {
            lu_hide_frames(c);
        }
    }
}

/* __all__ into builtins; functions become builtins of the builtins module,
   so that they look native (DESIGN 4.4). */
static int
export_all(PyObject *ns, PyObject *builtins_module)
{
    PyObject *names = lu_ns_lookup(ns, LU_ID(__all__));
    if (names == NULL) {
        return 0;
    }
    if (!PyTuple_CheckExact(names)) {
        lu_err_format(PyExc_SystemError, "a kernel module's __all__ must be a tuple");
        return -1;
    }
    PyObject *builtins = PyModule_GetDict(builtins_module);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(names); i++) {
        PyObject *name = PyTuple_GET_ITEM(names, i);
        PyObject *value = PyUnicode_CheckExact(name) ? lu_ns_lookup(ns, name) : NULL;
        if (value == NULL) {
            lu_err_format(PyExc_SystemError, "kernel __all__ names a missing global");
            return -1;
        }
        value = PyFunction_Check(value) ? lu_kernel_builtin(builtins_module, name, value)
                                        : Py_NewRef(value);
        int err = value == NULL || lu_ns_insert(builtins, name, value) < 0;
        Py_XDECREF(value);
        if (err) {
            return -1;
        }
    }
    return 0;
}

int
lu_kernel_boot(PyInterpreterState *interp)
{
    PyObject *builtins_module = lu_ns_lookup(interp->modules, LU_ID(builtins));
    PyObject *lucifer = lu_ns_lookup(interp->modules, LU_ID(_lucifer));
    for (const struct lu_frozen *m = lu_frozen_kernel; m->name != NULL; m++) {
        PyObject *code = PyMarshal_ReadObjectFromString((const char *)m->data, m->size);
        if (code == NULL) {
            return -1;
        }
        mark_kernel(code);
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
        int err = result == NULL || export_all(ns, builtins_module) < 0
            || lu_ns_insert(PyModule_GetDict(lucifer), ((PyModuleObject *)module)->md_name, module) < 0;
        Py_XDECREF(result);
        lu_static_types_inherit();
        Py_DECREF(module);
        if (err) {
            return -1;
        }
    }
    return 0;
}
