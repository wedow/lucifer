/* The number slots of int and bool are thunks into the kernel's special
   methods, which C callers reach without the interpreter's reflection.
   Preloaded into bin/lucifer, the constructor initializes the runtime,
   checks, prints "capi numeric_slots ok" and exits. */

#include "Python.h"

#include <stdio.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "numeric_slots.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static long
value(PyObject *o)
{
    long v = -1000000;
    if (o == NULL) {
        fprintf(stderr, "numeric_slots.c: an operation raised\n");
        PyErr_Clear();
        failures++;
        return v;
    }
    CHECK(PyLong_CheckExact(o) && _PyLong_IsCompact((PyLongObject *)o));
    v = (long)_PyLong_CompactValue((PyLongObject *)o);
    Py_DECREF(o);
    return v;
}

static void
check_int(void)
{
    PyNumberMethods *nb = PyLong_Type.tp_as_number;
    PyObject *two = PyLong_FromLong(2), *three = PyLong_FromLong(3), *ten = PyLong_FromLong(10);
    CHECK(nb != NULL && nb->nb_add != NULL);
    CHECK(value(nb->nb_add(two, three)) == 5);
    CHECK(value(nb->nb_subtract(two, three)) == -1);
    CHECK(value(nb->nb_multiply(three, ten)) == 30);
    CHECK(value(nb->nb_remainder(ten, three)) == 1);
    CHECK(value(nb->nb_floor_divide(ten, three)) == 3);
    CHECK(value(nb->nb_lshift(three, two)) == 12);
    CHECK(value(nb->nb_rshift(ten, two)) == 2);
    CHECK(value(nb->nb_and(ten, three)) == 2);
    CHECK(value(nb->nb_or(ten, three)) == 11);
    CHECK(value(nb->nb_xor(ten, three)) == 9);
    CHECK(value(nb->nb_negative(three)) == -3);
    CHECK(value(nb->nb_invert(three)) == -4);
    CHECK(value(nb->nb_absolute(nb->nb_negative(three))) == 3);
    CHECK(value(nb->nb_power(two, ten, Py_None)) == 1024);
    CHECK(value(nb->nb_power(two, ten, PyLong_FromLong(1000))) == 24);
    CHECK(value(nb->nb_index(three)) == 3);
    CHECK(value(nb->nb_int(three)) == 3);
    CHECK(nb->nb_bool(PyLong_FromLong(0)) == 0 && nb->nb_bool(three) == 1);
    PyObject *half = nb->nb_true_divide(PyLong_FromLong(7), two);
    CHECK(half != NULL && PyFloat_CheckExact(half) && PyFloat_AS_DOUBLE(half) == 3.5);
    PyObject *divmod = nb->nb_divmod(ten, three);
    CHECK(divmod != NULL && PyTuple_CheckExact(divmod) && PyTuple_GET_SIZE(divmod) == 2);
    /* A non-int operand answers NotImplemented rather than raising. */
    PyObject *r = nb->nb_add(two, Py_None);
    CHECK(r == Py_NotImplemented);
    Py_XDECREF(r);
    CHECK(PyLong_Type.tp_hash(PyLong_FromLong(-1)) == -2);
    CHECK(PyLong_Type.tp_richcompare(two, three, Py_LT) == Py_True);
    CHECK(PyLong_Type.tp_richcompare(two, three, Py_GE) == Py_False);
}

static void
check_bool(void)
{
    PyNumberMethods *nb = PyBool_Type.tp_as_number;
    /* bool's own logic operators keep bool for two bools and give int otherwise. */
    CHECK(nb->nb_and(Py_True, Py_True) == Py_True);
    CHECK(nb->nb_or(Py_False, Py_False) == Py_False);
    CHECK(nb->nb_xor(Py_True, Py_True) == Py_False);
    CHECK(value(nb->nb_and(Py_True, PyLong_FromLong(3))) == 1);
    /* bool inherits int's addition, and its result is an exact int. */
    CHECK(value(nb->nb_add(Py_True, Py_True)) == 2);
    /* The subclass's reflected method goes first: 3 & True is bool.__rand__. */
    CHECK(value(PyLong_Type.tp_as_number->nb_and(PyLong_FromLong(3), Py_True)) == 1);
    CHECK(nb->nb_bool(Py_True) == 1 && nb->nb_bool(Py_False) == 0);
}

__attribute__((constructor)) static void
check(void)
{
    Py_InitializeEx(0);
    check_int();
    check_bool();
    if (failures == 0) {
        puts("capi numeric_slots ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
