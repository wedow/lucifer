/* str layouts (Objects/unicodeobject.c, layout half): compact ASCII, UCS1,
   UCS2 and UCS4 strings, the empty and Latin-1 singletons, UTF-8 decoding
   and the cached UTF-8 form, hashing and interning. */

#include "prim/prim.h"
#include "interp/image.h"

#include <string.h>

static void
str_dealloc(PyObject *op)
{
    if (!PyUnicode_IS_ASCII(op)) {
        PyObject_Free(((PyCompactUnicodeObject *)op)->utf8);
    }
    if (!PyUnicode_IS_COMPACT(op)) {
        PyObject_Free(((PyUnicodeObject *)op)->data.any);
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
    lu_striter_init();
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
    if (size < 0) {
        lu_err_format(PyExc_SystemError, "Negative size passed to PyUnicode_New");
        return NULL;
    }
    if (size > (PY_SSIZE_T_MAX - (Py_ssize_t)sizeof(PyCompactUnicodeObject)) / kind - 1) {
        return PyErr_NoMemory();
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
    if (kind != PyUnicode_1BYTE_KIND && kind != PyUnicode_2BYTE_KIND && kind != PyUnicode_4BYTE_KIND) {
        PyErr_SetString(PyExc_SystemError, "invalid kind");
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

#define CONT(c) ((c) >= 0x80 && (c) < 0xC0)

/* One UTF-8 sequence at s[0..avail): 0 and its character, 1 invalid start byte,
   2-4 invalid continuation byte (the error covers 1 to 3 bytes), 5 truncated. */
static int
utf8_step(const unsigned char *s, Py_ssize_t avail, int surrogates, Py_UCS4 *out, int *len)
{
    Py_UCS4 ch = s[0], c2, c3, c4;
    if (ch < 0x80) { *out = ch; *len = 1; return 0; }
    if (ch < 0xE0) {
        if (ch < 0xC2) return 1;
        if (avail < 2) return 5;
        if (!CONT(s[1])) return 2;
        *out = (ch << 6) + s[1] - ((0xC0 << 6) + 0x80);
        *len = 2;
        return 0;
    }
    if (ch < 0xF0) {
        if (avail < 3) {
            if (avail < 2) return 5;
            c2 = s[1];
            if (!CONT(c2) || (c2 < 0xA0 ? ch == 0xE0 : (ch == 0xED && !surrogates))) return 2;
            return 5;
        }
        c2 = s[1]; c3 = s[2];
        if (!CONT(c2)) return 2;
        if (ch == 0xE0 ? c2 < 0xA0 : (ch == 0xED && c2 >= 0xA0 && !surrogates)) return 2;
        if (!CONT(c3)) return 3;
        *out = (ch << 12) + (c2 << 6) + c3 - ((0xE0 << 12) + (0x80 << 6) + 0x80);
        *len = 3;
        return 0;
    }
    if (ch < 0xF5) {
        if (avail < 4) {
            if (avail < 2) return 5;
            c2 = s[1];
            if (!CONT(c2) || (c2 < 0x90 ? ch == 0xF0 : ch == 0xF4)) return 2;
            if (avail < 3) return 5;
            if (!CONT(s[2])) return 3;
            return 5;
        }
        c2 = s[1]; c3 = s[2]; c4 = s[3];
        if (!CONT(c2)) return 2;
        if (ch == 0xF0 ? c2 < 0x90 : (ch == 0xF4 && c2 >= 0x90)) return 2;
        if (!CONT(c3)) return 3;
        if (!CONT(c4)) return 4;
        *out = (ch << 18) + (c2 << 12) + (c3 << 6) + c4
             - ((0xF0 << 18) + (0x80 << 12) + (0x80 << 6) + 0x80);
        *len = 4;
        return 0;
    }
    return 1;
}

#undef CONT

const char *
lu_utf8_step(const unsigned char *s, Py_ssize_t avail, int surrogatepass, Py_UCS4 *ch, int *len)
{
    int bad = utf8_step(s, avail, surrogatepass, ch, len);
    if (!bad) {
        return NULL;
    }
    if (surrogatepass) {
        /* A failed surrogatepass handler re-raises the strict decode error. */
        bad = utf8_step(s, avail, 0, ch, len);
    }
    *len = bad == 5 ? (int)avail : bad == 1 ? 1 : bad - 1;
    return bad == 1 ? "invalid start byte" : bad == 5 ? "unexpected end of data"
                                                     : "invalid continuation byte";
}

static Py_ssize_t
ascii_prefix(const unsigned char *s, Py_ssize_t size)
{
    Py_ssize_t i = 0;
    uint64_t word;
    while (size - i >= (Py_ssize_t)sizeof(word)) {
        /* memcpy allows an unaligned source without aliasing or reading past size. */
        memcpy(&word, s + i, sizeof(word));
        if (word & UINT64_C(0x8080808080808080)) {
            break;
        }
        i += sizeof(word);
    }
    while (i < size && s[i] < 0x80) {
        i++;
    }
    return i;
}

PyObject *
lu_str_decode_utf8(const char *str, Py_ssize_t size, int surrogatepass, struct lu_utf8_error *error)
{
    /* CPython checks that its initial result can fit before reading the bytes. */
    if (size > (PY_SSIZE_T_MAX - (Py_ssize_t)sizeof(PyCompactUnicodeObject)) - 1) {
        return PyErr_NoMemory();
    }
    if (size <= 0) {
        return (PyObject *)&_PyRuntime.singletons.empty_str;
    }
    const unsigned char *s = (const unsigned char *)str;
    Py_ssize_t prefix = ascii_prefix(s, size);
    if (prefix == size) {
        if (size == 1) {
            return (PyObject *)&_PyRuntime.singletons.latin1[s[0]];
        }
        PyObject *op = PyUnicode_New(size, 0x7f);
        if (op != NULL) {
            Py_UCS1 *data = PyUnicode_1BYTE_DATA(op);
            memcpy(data, s, size);
            data[size] = 0;
        }
        return op;
    }

    Py_ssize_t length = prefix;
    Py_UCS4 maxchar = 0;
    for (Py_ssize_t i = prefix; i < size; length++) {
        Py_UCS4 ch;
        int len;
        const char *reason = lu_utf8_step(s + i, size - i, surrogatepass, &ch, &len);
        if (reason != NULL) {
            *error = (struct lu_utf8_error){i, i + len, reason};
            return NULL;
        }
        maxchar = ch > maxchar ? ch : maxchar;
        i += len;
    }
    if (length == 1 && maxchar < 256) {
        return (PyObject *)&_PyRuntime.singletons.latin1[maxchar];
    }
    PyObject *op = PyUnicode_New(length, maxchar);
    if (op == NULL) {
        return NULL;
    }
    int kind = PyUnicode_KIND(op);
    void *data = PyUnicode_DATA(op);
    if (kind == PyUnicode_1BYTE_KIND) {
        memcpy(data, s, prefix);
    }
    else {
        for (Py_ssize_t i = 0; i < prefix; i++) {
            PyUnicode_WRITE(kind, data, i, s[i]);
        }
    }
    for (Py_ssize_t i = prefix, j = prefix; i < size; j++) {
        Py_UCS4 ch;
        int len;
        utf8_step(s + i, size - i, surrogatepass, &ch, &len);
        PyUnicode_WRITE(kind, data, j, ch);
        i += len;
    }
    PyUnicode_WRITE(kind, data, length, 0);
    return op;
}

PyObject *
lu_str_from_utf8(const char *str, Py_ssize_t size, int surrogatepass)
{
    struct lu_utf8_error error = {0};
    PyObject *op = lu_str_decode_utf8(str, size, surrogatepass, &error);
    if (error.reason != NULL) {
        PyObject *exc = PyUnicodeDecodeError_Create("utf-8", str, size, error.start, error.end,
                                                    error.reason);
        if (exc != NULL) {
            PyErr_SetRaisedException(exc);
        }
    }
    return op;
}

PyObject *
PyUnicode_DecodeUTF8(const char *s, Py_ssize_t size, const char *errors)
{
    int surrogatepass = errors != NULL && strcmp(errors, "surrogatepass") == 0;
    if (errors != NULL && !surrogatepass && strcmp(errors, "strict") != 0) {
        /* another error handler: the codec's, as bytes.decode runs it */
        PyObject *bytes = PyBytes_FromStringAndSize(s, size);
        PyObject *result = bytes != NULL ? PyObject_CallMethod(bytes, "decode", "ss", "utf-8", errors) : NULL;
        Py_XDECREF(bytes);
        return result;
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
    if (s == NULL && size > 0) {
        PyErr_SetString(PyExc_SystemError,
                        "NULL string with positive size with NULL passed to PyUnicode_FromStringAndSize");
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
        PyErr_BadArgument();
        if (size != NULL) {
            *size = -1;     /* the failure's size (Objects/unicodeobject.c) */
        }
        return NULL;
    }
    if (PyUnicode_IS_ASCII(op)) {
        if (size != NULL) {
            *size = PyUnicode_GET_LENGTH(op);
        }
        return (const char *)PyUnicode_DATA(op);
    }
    PyCompactUnicodeObject *c = (PyCompactUnicodeObject *)op;
    char *utf8 = __atomic_load_n(&c->utf8, __ATOMIC_ACQUIRE);
    if (utf8 == NULL) {
        int kind = PyUnicode_KIND(op);
        const void *data = PyUnicode_DATA(op);
        Py_ssize_t n = PyUnicode_GET_LENGTH(op);
        char *buf = PyObject_Malloc(n * 4 + 1), *p = buf;
        if (buf == NULL) {
            PyErr_NoMemory();
            if (size != NULL) {
                *size = -1;
            }
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
                /* The strict handler's error over the run of surrogates. */
                Py_ssize_t end = i + 1;
                while (end < n && Py_UNICODE_IS_SURROGATE(PyUnicode_READ(kind, data, end))) {
                    end++;
                }
                PyObject *exc = PyObject_CallFunction(PyExc_UnicodeEncodeError, "sOnns", "utf-8", op,
                                                      i, end, "surrogates not allowed");
                if (exc != NULL) {
                    PyErr_SetObject(PyExc_UnicodeEncodeError, exc);
                    Py_DECREF(exc);
                }
                if (size != NULL) {
                    *size = -1;
                }
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
        /* Threads may race to fill the cache: the first published copy wins,
           its length stored before it is (unicode_fill_utf8 locks). */
        __atomic_store_n(&c->utf8_length, p - buf, __ATOMIC_RELAXED);
        utf8 = NULL;
        if (!__atomic_compare_exchange_n(&c->utf8, &utf8, buf, 0, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
            PyObject_Free(buf);
        }
        else {
            utf8 = buf;
        }
    }
    if (size != NULL) {
        *size = c->utf8_length;
    }
    return utf8;
}

const char *
PyUnicode_AsUTF8(PyObject *op)
{
    return PyUnicode_AsUTF8AndSize(op, NULL);
}

/* PyUnicode_AsUTF8 for a C string: no embedded NUL. */
LU_EXPORT const char *
_PyUnicode_AsUTF8NoNUL(PyObject *op)
{
    Py_ssize_t size;
    const char *s = PyUnicode_AsUTF8AndSize(op, &size);
    if (s != NULL && strlen(s) != (size_t)size) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        return NULL;
    }
    return s;
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

/* The interned str equal to s, which is put in the table when none is: the
   code image's, whose strs are interned without being in the table until
   one is first asked for, or s itself, made immortal. NULL if the table
   could not grow. The caller holds the table's lock. */
static PyObject *
intern_miss(PyInterpreterState *interp, PyObject *s)
{
    PyObject *t = lu_ns_lookup(interp->interned, s);
    if (t != NULL) {
        return t;
    }
    t = lu_image_interned(interp, s);
    if (t != NULL) {
        return lu_ns_insert(interp->interned, t, t) < 0 ? NULL : t;
    }
    s->ob_tid = 0;
    s->ob_ref_shared = 0;
    _Py_atomic_store_uint32_relaxed(&s->ob_ref_local, _Py_IMMORTAL_REFCNT_LOCAL);
    if (lu_ns_insert(interp->interned, s, s) < 0) {
        return NULL;
    }
    _Py_atomic_store_uint8_relaxed(&((PyASCIIObject *)s)->state.interned, SSTATE_INTERNED_IMMORTAL);
    return s;
}

/* _PyUnicode_InternImmortal: in the free-threaded build every interned
   string is immortal. The interned table is per interpreter (FOUNDATIONS
   2.1); a string found there replaces *p. A miss takes the table's lock, so
   that threads interning equal strs at once get one object. */
void
lu_intern(PyInterpreterState *interp, PyObject **p)
{
    PyObject *s = *p;
    if (!PyUnicode_CheckExact(s) || PyUnicode_CHECK_INTERNED(s)) {
        return;
    }
    PyObject *t = lu_ns_lookup(interp->interned, s);
    if (t == NULL) {
        Py_BEGIN_CRITICAL_SECTION(interp->interned);
        t = intern_miss(interp, s);
        Py_END_CRITICAL_SECTION();
        if (t == NULL) {
            PyErr_Clear();      /* an uninterned string is still correct */
            return;
        }
    }
    if (t != s) {
        Py_SETREF(*p, Py_NewRef(t));
    }
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
