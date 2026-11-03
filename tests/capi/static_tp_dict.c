/* Static builtin types keep their dict in interpreter state, as in the
   oracle (FOUNDATIONS 2.1): after the kernel is up, PyLong_Type.tp_dict
   reads NULL while int.__dict__ maps names, and a class made through the
   C-API has its own tp_dict. Preloaded into bin/lucifer, the constructor
   initializes the runtime, checks, prints "capi static_tp_dict ok" and
   exits. */

#include "Python.h"

#include <stdio.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "static_tp_dict.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static PyObject *
lookup(PyObject *mapping, const char *name)
{
    PyObject *key = PyUnicode_FromString(name);
    PyObject *value = key != NULL ? PyObject_GetItem(mapping, key) : NULL;
    Py_XDECREF(key);
    return value;
}

static void
check_static(PyTypeObject *tp, const char *name)
{
    CHECK(tp->tp_dict == NULL);
    PyObject *dict = PyObject_GetAttrString((PyObject *)tp, "__dict__");
    CHECK(dict != NULL);
    if (dict == NULL) {
        PyErr_Clear();
        return;
    }
    CHECK(Py_TYPE(dict) == &PyDictProxy_Type);
    PyObject *value = lookup(dict, name);
    CHECK(value != NULL);
    Py_XDECREF(value);
    Py_DECREF(dict);
}

static void
check_heap(void)
{
    PyObject *bases = PyTuple_Pack(1, (PyObject *)&PyBaseObject_Type);
    PyObject *ns = PyDict_New();
    PyObject *name = PyUnicode_FromString("Heap");
    PyObject *cls = bases && ns && name
        ? PyObject_CallFunctionObjArgs((PyObject *)&PyType_Type, name, bases, ns, NULL) : NULL;
    CHECK(cls != NULL);
    if (cls != NULL) {
        CHECK(((PyTypeObject *)cls)->tp_dict != NULL);
        CHECK(PyDict_Check(((PyTypeObject *)cls)->tp_dict));
    }
    PyErr_Clear();
    Py_XDECREF(cls);
    Py_XDECREF(name);
    Py_XDECREF(ns);
    Py_XDECREF(bases);
}

static struct PyModuleDef static_tp_dict_module = {
    PyModuleDef_HEAD_INIT, .m_name = "static_tp_dict",
};

PyMODINIT_FUNC
PyInit_static_tp_dict(void)
{
    return PyModuleDef_Init(&static_tp_dict_module);
}

__attribute__((constructor)) static void
check(void)
{
    Py_InitializeEx(0);
    check_static(&PyLong_Type, "__doc__");
    check_static(&PyBaseObject_Type, "__init__");
    check_static(&PyType_Type, "mro");
    check_heap();
    if (failures == 0) {
        puts("capi static_tp_dict ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
