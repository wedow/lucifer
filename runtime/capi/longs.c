/* Conversions between int objects and C integers (Objects/longobject.c).
   Every one reads the int's magnitude into 64 bits once, then applies the
   range and sign rules of its C type. */

#include "lucifer.h"

#include <math.h>

extern PyObject *_PyNumber_Index(PyObject *item);

/* The int's magnitude when it fits 64 bits; 0 with *neg set otherwise OK, -1 when too large. */
static int
magnitude(PyObject *v, unsigned long long *mag, int *neg)
{
    PyLongObject *a = (PyLongObject *)v;
    uintptr_t tag = a->long_value.lv_tag;
    Py_ssize_t n = (Py_ssize_t)(tag >> _PyLong_NON_SIZE_BITS);
    *neg = (tag & _PyLong_SIGN_MASK) == 2;
    unsigned long long m = 0;
    for (Py_ssize_t i = n; --i >= 0;) {
        if (m > (~0ULL >> PyLong_SHIFT)) {
            return -1;
        }
        m = (m << PyLong_SHIFT) | a->long_value.ob_digit[i];
    }
    *mag = m;
    return 0;
}

/* An int for any object with __index__: a new reference, or NULL. */
static PyObject *
as_int(PyObject *op)
{
    if (op == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return _PyNumber_Index(op);
}

/* The signed value of op within [min, max]: 0 and *value, or -1 with *overflow
   set to the direction when it is out of range (and no error), or -1 with an error. */
static int
signed_value(PyObject *op, long long min, long long max, long long *value, int *overflow)
{
    PyObject *v = as_int(op);
    if (v == NULL) {
        return -1;
    }
    unsigned long long m = 0;
    int neg, big = magnitude(v, &m, &neg);
    Py_DECREF(v);
    *overflow = 0;
    if (big || (!neg && m > (unsigned long long)max) || (neg && m > 0ULL - (unsigned long long)min)) {
        *overflow = neg ? -1 : 1;
        return -1;
    }
    *value = neg ? (long long)(0ULL - m) : (long long)m;
    return 0;
}

static long long
as_signed(PyObject *op, long long min, long long max, const char *ctype)
{
    long long value = -1;
    int overflow;
    if (signed_value(op, min, max, &value, &overflow) < 0 && overflow) {
        PyErr_Format(PyExc_OverflowError, "Python int too large to convert to C %s", ctype);
        return -1;
    }
    return value;
}

long
PyLong_AsLong(PyObject *op)
{
    return (long)as_signed(op, LONG_MIN, LONG_MAX, "long");
}

long long
PyLong_AsLongLong(PyObject *op)
{
    return as_signed(op, LLONG_MIN, LLONG_MAX, "long long");
}

int
PyLong_AsInt(PyObject *op)
{
    return (int)as_signed(op, INT_MIN, INT_MAX, "int");
}

/* The fixed-width conversions return 0 and store through value, or -1 with an error. */
static int
fixed(PyObject *op, long long min, long long max, const char *ctype, long long *out)
{
    *out = as_signed(op, min, max, ctype);
    return *out == -1 && PyErr_Occurred() ? -1 : 0;
}

int
PyLong_AsInt32(PyObject *op, int32_t *value)
{
    long long v;
    int r = fixed(op, INT32_MIN, INT32_MAX, "int32_t", &v);
    *value = (int32_t)v;
    return r;
}

int
PyLong_AsInt64(PyObject *op, int64_t *value)
{
    long long v;
    int r = fixed(op, INT64_MIN, INT64_MAX, "int64_t", &v);
    *value = (int64_t)v;
    return r;
}

static long long
as_signed_overflow(PyObject *op, int *overflow)
{
    long long value = -1;
    signed_value(op, LLONG_MIN, LLONG_MAX, &value, overflow);
    if (*overflow) {
        return -1;
    }
    return value;
}

long
PyLong_AsLongAndOverflow(PyObject *op, int *overflow)
{
    return (long)as_signed_overflow(op, overflow);
}

long long
PyLong_AsLongLongAndOverflow(PyObject *op, int *overflow)
{
    return as_signed_overflow(op, overflow);
}

/* The unsigned value of an int (not of an __index__ object), exact in range. */
static unsigned long long
as_unsigned(PyObject *op, unsigned long long max, const char *ctype)
{
    if (op == NULL || !PyLong_Check(op)) {
        if (op != NULL) {
            PyErr_SetString(PyExc_TypeError, "an integer is required");
        }
        else {
            PyErr_BadInternalCall();
        }
        return (unsigned long long)-1;
    }
    unsigned long long m = 0;
    int neg, big = magnitude(op, &m, &neg);
    if (neg && m != 0) {
        PyErr_SetString(PyExc_OverflowError, "can't convert negative value to unsigned int");
        return (unsigned long long)-1;
    }
    if (big || m > max) {
        PyErr_Format(PyExc_OverflowError, "Python int too large to convert to C %s", ctype);
        return (unsigned long long)-1;
    }
    return m;
}

unsigned long
PyLong_AsUnsignedLong(PyObject *op)
{
    return (unsigned long)as_unsigned(op, ULONG_MAX, "unsigned long");
}

unsigned long long
PyLong_AsUnsignedLongLong(PyObject *op)
{
    return as_unsigned(op, ULLONG_MAX, "unsigned long long");
}

size_t
PyLong_AsSize_t(PyObject *op)
{
    return (size_t)as_unsigned(op, SIZE_MAX, "size_t");
}

static int
fixed_unsigned(PyObject *op, unsigned long long max, const char *ctype, unsigned long long *out)
{
    PyObject *v = as_int(op);
    if (v == NULL) {
        return -1;
    }
    *out = as_unsigned(v, max, ctype);
    Py_DECREF(v);
    return *out == (unsigned long long)-1 && PyErr_Occurred() ? -1 : 0;
}

int
PyLong_AsUInt32(PyObject *op, uint32_t *value)
{
    unsigned long long v;
    int r = fixed_unsigned(op, UINT32_MAX, "uint32_t", &v);
    *value = (uint32_t)v;
    return r;
}

int
PyLong_AsUInt64(PyObject *op, uint64_t *value)
{
    unsigned long long v;
    int r = fixed_unsigned(op, UINT64_MAX, "uint64_t", &v);
    *value = (uint64_t)v;
    return r;
}

/* The value modulo 2**64, of an int or an object with __index__. */
static unsigned long long
as_mask(PyObject *op)
{
    PyObject *v = as_int(op);
    if (v == NULL) {
        return (unsigned long long)-1;
    }
    PyLongObject *a = (PyLongObject *)v;
    uintptr_t tag = a->long_value.lv_tag;
    Py_ssize_t n = (Py_ssize_t)(tag >> _PyLong_NON_SIZE_BITS);
    unsigned long long m = 0;
    for (Py_ssize_t i = n; --i >= 0;) {
        m = (m << PyLong_SHIFT) | a->long_value.ob_digit[i];
    }
    if ((tag & _PyLong_SIGN_MASK) == 2) {
        m = 0ULL - m;
    }
    Py_DECREF(v);
    return m;
}

unsigned long
PyLong_AsUnsignedLongMask(PyObject *op)
{
    return (unsigned long)as_mask(op);
}

unsigned long long
PyLong_AsUnsignedLongLongMask(PyObject *op)
{
    return as_mask(op);
}

void *
PyLong_AsVoidPtr(PyObject *op)
{
    long long v = as_signed(op, LLONG_MIN, LLONG_MAX, "long long");
    if (v == -1 && PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_OverflowError)) {
        PyErr_Clear();
        return (void *)(uintptr_t)PyLong_AsUnsignedLongLong(op);
    }
    return (void *)(intptr_t)v;
}

double
PyLong_AsDouble(PyObject *op)
{
    if (op == NULL || !PyLong_Check(op)) {
        PyErr_BadInternalCall();
        return -1.0;
    }
    return lu_long_as_double(op);
}

/* The other direction. */

static PyObject *
from_unsigned(unsigned long long value)
{
    digit digits[3];
    Py_ssize_t n = 0;
    for (unsigned long long v = value; v != 0; v >>= PyLong_SHIFT) {
        digits[n++] = (digit)(v & PyLong_MASK);
    }
    return lu_long_from_digits(0, n, digits);
}

PyObject *
PyLong_FromUnsignedLong(unsigned long value)
{
    return from_unsigned(value);
}

PyObject *
PyLong_FromUnsignedLongLong(unsigned long long value)
{
    return from_unsigned(value);
}

PyObject *
PyLong_FromSize_t(size_t value)
{
    return from_unsigned(value);
}

PyObject *
PyLong_FromVoidPtr(void *p)
{
    return from_unsigned((uintptr_t)p);
}

PyObject *
PyLong_FromInt32(int32_t value)
{
    return PyLong_FromLongLong(value);
}

PyObject *
PyLong_FromInt64(int64_t value)
{
    return PyLong_FromLongLong(value);
}

PyObject *
PyLong_FromUInt32(uint32_t value)
{
    return from_unsigned(value);
}

PyObject *
PyLong_FromUInt64(uint64_t value)
{
    return from_unsigned(value);
}

PyObject *
PyLong_FromDouble(double value)
{
    return lu_long_from_double(value);
}
