/* complex: the arithmetic under the kernel's complex (DESIGN 4.4, scalars),
   from CPython's Objects/complexobject.c. Operands are complex, float or int;
   any other operand answers NotImplemented. Construction, comparison with
   ints and formatting are lib/kernel/complex.py over these. */

#include "prim/prim.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <ctype.h>
#include <string.h>

/* _Py_ADJUST_ERANGE2 */
#define _Py_ADJUST_ERANGE2(X, Y) do {         if ((X) == HUGE_VAL || (X) == -HUGE_VAL || (Y) == HUGE_VAL || (Y) == -HUGE_VAL) {             if (errno == 0) errno = ERANGE;         }         else if (errno == ERANGE) errno = 0;     } while (0)

/* elementary operations on complex numbers */

static Py_complex c_1 = {1., 0.};

Py_complex
_Py_c_sum(Py_complex a, Py_complex b)
{
    Py_complex r;
    r.real = a.real + b.real;
    r.imag = a.imag + b.imag;
    return r;
}

Py_complex
_Py_cr_sum(Py_complex a, double b)
{
    Py_complex r = a;
    r.real += b;
    return r;
}

static inline Py_complex
_Py_rc_sum(double a, Py_complex b)
{
    return _Py_cr_sum(b, a);
}

Py_complex
_Py_c_diff(Py_complex a, Py_complex b)
{
    Py_complex r;
    r.real = a.real - b.real;
    r.imag = a.imag - b.imag;
    return r;
}

Py_complex
_Py_cr_diff(Py_complex a, double b)
{
    Py_complex r = a;
    r.real -= b;
    return r;
}

Py_complex
_Py_rc_diff(double a, Py_complex b)
{
    Py_complex r;
    r.real = a - b.real;
    r.imag = -b.imag;
    return r;
}

Py_complex
_Py_c_neg(Py_complex a)
{
    Py_complex r;
    r.real = -a.real;
    r.imag = -a.imag;
    return r;
}

Py_complex
_Py_c_prod(Py_complex z, Py_complex w)
{
    double a = z.real, b = z.imag, c = w.real, d = w.imag;
    double ac = a*c, bd = b*d, ad = a*d, bc = b*c;
    Py_complex r = {ac - bd, ad + bc};

    /* Recover infinities that computed as nan+nanj.  See e.g. the C11,
       Annex G.5.1, routine _Cmultd(). */
    if (isnan(r.real) && isnan(r.imag)) {
        int recalc = 0;

        if (isinf(a) || isinf(b)) {  /* z is infinite */
            /* "Box" the infinity and change nans in the other factor to 0 */
            a = copysign(isinf(a) ? 1.0 : 0.0, a);
            b = copysign(isinf(b) ? 1.0 : 0.0, b);
            if (isnan(c)) {
                c = copysign(0.0, c);
            }
            if (isnan(d)) {
                d = copysign(0.0, d);
            }
            recalc = 1;
        }
        if (isinf(c) || isinf(d)) {  /* w is infinite */
            /* "Box" the infinity and change nans in the other factor to 0 */
            c = copysign(isinf(c) ? 1.0 : 0.0, c);
            d = copysign(isinf(d) ? 1.0 : 0.0, d);
            if (isnan(a)) {
                a = copysign(0.0, a);
            }
            if (isnan(b)) {
                b = copysign(0.0, b);
            }
            recalc = 1;
        }
        if (!recalc && (isinf(ac) || isinf(bd) || isinf(ad) || isinf(bc))) {
            /* Recover infinities from overflow by changing nans to 0 */
            if (isnan(a)) {
                a = copysign(0.0, a);
            }
            if (isnan(b)) {
                b = copysign(0.0, b);
            }
            if (isnan(c)) {
                c = copysign(0.0, c);
            }
            if (isnan(d)) {
                d = copysign(0.0, d);
            }
            recalc = 1;
        }
        if (recalc) {
            r.real = Py_INFINITY*(a*c - b*d);
            r.imag = Py_INFINITY*(a*d + b*c);
        }
    }

    return r;
}

Py_complex
_Py_cr_prod(Py_complex a, double b)
{
    Py_complex r = a;
    r.real *= b;
    r.imag *= b;
    return r;
}

static inline Py_complex
_Py_rc_prod(double a, Py_complex b)
{
    return _Py_cr_prod(b, a);
}

/* Avoid bad optimization on Windows ARM64 until the compiler is fixed */
#ifdef _M_ARM64
#pragma optimize("", off)
#endif
Py_complex
_Py_c_quot(Py_complex a, Py_complex b)
{
    /******************************************************************
    This was the original algorithm.  It's grossly prone to spurious
    overflow and underflow errors.  It also merrily divides by 0 despite
    checking for that(!).  The code still serves a doc purpose here, as
    the algorithm following is a simple by-cases transformation of this
    one:

    Py_complex r;
    double d = b.real*b.real + b.imag*b.imag;
    if (d == 0.)
        errno = EDOM;
    r.real = (a.real*b.real + a.imag*b.imag)/d;
    r.imag = (a.imag*b.real - a.real*b.imag)/d;
    return r;
    ******************************************************************/

    /* This algorithm is better, and is pretty obvious:  first divide the
     * numerators and denominator by whichever of {b.real, b.imag} has
     * larger magnitude.  The earliest reference I found was to CACM
     * Algorithm 116 (Complex Division, Robert L. Smith, Stanford
     * University).
     */
     Py_complex r;      /* the result */
     const double abs_breal = b.real < 0 ? -b.real : b.real;
     const double abs_bimag = b.imag < 0 ? -b.imag : b.imag;

    if (abs_breal >= abs_bimag) {
        /* divide tops and bottom by b.real */
        if (abs_breal == 0.0) {
            errno = EDOM;
            r.real = r.imag = 0.0;
        }
        else {
            const double ratio = b.imag / b.real;
            const double denom = b.real + b.imag * ratio;
            r.real = (a.real + a.imag * ratio) / denom;
            r.imag = (a.imag - a.real * ratio) / denom;
        }
    }
    else if (abs_bimag >= abs_breal) {
        /* divide tops and bottom by b.imag */
        const double ratio = b.real / b.imag;
        const double denom = b.real * ratio + b.imag;
        assert(b.imag != 0.0);
        r.real = (a.real * ratio + a.imag) / denom;
        r.imag = (a.imag * ratio - a.real) / denom;
    }
    else {
        /* At least one of b.real or b.imag is a NaN */
        r.real = r.imag = Py_NAN;
    }

    /* Recover infinities and zeros that computed as nan+nanj.  See e.g.
       the C11, Annex G.5.2, routine _Cdivd(). */
    if (isnan(r.real) && isnan(r.imag)) {
        if ((isinf(a.real) || isinf(a.imag))
            && isfinite(b.real) && isfinite(b.imag))
        {
            const double x = copysign(isinf(a.real) ? 1.0 : 0.0, a.real);
            const double y = copysign(isinf(a.imag) ? 1.0 : 0.0, a.imag);
            r.real = Py_INFINITY * (x*b.real + y*b.imag);
            r.imag = Py_INFINITY * (y*b.real - x*b.imag);
        }
        else if ((isinf(abs_breal) || isinf(abs_bimag))
                 && isfinite(a.real) && isfinite(a.imag))
        {
            const double x = copysign(isinf(b.real) ? 1.0 : 0.0, b.real);
            const double y = copysign(isinf(b.imag) ? 1.0 : 0.0, b.imag);
            r.real = 0.0 * (a.real*x + a.imag*y);
            r.imag = 0.0 * (a.imag*x - a.real*y);
        }
    }

    return r;
}

Py_complex
_Py_cr_quot(Py_complex a, double b)
{
    Py_complex r = a;
    if (b) {
        r.real /= b;
        r.imag /= b;
    }
    else {
        errno = EDOM;
        r.real = r.imag = 0.0;
    }
    return r;
}

/* an equivalent of _Py_c_quot() function, when 1st argument is real */
Py_complex
_Py_rc_quot(double a, Py_complex b)
{
    Py_complex r;
    const double abs_breal = b.real < 0 ? -b.real : b.real;
    const double abs_bimag = b.imag < 0 ? -b.imag : b.imag;

    if (abs_breal >= abs_bimag) {
        if (abs_breal == 0.0) {
            errno = EDOM;
            r.real = r.imag = 0.0;
        }
        else {
            const double ratio = b.imag / b.real;
            const double denom = b.real + b.imag * ratio;
            r.real = a / denom;
            r.imag = (-a * ratio) / denom;
        }
    }
    else if (abs_bimag >= abs_breal) {
        const double ratio = b.real / b.imag;
        const double denom = b.real * ratio + b.imag;
        assert(b.imag != 0.0);
        r.real = (a * ratio) / denom;
        r.imag = (-a) / denom;
    }
    else {
        r.real = r.imag = Py_NAN;
    }

    if (isnan(r.real) && isnan(r.imag) && isfinite(a)
        && (isinf(abs_breal) || isinf(abs_bimag)))
    {
        const double x = copysign(isinf(b.real) ? 1.0 : 0.0, b.real);
        const double y = copysign(isinf(b.imag) ? 1.0 : 0.0, b.imag);
        r.real = 0.0 * (a*x);
        r.imag = 0.0 * (-a*y);
    }

    return r;
}
#ifdef _M_ARM64
#pragma optimize("", on)
#endif

Py_complex
_Py_c_pow(Py_complex a, Py_complex b)
{
    Py_complex r;
    double vabs,len,at,phase;
    if (b.real == 0. && b.imag == 0.) {
        r.real = 1.;
        r.imag = 0.;
    }
    else if (a.real == 0. && a.imag == 0.) {
        if (b.imag != 0. || b.real < 0.)
            errno = EDOM;
        r.real = 0.;
        r.imag = 0.;
    }
    else {
        vabs = hypot(a.real,a.imag);
        len = pow(vabs,b.real);
        at = atan2(a.imag, a.real);
        phase = at*b.real;
        if (b.imag != 0.0) {
            len *= exp(-at*b.imag);
            phase += b.imag*log(vabs);
        }
        r.real = len*cos(phase);
        r.imag = len*sin(phase);

        _Py_ADJUST_ERANGE2(r.real, r.imag);
    }
    return r;
}

static Py_complex
c_powu(Py_complex x, long n)
{
    Py_complex r, p;
    long mask = 1;
    r = c_1;
    p = x;
    while (mask > 0 && n >= mask) {
        if (n & mask)
            r = _Py_c_prod(r,p);
        mask <<= 1;
        p = _Py_c_prod(p,p);
    }
    return r;
}

static Py_complex
c_powi(Py_complex x, long n)
{
    if (n > 0)
        return c_powu(x,n);
    else
        return _Py_c_quot(c_1, c_powu(x,-n));

}

double
_Py_c_abs(Py_complex z)
{
    /* sets errno = ERANGE on overflow;  otherwise errno = 0 */
    double result;

    if (!isfinite(z.real) || !isfinite(z.imag)) {
        /* C99 rules: if either the real or the imaginary part is an
           infinity, return infinity, even if the other part is a
           NaN. */
        if (isinf(z.real)) {
            result = fabs(z.real);
            errno = 0;
            return result;
        }
        if (isinf(z.imag)) {
            result = fabs(z.imag);
            errno = 0;
            return result;
        }
        /* either the real or imaginary part is a NaN,
           and neither is infinite. Result should be NaN. */
        return Py_NAN;
    }
    result = hypot(z.real, z.imag);
    if (!isfinite(result))
        errno = ERANGE;
    else
        errno = 0;
    return result;
}


/* The primitives. */

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__, (n)); return NULL; } } while (0)
#define COMPLEX(i) do { if (!PyComplex_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() needs a complex", __func__); return NULL; } } while (0)

#define CVAL(o) (((PyComplexObject *)(o))->cval)

/* Complex arithmetic combines a pure-real (float or int) operand with a
   complex one directly, without first coercing it to a complex (C11 Annex
   G.5.1, G.5.2). */
#define COMPLEX_BINOP(NAME, FUNC)                                           \
    static PyObject *                                                       \
    prim_complex_##NAME(PyObject *m, PyObject *const *args, Py_ssize_t nargs) \
    {                                                                       \
        PyObject *v, *w;                                                    \
        Py_complex a;                                                       \
        int r;                                                              \
        ARGS(2);                                                            \
        v = args[0];                                                        \
        w = args[1];                                                        \
        errno = 0;                                                          \
        if (PyComplex_Check(w)) {                                           \
            Py_complex b = CVAL(w);                                         \
            if (PyComplex_Check(v)) {                                       \
                a = _Py_c_##FUNC(CVAL(v), b);                               \
            }                                                               \
            else if ((r = lu_as_double(v, &a.real)) != 0) {                 \
                return r > 0 ? Py_NewRef(Py_NotImplemented) : NULL;         \
            }                                                               \
            else {                                                          \
                a = _Py_rc_##FUNC(a.real, b);                               \
            }                                                               \
        }                                                                   \
        else if (!PyComplex_Check(v)) {                                     \
            return Py_NewRef(Py_NotImplemented);                            \
        }                                                                   \
        else {                                                              \
            double b;                                                       \
            if ((r = lu_as_double(w, &b)) != 0) {                           \
                return r > 0 ? Py_NewRef(Py_NotImplemented) : NULL;         \
            }                                                               \
            a = _Py_cr_##FUNC(CVAL(v), b);                                  \
        }                                                                   \
        if (errno == EDOM) {                                                \
            lu_err_format(PyExc_ZeroDivisionError, "division by zero");     \
            return NULL;                                                    \
        }                                                                   \
        return PyComplex_FromCComplex(a);                                   \
    }

COMPLEX_BINOP(add, sum)
COMPLEX_BINOP(mul, prod)
COMPLEX_BINOP(sub, diff)
COMPLEX_BINOP(truediv, quot)

/* A complex, float or int as a complex: 0, 1 (another type) or -1 (error). */
static int
to_complex(PyObject *o, Py_complex *c)
{
    if (PyComplex_Check(o)) {
        *c = CVAL(o);
        return 0;
    }
    c->imag = 0.0;
    return lu_as_double(o, &c->real);
}

static PyObject *
complex_power(PyObject *v, PyObject *w, PyObject *z)
{
    Py_complex p, a, b;
    int r;
    if ((r = to_complex(v, &a)) != 0 || (r = to_complex(w, &b)) != 0) {
        return r > 0 ? Py_NewRef(Py_NotImplemented) : NULL;
    }
    if (z != Py_None) {
        lu_err_format(PyExc_ValueError, "complex modulo");
        return NULL;
    }
    errno = 0;
    /* A small integer exponent has a faster and more accurate algorithm. */
    if (b.imag == 0.0 && b.real == floor(b.real) && fabs(b.real) <= 100.0) {
        p = c_powi(a, (long)b.real);
        _Py_ADJUST_ERANGE2(p.real, p.imag);
    }
    else {
        p = _Py_c_pow(a, b);
    }
    if (errno == EDOM) {
        lu_err_format(PyExc_ZeroDivisionError, "zero to a negative or complex power");
        return NULL;
    }
    if (errno == ERANGE) {
        lu_err_format(PyExc_OverflowError, "complex exponentiation");
        return NULL;
    }
    return PyComplex_FromCComplex(p);
}

PyObject *
lu_complex_pow(PyObject *v, PyObject *w)
{
    return complex_power(v, w, Py_None);
}

static PyObject *
prim_complex_pow(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    return complex_power(args[0], args[1], args[2]);
}

static PyObject *
prim_complex_neg(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); COMPLEX(0);
    return PyComplex_FromCComplex(_Py_c_neg(CVAL(args[0])));
}

static PyObject *
prim_complex_abs(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); COMPLEX(0);
    errno = 0;
    double result = _Py_c_abs(CVAL(args[0]));
    if (errno == ERANGE) {
        lu_err_format(PyExc_OverflowError, "absolute value too large");
        return NULL;
    }
    return PyFloat_FromDouble(result);
}

/* complex_new(cls, real, imag): an instance of cls (complex or a subclass)
   from two floats. */
static PyObject *
prim_complex_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyComplex_Type)
        || !PyFloat_Check(args[1]) || !PyFloat_Check(args[2])) {
        lu_err_format(PyExc_TypeError, "complex_new() needs a complex class and two floats");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[0];
    if (tp == &PyComplex_Type) {
        return PyComplex_FromCComplex((Py_complex){ PyFloat_AS_DOUBLE(args[1]),
                                                    PyFloat_AS_DOUBLE(args[2]) });
    }
    PyObject *r = tp->tp_alloc(tp, 0);
    if (r != NULL) {
        CVAL(r).real = PyFloat_AS_DOUBLE(args[1]);
        CVAL(r).imag = PyFloat_AS_DOUBLE(args[2]);
    }
    return r;
}

/* complex_part(z, i): the real (0) or imaginary (1) part, as a float. */
static PyObject *
prim_complex_part(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); COMPLEX(0);
    return PyFloat_FromDouble(args[1] == Py_False ? CVAL(args[0]).real : CVAL(args[0]).imag);
}

static PyObject *
prim_complex_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); COMPLEX(0);
    Py_uhash_t hashreal = (Py_uhash_t)_Py_HashDouble(args[0], CVAL(args[0]).real);
    Py_uhash_t hashimag = (Py_uhash_t)_Py_HashDouble(args[0], CVAL(args[0]).imag);
    /* an imaginary part of 0 hashes to 0, so hash(x + 0j) == hash(x) */
    Py_uhash_t combined = hashreal + _PyHASH_IMAG * hashimag;
    if (combined == (Py_uhash_t)-1) {
        combined = (Py_uhash_t)-2;
    }
    return PyLong_FromLongLong((Py_hash_t)combined);
}

/* complex_cmp(v, w, op): == and != only; an int compares as a float would. */
static PyObject *
prim_complex_cmp(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long op;
    Py_complex i, j;
    int equal;
    ARGS(3); COMPLEX(0);
    if (lu_long_as_long(args[2], &op) < 0) {
        return NULL;
    }
    if (op != Py_EQ && op != Py_NE) {
        return Py_NewRef(Py_NotImplemented);
    }
    i = CVAL(args[0]);
    if (PyLong_Check(args[1])) {
        if (i.imag == 0.0) {
            return lu_float_richcompare(i.real, args[1], (int)op);
        }
        equal = 0;
    }
    else if (PyFloat_Check(args[1])) {
        equal = i.real == PyFloat_AS_DOUBLE(args[1]) && i.imag == 0.0;
    }
    else if (PyComplex_Check(args[1])) {
        j = CVAL(args[1]);
        equal = i.real == j.real && i.imag == j.imag;
    }
    else {
        return Py_NewRef(Py_NotImplemented);
    }
    return (equal == (op == Py_EQ)) ? Py_True : Py_False;
}

static PyObject *
prim_complex_repr(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    char *re = NULL, *im = NULL;
    const char *lead = "", *tail = "";
    PyObject *result = NULL;
    ARGS(1); COMPLEX(0);
    Py_complex c = CVAL(args[0]);
    if (c.real == 0. && copysign(1.0, c.real) == 1.0) {
        /* Real part is +0: just the imaginary part, without parens. */
        im = PyOS_double_to_string(c.imag, 'r', 0, 0, NULL);
        if (im == NULL) {
            return PyErr_NoMemory();
        }
    }
    else {
        re = PyOS_double_to_string(c.real, 'r', 0, 0, NULL);
        im = PyOS_double_to_string(c.imag, 'r', 0, Py_DTSF_SIGN, NULL);
        if (re == NULL || im == NULL) {
            PyMem_Free(re);
            PyMem_Free(im);
            return PyErr_NoMemory();
        }
        lead = "(";
        tail = ")";
    }
    size_t n = strlen(lead) + (re ? strlen(re) : 0) + strlen(im) + strlen(tail) + 2;
    char *buf = PyMem_Malloc(n);
    if (buf != NULL) {
        snprintf(buf, n, "%s%s%sj%s", lead, re ? re : "", im, tail);
        result = PyUnicode_FromString(buf);
        PyMem_Free(buf);
    }
    else {
        PyErr_NoMemory();
    }
    PyMem_Free(re);
    PyMem_Free(im);
    return result;
}

/* complex_from_text(str): the complex, MISSING for a malformed string, or None
   for a misplaced underscore
   (complex_from_string_inner). */
static PyObject *
prim_complex_from_text(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    double x = 0.0, y = 0.0, z;
    int got_bracket = 0;
    const char *s, *start;
    char *end;
    Py_ssize_t len;
    PyObject *result = Py_NewRef(LU_MISSING);
    ARGS(1);
    char *buf = lu_number_text(args[0], &len);
    if (buf == NULL) {
        Py_DECREF(result);
        return NULL;
    }
    len = lu_strip_underscores(buf, len);
    if (len < 0) {
        Py_DECREF(result);
        result = Py_NewRef(Py_None);        /* a misplaced underscore */
        goto done;
    }
    start = s = buf;
    while (Py_ISSPACE(*s)) {
        s++;
    }
    if (*s == '(') {
        got_bracket = 1;
        s++;
        while (Py_ISSPACE(*s)) {
            s++;
        }
    }
    /* <float> | <float>j | <float><signed-float>j, and for compatibility
       <float><sign>j, <sign>j and j */
    z = PyOS_string_to_double(s, &end, NULL);
    if (z == -1.0 && PyErr_Occurred()) {
        PyErr_Clear();
    }
    if (end != s) {
        s = end;
        if (*s == '+' || *s == '-') {
            x = z;
            y = PyOS_string_to_double(s, &end, NULL);
            if (y == -1.0 && PyErr_Occurred()) {
                PyErr_Clear();
            }
            if (end != s) {
                s = end;
            }
            else {
                y = *s == '+' ? 1.0 : -1.0;
                s++;
            }
            if (!(*s == 'j' || *s == 'J')) {
                goto done;
            }
            s++;
        }
        else if (*s == 'j' || *s == 'J') {
            s++;
            y = z;
        }
        else {
            x = z;
        }
    }
    else {
        if (*s == '+' || *s == '-') {
            y = *s == '+' ? 1.0 : -1.0;
            s++;
        }
        else {
            y = 1.0;
        }
        if (!(*s == 'j' || *s == 'J')) {
            goto done;
        }
        s++;
    }
    while (Py_ISSPACE(*s)) {
        s++;
    }
    if (got_bracket) {
        if (*s != ')') {
            goto done;
        }
        s++;
        while (Py_ISSPACE(*s)) {
            s++;
        }
    }
    if (s - start == len) {
        Py_DECREF(result);
        result = PyComplex_FromCComplex((Py_complex){ x, y });
    }
  done:
    PyMem_Free(buf);
    return result;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_complex_methods[] = {
    FAST(complex_add), FAST(complex_sub), FAST(complex_mul), FAST(complex_truediv),
    FAST(complex_pow), FAST(complex_neg), FAST(complex_abs), FAST(complex_new),
    FAST(complex_part), FAST(complex_hash), FAST(complex_cmp), FAST(complex_repr),
    FAST(complex_from_text),
    { NULL, NULL, 0, NULL },
};
