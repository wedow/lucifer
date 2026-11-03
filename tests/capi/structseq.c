/* PyStructSequence from C: a type with a hidden field, filled with SetItem,
   read as a tuple and by attribute. Preloaded into bin/lucifer. */

#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static PyStructSequence_Field fields[] = {
    { "left", "the first" }, { "right", "the second" }, { "extra", "hidden" }, { NULL, NULL },
};
static PyStructSequence_Desc desc = { "capi.pair", "pair\n\nA pair with a hidden extra.", fields, 2 };

__attribute__((constructor)) static void
run(void)
{
    Py_InitializeEx(0);
    PyTypeObject *type = PyStructSequence_NewType(&desc);
    CHECK(type != NULL);
    PyObject *pair = PyStructSequence_New(type);
    CHECK(pair != NULL);
    PyStructSequence_SetItem(pair, 0, PyLong_FromLong(7));
    PyStructSequence_SetItem(pair, 1, PyLong_FromLong(8));
    PyStructSequence_SetItem(pair, 2, PyLong_FromLong(9));
    CHECK(PyTuple_Check(pair) && PyTuple_GET_SIZE(pair) == 2);
    CHECK(PyStructSequence_GetItem(pair, 1) == PyLong_FromLong(8));      /* small ints are shared */
    PyObject *extra = PyObject_GetAttrString(pair, "extra");
    PyObject *left = PyObject_GetAttrString(pair, "left");
    CHECK(extra == PyLong_FromLong(9));
    CHECK(left == PyLong_FromLong(7));
    printf("capi structseq %s\n", failures ? "FAILED" : "ok");
    fflush(stdout);
    _exit(failures != 0);
}
