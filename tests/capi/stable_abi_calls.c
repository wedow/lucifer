/* The stable ABI's Py_TYPE and Py_REFCNT are functions too (the limited
   API of 3.14 calls them): parenthesized, the names bypass the macros. */

#include "Python.h"

#include <stdio.h>
#include <unistd.h>

static struct PyModuleDef module = { PyModuleDef_HEAD_INIT, .m_name = "stable_abi_calls" };

PyMODINIT_FUNC
PyInit_stable_abi_calls(void)
{
    return PyModuleDef_Init(&module);
}

__attribute__((constructor)) static void
check(void)
{
    PyObject *type = (PyObject *)&PyLong_Type;
    int ok = (Py_TYPE)(type) == &PyType_Type && (Py_REFCNT)(type) > 0;
    if (ok) {
        puts("capi stable_abi_calls ok");
    }
    else {
        fprintf(stderr, "stable_abi_calls: Py_TYPE or Py_REFCNT wrong\n");
    }
    fflush(stdout);
    _exit(!ok);
}
