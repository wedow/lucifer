/* A cell written as extensions built against CPython's headers write it:
   PyCell_SET, a store of the cell's word in line, which no runtime code
   sees. */
#include "Python.h"

static PyObject *
set(PyObject *m, PyObject *const *args, Py_ssize_t n)
{
    if (n != 2 || !PyCell_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "set(cell, value)");
        return NULL;
    }
    PyObject *old = PyCell_GET(args[0]);
    PyCell_SET(args[0], Py_NewRef(args[1]));
    Py_XDECREF(old);
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    { "set", (PyCFunction)(void (*)(void))set, METH_FASTCALL, NULL },
    { NULL, NULL, 0, NULL },
};

static PyModuleDef_Slot slots[] = {
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

static struct PyModuleDef def = { PyModuleDef_HEAD_INIT, "cell_set", NULL, 0, methods, slots };

PyMODINIT_FUNC
PyInit_cell_set(void)
{
    return PyModuleDef_Init(&def);
}
