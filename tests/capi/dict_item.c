/* PyDict_SetItem and PyDict_DelItem act on the dict itself: a dict subclass's
   __setitem__ and __delitem__ run for d[k] = v and del d[k] (the item
   protocol, PyObject_SetItem) but not for the dict API, as in CPython. The
   overrides are C functions that count their calls. Preloaded into bin/lucifer, the
   constructor initializes the runtime, checks, prints "capi dict_item ok"
   and exits. */

#include "Python.h"

#include <stdio.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "dict_item.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static int override_calls;

static PyObject *
override(PyObject *self, PyObject *args)
{
    override_calls++;
    Py_RETURN_NONE;
}

static PyMethodDef override_def = { "override", override, METH_VARARGS, NULL };

/* len(obj) == n, through the object's own method. */
static int
len_is(PyObject *obj, long n)
{
    PyObject *len = PyObject_CallNoArgs(PyObject_GetAttrString(obj, "__len__"));
    PyObject *same = PyObject_CallOneArg(PyObject_GetAttrString(len, "__eq__"), PyLong_FromLong(n));
    return same == Py_True;
}

static void
check(void)
{
    PyObject *hook = PyCFunction_NewEx(&override_def, NULL, NULL);
    PyObject *ns = PyDict_New();
    PyObject *zero = PyLong_FromLong(0);
    PyObject *one = PyLong_FromLong(1);
    PyObject *bases = PyTuple_Pack(1, (PyObject *)&PyDict_Type);
    PyObject *name = PyUnicode_FromString("Recording");
    CHECK(PyDict_SetItem(ns, PyUnicode_FromString("__setitem__"),
                         hook) == 0);
    CHECK(PyDict_SetItem(ns, PyUnicode_FromString("__delitem__"),
                         hook) == 0);
    PyObject *cls = PyObject_CallFunctionObjArgs((PyObject *)&PyType_Type, name, bases, ns, NULL);
    CHECK(cls != NULL);
    PyObject *d = PyObject_CallNoArgs(cls);
    CHECK(d != NULL);

    /* The dict API stores and deletes without calling the overrides. */
    CHECK(PyDict_SetItem(d, one, one) == 0);
    CHECK(len_is(d, 1) && override_calls == 0);
    CHECK(PyDict_DelItem(d, one) == 0);
    CHECK(len_is(d, 0) && override_calls == 0);
    CHECK(PyDict_DelItem(d, one) == -1 && PyErr_ExceptionMatches(PyExc_KeyError));
    PyErr_Clear();
    CHECK(PyDict_SetItem(d, one, one) == 0);
    PyDict_Clear(d);
    CHECK(len_is(d, 0) && override_calls == 0);

    /* The item protocol, which d[k] = v and del d[k] use, calls them. */
    CHECK(PyObject_SetItem(d, zero, one) == 0);
    CHECK(len_is(d, 0) && override_calls == 1);
    CHECK(PyObject_DelItem(d, one) == 0);
    CHECK(override_calls == 2);

    if (failures == 0) {
        puts("capi dict_item ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}

static struct PyModuleDef dict_item_module = {
    PyModuleDef_HEAD_INIT, .m_name = "dict_item",
};

PyMODINIT_FUNC
PyInit_dict_item(void)
{
    return PyModuleDef_Init(&dict_item_module);
}

__attribute__((constructor)) static void
run(void)
{
    Py_InitializeEx(0);
    check();
}
