/* Conversions between int objects and C integers (Objects/longobject.c).
   Every one reads the int's magnitude into 64 bits once, then applies the
   range and sign rules of its C type. */

#include "lucifer.h"

#include <ctype.h>
#include <string.h>
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
    *overflow = 0;
    PyObject *v = as_int(op);
    if (v == NULL) {
        return -1;
    }
    unsigned long long m = 0;
    int neg, big = magnitude(v, &m, &neg);
    Py_DECREF(v);
    if (big || (!neg && m > (unsigned long long)max) || (neg && m > 0ULL - (unsigned long long)min)) {
        *overflow = neg ? -1 : 1;
        return -1;
    }
    *value = neg ? (long long)(0ULL - m) : (long long)m;
    return 0;
}

static long long
as_signed(PyObject *op, long long min, long long max, const char *too_large)
{
    long long value = -1;
    int overflow;
    if (signed_value(op, min, max, &value, &overflow) < 0 && overflow) {
        PyErr_SetString(PyExc_OverflowError, too_large);
        return -1;
    }
    return value;
}

long
PyLong_AsLong(PyObject *op)
{
    return (long)as_signed(op, LONG_MIN, LONG_MAX, "Python int too large to convert to C long");
}

long long
PyLong_AsLongLong(PyObject *op)
{
    return as_signed(op, LLONG_MIN, LLONG_MAX, "int too big to convert");
}

int
PyLong_AsInt(PyObject *op)
{
    return (int)as_signed(op, INT_MIN, INT_MAX, "Python int too large to convert to C int");
}

/* The fixed-width conversions return 0 and store through value, or -1 with an error. */
static int
fixed(PyObject *op, long long min, long long max, const char *too_large, long long *out)
{
    *out = as_signed(op, min, max, too_large);
    return *out == -1 && PyErr_Occurred() ? -1 : 0;
}

int
PyLong_AsInt32(PyObject *op, int32_t *value)
{
    long long v = 0;
    int r = fixed(op, INT32_MIN, INT32_MAX, "Python int too large to convert to C int32_t", &v);
    *value = (int32_t)v;
    return r;
}

int
PyLong_AsInt64(PyObject *op, int64_t *value)
{
    long long v = 0;
    int r = fixed(op, INT64_MIN, INT64_MAX, "Python int too large to convert to C int64_t", &v);
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

/* The unsigned value of an int (not of an __index__ object), exact in range;
   the errors name the C type as each CPython conversion does. */
static unsigned long long
as_unsigned(PyObject *op, unsigned long long max, PyObject *neg_type, const char *negative,
            const char *too_large)
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
        PyErr_SetString(neg_type, negative);
        return (unsigned long long)-1;
    }
    if (big || m > max) {
        PyErr_SetString(PyExc_OverflowError, too_large);
        return (unsigned long long)-1;
    }
    return m;
}

unsigned long
PyLong_AsUnsignedLong(PyObject *op)
{
    return (unsigned long)as_unsigned(op, ULONG_MAX, PyExc_OverflowError,
                                      "can't convert negative value to unsigned int",
                                      "Python int too large to convert to C unsigned long");
}

unsigned long long
PyLong_AsUnsignedLongLong(PyObject *op)
{
    return as_unsigned(op, ULLONG_MAX, PyExc_OverflowError, "can't convert negative int to unsigned",
                       "int too big to convert");
}

size_t
PyLong_AsSize_t(PyObject *op)
{
    return (size_t)as_unsigned(op, SIZE_MAX, PyExc_OverflowError, "can't convert negative value to size_t",
                               "Python int too large to convert to C size_t");
}

static int
fixed_unsigned(PyObject *op, unsigned long long max, const char *too_large, unsigned long long *out)
{
    PyObject *v = as_int(op);
    if (v == NULL) {
        return -1;
    }
    *out = as_unsigned(v, max, PyExc_ValueError, "Cannot convert negative int", too_large);
    Py_DECREF(v);
    return *out == (unsigned long long)-1 && PyErr_Occurred() ? -1 : 0;
}

int
PyLong_AsUInt32(PyObject *op, uint32_t *value)
{
    unsigned long long v = 0;
    int r = fixed_unsigned(op, UINT32_MAX, "Python int too large to convert to C uint32_t", &v);
    *value = (uint32_t)v;
    return r;
}

int
PyLong_AsUInt64(PyObject *op, uint64_t *value)
{
    unsigned long long v = 0;
    int r = fixed_unsigned(op, UINT64_MAX, "Python int too large to convert to C uint64_t", &v);
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
    long long v = as_signed(op, LLONG_MIN, LLONG_MAX, "int too big to convert");
    if (v == -1 && PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_OverflowError)) {
        PyErr_Clear();
        return (void *)(uintptr_t)PyLong_AsUnsignedLongLong(op);
    }
    return (void *)(intptr_t)v;
}

double
PyLong_AsDouble(PyObject *op)
{
    if (op == NULL) {
        PyErr_BadInternalCall();
        return -1.0;
    }
    if (!PyLong_Check(op)) {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
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

/* int(text, base) over a str; the error is int()'s. */
PyObject *
PyLong_FromUnicodeObject(PyObject *u, int base)
{
    PyObject *b = PyLong_FromLong(base);
    PyObject *args[2] = { u, b };
    PyObject *r = b != NULL ? lu_call((PyObject *)&PyLong_Type, args, 2, NULL) : NULL;
    Py_XDECREF(b);
    return r;
}

/* PyLong_FromString (Objects/longobject.c): optional whitespace, sign and
   base prefix, digits with single underscores between them, then only
   whitespace to the end of the string. *pend is the end, or where the syntax
   went wrong. */
PyObject *
PyLong_FromString(const char *str, char **pend, int base)
{
    if ((base != 0 && base < 2) || base > 36) {
        PyErr_SetString(PyExc_ValueError, "int() arg 2 must be >= 2 and <= 36");
        return NULL;
    }
    const char *p = str, *sign;
    int error_if_nonzero = 0;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    sign = p;
    if (*p == '+' || *p == '-') {
        p++;
    }
    if (base == 0) {
        base = p[0] != '0' ? 10 : (p[1] | 0x20) == 'x' ? 16 : (p[1] | 0x20) == 'o' ? 8
             : (p[1] | 0x20) == 'b' ? 2 : (error_if_nonzero = 1, 10);
    }
    if (p[0] == '0' && ((base == 16 && (p[1] | 0x20) == 'x') || (base == 8 && (p[1] | 0x20) == 'o')
                        || (base == 2 && (p[1] | 0x20) == 'b'))) {
        p += 2;
        if (*p == '_') {
            p++;
        }
    }
    const char *digits = p, *prev = NULL;
    for (; *p; p++) {
        int c = (unsigned char)*p;
        int d = isdigit(c) ? c - '0' : isalpha(c) ? (c | 0x20) - 'a' + 10 : 99;
        if (*p == '_' ? (prev != NULL && *prev == '_') || p == digits : d >= base) {
            break;
        }
        prev = p;
    }
    const char *end = p;
    int valid = end != digits && *prev != '_', ok;
    while (valid && isspace((unsigned char)*p)) {
        p++;
    }
    ok = valid && *p == '\0';
    PyObject *r = NULL;
    if (ok) {
        PyObject *text = PyUnicode_FromStringAndSize(digits, end - digits);
        PyObject *value = text != NULL ? PyLong_FromUnicodeObject(text, base) : NULL;
        Py_XDECREF(text);
        r = value != NULL && *sign == '-' ? PyNumber_Negative(value) : value;
        if (r != value) {
            Py_DECREF(value);
        }
        if (r != NULL && error_if_nonzero && !(_PyLong_IsCompact((PyLongObject *)r)
                                               && _PyLong_CompactValue((PyLongObject *)r) == 0)) {
            Py_CLEAR(r);
            ok = 0;
            base = 0;
        }
        else if (r == NULL) {
            return NULL;
        }
    }
    if (pend != NULL) {
        *pend = (char *)(valid ? p : end);
    }
    if (!ok) {
        Py_XDECREF(r);
        PyObject *orig = PyUnicode_FromStringAndSize(str, strlen(str) < 200 ? (Py_ssize_t)strlen(str) : 200);
        if (orig != NULL) {
            PyErr_Format(PyExc_ValueError, "invalid literal for int() with base %d: %.200R", base, orig);
            Py_DECREF(orig);
        }
        return NULL;
    }
    return r;
}

int
PyLong_IsZero(PyObject *obj)
{
    if (!PyLong_Check(obj)) {
        PyErr_Format(PyExc_TypeError, "expected int, got %s", Py_TYPE(obj)->tp_name);
        return -1;
    }
    return _PyLong_IsCompact((PyLongObject *)obj) && _PyLong_CompactValue((PyLongObject *)obj) == 0;
}
