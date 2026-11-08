/* int: the digit-vector kernels under the kernel's int and bool (DESIGN 4.4,
   scalars). Magnitudes are arrays of 30-bit digits in the cp314t compact
   layout; the algorithms are those of CPython's Objects/longobject.c
   (x_add, x_sub, x_mul, k_mul, x_divrem, long_pow, the shifts, two's
   complement bitwise operations, base conversion both ways,
   long_true_divide and _PyLong_Frexp), rewritten over raw digit arrays so
   that no intermediate is an object. Python semantics (dunders, reflection,
   argument parsing, messages) are lib/kernel/int.py; the ceval fast paths
   call lu_long_binary and its siblings directly. */

#include "prim/prim.h"
#include "opcode.h"

#include <float.h>
#include <math.h>
#include <string.h>

typedef uint64_t twodigits;
typedef int64_t stwodigits;

#define SHIFT PyLong_SHIFT
#define BASE ((digit)PyLong_BASE)
#define MASK ((digit)PyLong_MASK)
#define KARATSUBA_CUTOFF 70
#define DECIMAL_SHIFT 9
#define DECIMAL_BASE ((digit)1000000000)
/* _PY_LONG_MAX_STR_DIGITS_THRESHOLD */
#define STR_DIGITS_THRESHOLD 640
#define MAX_LONG_DIGITS ((INT64_MAX - 1) / SHIFT)

#define TO_INT_ERROR "Exceeds the limit (%d digits) for integer string conversion: value has %zd digits; use sys.set_int_max_str_digits() to increase the limit"
#define TO_STR_ERROR "Exceeds the limit (%d digits) for integer string conversion; use sys.set_int_max_str_digits() to increase the limit"

/* A magnitude and sign borrowed from an int. A zero has no digits. */
typedef struct {
    const digit *d;
    Py_ssize_t n;
    int neg;
} big;

static big
view(PyObject *o)
{
    PyLongObject *v = (PyLongObject *)o;
    uintptr_t tag = v->long_value.lv_tag;
    return (big){ v->long_value.ob_digit, (Py_ssize_t)(tag >> _PyLong_NON_SIZE_BITS),
                  (tag & _PyLong_SIGN_MASK) == 2 };
}

static int
bit_length_digit(digit x)
{
    return x ? 32 - __builtin_clz(x) : 0;
}

static digit *
dalloc(Py_ssize_t n)
{
    digit *p = PyMem_Malloc((n > 0 ? n : 1) * sizeof(digit));
    if (p == NULL) {
        PyErr_NoMemory();
    }
    return p;
}

static digit *
dcalloc(Py_ssize_t n)
{
    digit *p = PyMem_Calloc(n > 0 ? n : 1, sizeof(digit));
    if (p == NULL) {
        PyErr_NoMemory();
    }
    return p;
}

static Py_ssize_t
normalize(const digit *d, Py_ssize_t n)
{
    while (n > 0 && d[n - 1] == 0) {
        n--;
    }
    return n;
}

/* An int from a digit buffer, which it frees. */
static PyObject *
finish(int neg, digit *buf, Py_ssize_t n)
{
    PyObject *r = lu_long_from_digits(neg, n, buf);
    PyMem_Free(buf);
    return r;
}

static PyObject *
too_many_digits(void)
{
    lu_err_format(PyExc_OverflowError, "too many digits in integer");
    return NULL;
}

/* Magnitude kernels. Inputs are normalized. */

static int
mag_cmp(const digit *a, Py_ssize_t na, const digit *b, Py_ssize_t nb)
{
    if (na != nb) {
        return na < nb ? -1 : 1;
    }
    while (--na >= 0) {
        if (a[na] != b[na]) {
            return a[na] < b[na] ? -1 : 1;
        }
    }
    return 0;
}

/* z[max(na, nb) + 1] = a + b; the normalized length. */
static Py_ssize_t
mag_add(const digit *a, Py_ssize_t na, const digit *b, Py_ssize_t nb, digit *z)
{
    if (na < nb) {
        const digit *t = a; a = b; b = t;
        Py_ssize_t tn = na; na = nb; nb = tn;
    }
    digit carry = 0;
    Py_ssize_t i;
    for (i = 0; i < nb; i++) {
        carry += a[i] + b[i];
        z[i] = carry & MASK;
        carry >>= SHIFT;
    }
    for (; i < na; i++) {
        carry += a[i];
        z[i] = carry & MASK;
        carry >>= SHIFT;
    }
    z[i] = carry;
    return normalize(z, na + 1);
}

/* z[na] = a - b, for a >= b; the normalized length. */
static Py_ssize_t
mag_sub(const digit *a, Py_ssize_t na, const digit *b, Py_ssize_t nb, digit *z)
{
    digit borrow = 0;
    Py_ssize_t i;
    for (i = 0; i < nb; i++) {
        borrow = a[i] - b[i] - borrow;
        z[i] = borrow & MASK;
        borrow = (borrow >> SHIFT) & 1;
    }
    for (; i < na; i++) {
        borrow = a[i] - borrow;
        z[i] = borrow & MASK;
        borrow = (borrow >> SHIFT) & 1;
    }
    return normalize(z, na);
}

/* x[0:m] += y[0:n], m >= n; the carry out. */
static digit
v_iadd(digit *x, Py_ssize_t m, const digit *y, Py_ssize_t n)
{
    digit carry = 0;
    Py_ssize_t i;
    for (i = 0; i < n; i++) {
        carry += x[i] + y[i];
        x[i] = carry & MASK;
        carry >>= SHIFT;
    }
    for (; carry && i < m; i++) {
        carry += x[i];
        x[i] = carry & MASK;
        carry >>= SHIFT;
    }
    return carry;
}

static digit
v_isub(digit *x, Py_ssize_t m, const digit *y, Py_ssize_t n)
{
    digit borrow = 0;
    Py_ssize_t i;
    for (i = 0; i < n; i++) {
        borrow = x[i] - y[i] - borrow;
        x[i] = borrow & MASK;
        borrow = (borrow >> SHIFT) & 1;
    }
    for (; borrow && i < m; i++) {
        borrow = x[i] - borrow;
        x[i] = borrow & MASK;
        borrow = (borrow >> SHIFT) & 1;
    }
    return borrow;
}

/* z[0:m] = a[0:m] << d, 0 <= d < SHIFT; the bits shifted out. */
static digit
v_lshift(digit *z, const digit *a, Py_ssize_t m, int d)
{
    digit carry = 0;
    for (Py_ssize_t i = 0; i < m; i++) {
        twodigits acc = (twodigits)a[i] << d | carry;
        z[i] = (digit)acc & MASK;
        carry = (digit)(acc >> SHIFT);
    }
    return carry;
}

static digit
v_rshift(digit *z, const digit *a, Py_ssize_t m, int d)
{
    digit carry = 0;
    digit mask = ((digit)1 << d) - 1;
    for (Py_ssize_t i = m; --i >= 0;) {
        twodigits acc = (twodigits)carry << SHIFT | a[i];
        carry = (digit)acc & mask;
        z[i] = (digit)(acc >> d);
    }
    return carry;
}

/* z[0:na+nb] += a * b, schoolbook; z is zeroed by the caller. */
static void
x_mul(const digit *a, Py_ssize_t na, const digit *b, Py_ssize_t nb, digit *z)
{
    for (Py_ssize_t i = 0; i < na; i++) {
        twodigits carry = 0;
        twodigits f = a[i];
        digit *pz = z + i;
        for (Py_ssize_t j = 0; j < nb; j++) {
            carry += pz[j] + b[j] * f;
            pz[j] = (digit)(carry & MASK);
            carry >>= SHIFT;
        }
        if (carry) {
            pz[nb] += (digit)(carry & MASK);
        }
    }
}

static int k_mul(const digit *a, Py_ssize_t na, const digit *b, Py_ssize_t nb, digit *z);

/* b much longer than a: multiply a by na-digit slices of b. */
static int
k_lopsided_mul(const digit *a, Py_ssize_t na, const digit *b, Py_ssize_t nb, digit *z)
{
    digit *prod = dalloc(2 * na);
    if (prod == NULL) {
        return -1;
    }
    for (Py_ssize_t done = 0; done < nb;) {
        Py_ssize_t size = nb - done < na ? nb - done : na;
        Py_ssize_t n = normalize(b + done, size);
        if (n > 0) {
            memset(prod, 0, (na + n) * sizeof(digit));
            if (k_mul(a, na, b + done, n, prod) < 0) {
                PyMem_Free(prod);
                return -1;
            }
            v_iadd(z + done, na + nb - done, prod, na + n);
        }
        done += size;
    }
    PyMem_Free(prod);
    return 0;
}

/* z[0:na+nb] += a * b by Karatsuba; z is zeroed by the caller. */
static int
k_mul(const digit *a, Py_ssize_t na, const digit *b, Py_ssize_t nb, digit *z)
{
    if (na > nb) {
        const digit *t = a; a = b; b = t;
        Py_ssize_t tn = na; na = nb; nb = tn;
    }
    if (na <= KARATSUBA_CUTOFF) {
        x_mul(a, na, b, nb, z);
        return 0;
    }
    if (2 * na <= nb) {
        return k_lopsided_mul(a, na, b, nb, z);
    }
    Py_ssize_t shift = nb >> 1;
    Py_ssize_t nal = normalize(a, shift), nbl = normalize(b, shift);
    Py_ssize_t nah = na - shift, nbh = nb - shift;
    /* a = ah * B**shift + al, b likewise: ah*bh and al*bl land in z. */
    if (k_mul(a + shift, nah, b + shift, nbh, z + 2 * shift) < 0) {
        return -1;
    }
    if (nal > 0 && nbl > 0 && k_mul(a, nal, b, nbl, z) < 0) {
        return -1;
    }
    Py_ssize_t nsa = (nah > nal ? nah : nal) + 1, nsb = (nbh > nbl ? nbh : nbl) + 1;
    digit *tmp = dcalloc(nsa + nsb + nsa + nsb);
    if (tmp == NULL) {
        return -1;
    }
    digit *sa = tmp, *sb = tmp + nsa, *t3 = tmp + nsa + nsb;
    nsa = mag_add(a + shift, nah, a, nal, sa);
    nsb = mag_add(b + shift, nbh, b, nbl, sb);
    int err = k_mul(sa, nsa, sb, nsb, t3);
    if (err == 0) {
        Py_ssize_t n3 = nsa + nsb;
        v_isub(t3, n3, z + 2 * shift, nah + nbh);
        v_isub(t3, n3, z, nal + nbl);
        v_iadd(z + shift, na + nb - shift, t3, normalize(t3, n3));
    }
    PyMem_Free(tmp);
    return err;
}

/* q[size] = in / n, in place allowed; the remainder. */
static digit
inplace_divrem1(digit *pout, const digit *pin, Py_ssize_t size, digit n)
{
    twodigits rem = 0;
    while (--size >= 0) {
        twodigits dividend = (rem << SHIFT) | pin[size];
        digit q = (digit)(dividend / n);
        rem = dividend - (twodigits)q * n;
        pout[size] = q;
    }
    return (digit)rem;
}

/* Knuth's algorithm D: v / w for size_w >= 2 and size_v >= size_w. The
   quotient (size_v - size_w + 1 digits) and the remainder (size_w digits)
   are written to q and r. */
static int
x_divrem(const digit *v1, Py_ssize_t size_v, const digit *w1, Py_ssize_t size_w,
         digit *q, digit *r)
{
    int d = SHIFT - bit_length_digit(w1[size_w - 1]);
    digit *v = dalloc(size_v + 1), *w = dalloc(size_w);
    if (v == NULL || w == NULL) {
        PyMem_Free(v);
        PyMem_Free(w);
        return -1;
    }
    v_lshift(w, w1, size_w, d);
    digit carry = v_lshift(v, v1, size_v, d);
    if (carry != 0 || v[size_v - 1] >= w[size_w - 1]) {
        v[size_v++] = carry;
    }
    Py_ssize_t k = size_v - size_w;
    digit wm1 = w[size_w - 1], wm2 = w[size_w - 2];
    digit *ak = q + k;
    for (digit *vk = v + k; vk-- > v;) {
        digit vtop = vk[size_w];
        twodigits vv = ((twodigits)vtop << SHIFT) | vk[size_w - 1];
        digit qd = (digit)(vv / wm1);
        digit rd = (digit)(vv - (twodigits)wm1 * qd);
        while ((twodigits)wm2 * qd > (((twodigits)rd << SHIFT) | vk[size_w - 2])) {
            --qd;
            rd += wm1;
            if (rd >= BASE) {
                break;
            }
        }
        stwodigits zhi = 0;
        for (Py_ssize_t i = 0; i < size_w; i++) {
            stwodigits z = (stwodigits)vk[i] + zhi - (stwodigits)qd * (stwodigits)w[i];
            vk[i] = (digit)z & MASK;
            zhi = z >> SHIFT;
        }
        if ((stwodigits)vtop + zhi < 0) {
            carry = 0;
            for (Py_ssize_t i = 0; i < size_w; i++) {
                carry += vk[i] + w[i];
                vk[i] = carry & MASK;
                carry >>= SHIFT;
            }
            --qd;
        }
        *--ak = qd;
    }
    v_rshift(r, v, size_w, d);
    PyMem_Free(v);
    PyMem_Free(w);
    return 0;
}

/* Truncating division of magnitudes: new digit buffers *q (nq digits) and
   *r (nr digits), both normalized and caller-freed. b is nonzero. */
static int
mag_divrem(big a, big b, digit **q, Py_ssize_t *nq, digit **r, Py_ssize_t *nr)
{
    if (mag_cmp(a.d, a.n, b.d, b.n) < 0) {
        *q = dcalloc(1);
        *r = dalloc(a.n);
        if (*q == NULL || *r == NULL) {
            PyMem_Free(*q);
            PyMem_Free(*r);
            return -1;
        }
        memcpy(*r, a.d, a.n * sizeof(digit));
        *nq = 0;
        *nr = a.n;
        return 0;
    }
    if (b.n == 1) {
        *q = dalloc(a.n);
        *r = dalloc(1);
        if (*q == NULL || *r == NULL) {
            PyMem_Free(*q);
            PyMem_Free(*r);
            return -1;
        }
        (*r)[0] = inplace_divrem1(*q, a.d, a.n, b.d[0]);
        *nq = normalize(*q, a.n);
        *nr = (*r)[0] ? 1 : 0;
        return 0;
    }
    *q = dcalloc(a.n - b.n + 1);
    *r = dalloc(b.n);
    if (*q == NULL || *r == NULL || x_divrem(a.d, a.n, b.d, b.n, *q, *r) < 0) {
        PyMem_Free(*q);
        PyMem_Free(*r);
        return -1;
    }
    *nq = normalize(*q, a.n - b.n + 1);
    *nr = normalize(*r, b.n);
    return 0;
}

/* Operations on ints. Operands are PyLong objects of any subtype; results
   are exact ints. */

static PyObject *
zero(void)
{
    return lu_small_int(0);
}

static PyObject *
long_add_signed(big a, big b)
{
    Py_ssize_t cap = (a.n > b.n ? a.n : b.n) + 1;
    digit *z = dalloc(cap);
    if (z == NULL) {
        return NULL;
    }
    int neg = a.neg;
    Py_ssize_t n;
    if (a.neg == b.neg) {
        n = mag_add(a.d, a.n, b.d, b.n, z);
    }
    else if (mag_cmp(a.d, a.n, b.d, b.n) >= 0) {
        n = mag_sub(a.d, a.n, b.d, b.n, z);
    }
    else {
        neg = b.neg;
        n = mag_sub(b.d, b.n, a.d, a.n, z);
    }
    return finish(n ? neg : 0, z, n);
}

static PyObject *
long_neg_of(big a, int neg)
{
    digit *z = dalloc(a.n);
    if (z == NULL) {
        return NULL;
    }
    memcpy(z, a.d, a.n * sizeof(digit));
    return finish(neg, z, a.n);
}

static int
both_compact(PyObject *a, PyObject *b)
{
    return _PyLong_IsCompact((PyLongObject *)a) && _PyLong_IsCompact((PyLongObject *)b);
}

static stwodigits
compact(PyObject *a)
{
    return _PyLong_CompactValue((PyLongObject *)a);
}

PyObject *
lu_long_add(PyObject *a, PyObject *b)
{
    if (both_compact(a, b)) {
        return PyLong_FromLongLong(compact(a) + compact(b));
    }
    return long_add_signed(view(a), view(b));
}

PyObject *
lu_long_sub(PyObject *a, PyObject *b)
{
    if (both_compact(a, b)) {
        return PyLong_FromLongLong(compact(a) - compact(b));
    }
    big nb = view(b);
    nb.neg = !nb.neg;
    return long_add_signed(view(a), nb);
}

PyObject *
lu_long_mul(PyObject *a, PyObject *b)
{
    if (both_compact(a, b)) {
        return PyLong_FromLongLong(compact(a) * compact(b));
    }
    big x = view(a), y = view(b);
    if (x.n == 0 || y.n == 0) {
        return zero();
    }
    if (x.n + y.n > MAX_LONG_DIGITS) {
        return too_many_digits();
    }
    digit *z = dcalloc(x.n + y.n);
    if (z == NULL || k_mul(x.d, x.n, y.d, y.n, z) < 0) {
        PyMem_Free(z);
        return NULL;
    }
    return finish(x.neg != y.neg, z, x.n + y.n);
}

static PyObject *
zero_division(void)
{
    lu_err_format(PyExc_ZeroDivisionError, "division by zero");
    return NULL;
}

/* Floor division and modulo: *pq and *pr are new references or NULL when
   not wanted. */
static int
long_divmod(PyObject *a, PyObject *b, PyObject **pq, PyObject **pr)
{
    big x = view(a), y = view(b);
    if (y.n == 0) {
        zero_division();
        return -1;
    }
    if (both_compact(a, b)) {
        stwodigits p = compact(a), d = compact(b);
        stwodigits q = p / d, r = p % d;
        if (r != 0 && ((r < 0) != (d < 0))) {
            q--;
            r += d;
        }
        if (pq != NULL) *pq = PyLong_FromLongLong(q);
        if (pr != NULL) *pr = PyLong_FromLongLong(r);
        return 0;
    }
    digit *q, *r;
    Py_ssize_t nq, nr;
    if (mag_divrem(x, y, &q, &nq, &r, &nr) < 0) {
        return -1;
    }
    PyObject *quo = lu_long_from_digits(x.neg != y.neg, nq, q);
    PyObject *rem = lu_long_from_digits(x.neg, nr, r);
    PyMem_Free(q);
    PyMem_Free(r);
    if (quo != NULL && rem != NULL && nr != 0 && x.neg != y.neg) {
        /* Truncated to floored: the remainder takes the divisor's sign. */
        PyObject *one = lu_small_int(1);
        PyObject *q2 = lu_long_sub(quo, one);
        PyObject *r2 = lu_long_add(rem, b);
        Py_SETREF(quo, q2);
        Py_SETREF(rem, r2);
    }
    if (quo == NULL || rem == NULL) {
        Py_XDECREF(quo);
        Py_XDECREF(rem);
        return -1;
    }
    if (pq != NULL) *pq = quo; else Py_DECREF(quo);
    if (pr != NULL) *pr = rem; else Py_DECREF(rem);
    return 0;
}

PyObject *
lu_long_floordiv(PyObject *a, PyObject *b)
{
    PyObject *q;
    return long_divmod(a, b, &q, NULL) < 0 ? NULL : q;
}

PyObject *
lu_long_mod(PyObject *a, PyObject *b)
{
    PyObject *r;
    return long_divmod(a, b, NULL, &r) < 0 ? NULL : r;
}

static PyObject *
long_divmod_pair(PyObject *a, PyObject *b)
{
    PyObject *q, *r;
    if (long_divmod(a, b, &q, &r) < 0) {
        return NULL;
    }
    PyObject *t = PyTuple_New(2);
    if (t == NULL) {
        Py_DECREF(q);
        Py_DECREF(r);
        return NULL;
    }
    PyTuple_SET_ITEM(t, 0, q);
    PyTuple_SET_ITEM(t, 1, r);
    return t;
}

PyObject *
lu_long_negate(PyObject *a)
{
    if (_PyLong_IsCompact((PyLongObject *)a)) {
        return PyLong_FromLongLong(-compact(a));
    }
    big x = view(a);
    return long_neg_of(x, !x.neg);
}

PyObject *
lu_long_invert(PyObject *a)
{
    /* ~a == -(a + 1) */
    PyObject *t = lu_long_add(a, lu_small_int(1));
    PyObject *r = t != NULL ? lu_long_negate(t) : NULL;
    Py_XDECREF(t);
    return r;
}

static PyObject *
long_abs(PyObject *a)
{
    big x = view(a);
    return long_neg_of(x, 0);
}

int
lu_long_compare(PyObject *a, PyObject *b)
{
    big x = view(a), y = view(b);
    if (x.neg != y.neg) {
        return x.neg ? -1 : 1;
    }
    int c = mag_cmp(x.d, x.n, y.d, y.n);
    return x.neg ? -c : c;
}

/* Shifts and bitwise operations write the result's digits in place: no
   scratch copy of an operand. Operands of at most two digits (|v| < 2**60)
   take machine words. */

/* A value of at most two digits. */
static int64_t
word(big a)
{
    int64_t v = a.n == 0 ? 0 : a.n == 1 ? (int64_t)a.d[0] : (int64_t)a.d[0] | (int64_t)a.d[1] << SHIFT;
    return a.neg ? -v : v;
}

/* An int allocated with n digits and filled: its normalized length and sign
   set, or the cached small int that equals it. */
static PyObject *
settle(PyLongObject *v, int neg, Py_ssize_t n)
{
    n = normalize(v->long_value.ob_digit, n);
    if (n <= 1) {
        long w = n ? (long)v->long_value.ob_digit[0] : 0;
        w = neg ? -w : w;
        if (w >= -LU_NSMALLNEGINTS && w < LU_NSMALLPOSINTS) {
            Py_DECREF(v);
            return lu_small_int((int)w);
        }
    }
    v->long_value.lv_tag = ((uintptr_t)n << _PyLong_NON_SIZE_BITS) | (uintptr_t)(n == 0 ? 1 : neg ? 2 : 0);
    return (PyObject *)v;
}

/* a << n for n >= 0. */
static PyObject *
long_lshift(PyObject *a, PyObject *count)
{
    big x = view(a);
    if (x.n == 0) {
        return zero();
    }
    long n;
    if (lu_long_as_long(count, &n) < 0) {
        PyErr_Clear();
        return too_many_digits();
    }
    if (x.n <= 2 && n < 64) {
        int64_t w = word(x);
        uint64_t m = w < 0 ? 0 - (uint64_t)w : (uint64_t)w;
        if (64 - __builtin_clzll(m) + n <= 62) {
            return PyLong_FromLongLong((long long)(m << n) * (w < 0 ? -1 : 1));
        }
    }
    if (n > (long)MAX_LONG_DIGITS * SHIFT) {
        return too_many_digits();
    }
    Py_ssize_t wordshift = n / SHIFT;
    PyLongObject *r = lu_long_alloc(x.n + wordshift + 1);
    if (r == NULL) {
        return NULL;
    }
    digit *z = r->long_value.ob_digit;
    memset(z, 0, wordshift * sizeof(digit));
    z[x.n + wordshift] = v_lshift(z + wordshift, x.d, x.n, (int)(n % SHIFT));
    return settle(r, x.neg, x.n + wordshift + 1);
}

/* a >> n for n >= 0, rounding toward negative infinity: for a negative a,
   -ceil(|a| / 2**n), one more than the shifted magnitude when a bit set in
   |a| is shifted out. */
static PyObject *
long_rshift(PyObject *a, PyObject *count)
{
    big x = view(a);
    long n = 0;
    int huge = lu_long_as_long(count, &n) < 0;
    PyErr_Clear();
    if (x.n <= 2) {
        return PyLong_FromLongLong(word(x) >> (huge || n > 63 ? 63 : n));
    }
    Py_ssize_t wordshift = huge ? x.n : n / SHIFT;
    int remshift = huge ? 0 : (int)(n % SHIFT);
    if (wordshift >= x.n) {
        return PyLong_FromLong(x.neg ? -1 : 0);
    }
    Py_ssize_t keep = x.n - wordshift;
    PyLongObject *r = lu_long_alloc(keep + 1);
    if (r == NULL) {
        return NULL;
    }
    digit *z = r->long_value.ob_digit;
    digit lost = v_rshift(z, x.d + wordshift, keep, remshift);
    z[keep] = 0;
    if (x.neg) {
        for (Py_ssize_t i = 0; i < wordshift && !lost; i++) {
            lost = x.d[i];
        }
        if (lost) {
            v_iadd(z, keep + 1, (const digit[]){1}, 1);
        }
    }
    return settle(r, x.neg, keep + 1);
}

/* Digit i of a's two's complement, sign-extended without end: for a
   negative a, ~(|a| - 1), whose borrow *borrow carries (1 at digit 0). */
static inline digit
twos_digit(big a, Py_ssize_t i, digit *borrow)
{
    digit d = i < a.n ? a.d[i] : 0;
    if (!a.neg) {
        return d;
    }
    d -= *borrow;
    *borrow = d >> (8 * sizeof(digit) - 1);
    return ~d & MASK;
}

static PyObject *
long_bitwise(int op, PyObject *a, PyObject *b)
{
    big x = view(a), y = view(b);
    if (x.n <= 2 && y.n <= 2) {
        int64_t u = word(x), v = word(y);
        return PyLong_FromLongLong(op == NB_AND ? u & v : op == NB_OR ? u | v : u ^ v);
    }
    int neg = op == NB_AND ? x.neg & y.neg : op == NB_OR ? x.neg | y.neg : x.neg ^ y.neg;
    /* A non-negative operand of &, or both of |, bound the result's length. */
    Py_ssize_t size = x.n > y.n ? x.n : y.n;
    if (op == NB_AND && !x.neg && (y.neg || x.n < y.n)) {
        size = x.n;
    }
    else if (op == NB_AND && !y.neg) {
        size = y.n;
    }
    PyLongObject *r = lu_long_alloc(size + 1);
    if (r == NULL) {
        return NULL;
    }
    digit *z = r->long_value.ob_digit;
    digit bx = 1, by = 1, carry = 1;
    for (Py_ssize_t i = 0; i < size; i++) {
        digit u = twos_digit(x, i, &bx), v = twos_digit(y, i, &by);
        digit w = op == NB_AND ? u & v : op == NB_OR ? u | v : u ^ v;
        if (neg) {
            /* the magnitude of a negative result: ~w + 1 */
            w = (~w & MASK) + carry;
            carry = w >> SHIFT;
            w &= MASK;
        }
        z[i] = w;
    }
    z[size] = neg ? carry : 0;
    return settle(r, neg, size + 1);
}

/* pow */

static PyObject *
long_invmod(PyObject *a, PyObject *n)
{
    /* n > 0. Extended Euclid on (n, a mod n) tracking the cofactor of a. */
    PyObject *r0 = Py_NewRef(n), *r1 = lu_long_mod(a, n);
    PyObject *t0 = zero(), *t1 = lu_small_int(1);
    while (r1 != NULL && view(r1).n != 0) {
        PyObject *q = lu_long_floordiv(r0, r1);
        PyObject *qr = q != NULL ? lu_long_mul(q, r1) : NULL;
        PyObject *qt = q != NULL ? lu_long_mul(q, t1) : NULL;
        PyObject *r2 = qr != NULL ? lu_long_sub(r0, qr) : NULL;
        PyObject *t2 = qt != NULL ? lu_long_sub(t0, qt) : NULL;
        Py_XDECREF(q);
        Py_XDECREF(qr);
        Py_XDECREF(qt);
        Py_DECREF(r0);
        Py_DECREF(t0);
        r0 = r1;
        t0 = t1;
        r1 = r2;
        t1 = t2;
        if (r2 == NULL || t2 == NULL) {
            Py_XDECREF(r0); Py_XDECREF(r1); Py_XDECREF(t0); Py_XDECREF(t1);
            return NULL;
        }
    }
    Py_XDECREF(r1);
    Py_XDECREF(t1);
    PyObject *result = NULL;
    if (r1 != NULL && lu_long_compare(r0, lu_small_int(1)) == 0) {
        result = lu_long_mod(t0, n);
    }
    else if (r1 != NULL) {
        lu_err_format(PyExc_ValueError, "base is not invertible for the given modulus");
    }
    Py_DECREF(r0);
    Py_DECREF(t0);
    return result;
}

/* float_pow for an int exponent that is negative: the Python float result. */
static PyObject *
long_pow_float(PyObject *a, PyObject *b)
{
    double x = lu_long_as_double(a), y;
    if (x == -1.0 && PyErr_Occurred()) {
        return NULL;
    }
    y = lu_long_as_double(b);
    if (y == -1.0 && PyErr_Occurred()) {
        return NULL;
    }
    if (x == 0.0) {
        lu_err_format(PyExc_ZeroDivisionError, "zero to a negative power");
        return NULL;
    }
    return PyFloat_FromDouble(pow(x, y));
}

/* a ** b % c, c a nonzero int or NULL. */
PyObject *
lu_long_pow(PyObject *a, PyObject *b, PyObject *c)
{
    big y = view(b);
    PyObject *base, *mod = NULL, *z = NULL;
    int negative_output = 0;
    if (c != NULL) {
        big m = view(c);
        if (m.n == 0) {
            lu_err_format(PyExc_ValueError, "pow() 3rd argument cannot be 0");
            return NULL;
        }
        mod = m.neg ? lu_long_negate(c) : Py_NewRef(c);
        negative_output = m.neg;
        if (mod == NULL) {
            return NULL;
        }
        if (lu_long_compare(mod, lu_small_int(1)) == 0) {
            Py_DECREF(mod);
            return zero();
        }
    }
    else if (y.neg) {
        return long_pow_float(a, b);
    }
    PyObject *exp = y.neg ? lu_long_negate(b) : Py_NewRef(b);
    if (exp == NULL) {
        Py_XDECREF(mod);
        return NULL;
    }
    base = Py_NewRef(a);
    if (mod != NULL) {
        PyObject *r = y.neg ? long_invmod(a, mod) : lu_long_mod(a, mod);
        Py_SETREF(base, r);
        if (base == NULL) {
            goto done;
        }
    }
    z = lu_small_int(1);
    Py_INCREF(z);
    big e = view(exp);
    /* Left to right over the exponent's bits. */
    for (Py_ssize_t i = e.n * SHIFT - 1; i >= 0; i--) {
        PyObject *sq = lu_long_mul(z, z);
        Py_SETREF(z, sq);
        if (z != NULL && mod != NULL) {
            PyObject *r = lu_long_mod(z, mod);
            Py_SETREF(z, r);
        }
        if (z != NULL && (e.d[i / SHIFT] >> (i % SHIFT)) & 1) {
            PyObject *m = lu_long_mul(z, base);
            Py_SETREF(z, m);
            if (z != NULL && mod != NULL) {
                PyObject *r = lu_long_mod(z, mod);
                Py_SETREF(z, r);
            }
        }
        if (z == NULL) {
            goto done;
        }
    }
    if (negative_output && view(z).n != 0) {
        PyObject *r = lu_long_sub(z, mod);
        Py_SETREF(z, r);
    }
done:
    Py_XDECREF(base);
    Py_DECREF(exp);
    Py_XDECREF(mod);
    return z;
}

/* Floats. */

/* _PyLong_Frexp: a = x * 2**e with 0.5 <= |x| < 1, correctly rounded. */
static double
long_frexp(big a, int64_t *e)
{
    static const int half_even_correction[8] = { 0, -1, -2, 1, 0, -1, 2, 1 };
    digit x_digits[2 + (DBL_MANT_DIG + 1) / SHIFT] = { 0 };
    if (a.n == 0) {
        *e = 0;
        return 0.0;
    }
    int64_t a_bits = (int64_t)(a.n - 1) * SHIFT + bit_length_digit(a.d[a.n - 1]);
    Py_ssize_t x_size = 0;
    if (a_bits <= DBL_MANT_DIG + 2) {
        Py_ssize_t shift_digits = (DBL_MANT_DIG + 2 - a_bits) / SHIFT;
        int shift_bits = (int)((DBL_MANT_DIG + 2 - a_bits) % SHIFT);
        x_size = shift_digits;
        digit rem = v_lshift(x_digits + x_size, a.d, a.n, shift_bits);
        x_size += a.n;
        x_digits[x_size++] = rem;
    }
    else {
        Py_ssize_t shift_digits = (a_bits - DBL_MANT_DIG - 2) / SHIFT;
        int shift_bits = (int)((a_bits - DBL_MANT_DIG - 2) % SHIFT);
        digit rem = v_rshift(x_digits, a.d + shift_digits, a.n - shift_digits, shift_bits);
        x_size = a.n - shift_digits;
        /* Make the lowest bit sticky for the rounding below. */
        if (rem) {
            x_digits[0] |= 1;
        }
        else {
            while (shift_digits > 0) {
                if (a.d[--shift_digits]) {
                    x_digits[0] |= 1;
                    break;
                }
            }
        }
    }
    x_digits[0] += half_even_correction[x_digits[0] & 7];
    double dx = x_digits[--x_size];
    while (x_size > 0) {
        dx = dx * BASE + x_digits[--x_size];
    }
    dx /= 4.0 * 9007199254740992.0;       /* 4 * 2**DBL_MANT_DIG */
    if (dx == 1.0) {
        dx = 0.5;
        a_bits += 1;
    }
    *e = a_bits;
    return a.neg ? -dx : dx;
}

/* PyLong_AsDouble; -1.0 with OverflowError set when too large. */
double
lu_long_as_double(PyObject *v)
{
    PyLongObject *a = (PyLongObject *)v;
    if (_PyLong_IsCompact(a)) {
        return (double)_PyLong_CompactValue(a);
    }
    int64_t e;
    double x = long_frexp(view(v), &e);
    if (e > DBL_MAX_EXP) {
        lu_err_format(PyExc_OverflowError, "int too large to convert to float");
        return -1.0;
    }
    return ldexp(x, (int)e);
}

PyObject *
lu_long_from_double(double dval)
{
    if (isinf(dval)) {
        lu_err_format(PyExc_OverflowError, "cannot convert float infinity to integer");
        return NULL;
    }
    if (isnan(dval)) {
        lu_err_format(PyExc_ValueError, "cannot convert float NaN to integer");
        return NULL;
    }
    int neg = dval < 0;
    if (neg) {
        dval = -dval;
    }
    int expo;
    double frac = frexp(dval, &expo);
    if (expo <= 0) {
        return zero();
    }
    Py_ssize_t ndig = (expo - 1) / SHIFT + 1;
    digit *d = dalloc(ndig);
    if (d == NULL) {
        return NULL;
    }
    frac = ldexp(frac, (expo - 1) % SHIFT + 1);
    for (Py_ssize_t i = ndig; --i >= 0;) {
        digit bits = (digit)frac;
        d[i] = bits;
        frac = ldexp(frac - (double)bits, SHIFT);
    }
    return finish(neg, d, ndig);
}

#define MANT_DIG_DIGITS (DBL_MANT_DIG / SHIFT)
#define MANT_DIG_BITS (DBL_MANT_DIG % SHIFT)

/* long_true_divide: a / b correctly rounded. */
PyObject *
lu_long_truediv(PyObject *v, PyObject *w)
{
    big a = view(v), b = view(w);
    Py_ssize_t a_size = a.n, b_size = b.n;
    int negate = a.neg != b.neg;
    double result;
    if (b_size == 0) {
        return zero_division();
    }
    if (a_size == 0) {
        goto underflow_or_zero;
    }
    int a_is_small = a_size <= MANT_DIG_DIGITS
        || (a_size == MANT_DIG_DIGITS + 1 && a.d[MANT_DIG_DIGITS] >> MANT_DIG_BITS == 0);
    int b_is_small = b_size <= MANT_DIG_DIGITS
        || (b_size == MANT_DIG_DIGITS + 1 && b.d[MANT_DIG_DIGITS] >> MANT_DIG_BITS == 0);
    if (a_is_small && b_is_small) {
        double da = a.d[--a_size];
        while (a_size > 0) {
            da = da * BASE + a.d[--a_size];
        }
        double db = b.d[--b_size];
        while (b_size > 0) {
            db = db * BASE + b.d[--b_size];
        }
        result = da / db;
        goto success;
    }
    Py_ssize_t diff = a_size - b_size;
    if (diff > PY_SSIZE_T_MAX / SHIFT - 1) {
        goto overflow;
    }
    else if (diff < 1 - PY_SSIZE_T_MAX / SHIFT) {
        goto underflow_or_zero;
    }
    diff = diff * SHIFT + bit_length_digit(a.d[a_size - 1]) - bit_length_digit(b.d[b_size - 1]);
    if (diff > DBL_MAX_EXP) {
        goto overflow;
    }
    else if (diff < DBL_MIN_EXP - DBL_MANT_DIG - 1) {
        goto underflow_or_zero;
    }
    Py_ssize_t shift = (diff > DBL_MIN_EXP ? diff : DBL_MIN_EXP) - DBL_MANT_DIG - 2;
    int inexact = 0;
    digit *x;
    Py_ssize_t x_size;
    if (shift <= 0) {
        Py_ssize_t shift_digits = -shift / SHIFT;
        x = dcalloc(a_size + shift_digits + 1);
        if (x == NULL) {
            return NULL;
        }
        x[a_size + shift_digits] = v_lshift(x + shift_digits, a.d, a_size, (int)(-shift % SHIFT));
        x_size = a_size + shift_digits + 1;
    }
    else {
        Py_ssize_t shift_digits = shift / SHIFT;
        x = dalloc(a_size - shift_digits);
        if (x == NULL) {
            return NULL;
        }
        if (v_rshift(x, a.d + shift_digits, a_size - shift_digits, (int)(shift % SHIFT))) {
            inexact = 1;
        }
        while (!inexact && shift_digits > 0) {
            if (a.d[--shift_digits]) {
                inexact = 1;
            }
        }
        x_size = a_size - shift / SHIFT;
    }
    x_size = normalize(x, x_size);
    /* x //= b; a nonzero remainder makes the result inexact. */
    if (b_size == 1) {
        if (inplace_divrem1(x, x, x_size, b.d[0])) {
            inexact = 1;
        }
        x_size = normalize(x, x_size);
    }
    else {
        /* The shift makes x at least b. */
        digit *q = dcalloc(x_size - b_size + 1), *r = dalloc(b_size);
        if (q == NULL || r == NULL || x_divrem(x, x_size, b.d, b_size, q, r) < 0) {
            PyMem_Free(q); PyMem_Free(r); PyMem_Free(x);
            return NULL;
        }
        if (normalize(r, b_size) != 0) {
            inexact = 1;
        }
        PyMem_Free(r);
        PyMem_Free(x);
        x = q;
        x_size = normalize(q, x_size - b_size + 1);
    }
    Py_ssize_t x_bits = (x_size - 1) * SHIFT + bit_length_digit(x[x_size - 1]);
    Py_ssize_t extra_bits = (x_bits > DBL_MIN_EXP - shift ? x_bits : DBL_MIN_EXP - shift) - DBL_MANT_DIG;
    digit mask = (digit)1 << (extra_bits - 1);
    digit low = x[0] | inexact;
    if ((low & mask) && (low & (3U * mask - 1U))) {
        low += mask;
    }
    x[0] = low & ~(2U * mask - 1U);
    double dx = x[--x_size];
    while (x_size > 0) {
        dx = dx * BASE + x[--x_size];
    }
    PyMem_Free(x);
    if (shift + x_bits >= DBL_MAX_EXP
        && (shift + x_bits > DBL_MAX_EXP || dx == ldexp(1.0, (int)x_bits))) {
        goto overflow;
    }
    result = ldexp(dx, (int)shift);
success:
    return PyFloat_FromDouble(negate ? -result : result);
underflow_or_zero:
    return PyFloat_FromDouble(negate ? -0.0 : 0.0);
overflow:
    lu_err_format(PyExc_OverflowError, "integer division result too large for a float");
    return NULL;
}

/* The binary operators of exact ints: the BINARY_OP fast path. NotImplemented
   for the operators that need the kernel (power, true division, @). */
PyObject *
lu_long_binary(int op, PyObject *a, PyObject *b)
{
    switch (op) {
    case NB_ADD: case NB_INPLACE_ADD: return lu_long_add(a, b);
    case NB_SUBTRACT: case NB_INPLACE_SUBTRACT: return lu_long_sub(a, b);
    case NB_MULTIPLY: case NB_INPLACE_MULTIPLY: return lu_long_mul(a, b);
    case NB_FLOOR_DIVIDE: case NB_INPLACE_FLOOR_DIVIDE: return lu_long_floordiv(a, b);
    case NB_REMAINDER: case NB_INPLACE_REMAINDER: return lu_long_mod(a, b);
    case NB_AND: case NB_INPLACE_AND: return long_bitwise(NB_AND, a, b);
    case NB_OR: case NB_INPLACE_OR: return long_bitwise(NB_OR, a, b);
    case NB_XOR: case NB_INPLACE_XOR: return long_bitwise(NB_XOR, a, b);
    case NB_LSHIFT: case NB_INPLACE_LSHIFT:
    case NB_RSHIFT: case NB_INPLACE_RSHIFT:
        if (view(b).neg) {
            lu_err_format(PyExc_ValueError, "negative shift count");
            return NULL;
        }
        return (op == NB_LSHIFT || op == NB_INPLACE_LSHIFT) ? long_lshift(a, b) : long_rshift(a, b);
    default: return Py_NewRef(Py_NotImplemented);
    }
}

/* Conversions. */

static PyObject *
long_bit_length(PyObject *a)
{
    big x = view(a);
    if (x.n == 0) {
        return zero();
    }
    return PyLong_FromLongLong((long long)(x.n - 1) * SHIFT + bit_length_digit(x.d[x.n - 1]));
}

static PyObject *
long_bit_count(PyObject *a)
{
    big x = view(a);
    long long count = 0;
    for (Py_ssize_t i = 0; i < x.n; i++) {
        count += __builtin_popcount(x.d[i]);
    }
    return PyLong_FromLongLong(count);
}

static PyObject *
reject(const char *what)
{
    lu_err_format(PyExc_OverflowError, "%s", what);
    return NULL;
}

/* to_bytes: two's complement of `length` bytes. */
static PyObject *
long_to_bytes(PyObject *a, Py_ssize_t length, int little, int is_signed)
{
    big x = view(a);
    if (x.neg && !is_signed) {
        return reject("can't convert negative int to unsigned");
    }
    /* m = x for x >= 0, |x| - 1 for a negative x, whose bytes get inverted. */
    digit *m = dcalloc(x.n + 1);
    if (m == NULL) {
        return NULL;
    }
    memcpy(m, x.d, x.n * sizeof(digit));
    if (x.neg) {
        digit one = 1;
        v_isub(m, x.n + 1, &one, 1);
    }
    Py_ssize_t nm = normalize(m, x.n + 1);
    int64_t bits = nm ? (int64_t)(nm - 1) * SHIFT + bit_length_digit(m[nm - 1]) : 0;
    int64_t room = (int64_t)length * 8 - (is_signed ? 1 : 0);
    if (x.neg ? (length == 0 || bits > room) : (nm != 0 && bits > room)) {
        PyMem_Free(m);
        return reject("int too big to convert");
    }
    char *out = PyMem_Malloc(length > 0 ? length : 1);
    if (out == NULL) {
        PyMem_Free(m);
        return PyErr_NoMemory();
    }
    twodigits acc = 0;
    int accbits = 0;
    Py_ssize_t di = 0;
    for (Py_ssize_t i = 0; i < length; i++) {
        if (accbits < 8 && di < nm) {
            acc |= (twodigits)m[di++] << accbits;
            accbits += SHIFT;
        }
        unsigned char byte = (unsigned char)(acc & 0xff);
        acc >>= 8;
        accbits = accbits >= 8 ? accbits - 8 : 0;
        out[little ? i : length - 1 - i] = x.neg ? (char)~byte : (char)byte;
    }
    PyMem_Free(m);
    PyObject *r = PyBytes_FromStringAndSize(out, length);
    PyMem_Free(out);
    return r;
}

/* from_bytes: `n` bytes of two's complement or magnitude. */
PyObject *
lu_long_from_bytes(const unsigned char *s, Py_ssize_t n, int little, int is_signed)
{
    int neg = n > 0 && is_signed && (s[little ? n - 1 : 0] & 0x80);
    Py_ssize_t cap = (n * 8) / SHIFT + 2;
    digit *z = dcalloc(cap);
    if (z == NULL) {
        return NULL;
    }
    twodigits acc = 0;
    int accbits = 0;
    Py_ssize_t k = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        unsigned char byte = s[little ? i : n - 1 - i];
        acc |= (twodigits)(neg ? (unsigned char)~byte : byte) << accbits;
        accbits += 8;
        if (accbits >= SHIFT) {
            z[k++] = (digit)(acc & MASK);
            acc >>= SHIFT;
            accbits -= SHIFT;
        }
    }
    z[k++] = (digit)acc;
    Py_ssize_t nz = normalize(z, k);
    if (neg) {
        /* |x| = (inverted bytes) + 1 */
        digit one = 1;
        nz = mag_add(z, nz, &one, 1, z);
    }
    return finish(neg, z, nz);
}

/* int -> str. Bases 2, 8 and 16 are linear; base 10 follows Knuth 4.4
   method 1b and is bounded by int_max_str_digits. */

static int
max_str_digits(void)
{
    return __atomic_load_n(&PyInterpreterState_Get()->max_str_digits, __ATOMIC_RELAXED);
}

static PyObject *
format_binary(big a, int base, int prefix)
{
    int bits = base == 2 ? 1 : base == 8 ? 3 : 4;
    int64_t total = a.n ? (int64_t)(a.n - 1) * SHIFT + bit_length_digit(a.d[a.n - 1]) : 1;
    Py_ssize_t ndigits = (Py_ssize_t)((total + bits - 1) / bits);
    Py_ssize_t len = a.neg + (prefix ? 2 : 0) + ndigits;
    char *buf = PyMem_Malloc(len + 1), *p = buf + len;
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    *p = 0;
    if (a.n == 0) {
        *--p = '0';
    }
    twodigits accum = 0;
    int accumbits = 0;
    for (Py_ssize_t i = 0; i < a.n; i++) {
        accum |= (twodigits)a.d[i] << accumbits;
        accumbits += SHIFT;
        do {
            *--p = "0123456789abcdef"[accum & (base - 1)];
            accumbits -= bits;
            accum >>= bits;
        } while (i < a.n - 1 ? accumbits >= bits : accum > 0);
    }
    if (prefix) {
        *--p = base == 16 ? 'x' : base == 8 ? 'o' : 'b';
        *--p = '0';
    }
    if (a.neg) {
        *--p = '-';
    }
    PyObject *r = lu_str_from_utf8(buf, len, 0);
    PyMem_Free(buf);
    return r;
}

static PyObject *
format_decimal(big a)
{
    Py_ssize_t size_a = a.n;
    if (size_a >= 10 * STR_DIGITS_THRESHOLD / (3 * SHIFT) + 2) {
        int limit = max_str_digits();
        if (limit > 0 && limit / (3 * SHIFT) <= (size_a - 11) / 10) {
            lu_err_format(PyExc_ValueError, TO_STR_ERROR, limit);
            return NULL;
        }
    }
    int d = (33 * DECIMAL_SHIFT) / (10 * SHIFT - 33 * DECIMAL_SHIFT);
    Py_ssize_t cap = 1 + size_a + size_a / d;
    digit *pout = dalloc(cap);
    if (pout == NULL) {
        return NULL;
    }
    Py_ssize_t size = 0;
    for (Py_ssize_t i = size_a; --i >= 0;) {
        digit hi = a.d[i];
        for (Py_ssize_t j = 0; j < size; j++) {
            twodigits z = (twodigits)pout[j] << SHIFT | hi;
            hi = (digit)(z / DECIMAL_BASE);
            pout[j] = (digit)(z - (twodigits)hi * DECIMAL_BASE);
        }
        while (hi) {
            pout[size++] = hi % DECIMAL_BASE;
            hi /= DECIMAL_BASE;
        }
    }
    if (size == 0) {
        pout[size++] = 0;
    }
    Py_ssize_t len = a.neg + 1 + (size - 1) * DECIMAL_SHIFT;
    digit tenpow = 10, rem = pout[size - 1];
    while (rem >= tenpow) {
        tenpow *= 10;
        len++;
    }
    if (len > STR_DIGITS_THRESHOLD) {
        int limit = max_str_digits();
        if (limit > 0 && len - a.neg > limit) {
            PyMem_Free(pout);
            lu_err_format(PyExc_ValueError, TO_STR_ERROR, limit);
            return NULL;
        }
    }
    char *buf = PyMem_Malloc(len + 1), *p = buf + len;
    if (buf == NULL) {
        PyMem_Free(pout);
        return PyErr_NoMemory();
    }
    *p = 0;
    for (Py_ssize_t i = 0; i < size - 1; i++) {
        rem = pout[i];
        for (int j = 0; j < DECIMAL_SHIFT; j++) {
            *--p = '0' + rem % 10;
            rem /= 10;
        }
    }
    rem = pout[size - 1];
    do {
        *--p = '0' + rem % 10;
        rem /= 10;
    } while (rem != 0);
    if (a.neg) {
        *--p = '-';
    }
    PyMem_Free(pout);
    PyObject *r = lu_str_from_utf8(buf, len, 0);
    PyMem_Free(buf);
    return r;
}

/* str -> int (PyLong_FromString on a NUL-terminated ASCII buffer of `len`
   characters). Returns the int, or MISSING when the text is not an integer
   literal; errors from the size limits raise. */

#define ISSPACE(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))

static int
digit_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 37;
}

static PyObject *
long_from_text(const char *str, Py_ssize_t len, int base)
{
    const char *end = str + len, *p = str;
    int sign = 1, error_if_nonzero = 0;
    while (*p && ISSPACE(*p)) {
        p++;
    }
    if (*p == '+') {
        p++;
    }
    else if (*p == '-') {
        p++;
        sign = -1;
    }
    if (base == 0) {
        if (p[0] != '0') base = 10;
        else if (p[1] == 'x' || p[1] == 'X') base = 16;
        else if (p[1] == 'o' || p[1] == 'O') base = 8;
        else if (p[1] == 'b' || p[1] == 'B') base = 2;
        else {
            error_if_nonzero = 1;
            base = 10;
        }
    }
    if (p[0] == '0' && ((base == 16 && (p[1] == 'x' || p[1] == 'X'))
                        || (base == 8 && (p[1] == 'o' || p[1] == 'O'))
                        || (base == 2 && (p[1] == 'b' || p[1] == 'B')))) {
        p += 2;
        if (*p == '_') {
            p++;
        }
    }
    const char *start = p;
    if (*start == '_') {
        return Py_NewRef(LU_MISSING);
    }
    char prev = 0;
    Py_ssize_t digits = 0;
    while (digit_value((unsigned char)*p) < base || *p == '_') {
        if (*p == '_') {
            if (prev == '_') {
                return Py_NewRef(LU_MISSING);
            }
        }
        else {
            digits++;
        }
        prev = *p++;
    }
    if (prev == '_' || start == p) {
        return Py_NewRef(LU_MISSING);
    }
    const char *stop = p;
    while (*p && ISSPACE(*p)) {
        p++;
    }
    if (p != end) {
        return Py_NewRef(LU_MISSING);
    }
    digit *z;
    Py_ssize_t nz;
    if ((base & (base - 1)) == 0) {
        int bits_per_char = __builtin_ctz(base);
        nz = (digits * bits_per_char + SHIFT - 1) / SHIFT;
        z = dcalloc(nz + 1);
        if (z == NULL) {
            return NULL;
        }
        twodigits accum = 0;
        int bits_in_accum = 0;
        Py_ssize_t k = 0;
        for (const char *q = stop; q-- > start;) {
            if (*q == '_') {
                continue;
            }
            accum |= (twodigits)digit_value((unsigned char)*q) << bits_in_accum;
            bits_in_accum += bits_per_char;
            if (bits_in_accum >= SHIFT) {
                z[k++] = (digit)(accum & MASK);
                accum >>= SHIFT;
                bits_in_accum -= SHIFT;
            }
        }
        if (bits_in_accum) {
            z[k++] = (digit)accum;
        }
        nz = normalize(z, k);
    }
    else {
        if (digits > STR_DIGITS_THRESHOLD) {
            int limit = max_str_digits();
            if (limit > 0 && digits > limit) {
                lu_err_format(PyExc_ValueError, TO_INT_ERROR, limit, digits);
                return NULL;
            }
        }
        /* base**convwidth is the largest power of base within one digit. */
        twodigits convmult = base;
        int convwidth = 1;
        while (convmult * base <= BASE) {
            convmult *= base;
            convwidth++;
        }
        double fsize = (double)digits * (log((double)base) / log((double)BASE)) + 1.0;
        if (fsize > (double)MAX_LONG_DIGITS) {
            too_many_digits();
            return NULL;
        }
        Py_ssize_t cap = (Py_ssize_t)fsize + 2;
        z = dcalloc(cap);
        if (z == NULL) {
            return NULL;
        }
        nz = 0;
        const char *q = start;
        while (q < stop) {
            twodigits c = 0, mult = 1;
            for (int i = 0; i < convwidth && q < stop; q++) {
                if (*q == '_') {
                    continue;
                }
                c = c * base + digit_value((unsigned char)*q);
                mult *= base;
                i++;
            }
            for (Py_ssize_t i = 0; i < nz; i++) {
                c += (twodigits)z[i] * mult;
                z[i] = (digit)(c & MASK);
                c >>= SHIFT;
            }
            if (c) {
                z[nz++] = (digit)c;
            }
        }
    }
    if (error_if_nonzero && nz != 0) {
        PyMem_Free(z);
        return Py_NewRef(LU_MISSING);
    }
    return finish(sign < 0 && nz != 0, z, nz);
}

/* The primitives. */

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__, (n)); return NULL; } } while (0)
#define INT(i) do { if (!PyLong_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() needs an int", __func__); return NULL; } } while (0)

static PyObject *
prim_int_add(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return lu_long_add(args[0], args[1]);
}

static PyObject *
prim_int_sub(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return lu_long_sub(args[0], args[1]);
}

static PyObject *
prim_int_mul(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return lu_long_mul(args[0], args[1]);
}

static PyObject *
prim_int_floordiv(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return lu_long_floordiv(args[0], args[1]);
}

static PyObject *
prim_int_mod(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return lu_long_mod(args[0], args[1]);
}

static PyObject *
prim_int_divmod(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return long_divmod_pair(args[0], args[1]);
}

static PyObject *
prim_int_pow(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); INT(0); INT(1);
    if (args[2] != Py_None) {
        INT(2);
    }
    return lu_long_pow(args[0], args[1], args[2] == Py_None ? NULL : args[2]);
}

static PyObject *
prim_int_truediv(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return lu_long_truediv(args[0], args[1]);
}

static PyObject *
prim_int_neg(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    return lu_long_negate(args[0]);
}

static PyObject *
prim_int_abs(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    return long_abs(args[0]);
}

static PyObject *
prim_int_invert(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    return lu_long_invert(args[0]);
}

/* int_shift(a, n, left): a << n or a >> n; n negative is a ValueError. */
static PyObject *
prim_int_shift(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); INT(0); INT(1);
    return lu_long_binary(args[2] == Py_True ? NB_LSHIFT : NB_RSHIFT, args[0], args[1]);
}

/* int_bitwise(a, b, op): op is "and", "or" or "xor". */
static PyObject *
prim_int_bitwise(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); INT(0); INT(1);
    const char *op = PyUnicode_Check(args[2]) ? PyUnicode_AsUTF8(args[2]) : "";
    return long_bitwise(op[0] == 'a' ? NB_AND : op[0] == 'o' ? NB_OR : NB_XOR, args[0], args[1]);
}

static PyObject *
prim_int_cmp(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(0); INT(1);
    return lu_small_int(lu_long_compare(args[0], args[1]));
}

static PyObject *
prim_int_bit_length(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    return long_bit_length(args[0]);
}

static PyObject *
prim_int_bit_count(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    return long_bit_count(args[0]);
}

static PyObject *
prim_int_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    return PyLong_FromLongLong(lu_long_hash((PyLongObject *)args[0]));
}

/* int_to_bytes(a, length, little, signed) */
static PyObject *
prim_int_to_bytes(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); INT(0); INT(1);
    long length;
    if (lu_long_as_long(args[1], &length) < 0) {
        return NULL;
    }
    return long_to_bytes(args[0], length, args[2] == Py_True, args[3] == Py_True);
}

/* int_from_bytes(bytes, little, signed) */
static PyObject *
prim_int_from_bytes(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    if (!PyBytes_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "int_from_bytes() needs bytes");
        return NULL;
    }
    return lu_long_from_bytes((const unsigned char *)PyBytes_AS_STRING(args[0]),
                           PyBytes_GET_SIZE(args[0]), args[1] == Py_True, args[2] == Py_True);
}

/* int_format(a, base, prefix): base 2, 8, 10 or 16; prefix adds 0b/0o/0x. */
static PyObject *
prim_int_format(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); INT(0); INT(1);
    long base;
    if (lu_long_as_long(args[1], &base) < 0) {
        return NULL;
    }
    if (base == 10) {
        return format_decimal(view(args[0]));
    }
    if (base != 2 && base != 8 && base != 16) {
        lu_err_format(PyExc_ValueError, "int_format: base %ld", base);
        return NULL;
    }
    return format_binary(view(args[0]), (int)base, args[2] == Py_True);
}

/* The Unicode whitespace that int() reads as ASCII space. */
static int
is_unicode_space(Py_UCS4 c)
{
    return (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0 || c == 0x1680
        || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f
        || c == 0x205f || c == 0x3000;
}

/* int_from_text(str_or_bytes, base): the int, or MISSING for a bad literal. */
static PyObject *
prim_int_from_text(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(1);
    long base;
    if (lu_long_as_long(args[1], &base) < 0) {
        return NULL;
    }
    PyObject *s = args[0];
    Py_ssize_t len;
    char *buf;
    if (PyUnicode_Check(s)) {
        len = PyUnicode_GET_LENGTH(s);
        buf = PyMem_Malloc(len + 1);
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        int kind = PyUnicode_KIND(s);
        const void *data = PyUnicode_DATA(s);
        for (Py_ssize_t i = 0; i < len; i++) {
            Py_UCS4 c = PyUnicode_READ(kind, data, i);
            int digit = c < 128 ? -1 : _PyUnicode_ToDecimalDigit(c);
            buf[i] = c < 128 ? (char)c : is_unicode_space(c) ? ' ' : digit >= 0 ? (char)('0' + digit) : '?';
            if (c >= 0x1c && c <= 0x1f) {
                buf[i] = ' ';
            }
        }
    }
    else if (PyBytes_Check(s)) {
        len = PyBytes_GET_SIZE(s);
        buf = PyMem_Malloc(len + 1);
        if (buf == NULL) {
            return PyErr_NoMemory();
        }
        memcpy(buf, PyBytes_AS_STRING(s), len);
    }
    else {
        lu_err_format(PyExc_TypeError, "int_from_text() needs a str or bytes");
        return NULL;
    }
    buf[len] = 0;
    PyObject *r = long_from_text(buf, len, (int)base);
    PyMem_Free(buf);
    return r;
}

static PyObject *
prim_int_as_double(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    double d = lu_long_as_double(args[0]);
    if (d == -1.0 && PyErr_Occurred()) {
        return NULL;
    }
    return PyFloat_FromDouble(d);
}

static PyObject *
prim_int_from_double(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyFloat_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "int_from_double() needs a float");
        return NULL;
    }
    return lu_long_from_double(PyFloat_AS_DOUBLE(args[0]));
}

/* int_subtype(cls, value): an instance of the int subclass cls. */
static PyObject *
prim_int_subtype(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); INT(1);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyLong_Type)) {
        lu_err_format(PyExc_TypeError, "int_subtype() needs an int subclass");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[0];
    big x = view(args[1]);
    PyLongObject *r = (PyLongObject *)tp->tp_alloc(tp, x.n > 0 ? x.n : 1);
    if (r == NULL) {
        return NULL;
    }
    memcpy(r->long_value.ob_digit, x.d, (x.n > 0 ? x.n : 1) * sizeof(digit));
    r->long_value.lv_tag = ((uintptr_t)x.n << _PyLong_NON_SIZE_BITS)
        | (uintptr_t)(x.n == 0 ? 1 : x.neg ? 2 : 0);
    return (PyObject *)r;
}

/* int_max_str_digits(): the limit; set_int_max_str_digits(n) changes it. */
static PyObject *
prim_int_max_str_digits(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    return PyLong_FromLong(max_str_digits());
}

static PyObject *
prim_set_int_max_str_digits(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long n;
    ARGS(1); INT(0);
    if (lu_long_as_long(args[0], &n) < 0) {
        return NULL;
    }
    __atomic_store_n(&PyInterpreterState_Get()->max_str_digits, (int)n, __ATOMIC_RELAXED);
    return Py_NewRef(Py_None);
}

/* int_truth(x): whether an int is nonzero. */
static PyObject *
prim_int_is_zero(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); INT(0);
    return view(args[0]).n == 0 ? Py_True : Py_False;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_int_methods[] = {
    FAST(int_add), FAST(int_sub), FAST(int_mul), FAST(int_floordiv), FAST(int_mod),
    FAST(int_divmod), FAST(int_pow), FAST(int_truediv), FAST(int_neg), FAST(int_abs),
    FAST(int_invert), FAST(int_shift), FAST(int_bitwise), FAST(int_cmp),
    FAST(int_bit_length), FAST(int_bit_count), FAST(int_hash), FAST(int_to_bytes),
    FAST(int_from_bytes), FAST(int_format), FAST(int_from_text), FAST(int_as_double),
    FAST(int_from_double), FAST(int_subtype), FAST(int_max_str_digits),
    FAST(set_int_max_str_digits), FAST(int_is_zero),
    { NULL, NULL, 0, NULL },
};

/* sys.get_int_max_str_digits() and sys.set_int_max_str_digits(maxdigits). */

static PyObject *
sys_get_int_max_str_digits(PyObject *m, PyObject *unused)
{
    return PyLong_FromLong(max_str_digits());
}

static PyObject *
sys_set_int_max_str_digits(PyObject *m, PyObject *args, PyObject *kwargs)
{
    PyObject *value = NULL;
    if (PyTuple_GET_SIZE(args) == 1 && kwargs == NULL) {
        value = PyTuple_GET_ITEM(args, 0);
    }
    else if (PyTuple_GET_SIZE(args) == 0 && kwargs != NULL && ((PyDictObject *)kwargs)->ma_used == 1) {
        PyObject *key = PyUnicode_InternFromString("maxdigits");
        value = key != NULL ? lu_ns_lookup(kwargs, key) : NULL;
        Py_XDECREF(key);
    }
    if (value == NULL) {
        lu_err_format(PyExc_TypeError, "set_int_max_str_digits() missing required argument 'maxdigits' (pos 1)");
        return NULL;
    }
    long n;
    if (!PyLong_Check(value)) {
        lu_err_format(PyExc_TypeError, "'%.200s' object cannot be interpreted as an integer",
                      Py_TYPE(value)->tp_name);
        return NULL;
    }
    if (lu_long_as_long(value, &n) < 0) {
        return NULL;
    }
    if (n != 0 && n < STR_DIGITS_THRESHOLD) {
        lu_err_format(PyExc_ValueError, "maxdigits must be >= %d or 0 for unlimited", STR_DIGITS_THRESHOLD);
        return NULL;
    }
    __atomic_store_n(&PyInterpreterState_Get()->max_str_digits, (int)n, __ATOMIC_RELAXED);
    return Py_NewRef(Py_None);
}

PyMethodDef lu_sys_int_methods[] = {
    { "get_int_max_str_digits", sys_get_int_max_str_digits, METH_NOARGS, NULL },
    { "set_int_max_str_digits", (PyCFunction)(void (*)(void))sys_set_int_max_str_digits,
      METH_VARARGS | METH_KEYWORDS, NULL },
    { NULL, NULL, 0, NULL },
};
