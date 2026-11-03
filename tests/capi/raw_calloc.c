/* PyMem_RawCalloc with a zero count or size allocates one byte, as
   _PyMem_RawCalloc does, whatever the other argument. */

#include "Python.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static struct PyModuleDef module = { PyModuleDef_HEAD_INIT, .m_name = "raw_calloc" };

PyMODINIT_FUNC
PyInit_raw_calloc(void)
{
    return PyModuleDef_Init(&module);
}

__attribute__((constructor)) static void
check(void)
{
    size_t huge = (size_t)PY_SSIZE_T_MAX / 2;
    void *a = PyMem_RawCalloc(0, huge);
    void *b = PyMem_RawCalloc(huge, 0);
    int ok = a != NULL && b != NULL;
    PyMem_RawFree(a);
    PyMem_RawFree(b);
    if (ok) {
        puts("capi raw_calloc ok");
    }
    else {
        fprintf(stderr, "raw_calloc: %p %p\n", a, b);
    }
    fflush(stdout);
    _exit(!ok);
}
