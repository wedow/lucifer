/* A class made from a spec with a managed dict and a tp_finalize the spec
   writes straight into the type: every instance released must be
   finalized, however it is released. */
#include "Python.h"

static long finalized;

static void
t_finalize(PyObject *self)
{
    finalized++;
}

static int
t_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));
    return PyObject_VisitManagedDict(self, visit, arg);
}

static int
t_clear(PyObject *self)
{
    PyObject_ClearManagedDict(self);
    return 0;
}

static PyType_Slot t_slots[] = {
    { Py_tp_finalize, (void *)t_finalize },
    { Py_tp_traverse, (void *)t_traverse },
    { Py_tp_clear, (void *)t_clear },
    { 0, NULL },
};

static PyType_Spec t_spec = {
    "spec_finalize.T", sizeof(PyObject), 0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_MANAGED_DICT, t_slots,
};

static PyObject *
count(PyObject *m, PyObject *unused)
{
    return PyLong_FromLong(finalized);
}

static PyMethodDef methods[] = {
    { "count", count, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL },
};

static int
exec_module(PyObject *m)
{
    PyObject *t = PyType_FromSpec(&t_spec);
    return t == NULL ? -1 : PyModule_Add(m, "T", t);
}

static PyModuleDef_Slot slots[] = {
    { Py_mod_exec, exec_module },
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

static struct PyModuleDef def = { PyModuleDef_HEAD_INIT, "spec_finalize", NULL, 0, methods, slots };

PyMODINIT_FUNC
PyInit_spec_finalize(void)
{
    return PyModuleDef_Init(&def);
}
