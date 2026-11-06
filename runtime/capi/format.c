/* PyUnicode_FromFormatV (Objects/unicodeobject.c) and the error functions
   that take its formats. The result is assembled as code points, so a str
   argument with lone surrogates survives and a malformed %s byte is U+FFFD. */

#include "lucifer.h"

#include <stdarg.h>

struct ubuf {
    Py_UCS4 *p;
    Py_ssize_t n, cap;
    int failed;
};

static void
put(struct ubuf *b, Py_UCS4 c)
{
    if (b->n == b->cap) {
        Py_ssize_t cap = b->cap ? b->cap * 2 : 64;
        Py_UCS4 *p = PyMem_Realloc(b->p, cap * sizeof(Py_UCS4));
        if (p == NULL) {
            b->failed = 1;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    b->p[b->n++] = c;
}

static void
put_pad(struct ubuf *b, Py_ssize_t count, Py_UCS4 c)
{
    for (; count > 0; count--) {
        put(b, c);
    }
}

/* The code points of UTF-8 bytes. A malformed sequence is replaced by one
   U+FFFD per maximal invalid subpart, as CPython's UTF-8 decoder does: the
   leading byte plus the continuation bytes that a valid sequence of that
   length could start with, where the leading byte also bounds the second
   byte (no overlongs, no surrogates, no more than U+10FFFF). */
static Py_ssize_t
decode_utf8(const unsigned char *s, Py_ssize_t n, Py_UCS4 *out)
{
    Py_ssize_t count = 0;
    for (Py_ssize_t i = 0; i < n; count++) {
        unsigned char c = s[i];
        int len, lo, hi;                 /* sequence length, second-byte range */
        if (c < 0x80) {
            len = lo = 1, hi = 0;
        }
        else if (c >= 0xc2 && c <= 0xdf) {
            len = 2, lo = 0x80, hi = 0xbf;
        }
        else if (c == 0xe0) {
            len = 3, lo = 0xa0, hi = 0xbf;
        }
        else if (c >= 0xe1 && c <= 0xec) {
            len = 3, lo = 0x80, hi = 0xbf;
        }
        else if (c == 0xed) {
            len = 3, lo = 0x80, hi = 0x9f;
        }
        else if (c >= 0xee && c <= 0xef) {
            len = 3, lo = 0x80, hi = 0xbf;
        }
        else if (c == 0xf0) {
            len = 4, lo = 0x90, hi = 0xbf;
        }
        else if (c >= 0xf1 && c <= 0xf3) {
            len = 4, lo = 0x80, hi = 0xbf;
        }
        else if (c == 0xf4) {
            len = 4, lo = 0x80, hi = 0x8f;
        }
        else {
            len = 0;                     /* not a leading byte */
        }
        Py_UCS4 ch = 0xfffd;
        int sub = 1;                     /* bytes of the subpart consumed */
        if (len == 1) {
            ch = c;
        }
        else if (len > 1) {
            sub = 0;
            if (i + 1 < n && s[i + 1] >= lo && s[i + 1] <= hi) {
                sub = 2;
                for (int k = 2; k < len && i + k < n && (s[i + k] & 0xc0) == 0x80; k++) {
                    sub = k + 1;
                }
            }
            if (sub == len) {
                ch = (c & (len == 2 ? 0x1f : len == 3 ? 0x0f : 0x07));
                for (int k = 1; k < len; k++) {
                    ch = (ch << 6) | (s[i + k] & 0x3f);
                }
            }
            else {
                sub = sub ? sub : 1;     /* the leading byte alone */
            }
        }
        if (out != NULL) {
            out[count] = ch;
        }
        i += sub;
    }
    return count;
}

/* Pads and appends the code points text[0..n) to the width. */
static void
put_field(struct ubuf *b, const Py_UCS4 *text, Py_ssize_t n, Py_ssize_t width, int left)
{
    if (!left) {
        put_pad(b, width - n, ' ');
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        put(b, text[i]);
    }
    if (left) {
        put_pad(b, width - n, ' ');
    }
}

static void
put_str(struct ubuf *b, PyObject *s, Py_ssize_t width, Py_ssize_t prec, int left)
{
    Py_ssize_t n = PyUnicode_GET_LENGTH(s);
    n = prec >= 0 && prec < n ? prec : n;
    Py_UCS4 *text = PyMem_Malloc((n + 1) * sizeof(Py_UCS4));
    if (text == NULL) {
        b->failed = 1;
        return;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        text[i] = PyUnicode_READ_CHAR(s, i);
    }
    put_field(b, text, n, width, left);
    PyMem_Free(text);
}

/* %T and %N: a type's name, qualified by its module unless that is builtins or __main__. */
static PyObject *
type_name(PyObject *obj, int qualified, int colon)
{
    PyObject *type = (PyObject *)Py_TYPE(obj);
    PyObject *qualname = PyObject_GetAttrString(type, "__qualname__");
    if (!qualified || qualname == NULL) {
        return qualname;
    }
    PyObject *module = PyObject_GetAttrString(type, "__module__");
    if (module == NULL || !PyUnicode_Check(module)
        || strcmp(PyUnicode_AsUTF8(module), "builtins") == 0
        || strcmp(PyUnicode_AsUTF8(module), "__main__") == 0) {
        PyErr_Clear();
        Py_XDECREF(module);
        return qualname;
    }
    PyObject *full = PyUnicode_FromFormat(colon ? "%U:%U" : "%U.%U", module, qualname);
    Py_DECREF(module);
    Py_DECREF(qualname);
    return full;
}

PyObject *
PyUnicode_FromFormatV(const char *format, va_list vargs)
{
    for (const char *f = format; *f; f++) {
        if ((unsigned char)*f > 127) {
            PyErr_Format(PyExc_ValueError,
                         "PyUnicode_FromFormatV() expects an ASCII-encoded format "
                         "string, got a non-ASCII byte: 0x%02x", (unsigned char)*f);
            return NULL;
        }
    }
    struct ubuf b = { 0 };
    va_list ap;
    va_copy(ap, vargs);
    for (const char *f = format; *f && !b.failed; ) {
        if (*f != '%') {
            const char *end = strchr(f, '%');
            Py_ssize_t n = end != NULL ? end - f : (Py_ssize_t)strlen(f);
            Py_ssize_t count = decode_utf8((const unsigned char *)f, n, NULL);
            Py_UCS4 *text = PyMem_Malloc((count + 1) * sizeof(Py_UCS4));
            if (text == NULL) {
                b.failed = 1;
                break;
            }
            decode_utf8((const unsigned char *)f, n, text);
            put_field(&b, text, count, 0, 0);
            PyMem_Free(text);
            f += n;
            continue;
        }
        const char *spec = f++;
        int left = 0, zero = 0, alt = 0;
        for (; *f == '-' || *f == '0' || *f == '#'; f++) {
            left |= *f == '-';
            zero |= *f == '0';
            alt |= *f == '#';
        }
        Py_ssize_t width = -1, prec = -1;
        while (*f >= '0' && *f <= '9') {
            width = width < 0 ? *f - '0' : width * 10 + (*f - '0');
            f++;
        }
        if (*f == '.') {
            prec = 0;
            for (f++; *f >= '0' && *f <= '9'; f++) {
                prec = prec * 10 + (*f - '0');
            }
        }
        int size = 0;           /* 0 int, 1 long, 2 long long, 3 size_t, 4 intmax_t, 5 ptrdiff_t */
        for (;; f++) {
            if (*f == 'l') size = size == 1 ? 2 : 1;
            else if (*f == 'z') size = 3;
            else if (*f == 'j') size = 4;
            else if (*f == 't') size = 5;
            else break;
        }
        char conv = *f++;
        /* The conversions with their options validated (unicode_fromformat_parse):
           %% only bare, %c and %p without a size, width or precision, and a
           size modifier only where it is read. */
        int known = conv == 'd' || conv == 'i' || conv == 'o' || conv == 'u'
            || conv == 'x' || conv == 'X' || conv == 'c' || conv == 'p'
            || conv == 's' || conv == 'V' || conv == 'U' || conv == 'S'
            || conv == 'R' || conv == 'A' || conv == 'T' || conv == 'N' || conv == '%';
        if ((conv == '%' && f - spec > 2)
            || ((conv == 'c' || conv == 'p') && (size || width >= 0 || prec >= 0))
            || ((conv == 's' || conv == 'V') && size > 1)
            || (!known && size)) {
            PyErr_Format(PyExc_SystemError, "invalid format string: %s", spec);
            b.failed = 2;
            break;
        }
        switch (conv) {
        case '%':
            put(&b, '%');
            break;
        case 'c': {
            int c = va_arg(ap, int);
            if (c < 0 || c > 0x10ffff) {
                PyErr_SetString(PyExc_OverflowError, "character argument not in range(0x110000)");
                b.failed = 2;
                break;
            }
            Py_UCS4 ch = (Py_UCS4)c;
            put_field(&b, &ch, 1, width, left);
            break;
        }
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            char digits[80];
            int base = conv == 'x' || conv == 'X' ? 16 : conv == 'o' ? 8 : 10;
            int negative = 0;
            unsigned long long v;
            if (conv == 'd' || conv == 'i') {
                long long s = size == 0 ? va_arg(ap, int) : size == 1 ? va_arg(ap, long)
                    : size == 2 ? va_arg(ap, long long) : size == 3 ? (long long)va_arg(ap, Py_ssize_t)
                    : size == 4 ? (long long)va_arg(ap, intmax_t) : (long long)va_arg(ap, ptrdiff_t);
                negative = s < 0;
                v = negative ? 0ULL - (unsigned long long)s : (unsigned long long)s;
            }
            else {
                v = size == 0 ? va_arg(ap, unsigned int) : size == 1 ? va_arg(ap, unsigned long)
                    : size == 2 ? va_arg(ap, unsigned long long) : size == 3 ? va_arg(ap, size_t)
                    : size == 4 ? (unsigned long long)va_arg(ap, uintmax_t)
                    : (unsigned long long)va_arg(ap, ptrdiff_t);
            }
            int nd = 0;
            do {
                int d = (int)(v % base);
                digits[nd++] = (char)(d < 10 ? '0' + d : (conv == 'X' ? 'A' : 'a') + d - 10);
                v /= base;
            } while (v);
            /* The pads of Objects/unicodeobject.c's integer writer: the
               precision zero-fills the digits, the width space-fills, and a
               zero flag with no left-justify turns the space fill into
               zeroes (precision takes the width's place). */
            prec = Py_MAX(prec, nd);
            width = Py_MAX(width, prec + negative);
            if (zero && !left) {
                prec = width - negative;
            }
            Py_ssize_t spacepad = Py_MAX(width - prec - negative, 0);
            Py_ssize_t zeropad = Py_MAX(prec - nd, 0);
            if (spacepad && !left) {
                put_pad(&b, spacepad, ' ');
            }
            if (negative) {
                put(&b, '-');
            }
            put_pad(&b, zeropad, '0');
            while (nd) {
                put(&b, (unsigned char)digits[--nd]);
            }
            if (spacepad && left) {
                put_pad(&b, spacepad, ' ');
            }
            break;
        }
        case 'p': {
            char text[40];
            int n = snprintf(text, sizeof text, "%p", va_arg(ap, void *));
            Py_UCS4 u[40];
            for (int k = 0; k < n; k++) u[k] = (unsigned char)text[k];
            put_field(&b, u, n, width, left);
            break;
        }
        case 's': case 'V': {
            PyObject *obj = NULL;
            if (conv == 'V') {
                obj = va_arg(ap, PyObject *);
            }
            if (size == 1) {
                /* %ls and %lV: wide characters
                   (unicode_fromformat_write_wcstr) */
                const wchar_t *s = va_arg(ap, const wchar_t *);
                if (obj != NULL) {
                    put_str(&b, obj, width, prec, left);
                    break;
                }
                Py_ssize_t n = prec >= 0 ? (Py_ssize_t)wcsnlen(s, prec) : (Py_ssize_t)wcslen(s);
                Py_UCS4 *text = PyMem_Malloc((n > 0 ? n : 1) * sizeof(Py_UCS4));
                if (text == NULL) {
                    b.failed = 1;
                    break;
                }
                for (Py_ssize_t k = 0; k < n; k++) {
                    text[k] = (Py_UCS4)s[k];
                }
                put_field(&b, text, n, width, left);
                PyMem_Free(text);
                break;
            }
            const char *s = va_arg(ap, const char *);
            if (obj != NULL) {
                put_str(&b, obj, width, prec, left);
                break;
            }
            Py_ssize_t n = prec >= 0 ? (Py_ssize_t)strnlen(s, prec) : (Py_ssize_t)strlen(s);
            Py_ssize_t count = decode_utf8((const unsigned char *)s, n, NULL);
            Py_UCS4 *text = PyMem_Malloc((count + 1) * sizeof(Py_UCS4));
            if (text == NULL) {
                b.failed = 1;
                break;
            }
            decode_utf8((const unsigned char *)s, n, text);
            put_field(&b, text, count, width, left);
            PyMem_Free(text);
            break;
        }
        case 'U': case 'S': case 'R': case 'A': case 'T': case 'N': {
            PyObject *obj = va_arg(ap, PyObject *);
            PyObject *str = conv == 'U' ? Py_NewRef(obj) : conv == 'S' ? PyObject_Str(obj)
                : conv == 'R' ? PyObject_Repr(obj) : conv == 'A' ? PyObject_ASCII(obj)
                : type_name(obj, conv == 'T', alt);
            if (str == NULL) {
                b.failed = 2;
                break;
            }
            put_str(&b, str, width, prec, left);
            Py_DECREF(str);
            break;
        }
        default:
            PyErr_Format(PyExc_SystemError, "invalid format string: %s", spec);
            b.failed = 2;
            break;
        }
    }
    va_end(ap);
    PyObject *result = NULL;
    if (!b.failed) {
        result = b.n == 0 ? PyUnicode_New(0, 0)
            : PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, b.p, b.n);
    }
    else if (b.failed == 1) {
        PyErr_NoMemory();
    }
    PyMem_Free(b.p);
    return result;
}

PyObject *
PyUnicode_FromFormat(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyObject *result = PyUnicode_FromFormatV(format, ap);
    va_end(ap);
    return result;
}

PyObject *
PyErr_FormatV(PyObject *exception, const char *format, va_list vargs)
{
    PyErr_Clear();      /* %R and %S run Python, which an error already set would cut short */
    PyObject *message = PyUnicode_FromFormatV(format, vargs);
    if (message != NULL) {
        PyErr_SetObject(exception, message);
        Py_DECREF(message);
    }
    return NULL;
}

PyObject *
PyErr_Format(PyObject *exception, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyErr_FormatV(exception, format, ap);
    va_end(ap);
    return NULL;
}

#undef PyErr_BadInternalCall
void
PyErr_BadInternalCall(void)
{
    PyErr_SetString(PyExc_SystemError, "bad argument to internal function");
}

void
_PyErr_BadInternalCall(const char *filename, int lineno)
{
    PyErr_Format(PyExc_SystemError, "%s:%d: bad argument to internal function", filename, lineno);
}
