/* The frozen kernel loader. tools/freeze writes build/frozen.c: the
   lib/kernel modules as marshal blobs, in boot order. Each runs in its own
   namespace named after the module it implements, exports its __all__
   names into builtins, and is then _lucifer.NAME for the kernel modules
   after it. */

#include "interp/frozen.h"
#include "interp/exceptions.h"
#include "interp/code.h"
#include "interp/frame.h"

#include "marshal.h"

/* Marks a kernel code object and those nested in it, so that their frames
   are kernel-hidden and __build_class__ knows kernel class bodies. The
   globals a kernel function rebinds go into `rebound`: the JIT folds every
   other kernel global to its value. */
static int
mark_kernel(PyObject *code, PyObject *rebound)
{
    PyCodeObject *co = (PyCodeObject *)code;
    co->co_flags |= LU_CO_KERNEL;
    lu_codeunit *start = lu_code_start(co);
    int op = 0, arg = 0;
    for (Py_ssize_t i = 0; (co->co_flags & CO_OPTIMIZED) && i < Py_SIZE(co); i += 1 + lu_opcode_caches[op]) {
        op = start[i].op.code;
        arg = arg << 8 | start[i].op.arg;
        if ((op == STORE_GLOBAL || op == DELETE_GLOBAL)
            && lu_ns_insert(rebound, PyTuple_GET_ITEM(co->co_names, arg), Py_None) < 0) {
            return -1;
        }
        arg = op == EXTENDED_ARG ? arg : 0;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(co->co_consts); i++) {
        PyObject *c = PyTuple_GET_ITEM(co->co_consts, i);
        if (PyCode_Check(c) && mark_kernel(c, rebound) < 0) {
            return -1;
        }
    }
    return 0;
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
    interp->kernel_rebound = PyDict_New();
    if (interp->kernel_rebound == NULL) {
        return -1;
    }
    for (const struct lu_frozen *m = lu_frozen_kernel; m->name != NULL; m++) {
        PyObject *code = PyMarshal_ReadObjectFromString((const char *)m->data, m->size);
        if (code == NULL) {
            return -1;
        }
        if (mark_kernel(code, interp->kernel_rebound) < 0) {
            Py_DECREF(code);
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
        int err = result == NULL || export_all(ns, builtins_module) < 0
            || lu_ns_insert(PyModule_GetDict(lucifer), ((PyModuleObject *)module)->md_name, module) < 0;
        Py_XDECREF(result);
        err = lu_static_types_inherit() < 0 || err;
        Py_DECREF(module);
        if (err) {
            return -1;
        }
    }
    /* The kernel fills static types' dicts without telling anyone; from here
       on a change to one is a PyType_Modified. */
    interp->specializing = 1;
    interp->kernel_booted = 1;          /* every kernel function is registered */
    lu_exc_vectorcalls();
    return 0;
}
