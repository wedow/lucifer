/* Conversions between int objects and C integers, and the sign, export,
   writer and native-bytes APIs (Objects/longobject.c). Every conversion
   reads the int's magnitude into 64 bits once, then applies the range and
   sign rules of its C type. */

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

/* The fixed-width conversions go through PyLong_AsNativeBytes (LONG_TO_INT);
   *value is untouched when they fail with -1. */
#define LONG_TO_INT(type, obj, result) \
    do { \
        type value; \
        int flags = (Py_ASNATIVEBYTES_NATIVE_ENDIAN | Py_ASNATIVEBYTES_ALLOW_INDEX); \
        Py_ssize_t bytes = PyLong_AsNativeBytes(obj, &value, sizeof(value), flags); \
        if (bytes < 0) { \
            return -1; \
        } \
        if ((size_t)bytes > sizeof(value)) { \
            PyErr_SetString(PyExc_OverflowError, \
                            "Python int too large to convert to C " #type); \
            return -1; \
        } \
        *result = value; \
        return 0; \
    } while (0)

/* The Argument Clinic converters of unsigned C types (Objects/longobject.c):
   1 with *ptr set, or 0 with an exception. */
#define UNSIGNED_INT_CONVERTER(NAME, TYPE) \
    LU_EXPORT int \
    _PyLong_##NAME##_Converter(PyObject *obj, void *ptr) \
    { \
        Py_ssize_t bytes = PyLong_AsNativeBytes(obj, ptr, sizeof(TYPE), \
            Py_ASNATIVEBYTES_NATIVE_ENDIAN | Py_ASNATIVEBYTES_ALLOW_INDEX \
            | Py_ASNATIVEBYTES_REJECT_NEGATIVE | Py_ASNATIVEBYTES_UNSIGNED_BUFFER); \
        if (bytes < 0) { \
            return 0; \
        } \
        if ((size_t)bytes > sizeof(TYPE)) { \
            PyErr_SetString(PyExc_OverflowError, "Python int too large for C " #TYPE); \
            return 0; \
        } \
        return 1; \
    }

UNSIGNED_INT_CONVERTER(UnsignedShort, unsigned short)
UNSIGNED_INT_CONVERTER(UnsignedInt, unsigned int)
UNSIGNED_INT_CONVERTER(UnsignedLong, unsigned long)
UNSIGNED_INT_CONVERTER(UnsignedLongLong, unsigned long long)
UNSIGNED_INT_CONVERTER(Size_t, size_t)
UNSIGNED_INT_CONVERTER(UInt8, uint8_t)
UNSIGNED_INT_CONVERTER(UInt16, uint16_t)
UNSIGNED_INT_CONVERTER(UInt32, uint32_t)
UNSIGNED_INT_CONVERTER(UInt64, uint64_t)

LU_EXPORT PyObject *
_PyLong_FromTime_t(time_t t)
{
    return PyLong_FromLongLong((long long)t);
}

#define LONG_TO_UINT(type, obj, result) \
    do { \
        type value; \
        int flags = (Py_ASNATIVEBYTES_NATIVE_ENDIAN | Py_ASNATIVEBYTES_UNSIGNED_BUFFER \
                     | Py_ASNATIVEBYTES_REJECT_NEGATIVE | Py_ASNATIVEBYTES_ALLOW_INDEX); \
        Py_ssize_t bytes = PyLong_AsNativeBytes(obj, &value, sizeof(value), flags); \
        if (bytes < 0) { \
            return -1; \
        } \
        if ((size_t)bytes > sizeof(value)) { \
            PyErr_SetString(PyExc_OverflowError, \
                            "Python int too large to convert to C " #type); \
            return -1; \
        } \
        *result = value; \
        return 0; \
    } while (0)

int
PyLong_AsInt32(PyObject *op, int32_t *result)
{
    LONG_TO_INT(int32_t, op, result);
}

int
PyLong_AsInt64(PyObject *op, int64_t *result)
{
    LONG_TO_INT(int64_t, op, result);
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

int
PyLong_AsUInt32(PyObject *op, uint32_t *result)
{
    LONG_TO_UINT(uint32_t, op, result);
}

int
PyLong_AsUInt64(PyObject *op, uint64_t *result)
{
    LONG_TO_UINT(uint64_t, op, result);
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

/* Objects/longobject.c: only ints (subclasses included) convert; negative
   values through the signed conversion, everything else unsigned. */
void *
PyLong_AsVoidPtr(PyObject *vv)
{
    long long x;
    if (PyLong_Check(vv) && (((PyLongObject *)vv)->long_value.lv_tag & _PyLong_SIGN_MASK) == 2) {
        x = PyLong_AsLongLong(vv);
    }
    else {
        x = (long long)PyLong_AsUnsignedLongLong(vv);
    }
    if (x == -1 && PyErr_Occurred()) {
        return NULL;
    }
    return (void *)x;
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
        PyErr_Format(PyExc_TypeError, "expected int, got %T", obj);
        return -1;
    }
    return _PyLong_IsCompact((PyLongObject *)obj) && _PyLong_CompactValue((PyLongObject *)obj) == 0;
}

/* --- The sign and bit-length readers (Objects/longobject.c) --- */

/* The int's sign: 1, 0 or -1 for positive, zero or negative (long_sign). */
static int
long_sign(PyObject *v)
{
    return 1 - (int)(((PyLongObject *)v)->long_value.lv_tag & _PyLong_SIGN_MASK);
}

int
PyLong_GetSign(PyObject *v, int *sign)
{
    if (!PyLong_Check(v)) {
        PyErr_Format(PyExc_TypeError, "expect int, got %T", v);
        return -1;
    }
    *sign = long_sign(v);
    return 0;
}

int
PyLong_IsPositive(PyObject *obj)
{
    if (!PyLong_Check(obj)) {
        PyErr_Format(PyExc_TypeError, "expected int, got %T", obj);
        return -1;
    }
    return long_sign(obj) > 0;
}

int
PyLong_IsNegative(PyObject *obj)
{
    if (!PyLong_Check(obj)) {
        PyErr_Format(PyExc_TypeError, "expected int, got %T", obj);
        return -1;
    }
    return long_sign(obj) < 0;
}

/* Bits in the magnitude: 30 per digit below the top, then its bit length. */
int64_t
_PyLong_NumBits(PyObject *v)
{
    PyLongObject *a = (PyLongObject *)v;
    Py_ssize_t n = (Py_ssize_t)(a->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    if (n == 0) {
        return 0;
    }
    digit msd = a->long_value.ob_digit[n - 1];
    int bits = 0;
    while (msd != 0) {
        msd >>= 1;
        bits++;
    }
    return (int64_t)(n - 1) * PyLong_SHIFT + bits;
}

/* --- Import/Export of the digit representation (Objects/longobject.c) --- */

static const PyLongLayout long_layout = {
    .bits_per_digit = PyLong_SHIFT,
    .digits_order = -1,                     /* least significant first */
    .digit_endianness = PY_LITTLE_ENDIAN ? -1 : 1,
    .digit_size = sizeof(digit),
};

const PyLongLayout *
PyLong_GetNativeLayout(void)
{
    return &long_layout;
}

int
PyLong_Export(PyObject *obj, PyLongExport *export_long)
{
    if (!PyLong_Check(obj)) {
        memset(export_long, 0, sizeof(*export_long));
        PyErr_Format(PyExc_TypeError, "expect int, got %T", obj);
        return -1;
    }
    int overflow;
    long long value = PyLong_AsLongLongAndOverflow(obj, &overflow);
    assert(!(value == -1 && PyErr_Occurred()));
    if (!overflow) {
        export_long->value = value;
        export_long->negative = 0;
        export_long->ndigits = 0;
        export_long->digits = NULL;
        export_long->_reserved = 0;
    }
    else {
        PyLongObject *self = (PyLongObject *)obj;
        export_long->value = 0;
        export_long->negative = (self->long_value.lv_tag & _PyLong_SIGN_MASK) == 2;
        export_long->ndigits = (Py_ssize_t)(self->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
        if (export_long->ndigits == 0) {
            export_long->ndigits = 1;
        }
        export_long->digits = self->long_value.ob_digit;
        export_long->_reserved = (Py_uintptr_t)Py_NewRef(obj);
    }
    return 0;
}

void
PyLong_FreeExport(PyLongExport *export_long)
{
    PyObject *obj = (PyObject *)export_long->_reserved;
    if (obj) {
        export_long->_reserved = 0;
        Py_DECREF(obj);
    }
}

/* --- The PyLongWriter API --- */

PyLongWriter *
PyLongWriter_Create(int negative, Py_ssize_t ndigits, void **digits)
{
    *digits = NULL;
    if (ndigits <= 0) {
        PyErr_SetString(PyExc_ValueError, "ndigits must be positive");
        return NULL;
    }
    PyLongObject *obj = lu_long_alloc(ndigits);
    if (obj == NULL) {
        return NULL;
    }
    if (negative) {
        obj->long_value.lv_tag = (obj->long_value.lv_tag & ~(uintptr_t)_PyLong_SIGN_MASK) | 2;
    }
    *digits = obj->long_value.ob_digit;
    return (PyLongWriter *)obj;
}

void
PyLongWriter_Discard(PyLongWriter *writer)
{
    if (writer != NULL) {
        Py_DECREF((PyObject *)writer);
    }
}

/* maybe_small_long(long_normalize(obj)) by the canonical path: the small-int
   singletons take over when the digits say they should. */
PyObject *
PyLongWriter_Finish(PyLongWriter *writer)
{
    PyLongObject *obj = (PyLongObject *)writer;
    Py_ssize_t n = (Py_ssize_t)(obj->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    int negative = (obj->long_value.lv_tag & _PyLong_SIGN_MASK) == 2;
    PyObject *r = lu_long_from_digits(negative, n, obj->long_value.ob_digit);
    Py_DECREF(obj);
    return r;
}

/* --- The native-bytes API (Objects/longobject.c) --- */

PyObject *
_PyLong_FromByteArray(const unsigned char *bytes, size_t n,
                      int little_endian, int is_signed)
{
    return lu_long_from_bytes(bytes, (Py_ssize_t)n, little_endian, is_signed);
}

/* _PyLong_AsByteArray: the low n bytes of v, two's complement when signed;
   all n bytes are written even when the value does not fit, which returns -1. */
int
_PyLong_AsByteArray(PyLongObject *v, unsigned char *bytes, size_t n,
                    int little_endian, int is_signed, int with_exceptions)
{
    Py_ssize_t ndigits = (Py_ssize_t)(v->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    int do_twos_comp;
    if ((v->long_value.lv_tag & _PyLong_SIGN_MASK) == 2) {
        if (!is_signed) {
            if (with_exceptions) {
                PyErr_SetString(PyExc_OverflowError, "can't convert negative int to unsigned");
            }
            return -1;
        }
        do_twos_comp = 1;
    }
    else {
        do_twos_comp = 0;
    }
    unsigned char *p = little_endian ? bytes : bytes + n - 1;
    int pincr = little_endian ? 1 : -1;
    size_t j = 0;
    twodigits accum = 0;
    unsigned int accumbits = 0;
    digit carry = do_twos_comp ? 1 : 0;
    for (Py_ssize_t i = 0; i < ndigits; ++i) {
        digit thisdigit = v->long_value.ob_digit[i];
        if (do_twos_comp) {
            thisdigit = (thisdigit ^ PyLong_MASK) + carry;
            carry = thisdigit >> PyLong_SHIFT;
            thisdigit &= PyLong_MASK;
        }
        accum |= (twodigits)thisdigit << accumbits;
        if (i == ndigits - 1) {
            /* the top digit's sign bits need not be stored */
            digit s = do_twos_comp ? thisdigit ^ PyLong_MASK : thisdigit;
            while (s != 0) {
                s >>= 1;
                accumbits++;
            }
        }
        else {
            accumbits += PyLong_SHIFT;
        }
        while (accumbits >= 8) {
            if (j >= n) {
                goto overflow;
            }
            ++j;
            *p = (unsigned char)(accum & 0xff);
            p += pincr;
            accumbits -= 8;
            accum >>= 8;
        }
    }
    if (accumbits > 0) {
        if (j >= n) {
            goto overflow;
        }
        ++j;
        if (do_twos_comp) {
            /* fill the byte's leading bits with sign bits */
            accum |= (~(twodigits)0) << accumbits;
        }
        *p = (unsigned char)(accum & 0xff);
        p += pincr;
    }
    else if (j == n && is_signed) {
        /* the loop filled the buffer exactly: the sign bit must be there */
        int sign_bit_set = n > 0 && *(p - pincr) >= 0x80;
        if (sign_bit_set == do_twos_comp) {
            return 0;
        }
        goto overflow;
    }
    unsigned char signbyte = do_twos_comp ? 0xffU : 0U;
    for (; j < n; ++j, p += pincr) {
        *p = signbyte;
    }
    return 0;

overflow:
    if (with_exceptions) {
        PyErr_SetString(PyExc_OverflowError, "int too big to convert");
    }
    return -1;
}

/* All bits above n equal, use n-1 so the sign bit counts too. */
static int
fits_in_n_bits(Py_ssize_t v, Py_ssize_t n)
{
    if (n >= (Py_ssize_t)sizeof(Py_ssize_t) * 8) {
        return 1;
    }
    Py_ssize_t v_extended = v >> ((int)n - 1);
    return v_extended == 0 || v_extended == -1;
}

/* Py_ASNATIVEBYTES_NATIVE_ENDIAN resolves to the host's byte order. */
static void
resolve_endianness(int *endianness)
{
    *endianness = (*endianness == -1 || (*endianness & 2)) ? PY_LITTLE_ENDIAN : (*endianness & 1);
}

Py_ssize_t
PyLong_AsNativeBytes(PyObject *vv, void *buffer, Py_ssize_t n, int flags)
{
    PyLongObject *v;
    union {
        Py_ssize_t v;
        unsigned char b[sizeof(Py_ssize_t)];
    } cv;
    int do_decref = 0;
    Py_ssize_t res;

    if (vv == NULL || n < 0) {
        PyErr_BadInternalCall();
        return -1;
    }
    int little_endian = flags;
    resolve_endianness(&little_endian);

    if (PyLong_Check(vv)) {
        v = (PyLongObject *)vv;
    }
    else if (flags != -1 && (flags & Py_ASNATIVEBYTES_ALLOW_INDEX)) {
        v = (PyLongObject *)_PyNumber_Index(vv);
        if (v == NULL) {
            return -1;
        }
        do_decref = 1;
    }
    else {
        PyErr_Format(PyExc_TypeError, "expect int, got %T", vv);
        return -1;
    }

    if ((flags != -1 && (flags & Py_ASNATIVEBYTES_REJECT_NEGATIVE))
        && (v->long_value.lv_tag & _PyLong_SIGN_MASK) == 2) {
        PyErr_SetString(PyExc_ValueError, "Cannot convert negative int");
        if (do_decref) {
            Py_DECREF(v);
        }
        return -1;
    }

    if (_PyLong_IsCompact(v)) {
        res = sizeof(cv.b);
        cv.v = _PyLong_CompactValue(v);
        if (n <= 0) {
            /* nothing to do */
        }
        else if (n <= (Py_ssize_t)sizeof(cv.b)) {
            if (little_endian) {
                memcpy(buffer, cv.b, n);
            }
            else {
                for (Py_ssize_t i = 0; i < n; ++i) {
                    ((unsigned char *)buffer)[n - i - 1] = cv.b[i];
                }
            }
            if (fits_in_n_bits(cv.v, n * 8)) {
                res = n;
            }
            else if (cv.v > 0 && fits_in_n_bits(cv.v, n * 8 + 1)) {
                /* a positive MSB needs no extra bit for an unsigned buffer */
                if (flags == -1 || (flags & Py_ASNATIVEBYTES_UNSIGNED_BUFFER)) {
                    res = n;
                }
                else {
                    res = n + 1;
                }
            }
        }
        else {
            unsigned char fill = cv.v < 0 ? 0xFF : 0x00;
            if (little_endian) {
                memcpy(buffer, cv.b, sizeof(cv.b));
                memset((char *)buffer + sizeof(cv.b), fill, n - sizeof(cv.b));
            }
            else {
                unsigned char *b = (unsigned char *)buffer;
                for (Py_ssize_t i = 0; i < n - (Py_ssize_t)sizeof(cv.b); ++i) {
                    *b++ = fill;
                }
                for (Py_ssize_t i = sizeof(cv.b); i > 0; --i) {
                    *b++ = cv.b[i - 1];
                }
            }
        }
    }
    else {
        if (n > 0) {
            _PyLong_AsByteArray(v, buffer, (size_t)n, little_endian, 1, 0);
        }
        /* bytes for the magnitude; the implied sign bit cancels the rounding */
        int64_t nb = _PyLong_NumBits((PyObject *)v);
        assert(nb >= 0);
        res = (Py_ssize_t)(nb / 8) + 1;
        if (n > 0 && res == n + 1 && nb % 8 == 0) {
            if ((v->long_value.lv_tag & _PyLong_SIGN_MASK) == 2) {
                /* 0x80...00 from a negative value needs no extra sign bit */
                int is_edge_case = 1;
                unsigned char *b = (unsigned char *)buffer;
                for (Py_ssize_t i = 0; i < n && is_edge_case; ++i, ++b) {
                    if (i == 0) {
                        is_edge_case = (*b == (little_endian ? 0 : 0x80));
                    }
                    else if (i < n - 1) {
                        is_edge_case = (*b == 0);
                    }
                    else {
                        is_edge_case = (*b == (little_endian ? 0x80 : 0));
                    }
                }
                if (is_edge_case) {
                    res = n;
                }
            }
            else {
                /* a positive MSB needs no extra bit for an unsigned buffer */
                unsigned char *b = (unsigned char *)buffer;
                if (b[little_endian ? n - 1 : 0] & 0x80) {
                    if (flags == -1 || (flags & Py_ASNATIVEBYTES_UNSIGNED_BUFFER)) {
                        res = n;
                    }
                    else {
                        res = n + 1;
                    }
                }
            }
        }
    }

    if (do_decref) {
        Py_DECREF(v);
    }
    return res;
}

PyObject *
PyLong_FromNativeBytes(const void *buffer, size_t n, int flags)
{
    if (!buffer) {
        PyErr_BadInternalCall();
        return NULL;
    }
    int little_endian = flags;
    resolve_endianness(&little_endian);
    return _PyLong_FromByteArray((const unsigned char *)buffer, n, little_endian,
        (flags == -1 || !(flags & Py_ASNATIVEBYTES_UNSIGNED_BUFFER)) ? 1 : 0);
}

PyObject *
PyLong_FromUnsignedNativeBytes(const void *buffer, size_t n, int flags)
{
    if (!buffer) {
        PyErr_BadInternalCall();
        return NULL;
    }
    int little_endian = flags;
    resolve_endianness(&little_endian);
    return _PyLong_FromByteArray((const unsigned char *)buffer, n, little_endian, 0);
}
