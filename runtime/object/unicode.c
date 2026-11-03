/* str layouts (Objects/unicodeobject.c, layout half): compact ASCII, UCS1,
   UCS2 and UCS4 strings, the empty and Latin-1 singletons, UTF-8 decoding
   and the cached UTF-8 form, hashing and interning. */

#include "lucifer.h"

#include <string.h>

static void
str_dealloc(PyObject *op)
{
    if (!PyUnicode_IS_ASCII(op)) {
        PyObject_Free(((PyCompactUnicodeObject *)op)->utf8);
    }
    lu_object_free(op);
}

static void
init_state(PyObject *op, Py_ssize_t size, int kind, int ascii)
{
    PyASCIIObject *a = (PyASCIIObject *)op;
    a->length = size;
    a->hash = -1;
    a->state.kind = kind;
    a->state.compact = 1;
    a->state.ascii = ascii;
}

static void
init_static(PyObject *op, Py_UCS1 ch, Py_ssize_t size)
{
    op->ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL;
    op->ob_flags = _Py_STATICALLY_ALLOCATED_FLAG;
    op->ob_type = &PyUnicode_Type;
    init_state(op, size, PyUnicode_1BYTE_KIND, ch < 128);
    PyASCIIObject *a = (PyASCIIObject *)op;
    a->state.statically_allocated = 1;
    a->state.interned = SSTATE_INTERNED_IMMORTAL_STATIC;
    Py_UCS1 *data = PyUnicode_1BYTE_DATA(op);
    data[0] = ch;
    data[size] = 0;
}

void
lu_str_init(void)
{
    PyUnicode_Type.tp_basicsize = sizeof(PyUnicodeObject);
    PyUnicode_Type.tp_flags |= Py_TPFLAGS_DEFAULT;
    PyUnicode_Type.tp_dealloc = str_dealloc;
    struct lu_static_objects *s = &_PyRuntime.singletons;
    init_static((PyObject *)&s->empty_str, 0, 0);
    for (int i = 0; i < 256; i++) {
        init_static((PyObject *)&s->latin1[i], (Py_UCS1)i, 1);
    }
}

PyObject *
PyUnicode_New(Py_ssize_t size, Py_UCS4 maxchar)
{
    if (size == 0) {
        return (PyObject *)&_PyRuntime.singletons.empty_str;
    }
    int kind = maxchar < 256 ? 1 : maxchar < 65536 ? 2 : 4;
    int ascii = maxchar < 128;
    if (maxchar > 0x10ffff) {
        lu_err_format(PyExc_SystemError, "invalid maximum character passed to PyUnicode_New");
        return NULL;
    }
    if (size < 0 || size > (PY_SSIZE_T_MAX - (Py_ssize_t)sizeof(PyCompactUnicodeObject)) / kind - 1) {
        lu_err_format(size < 0 ? PyExc_SystemError : PyExc_OverflowError,
                      size < 0 ? "Negative size passed to PyUnicode_New"
                               : "string is too large");
        return NULL;
    }
    size_t header = ascii ? sizeof(PyASCIIObject) : sizeof(PyCompactUnicodeObject);
    PyObject *op = lu_object_new(&PyUnicode_Type, header + (size + 1) * kind);
    if (op == NULL) {
        return NULL;
    }
    init_state(op, size, kind, ascii);
    return op;
}

PyObject *
PyUnicode_FromKindAndData(int kind, const void *buffer, Py_ssize_t size)
{
    if (size < 0) {
        lu_err_format(PyExc_ValueError, "size must be positive");
        return NULL;
    }
    Py_UCS4 maxchar = 0;
    for (Py_ssize_t i = 0; i < size; i++) {
        Py_UCS4 ch = PyUnicode_READ(kind, buffer, i);
        maxchar = ch > maxchar ? ch : maxchar;
    }
    if (size == 1 && maxchar < 256) {
        return (PyObject *)&_PyRuntime.singletons.latin1[maxchar];
    }
    PyObject *op = PyUnicode_New(size, maxchar);
    if (op == NULL || size == 0) {
        return op;
    }
    int to = PyUnicode_KIND(op);
    void *data = PyUnicode_DATA(op);
    if (to == kind) {
        memcpy(data, buffer, size * kind);
    }
    else {
        for (Py_ssize_t i = 0; i < size; i++) {
            PyUnicode_WRITE(to, data, i, PyUnicode_READ(kind, buffer, i));
        }
    }
    PyUnicode_WRITE(to, data, size, 0);
    return op;
}

/* One UTF-8 sequence at s[0..n): its code point and length, or 0 if
   malformed. Encoded surrogates are accepted only with `surrogatepass`. */
static int
utf8_char(const unsigned char *s, Py_ssize_t n, int surrogatepass, Py_UCS4 *out)
{
    unsigned char c = s[0];
    int len;
    Py_UCS4 ch, min;
    if (c < 0x80) {
        *out = c;
        return 1;
    }
    if (c >= 0xc2 && c <= 0xdf) { len = 2; ch = c & 0x1f; min = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { len = 3; ch = c & 0x0f; min = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { len = 4; ch = c & 0x07; min = 0x10000; }
    else { return 0; }
    if (n < len) {
        return 0;
    }
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            return 0;
        }
        ch = (ch << 6) | (s[i] & 0x3f);
    }
    if (ch < min || ch > 0x10ffff || (!surrogatepass && ch >= 0xd800 && ch <= 0xdfff)) {
        return 0;
    }
    *out = ch;
    return len;
}

PyObject *
lu_str_from_utf8(const char *str, Py_ssize_t size, int surrogatepass)
{
    const unsigned char *s = (const unsigned char *)str;
    Py_ssize_t length = 0;
    Py_UCS4 maxchar = 0;
    for (Py_ssize_t i = 0; i < size; length++) {
        Py_UCS4 ch;
        int n = utf8_char(s + i, size - i, surrogatepass, &ch);
        if (n == 0) {
            lu_err_format(PyExc_UnicodeDecodeError,
                          "'utf-8' codec can't decode byte 0x%02x in position %zd",
                          s[i], i);
            return NULL;
        }
        maxchar = ch > maxchar ? ch : maxchar;
        i += n;
    }
    if (length == 1 && maxchar < 256) {
        return (PyObject *)&_PyRuntime.singletons.latin1[maxchar];
    }
    PyObject *op = PyUnicode_New(length, maxchar);
    if (op == NULL || length == 0) {
        return op;
    }
    int kind = PyUnicode_KIND(op);
    void *data = PyUnicode_DATA(op);
    for (Py_ssize_t i = 0, j = 0; i < size; j++) {
        Py_UCS4 ch;
        i += utf8_char(s + i, size - i, surrogatepass, &ch);
        PyUnicode_WRITE(kind, data, j, ch);
    }
    PyUnicode_WRITE(kind, data, length, 0);
    return op;
}

PyObject *
PyUnicode_DecodeUTF8(const char *s, Py_ssize_t size, const char *errors)
{
    int surrogatepass = errors != NULL && strcmp(errors, "surrogatepass") == 0;
    if (errors != NULL && !surrogatepass && strcmp(errors, "strict") != 0) {
        lu_err_format(PyExc_LookupError, "unknown error handler name '%s'", errors);
        return NULL;
    }
    return lu_str_from_utf8(s, size, surrogatepass);
}

PyObject *
PyUnicode_FromStringAndSize(const char *s, Py_ssize_t size)
{
    if (size < 0) {
        lu_err_format(PyExc_SystemError,
                      "Negative size passed to PyUnicode_FromStringAndSize");
        return NULL;
    }
    return lu_str_from_utf8(s, size, 0);
}

PyObject *
PyUnicode_FromString(const char *s)
{
    return lu_str_from_utf8(s, (Py_ssize_t)strlen(s), 0);
}

/* The UTF-8 form, cached in the object as CPython does. Surrogates do not
   encode. Concurrent first calls take the object's critical section once
   critical sections exist (K1i). */
const char *
PyUnicode_AsUTF8AndSize(PyObject *op, Py_ssize_t *size)
{
    if (!PyUnicode_Check(op)) {
        lu_err_format(PyExc_TypeError, "bad argument type for built-in operation");
        return NULL;
    }
    if (PyUnicode_IS_ASCII(op)) {
        if (size != NULL) {
            *size = PyUnicode_GET_LENGTH(op);
        }
        return (const char *)PyUnicode_DATA(op);
    }
    PyCompactUnicodeObject *c = (PyCompactUnicodeObject *)op;
    if (c->utf8 == NULL) {
        int kind = PyUnicode_KIND(op);
        const void *data = PyUnicode_DATA(op);
        Py_ssize_t n = PyUnicode_GET_LENGTH(op);
        char *buf = PyObject_Malloc(n * 4 + 1), *p = buf;
        if (buf == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        for (Py_ssize_t i = 0; i < n; i++) {
            Py_UCS4 ch = PyUnicode_READ(kind, data, i);
            if (ch < 0x80) {
                *p++ = (char)ch;
            }
            else if (ch < 0x800) {
                *p++ = (char)(0xc0 | (ch >> 6));
                *p++ = (char)(0x80 | (ch & 0x3f));
            }
            else if (ch >= 0xd800 && ch <= 0xdfff) {
                PyObject_Free(buf);
                lu_err_format(PyExc_UnicodeEncodeError,
                              "'utf-8' codec can't encode character '\\u%04x' in position "
                              "%zd: surrogates not allowed", (unsigned)ch, i);
                return NULL;
            }
            else if (ch < 0x10000) {
                *p++ = (char)(0xe0 | (ch >> 12));
                *p++ = (char)(0x80 | ((ch >> 6) & 0x3f));
                *p++ = (char)(0x80 | (ch & 0x3f));
            }
            else {
                *p++ = (char)(0xf0 | (ch >> 18));
                *p++ = (char)(0x80 | ((ch >> 12) & 0x3f));
                *p++ = (char)(0x80 | ((ch >> 6) & 0x3f));
                *p++ = (char)(0x80 | (ch & 0x3f));
            }
        }
        *p = 0;
        c->utf8_length = p - buf;
        c->utf8 = buf;
    }
    if (size != NULL) {
        *size = c->utf8_length;
    }
    return c->utf8;
}

const char *
PyUnicode_AsUTF8(PyObject *op)
{
    return PyUnicode_AsUTF8AndSize(op, NULL);
}

/* unicode_hash: SipHash over the canonical data, cached. */
Py_hash_t
lu_str_hash(PyObject *s)
{
    PyASCIIObject *a = (PyASCIIObject *)s;
    Py_hash_t h = _Py_atomic_load_ssize_relaxed(&a->hash);
    if (h == -1) {
        h = Py_HashBuffer(PyUnicode_DATA(s), PyUnicode_GET_LENGTH(s) * PyUnicode_KIND(s));
        _Py_atomic_store_ssize_relaxed(&a->hash, h);
    }
    return h;
}

/* unicode_eq: exact strs. */
int
lu_str_eq(PyObject *a, PyObject *b)
{
    Py_ssize_t n = PyUnicode_GET_LENGTH(a);
    return n == PyUnicode_GET_LENGTH(b) && PyUnicode_KIND(a) == PyUnicode_KIND(b)
        && memcmp(PyUnicode_DATA(a), PyUnicode_DATA(b), n * PyUnicode_KIND(a)) == 0;
}

/* _PyUnicode_InternImmortal: in the free-threaded build every interned
   string is immortal. The interned table is per interpreter (FOUNDATIONS
   2.1); a string found there replaces *p. */
void
lu_intern(PyInterpreterState *interp, PyObject **p)
{
    PyObject *s = *p;
    if (!PyUnicode_CheckExact(s) || PyUnicode_CHECK_INTERNED(s)) {
        return;
    }
    PyObject *t = lu_ns_lookup(interp->interned, s);
    if (t != NULL) {
        Py_SETREF(*p, Py_NewRef(t));
        return;
    }
    if (lu_ns_insert(interp->interned, s, s) < 0) {
        PyErr_Clear();          /* an uninterned string is still correct */
        return;
    }
    s->ob_tid = 0;
    s->ob_ref_shared = 0;
    _Py_atomic_store_uint32_relaxed(&s->ob_ref_local, _Py_IMMORTAL_REFCNT_LOCAL);
    _Py_atomic_store_uint8_relaxed(&((PyASCIIObject *)s)->state.interned,
                                   SSTATE_INTERNED_IMMORTAL);
}

PyObject *
PyUnicode_InternFromString(const char *cstr)
{
    PyObject *s = PyUnicode_FromString(cstr);
    if (s != NULL) {
        lu_intern(PyInterpreterState_Get(), &s);
    }
    return s;
}
