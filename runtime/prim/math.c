/* The libm and IEEE primitives under the math and _struct ports: each takes
   a float or int and answer a float, with no error checks
   (lib/modules/math.py applies CPython's math_1/math_2 rules to the result).
   float_pack and float_unpack are PyFloat_Pack2/4/8 and PyFloat_Unpack2/4/8
   of Objects/floatobject.c, little-endian. */

#include "prim/prim.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define UNARY(X) X(acos) X(acosh) X(asin) X(asinh) X(atan) X(atanh) X(cbrt) X(cos) X(cosh) \
    X(erf) X(erfc) X(exp) X(exp2) X(expm1) X(fabs) X(log) X(log10) X(log1p) X(log2) X(sin) \
    X(sinh) X(sqrt) X(tan) X(tanh)
#define BINARY(X) X(atan2) X(copysign) X(fmod) X(hypot) X(nextafter) X(pow) X(remainder)

static int
double_arg(PyObject *o, double *out)
{
    int r = lu_as_double(o, out);
    if (r > 0) {
        lu_err_format(PyExc_TypeError, "must be real number, not %.200s", Py_TYPE(o)->tp_name);
    }
    return r == 0 ? 0 : -1;
}

#define DEFINE_UNARY(name) \
    static PyObject *prim_m_##name(PyObject *m, PyObject *const *args, Py_ssize_t nargs) { \
        double x; \
        if (nargs != 1 || double_arg(args[0], &x) < 0) { return NULL; } \
        return PyFloat_FromDouble(name(x)); \
    }
UNARY(DEFINE_UNARY)

#define DEFINE_BINARY(name) \
    static PyObject *prim_m_##name(PyObject *m, PyObject *const *args, Py_ssize_t nargs) { \
        double x, y; \
        if (nargs != 2 || double_arg(args[0], &x) < 0 || double_arg(args[1], &y) < 0) { return NULL; } \
        return PyFloat_FromDouble(name(x, y)); \
    }
BINARY(DEFINE_BINARY)

/* to_double(x): a float or int as a float; None for another type, which the
   caller converts through __float__ or __index__. */
static PyObject *
prim_m_to_double(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x;
    int r = nargs == 1 ? lu_as_double(args[0], &x) : -1;
    if (r != 0) {
        return r > 0 ? Py_NewRef(Py_None) : NULL;
    }
    return PyFloat_FromDouble(x);
}

/* fma(x, y, z) */
static PyObject *
prim_m_fma(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x, y, z;
    if (nargs != 3 || double_arg(args[0], &x) < 0 || double_arg(args[1], &y) < 0
        || double_arg(args[2], &z) < 0) {
        return NULL;
    }
    return PyFloat_FromDouble(fma(x, y, z));
}

/* frexp(x): (mantissa, exponent) */
static PyObject *
prim_m_frexp(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x;
    int e = 0;
    if (nargs != 1 || double_arg(args[0], &x) < 0) {
        return NULL;
    }
    if (isfinite(x) && x != 0.0) {
        x = frexp(x, &e);
    }
    PyObject *f = PyFloat_FromDouble(x), *i = PyLong_FromLong(e);
    PyObject *t = f != NULL && i != NULL ? PyTuple_Pack(2, f, i) : NULL;
    Py_XDECREF(f);
    Py_XDECREF(i);
    return t;
}

/* modf(x): (fractional part, integral part) */
static PyObject *
prim_m_modf(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x, whole;
    if (nargs != 1 || double_arg(args[0], &x) < 0) {
        return NULL;
    }
    double frac = isinf(x) ? copysign(0.0, x) : modf(x, &whole);
    if (isinf(x)) {
        whole = x;
    }
    PyObject *f = PyFloat_FromDouble(frac), *w = PyFloat_FromDouble(whole);
    PyObject *t = f != NULL && w != NULL ? PyTuple_Pack(2, f, w) : NULL;
    Py_XDECREF(f);
    Py_XDECREF(w);
    return t;
}

/* ldexp(x, exponent): the exponent is a C int, already clamped by the caller. */
static PyObject *
prim_m_ldexp(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x;
    long e;
    if (nargs != 2 || double_arg(args[0], &x) < 0 || lu_long_as_long(args[1], &e) < 0) {
        return NULL;
    }
    return PyFloat_FromDouble(ldexp(x, (int)e));
}

/* PyFloat_Pack2 */
static int
pack2(double x, unsigned char out[2])
{
    unsigned char sign;
    int e;
    double f;
    unsigned short bits;

    if (x == 0.0) {
        sign = (copysign(1.0, x) == -1.0);
        e = 0;
        bits = 0;
    }
    else if (isinf(x)) {
        sign = (x < 0.0);
        e = 0x1f;
        bits = 0;
    }
    else if (isnan(x)) {
        sign = (copysign(1.0, x) == -1.0);
        e = 0x1f;
        uint64_t v;
        memcpy(&v, &x, sizeof(v));
        v &= 0xffc0000000000ULL;
        bits = (unsigned short)(v >> 42);
        if (!bits) {
            bits |= (1 << 9);
        }
    }
    else {
        sign = (x < 0.0);
        if (sign) {
            x = -x;
        }
        f = frexp(x, &e);
        f *= 2.0;
        e--;
        if (e >= 16) {
            return -1;
        }
        else if (e < -25) {
            f = 0.0;
            e = 0;
        }
        else if (e < -14) {
            f = ldexp(f, 14 + e);
            e = 0;
        }
        else {
            e += 15;
            f -= 1.0;
        }
        f *= 1024.0;
        bits = (unsigned short)f;
        if ((f - bits > 0.5) || ((f - bits == 0.5) && (bits % 2 == 1))) {
            ++bits;
            if (bits == 1024) {
                bits = 0;
                ++e;
                if (e == 31) {
                    return -1;
                }
            }
        }
    }
    bits |= (e << 10) | (sign << 15);
    out[0] = (unsigned char)(bits & 0xFF);
    out[1] = (unsigned char)((bits >> 8) & 0xFF);
    return 0;
}

/* PyFloat_Unpack2 */
static double
unpack2(const unsigned char *p)
{
    unsigned sign = (p[1] >> 7) & 1;
    int e = (p[1] & 0x7C) >> 2;
    unsigned f = ((p[1] & 0x03) << 8) | p[0];
    double x;

    if (e == 0x1f) {
        if (f == 0) {
            return sign ? -INFINITY : INFINITY;
        }
        uint64_t v = sign ? 0xfff0000000000000ULL : 0x7ff0000000000000ULL;
        v += (uint64_t)f << 42;
        memcpy(&x, &v, sizeof(v));
        return x;
    }
    x = (double)f / 1024.0;
    if (e == 0) {
        e = -14;
    }
    else {
        x += 1.0;
        e -= 15;
    }
    x = ldexp(x, e);
    return sign ? -x : x;
}

/* float_pack(x, size): the little-endian IEEE bytes of a 2, 4 or 8 byte float. */
static PyObject *
prim_float_pack(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x;
    long size;
    unsigned char out[8];
    if (nargs != 2 || double_arg(args[0], &x) < 0 || lu_long_as_long(args[1], &size) < 0) {
        return NULL;
    }
    if (size == 2) {
        if (pack2(x, out) < 0) {
            lu_err_format(PyExc_OverflowError, "float too large to pack with e format");
            return NULL;
        }
    }
    else if (size == 4) {
        float f = (float)x;
        if (isinf(f) && !isinf(x)) {
            lu_err_format(PyExc_OverflowError, "float too large to pack with f format");
            return NULL;
        }
        memcpy(out, &f, 4);
    }
    else {
        memcpy(out, &x, 8);
    }
    return PyBytes_FromStringAndSize((const char *)out, size);
}

/* float_unpack(data): the float of 2, 4 or 8 little-endian IEEE bytes. */
static PyObject *
prim_float_unpack(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1 || !PyBytes_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "float_unpack(bytes)");
        return NULL;
    }
    const unsigned char *p = (const unsigned char *)PyBytes_AS_STRING(args[0]);
    double x;
    switch (PyBytes_GET_SIZE(args[0])) {
    case 2:
        x = unpack2(p);
        break;
    case 4: {
        float f;
        memcpy(&f, p, 4);
        x = f;
        break;
    }
    default:
        memcpy(&x, p, 8);
        break;
    }
    return PyFloat_FromDouble(x);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
#define LISTED(name) FAST(m_##name),
PyMethodDef lu_prim_math_methods[] = {
    UNARY(LISTED) BINARY(LISTED)
    FAST(m_to_double), FAST(m_fma), FAST(m_frexp), FAST(m_modf), FAST(m_ldexp),
    FAST(float_pack), FAST(float_unpack),
    { NULL, NULL, 0, NULL },
};
