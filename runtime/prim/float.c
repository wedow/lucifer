/* float: the IEEE double kernels under the kernel's float (DESIGN 4.4,
   scalars), from CPython's Objects/floatobject.c. Arithmetic and comparison
   take a float or an int on either side and answer NotImplemented for any
   other operand, so reflection and the error messages live in
   lib/kernel/float.py and operators.py. Repr and formatting are
   PyOS_double_to_string over dtoa.c; parsing is PyOS_string_to_double over
   fast_float.c. */

#include "prim/prim.h"
#include "prim/bytes.h"
#include "opcode.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>

extern double _Py_parse_inf_or_nan(const char *p, char **endptr);
extern double _Py_dg_strtod(const char *str, char **ptr);
extern char *_Py_dg_dtoa(double d, int mode, int ndigits, int *decpt, int *sign, char **rve);
extern void _Py_dg_freedtoa(char *s);

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__, (n)); return NULL; } } while (0)
#define FLOAT(i) do { if (!PyFloat_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() needs a float", __func__); return NULL; } } while (0)

static PyObject *
bool_of(int v)
{
    return v ? Py_True : Py_False;
}

/* 0 for a float or int (as a double), 1 for another type, -1 with an error
   (an int too large for a double). */
double
PyFloat_AsDouble(PyObject *op)
{
    if (op == NULL) {
        PyErr_BadArgument();
        return -1;
    }
    if (PyFloat_Check(op)) {
        return PyFloat_AS_DOUBLE(op);
    }
    PyNumberMethods *nb = Py_TYPE(op)->tp_as_number;
    if (nb == NULL || nb->nb_float == NULL) {
        if (nb != NULL && nb->nb_index != NULL) {
            PyObject *index = PyNumber_Index(op);
            double value = index != NULL ? lu_long_as_double(index) : -1.0;
            Py_XDECREF(index);
            return value;
        }
        PyErr_Format(PyExc_TypeError, "must be real number, not %.50s", Py_TYPE(op)->tp_name);
        return -1;
    }
    PyObject *res = nb->nb_float(op);
    if (res == NULL) {
        return -1;
    }
    if (!PyFloat_CheckExact(res)) {
        if (!PyFloat_Check(res)) {
            PyErr_Format(PyExc_TypeError, "%.50s.__float__ returned non-float (type %.50s)",
                         Py_TYPE(op)->tp_name, Py_TYPE(res)->tp_name);
            Py_DECREF(res);
            return -1;
        }
        if (PyErr_WarnFormat(PyExc_DeprecationWarning, 1,
                "%.50s.__float__ returned non-float (type %.50s).  "
                "The ability to return an instance of a strict subclass of float "
                "is deprecated, and may be removed in a future version of Python.",
                Py_TYPE(op)->tp_name, Py_TYPE(res)->tp_name)) {
            Py_DECREF(res);
            return -1;
        }
    }
    double value = PyFloat_AS_DOUBLE(res);
    Py_DECREF(res);
    return value;
}

int
lu_as_double(PyObject *o, double *d)
{
    if (PyFloat_Check(o)) {
        *d = PyFloat_AS_DOUBLE(o);
        return 0;
    }
    if (PyLong_Check(o)) {
        *d = lu_long_as_double(o);
        return (*d == -1.0 && PyErr_Occurred()) ? -1 : 0;
    }
    return 1;
}

/* CONVERT_TO_DOUBLE: the operand as a double, or return NotImplemented or the error. */
#define CONVERT(i, var) do { \
        int r_ = lu_as_double(args[i], &(var)); \
        if (r_ > 0) { return Py_NewRef(Py_NotImplemented); } \
        if (r_ < 0) { return NULL; } \
    } while (0)

/* PyErr_SetFromErrno for the ERANGE and EDOM of libm: OverflowError or
   ValueError with (errno, strerror). */
static void
errno_error(void)
{
    PyObject *code = PyLong_FromLong(errno);
    PyObject *text = PyUnicode_FromString(strerror(errno));
    PyObject *args = (code != NULL && text != NULL) ? PyTuple_Pack(2, code, text) : NULL;
    if (args != NULL) {
        PyErr_SetObject(errno == ERANGE ? PyExc_OverflowError : PyExc_ValueError, args);
    }
    Py_XDECREF(code);
    Py_XDECREF(text);
    Py_XDECREF(args);
}

static PyObject *
prim_float_add(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double a, b;
    ARGS(2); CONVERT(0, a); CONVERT(1, b);
    return PyFloat_FromDouble(a + b);
}

static PyObject *
prim_float_sub(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double a, b;
    ARGS(2); CONVERT(0, a); CONVERT(1, b);
    return PyFloat_FromDouble(a - b);
}

static PyObject *
prim_float_mul(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double a, b;
    ARGS(2); CONVERT(0, a); CONVERT(1, b);
    return PyFloat_FromDouble(a * b);
}

static PyObject *
prim_float_truediv(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double a, b;
    ARGS(2); CONVERT(0, a); CONVERT(1, b);
    if (b == 0.0) {
        lu_err_format(PyExc_ZeroDivisionError, "division by zero");
        return NULL;
    }
    return PyFloat_FromDouble(a / b);
}

static PyObject *
prim_float_mod(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double vx, wx, mod;
    ARGS(2); CONVERT(0, vx); CONVERT(1, wx);
    if (wx == 0.0) {
        lu_err_format(PyExc_ZeroDivisionError, "division by zero");
        return NULL;
    }
    mod = fmod(vx, wx);
    if (mod) {
        /* the remainder has the sign of the denominator */
        if ((wx < 0) != (mod < 0)) {
            mod += wx;
        }
    }
    else {
        mod = copysign(0.0, wx);
    }
    return PyFloat_FromDouble(mod);
}

static void
float_div_mod(double vx, double wx, double *floordiv, double *mod)
{
    double div;
    *mod = fmod(vx, wx);
    /* fmod is typically exact, so vx-mod is mathematically an exact multiple
       of wx; the floating-point division may still be a little off. */
    div = (vx - *mod) / wx;
    if (*mod) {
        if ((wx < 0) != (*mod < 0)) {
            *mod += wx;
            div -= 1.0;
        }
    }
    else {
        *mod = copysign(0.0, wx);
    }
    /* snap quotient to nearest integral value */
    if (div) {
        *floordiv = floor(div);
        if (div - *floordiv > 0.5) {
            *floordiv += 1.0;
        }
    }
    else {
        /* zero with the sign of the true quotient */
        *floordiv = copysign(0.0, vx / wx);
    }
}

static PyObject *
prim_float_divmod(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double vx, wx, mod, floordiv;
    ARGS(2); CONVERT(0, vx); CONVERT(1, wx);
    if (wx == 0.0) {
        lu_err_format(PyExc_ZeroDivisionError, "division by zero");
        return NULL;
    }
    float_div_mod(vx, wx, &floordiv, &mod);
    PyObject *q = PyFloat_FromDouble(floordiv);
    PyObject *r = PyFloat_FromDouble(mod);
    PyObject *pair = (q != NULL && r != NULL) ? PyTuple_Pack(2, q, r) : NULL;
    Py_XDECREF(q);
    Py_XDECREF(r);
    return pair;
}

static PyObject *
prim_float_floordiv(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double vx, wx, mod, floordiv;
    ARGS(2); CONVERT(0, vx); CONVERT(1, wx);
    if (wx == 0.0) {
        lu_err_format(PyExc_ZeroDivisionError, "division by zero");
        return NULL;
    }
    float_div_mod(vx, wx, &floordiv, &mod);
    return PyFloat_FromDouble(floordiv);
}

/* an odd integer; x is not an infinity or nan */
#define DOUBLE_IS_ODD_INTEGER(x) (fmod(fabs(x), 2.0) == 1.0)

static PyObject *prim_float_pow(PyObject *m, PyObject *const *args, Py_ssize_t nargs);

PyObject *
lu_float_pow(PyObject *a, PyObject *b)
{
    PyObject *args[3] = { a, b, Py_None };
    return prim_float_pow(NULL, args, 3);
}

/* float_pow(v, w, z): z must be None. */
static PyObject *
prim_float_pow(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double iv, iw, ix;
    int negate_result = 0;
    ARGS(3);
    if (args[2] != Py_None) {
        lu_err_format(PyExc_TypeError,
                      "pow() 3rd argument not allowed unless all arguments are integers");
        return NULL;
    }
    CONVERT(0, iv); CONVERT(1, iw);
    if (iw == 0) {              /* v**0 is 1, even 0**0 */
        return PyFloat_FromDouble(1.0);
    }
    if (isnan(iv)) {
        return PyFloat_FromDouble(iv);
    }
    if (isnan(iw)) {            /* v**nan = nan, unless v == 1 */
        return PyFloat_FromDouble(iv == 1.0 ? 1.0 : iw);
    }
    if (isinf(iw)) {
        iv = fabs(iv);
        if (iv == 1.0) {
            return PyFloat_FromDouble(1.0);
        }
        if ((iw > 0.0) == (iv > 1.0)) {
            return PyFloat_FromDouble(fabs(iw));
        }
        return PyFloat_FromDouble(0.0);
    }
    if (isinf(iv)) {
        int iw_is_odd = DOUBLE_IS_ODD_INTEGER(iw);
        if (iw > 0.0) {
            return PyFloat_FromDouble(iw_is_odd ? iv : fabs(iv));
        }
        return PyFloat_FromDouble(iw_is_odd ? copysign(0.0, iv) : 0.0);
    }
    if (iv == 0.0) {
        int iw_is_odd = DOUBLE_IS_ODD_INTEGER(iw);
        if (iw < 0.0) {
            lu_err_format(PyExc_ZeroDivisionError, "zero to a negative power");
            return NULL;
        }
        return PyFloat_FromDouble(iw_is_odd ? iv : 0.0);
    }
    if (iv < 0.0) {
        if (iw != floor(iw)) {
            /* A negative number to a fractional power is complex. */
            return lu_complex_pow(args[0], args[1]);
        }
        iv = -iv;
        negate_result = DOUBLE_IS_ODD_INTEGER(iw);
    }
    if (iv == 1.0) {
        return PyFloat_FromDouble(negate_result ? -1.0 : 1.0);
    }
    errno = 0;
    ix = pow(iv, iw);
    if (errno == 0) {
        if (ix == HUGE_VAL || ix == -HUGE_VAL) {
            errno = ERANGE;
        }
    }
    else if (errno == ERANGE && ix == 0.0) {
        errno = 0;
    }
    if (negate_result) {
        ix = -ix;
    }
    if (errno != 0) {
        errno_error();
        return NULL;
    }
    return PyFloat_FromDouble(ix);
}

static PyObject *
prim_float_neg(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); FLOAT(0);
    return PyFloat_FromDouble(-PyFloat_AS_DOUBLE(args[0]));
}

static PyObject *
prim_float_abs(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); FLOAT(0);
    return PyFloat_FromDouble(fabs(PyFloat_AS_DOUBLE(args[0])));
}

/* float_exact(x): x as an exact float (a subclass instance loses its type). */
static PyObject *
prim_float_exact(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); FLOAT(0);
    return PyFloat_FromDouble(PyFloat_AS_DOUBLE(args[0]));
}

/* float_subtype(cls, value): an instance of the float subclass cls. */
static PyObject *
prim_float_subtype(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); FLOAT(1);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyFloat_Type)) {
        lu_err_format(PyExc_TypeError, "float_subtype() needs a float subclass");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyObject *r = tp->tp_alloc(tp, 0);
    if (r != NULL) {
        ((PyFloatObject *)r)->ob_fval = PyFloat_AS_DOUBLE(args[1]);
    }
    return r;
}

static PyObject *
prim_float_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); FLOAT(0);
    return PyLong_FromLongLong(_Py_HashDouble(args[0], PyFloat_AS_DOUBLE(args[0])));
}

/* Comparison of a float with an int, exact (float_richcompare). The float's
   magnitude is weighed against the int's bit length, then equal-length
   values are compared as ints. */

static int
long_sign(PyObject *w)
{
    uintptr_t tag = ((PyLongObject *)w)->long_value.lv_tag;
    return 1 - (int)(tag & _PyLong_SIGN_MASK);
}

static int64_t
long_nbits(PyObject *w)
{
    PyLongObject *v = (PyLongObject *)w;
    Py_ssize_t n = (Py_ssize_t)(v->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    if (n == 0 || long_sign(w) == 0) {
        return 0;
    }
    digit top = v->long_value.ob_digit[n - 1];
    return (int64_t)(n - 1) * PyLong_SHIFT + (32 - __builtin_clz(top));
}

static int
apply_op(double i, double j, int op)
{
    switch (op) {
    case Py_LT: return i < j;
    case Py_LE: return i <= j;
    case Py_EQ: return i == j;
    case Py_NE: return i != j;
    case Py_GT: return i > j;
    default: return i >= j;
    }
}

static int
apply_cmp(int c, int op)
{
    switch (op) {
    case Py_LT: return c < 0;
    case Py_LE: return c <= 0;
    case Py_EQ: return c == 0;
    case Py_NE: return c != 0;
    case Py_GT: return c > 0;
    default: return c >= 0;
    }
}

/* float_cmp(v, w, op): v is a float; op is Py_LT .. Py_GE. */
PyObject *
lu_float_richcompare(double i, PyObject *w, int op)
{
    double j;
    if (PyFloat_Check(w)) {
        j = PyFloat_AS_DOUBLE(w);
    }
    else if (!isfinite(i)) {
        if (!PyLong_Check(w)) {
            return Py_NewRef(Py_NotImplemented);
        }
        j = 0.0;        /* an infinity or nan outweighs any int */
    }
    else if (PyLong_Check(w)) {
        int vsign = i == 0.0 ? 0 : i < 0.0 ? -1 : 1;
        int wsign = long_sign(w);
        int exponent;
        if (vsign != wsign) {
            i = (double)vsign;
            j = (double)wsign;
            return bool_of(apply_op(i, j, op));
        }
        int64_t nbits = long_nbits(w);
        if (nbits > DBL_MAX_EXP) {
            /* w is larger in magnitude than any finite double */
            i = (double)vsign;
            j = wsign * 2.0;
        }
        else if (nbits <= 48) {
            j = lu_long_as_double(w);
        }
        else {
            (void)frexp(i, &exponent);
            if (exponent < nbits) {
                j = i;
                i = 0.0;
            }
            else if (exponent > nbits) {
                j = 0.0;
            }
            else {
                /* Same number of bits before the radix point: compare the
                   integer part of v with w as ints. */
                double intpart;
                double fracpart = modf(i, &intpart);
                if (fracpart != 0.0) {
                    switch (op) {
                    case Py_EQ:
                        return Py_False;
                    case Py_NE:
                        return Py_True;
                    case Py_LT:
                    case Py_LE:
                        op = vsign > 0 ? Py_LT : Py_LE;
                        break;
                    case Py_GT:
                    case Py_GE:
                        op = vsign > 0 ? Py_GE : Py_GT;
                        break;
                    }
                }
                PyObject *vv = lu_long_from_double(intpart);
                if (vv == NULL) {
                    return NULL;
                }
                int c = lu_long_compare(vv, w);
                Py_DECREF(vv);
                return bool_of(apply_cmp(c, op));
            }
        }
    }
    else {
        return Py_NewRef(Py_NotImplemented);
    }
    return bool_of(apply_op(i, j, op));
}

static PyObject *
prim_float_cmp(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long op;
    ARGS(3); FLOAT(0);
    if (lu_long_as_long(args[2], &op) < 0) {
        return NULL;
    }
    return lu_float_richcompare(PyFloat_AS_DOUBLE(args[0]), args[1], (int)op);
}

/* float_repr(x): the shortest string that reads back as x. */
static PyObject *
ascii_result(char *buf)
{
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    PyObject *r = PyUnicode_FromString(buf);
    PyMem_Free(buf);
    return r;
}

static PyObject *
prim_float_repr(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); FLOAT(0);
    return ascii_result(PyOS_double_to_string(PyFloat_AS_DOUBLE(args[0]), 'r', 0,
                                              Py_DTSF_ADD_DOT_0, NULL));
}

/* float_format(x, code, precision, flags): PyOS_double_to_string. */
static PyObject *
prim_float_format(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long precision, flags;
    ARGS(4); FLOAT(0);
    if (!PyUnicode_Check(args[1]) || PyUnicode_GET_LENGTH(args[1]) != 1) {
        lu_err_format(PyExc_TypeError, "float_format() needs a format code");
        return NULL;
    }
    if (lu_long_as_long(args[2], &precision) < 0 || lu_long_as_long(args[3], &flags) < 0) {
        return NULL;
    }
    char code = (char)PyUnicode_READ_CHAR(args[1], 0);
    return ascii_result(PyOS_double_to_string(PyFloat_AS_DOUBLE(args[0]), code, (int)precision,
                                              (int)flags, NULL));
}

/* Text to float. */

#define ISSPACE(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))

/* The bytes of a str or bytes-like object, NUL terminated, with the Unicode
   whitespace and decimal digits of a non-ASCII str as ASCII
   (_PyUnicode_TransformDecimalAndSpaceToASCII). A bytes-like object is read
   through PyObject_GetBuffer(PyBUF_SIMPLE), as PyFloat_FromString reads it,
   so a non-contiguous buffer is a BufferError. */
char *
lu_number_text(PyObject *s, Py_ssize_t *len)
{
    char *buf;
    if (PyUnicode_Check(s)) {
        *len = PyUnicode_GET_LENGTH(s);
        buf = PyMem_Malloc(*len + 1);
        if (buf == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        int kind = PyUnicode_KIND(s);
        const void *data = PyUnicode_DATA(s);
        int ascii = PyUnicode_IS_ASCII(s);
        for (Py_ssize_t i = 0; i < *len; i++) {
            Py_UCS4 c = PyUnicode_READ(kind, data, i);
            if (ascii) {
                buf[i] = (char)c;
            }
            else if (Py_UNICODE_ISSPACE(c)) {
                buf[i] = ' ';
            }
            else if (Py_UNICODE_TODECIMAL(c) >= 0) {
                buf[i] = (char)('0' + Py_UNICODE_TODECIMAL(c));
            }
            else {
                buf[i] = c < 128 ? (char)c : '?';
            }
        }
    }
    else if (PyBytes_Check(s)) {
        *len = PyBytes_GET_SIZE(s);
        buf = PyMem_Malloc(*len + 1);
        if (buf == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        memcpy(buf, PyBytes_AS_STRING(s), *len);
    }
    else {
        lu_bv v;
        if (lu_bv_get(s, &v) < 0) {
            return NULL;
        }
        buf = PyMem_Malloc(v.n + 1);
        if (buf == NULL) {
            PyErr_NoMemory();
            lu_bv_put(&v);
            return NULL;
        }
        *len = v.n;
        memcpy(buf, v.p, v.n);
        lu_bv_put(&v);
    }
    buf[*len] = 0;
    return buf;
}

/* Remove the underscores that sit between digits
   (_Py_string_to_number_with_underscores). Returns the new length, or -1
   when one is misplaced or a NUL is embedded. */
Py_ssize_t
lu_strip_underscores(char *s, Py_ssize_t len)
{
    char prev = '\0';
    char *end = s;
    const char *p, *last = s + len;
    if (strchr(s, '_') == NULL) {
        return len;
    }
    for (p = s; *p; p++) {
        if (*p == '_') {
            if (!(prev >= '0' && prev <= '9')) {
                return -1;
            }
        }
        else {
            *end++ = *p;
            if (prev == '_' && !(*p >= '0' && *p <= '9')) {
                return -1;
            }
        }
        prev = *p;
    }
    if (prev == '_' || p != last) {
        return -1;
    }
    *end = '\0';
    return end - s;
}

/* float_from_text(str_or_bytes): the float, or MISSING for a bad literal. */
static PyObject *
prim_float_from_text(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    Py_ssize_t len;
    ARGS(1);
    char *buf = lu_number_text(args[0], &len);
    if (buf == NULL) {
        return NULL;
    }
    PyObject *r = Py_NewRef(LU_MISSING);
    len = lu_strip_underscores(buf, len);
    if (len >= 0) {
        const char *s = buf, *last = buf + len, *end;
        while (s < last && ISSPACE(*s)) {
            s++;
        }
        if (s < last) {
            while (s < last - 1 && ISSPACE(last[-1])) {
                last--;
            }
            double x = PyOS_string_to_double(s, (char **)&end, NULL);
            if (end == last && !(x == -1.0 && PyErr_Occurred())) {
                Py_DECREF(r);
                r = PyFloat_FromDouble(x);
            }
            else {
                PyErr_Clear();
            }
        }
    }
    PyMem_Free(buf);
    return r;
}

/* float_integral(x, mode): floor (0), ceil (1) or round half to even (2),
   as a float. */
static PyObject *
prim_float_integral(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long mode;
    ARGS(2); FLOAT(0);
    if (lu_long_as_long(args[1], &mode) < 0) {
        return NULL;
    }
    double x = PyFloat_AS_DOUBLE(args[0]);
    if (mode == 0) {
        x = floor(x);
    }
    else if (mode == 1) {
        x = ceil(x);
    }
    else {
        double rounded = round(x);
        if (fabs(x - rounded) == 0.5) {
            rounded = 2.0 * round(x / 2.0);     /* halfway: to even */
        }
        x = rounded;
    }
    return PyFloat_FromDouble(x);
}

/* float_is_integer(x): finite and integral. */
static PyObject *
prim_float_is_integer(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); FLOAT(0);
    double x = PyFloat_AS_DOUBLE(args[0]);
    return bool_of(isfinite(x) && floor(x) == x);
}

/* double_round: round a finite double to the closest multiple of
   10**-ndigits, through the correctly rounded decimal conversions. */
static PyObject *
double_round(double x, int ndigits)
{
    double rounded;
    Py_ssize_t buflen, mybuflen = 100;
    char *buf, *buf_end, shortbuf[100], *mybuf = shortbuf;
    int decpt, sign;
    PyObject *result = NULL;

    buf = _Py_dg_dtoa(x, 3, ndigits, &decpt, &sign, &buf_end);
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    /* space needed: digits, 1 for '0', 1 for sign, 5 for exp, 1 for NUL */
    buflen = buf_end - buf;
    if (buflen + 8 > mybuflen) {
        mybuflen = buflen + 8;
        mybuf = (char *)PyMem_Malloc(mybuflen);
        if (mybuf == NULL) {
            PyErr_NoMemory();
            goto exit;
        }
    }
    snprintf(mybuf, mybuflen, "%s0%se%d", (sign ? "-" : ""), buf, decpt - (int)buflen);
    errno = 0;
    rounded = _Py_dg_strtod(mybuf, NULL);
    if (errno == ERANGE && fabs(rounded) >= 1.) {
        lu_err_format(PyExc_OverflowError, "rounded value too large to represent");
    }
    else {
        result = PyFloat_FromDouble(rounded);
    }
    if (mybuf != shortbuf) {
        PyMem_Free(mybuf);
    }
  exit:
    _Py_dg_freedtoa(buf);
    return result;
}

/* float_round(x, ndigits): x rounded to ndigits decimal places, as a float.
   ndigits is an int of any size. */
static PyObject *
prim_float_round(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long ndigits;
    ARGS(2); FLOAT(0);
    if (!PyLong_Check(args[1])) {
        lu_err_format(PyExc_TypeError, "float_round() needs an int");
        return NULL;
    }
    double x = PyFloat_AS_DOUBLE(args[0]);
    if (lu_long_as_long(args[1], &ndigits) < 0) {
        /* beyond a C long: clip, as PyNumber_AsSsize_t does */
        PyErr_Clear();
        ndigits = long_sign(args[1]) < 0 ? LONG_MIN : LONG_MAX;
    }
    /* nans and infinities round to themselves */
    if (!isfinite(x)) {
        return PyFloat_FromDouble(x);
    }
    /* For ndigits > NDIGITS_MAX, x rounds to itself; for ndigits <
       NDIGITS_MIN, to +-0.0. 0.30103 is an upper bound for log10(2). */
#define NDIGITS_MAX ((int)((DBL_MANT_DIG-DBL_MIN_EXP) * 0.30103))
#define NDIGITS_MIN (-(int)((DBL_MAX_EXP + 1) * 0.30103))
    if (ndigits > NDIGITS_MAX) {
        return PyFloat_FromDouble(x);
    }
    if (ndigits < NDIGITS_MIN) {
        return PyFloat_FromDouble(0.0 * x);
    }
    return double_round(x, (int)ndigits);
#undef NDIGITS_MAX
#undef NDIGITS_MIN
}

/* float_as_ratio(x): (numerator, denominator) in lowest terms. */
static PyObject *
prim_float_as_ratio(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); FLOAT(0);
    double x = PyFloat_AS_DOUBLE(args[0]);
    int exponent;
    if (isinf(x)) {
        lu_err_format(PyExc_OverflowError, "cannot convert Infinity to integer ratio");
        return NULL;
    }
    if (isnan(x)) {
        lu_err_format(PyExc_ValueError, "cannot convert NaN to integer ratio");
        return NULL;
    }
    double float_part = frexp(x, &exponent);    /* x == float_part * 2**exponent */
    for (int i = 0; i < 300 && float_part != floor(float_part); i++) {
        float_part *= 2.0;
        exponent--;
    }
    PyObject *numerator = lu_long_from_double(float_part);
    PyObject *denominator = PyLong_FromLong(1);
    PyObject *shift = PyLong_FromLong(exponent < 0 ? -exponent : exponent);
    PyObject *pair = NULL;
    if (numerator != NULL && denominator != NULL && shift != NULL) {
        PyObject *shifted = lu_long_binary(NB_LSHIFT, exponent > 0 ? numerator : denominator, shift);
        if (shifted != NULL) {
            if (exponent > 0) {
                Py_SETREF(numerator, shifted);
            }
            else {
                Py_SETREF(denominator, shifted);
            }
            pair = PyTuple_Pack(2, numerator, denominator);
        }
    }
    Py_XDECREF(numerator);
    Py_XDECREF(denominator);
    Py_XDECREF(shift);
    return pair;
}

/* Hexadecimal. */

/* TOHEX_NBITS is DBL_MANT_DIG rounded up to the next integer of the form 4k+1. */
#define TOHEX_NBITS DBL_MANT_DIG + 3 - (DBL_MANT_DIG+2)%4

static int
hex_from_char(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* float_hex(x) */
static PyObject *
prim_float_hex(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x, m_;
    int e, shift, i, si, esign;
    char s[(TOHEX_NBITS-1)/4+3];
    char out[(TOHEX_NBITS-1)/4+32];
    ARGS(1); FLOAT(0);
    x = PyFloat_AS_DOUBLE(args[0]);
    if (isnan(x) || isinf(x)) {
        return ascii_result(PyOS_double_to_string(x, 'r', 0, Py_DTSF_ADD_DOT_0, NULL));
    }
    if (x == 0.0) {
        return PyUnicode_FromString(copysign(1.0, x) == -1.0 ? "-0x0.0p+0" : "0x0.0p+0");
    }
    m_ = frexp(fabs(x), &e);
    shift = 1 - (DBL_MIN_EXP - e > 0 ? DBL_MIN_EXP - e : 0);
    m_ = ldexp(m_, shift);
    e -= shift;
    si = 0;
    s[si++] = "0123456789abcdef"[(int)m_];
    m_ -= (int)m_;
    s[si++] = '.';
    for (i = 0; i < (TOHEX_NBITS-1)/4; i++) {
        m_ *= 16.0;
        s[si++] = "0123456789abcdef"[(int)m_];
        m_ -= (int)m_;
    }
    s[si] = '\0';
    if (e < 0) {
        esign = '-';
        e = -e;
    }
    else {
        esign = '+';
    }
    snprintf(out, sizeof out, "%s0x%sp%c%d", x < 0.0 ? "-" : "", s, esign, e);
    return PyUnicode_FromString(out);
}

/* float_fromhex(str): the float, or ValueError or OverflowError. */
static PyObject *
prim_float_fromhex(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x;
    long exp, top_exp, lsb, key_digit;
    const char *s, *coeff_start, *s_store, *coeff_end, *exp_start, *s_end;
    int half_eps, digit, round_up, negate = 0;
    Py_ssize_t length, ndigits, fdigits, i;
    ARGS(1);
    if (!PyUnicode_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "float_fromhex() needs a str");
        return NULL;
    }
    s = PyUnicode_AsUTF8AndSize(args[0], &length);
    if (s == NULL) {
        return NULL;
    }
    s_end = s + length;

    /* leading whitespace */
    while (ISSPACE(*s)) {
        s++;
    }
    /* infinities and nans */
    x = _Py_parse_inf_or_nan(s, (char **)&coeff_end);
    if (coeff_end != s) {
        s = coeff_end;
        goto finished;
    }
    /* optional sign */
    if (*s == '-') {
        s++;
        negate = 1;
    }
    else if (*s == '+') {
        s++;
    }
    /* [0x] */
    s_store = s;
    if (*s == '0') {
        s++;
        if (*s == 'x' || *s == 'X') {
            s++;
        }
        else {
            s = s_store;
        }
    }
    /* coefficient: <integer> [. <fraction>] */
    coeff_start = s;
    while (hex_from_char(*s) >= 0) {
        s++;
    }
    s_store = s;
    if (*s == '.') {
        s++;
        while (hex_from_char(*s) >= 0) {
            s++;
        }
        coeff_end = s - 1;
    }
    else {
        coeff_end = s;
    }
    /* ndigits = total # of hex digits; fdigits = # after point */
    ndigits = coeff_end - coeff_start;
    fdigits = coeff_end - s_store;
    if (ndigits == 0) {
        goto parse_error;
    }
    {
        long lim1 = DBL_MIN_EXP - DBL_MANT_DIG - LONG_MIN / 2;
        long lim2 = LONG_MAX / 2 + 1 - DBL_MAX_EXP;
        if (ndigits > (lim1 < lim2 ? lim1 : lim2) / 4) {
            goto insane_length_error;
        }
    }
    /* [p <exponent>] */
    if (*s == 'p' || *s == 'P') {
        s++;
        exp_start = s;
        if (*s == '-' || *s == '+') {
            s++;
        }
        if (!('0' <= *s && *s <= '9')) {
            goto parse_error;
        }
        s++;
        while ('0' <= *s && *s <= '9') {
            s++;
        }
        exp = strtol(exp_start, NULL, 10);
    }
    else {
        exp = 0;
    }

/* for 0 <= j < ndigits, HEX_DIGIT(j) gives the jth most significant digit */
#define HEX_DIGIT(j) hex_from_char(*((j) < fdigits ? coeff_end-(j) : coeff_end-1-(j)))

    /* Discard leading zeros, and catch extreme overflow and underflow */
    while (ndigits > 0 && HEX_DIGIT(ndigits-1) == 0) {
        ndigits--;
    }
    if (ndigits == 0 || exp < LONG_MIN/2) {
        x = 0.0;
        goto finished;
    }
    if (exp > LONG_MAX/2) {
        goto overflow_error;
    }
    /* Adjust exponent for fractional part. */
    exp = exp - 4*((long)fdigits);
    /* top_exp = 1 more than exponent of most sig. bit of coefficient */
    top_exp = exp + 4*((long)ndigits - 1);
    for (digit = HEX_DIGIT(ndigits-1); digit != 0; digit /= 2) {
        top_exp++;
    }
    /* catch almost all nonextreme cases of overflow and underflow here */
    if (top_exp < DBL_MIN_EXP - DBL_MANT_DIG) {
        x = 0.0;
        goto finished;
    }
    if (top_exp > DBL_MAX_EXP) {
        goto overflow_error;
    }
    /* lsb = exponent of least significant bit of the *rounded* value. */
    lsb = (top_exp > (long)DBL_MIN_EXP ? top_exp : (long)DBL_MIN_EXP) - DBL_MANT_DIG;
    x = 0.0;
    if (exp >= lsb) {
        /* no rounding required */
        for (i = ndigits-1; i >= 0; i--) {
            x = 16.0*x + HEX_DIGIT(i);
        }
        x = ldexp(x, (int)(exp));
        goto finished;
    }
    /* rounding required. key_digit is the index of the hex digit containing
       the first bit to be rounded away. */
    half_eps = 1 << (int)((lsb - exp - 1) % 4);
    key_digit = (lsb - exp - 1) / 4;
    for (i = ndigits-1; i > key_digit; i--) {
        x = 16.0*x + HEX_DIGIT(i);
    }
    digit = HEX_DIGIT(key_digit);
    x = 16.0*x + (double)(digit & (16-2*half_eps));
    /* round-half-even: round up if bit lsb-1 is 1 and at least one of bits
       lsb, lsb-2, lsb-3, lsb-4, ... is 1. */
    if ((digit & half_eps) != 0) {
        round_up = 0;
        if ((digit & (3*half_eps-1)) != 0 || (half_eps == 8 &&
                key_digit+1 < ndigits && (HEX_DIGIT(key_digit+1) & 1) != 0)) {
            round_up = 1;
        }
        else {
            for (i = key_digit-1; i >= 0; i--) {
                if (HEX_DIGIT(i) != 0) {
                    round_up = 1;
                    break;
                }
            }
        }
        if (round_up) {
            x += 2*half_eps;
            if (top_exp == DBL_MAX_EXP && x == ldexp((double)(2*half_eps), DBL_MANT_DIG)) {
                /* overflow corner case: pre-rounded value < 2**DBL_MAX_EXP;
                   rounded=2**DBL_MAX_EXP. */
                goto overflow_error;
            }
        }
    }
    x = ldexp(x, (int)(exp+4*key_digit));
#undef HEX_DIGIT

  finished:
    /* optional trailing whitespace leading to the end of the string */
    while (ISSPACE(*s)) {
        s++;
    }
    if (s != s_end) {
        goto parse_error;
    }
    return PyFloat_FromDouble(negate ? -x : x);

  overflow_error:
    lu_err_format(PyExc_OverflowError, "hexadecimal value too large to represent as a float");
    return NULL;

  parse_error:
    lu_err_format(PyExc_ValueError, "invalid hexadecimal floating-point string");
    return NULL;

  insane_length_error:
    lu_err_format(PyExc_ValueError, "hexadecimal string too long to convert");
    return NULL;
}

/* The methods C reads from float's tp_methods, as in CPython (_decimal calls
   as_integer_ratio through it); the kernel's definitions replace them in the
   dict. */
static PyObject *
float_as_integer_ratio(PyObject *self, PyObject *unused)
{
    return prim_float_as_ratio(NULL, &self, 1);
}

PyMethodDef lu_float_methods[] = {
    { "as_integer_ratio", float_as_integer_ratio, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL },
};

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
#define PURE(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL | LU_METH_NOPY, NULL }
PyMethodDef lu_prim_float_methods[] = {
    PURE(float_add), PURE(float_sub), PURE(float_mul), PURE(float_truediv), PURE(float_floordiv),
    PURE(float_mod), PURE(float_divmod), PURE(float_pow), PURE(float_neg), PURE(float_abs),
    PURE(float_exact), FAST(float_subtype), PURE(float_hash), PURE(float_cmp), PURE(float_repr),
    PURE(float_format), FAST(float_from_text), PURE(float_integral), PURE(float_is_integer),
    PURE(float_round), PURE(float_as_ratio), PURE(float_hex), PURE(float_fromhex),
    { NULL, NULL, 0, NULL },
};

/* float(v) for a str, bytes or other bytes-like object only. The parse and
   the errors live in the kernel's float(); the type gate is here so a
   non-buffer answers before the delegation. */
PyObject *
PyFloat_FromString(PyObject *v)
{
    if (!PyUnicode_Check(v) && !PyBytes_Check(v) && !PyByteArray_Check(v) && !PyObject_CheckBuffer(v)) {
        PyErr_Format(PyExc_TypeError, "float() argument must be a string or a real number, not '%.200s'",
                     Py_TYPE(v)->tp_name);
        return NULL;
    }
    return PyObject_CallOneArg((PyObject *)&PyFloat_Type, v);
}

/* The float info, max and min (floatobject.c). The kernel builds
   sys.float_info with the values of the same <float.h> macros. */
PyObject *
PyFloat_GetInfo(void)
{
    PyObject *info = PyDict_GetItemString(PyInterpreterState_Get()->sysdict, "float_info");
    if (info == NULL) {
        lu_err_format(PyExc_SystemError, "can't find float_info in sys");
        return NULL;
    }
    return Py_NewRef(info);
}

double
PyFloat_GetMax(void)
{
    return DBL_MAX;
}

double
PyFloat_GetMin(void)
{
    return DBL_MIN;
}

/* PyFloat_{Pack,Unpack}{2,4,8} (floatobject.c): the IEEE 754 serializations.
   The host float and double are IEEE 754 little-endian, as
   float.__getformat__ reports, so 4 and 8 copy the native bytes (swapped for
   big-endian output); the half float has no native type and is converted in
   software, with round-half-to-even as numpy's NPY_HALF_ROUND_TIES_TO_EVEN.
   A signaling NaN stays signaling: the conversions to a narrower width quiet
   it, and the quiet bit is put back when a payload survives. */

int
PyFloat_Pack2(double x, char *data, int le)
{
    unsigned char *p = (unsigned char *)data;
    unsigned char sign;
    int e;
    double f;
    unsigned short bits;
    int incr = 1;

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
        bits = (unsigned short)(v >> 42); /* NaN's type & payload */
        /* set qNaN if no payload */
        if (!bits) {
            bits |= (1<<9);
        }
    }
    else {
        sign = (x < 0.0);
        if (sign) {
            x = -x;
        }

        f = frexp(x, &e);
        if (f < 0.5 || f >= 1.0) {
            PyErr_SetString(PyExc_SystemError,
                            "frexp() result out of range");
            return -1;
        }

        /* Normalize f to be in the range [1.0, 2.0) */
        f *= 2.0;
        e--;

        if (e >= 16) {
            goto Overflow;
        }
        else if (e < -25) {
            /* |x| < 2**-25. Underflow to zero. */
            f = 0.0;
            e = 0;
        }
        else if (e < -14) {
            /* |x| < 2**-14. Gradual underflow */
            f = ldexp(f, 14 + e);
            e = 0;
        }
        else /* if (!(e == 0 && f == 0.0)) */ {
            e += 15;
            f -= 1.0; /* Get rid of leading 1 */
        }

        f *= 1024.0; /* 2**10 */
        /* Round to even */
        bits = (unsigned short)f; /* Note the truncation */
        assert(bits < 1024);
        assert(e < 31);
        if ((f - bits > 0.5) || ((f - bits == 0.5) && (bits % 2 == 1))) {
            ++bits;
            if (bits == 1024) {
                /* The carry propagated out of a string of 10 1 bits. */
                bits = 0;
                ++e;
                if (e == 31)
                    goto Overflow;
            }
        }
    }

    bits |= (e << 10) | (sign << 15);

    /* Write out result. */
    if (le) {
        p += 1;
        incr = -1;
    }

    /* First byte */
    *p = (unsigned char)((bits >> 8) & 0xFF);
    p += incr;

    /* Second byte */
    *p = (unsigned char)(bits & 0xFF);

    return 0;

  Overflow:
    PyErr_SetString(PyExc_OverflowError,
                    "float too large to pack with e format");
    return -1;
}

int
PyFloat_Pack4(double x, char *data, int le)
{
    unsigned char *p = (unsigned char *)data;
    float y = (float)x;
    int i, incr = 1;

    if (isinf(y) && !isinf(x)) {
        goto Overflow;
    }

    /* correct y if x was a sNaN, transformed to qNaN by conversion */
    if (isnan(x)) {
        uint64_t v;

        memcpy(&v, &x, 8);
        if ((v & (1ULL << 51)) == 0) {
            uint32_t u32;
            memcpy(&u32, &y, 4);
            /* if have payload, make sNaN */
            if (u32 & 0x3fffff) {
                u32 &= ~(1u << 22);
            }
            memcpy(&y, &u32, 4);
        }
    }

    unsigned char s[sizeof(float)];
    memcpy(s, &y, sizeof(float));

    if (!le) {
        p += 3;
        incr = -1;
    }

    for (i = 0; i < 4; i++) {
        *p = s[i];
        p += incr;
    }
    return 0;

  Overflow:
    PyErr_SetString(PyExc_OverflowError,
                    "float too large to pack with f format");
    return -1;
}

int
PyFloat_Pack8(double x, char *data, int le)
{
    unsigned char *p = (unsigned char *)data;
    int i, incr = 1;

    unsigned char s[sizeof(double)];
    memcpy(s, &x, sizeof(double));

    if (!le) {
        p += 7;
        incr = -1;
    }

    for (i = 0; i < 8; i++) {
        *p = s[i];
        p += incr;
    }
    return 0;
}

double
PyFloat_Unpack2(const char *data, int le)
{
    unsigned char *p = (unsigned char *)data;
    unsigned char sign;
    int e;
    unsigned int f;
    double x;
    int incr = 1;

    if (le) {
        p += 1;
        incr = -1;
    }

    /* First byte */
    sign = (*p >> 7) & 1;
    e = (*p & 0x7C) >> 2;
    f = (*p & 0x03) << 8;
    p += incr;

    /* Second byte */
    f |= *p;

    if (e == 0x1f) {
        if (f == 0) {
            /* Infinity */
            return sign ? -Py_INFINITY : Py_INFINITY;
        }
        else {
            /* NaN */
            uint64_t v = sign ? 0xfff0000000000000ULL : 0x7ff0000000000000ULL;

            v += (uint64_t)f << 42; /* add NaN's type & payload */
            memcpy(&x, &v, sizeof(v));
            return x;
        }
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

    if (sign)
        x = -x;

    return x;
}

double
PyFloat_Unpack4(const char *data, int le)
{
    unsigned char *p = (unsigned char *)data;
    float x;

    if (!le) {
        char buf[4];
        char *d = &buf[3];
        int i;

        for (i = 0; i < 4; i++) {
            *d-- = *p++;
        }
        memcpy(&x, buf, 4);
    }
    else {
        memcpy(&x, p, 4);
    }

    /* return sNaN double if x was sNaN float */
    if (isnan(x)) {
        uint32_t v;
        memcpy(&v, &x, 4);

        if ((v & (1u << 22)) == 0) {
            double y = x; /* will make qNaN double */
            uint64_t u64;
            memcpy(&u64, &y, 8);
            u64 &= ~(1ULL << 51); /* make sNaN */
            memcpy(&y, &u64, 8);
            return y;
        }
    }

    return x;
}

double
PyFloat_Unpack8(const char *data, int le)
{
    unsigned char *p = (unsigned char *)data;
    double x;

    if (!le) {
        char buf[8];
        char *d = &buf[7];
        int i;

        for (i = 0; i < 8; i++) {
            *d-- = *p++;
        }
        memcpy(&x, buf, 8);
    }
    else {
        memcpy(&x, p, 8);
    }

    return x;
}
