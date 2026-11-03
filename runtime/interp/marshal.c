/* The marshal reader, adapted from Python/marshal.c v3.14.10 (r_object and
   its helpers; see third_party/cpython/CHANGES.md). It loads the frozen
   kernel before any Python exists, and backs PyMarshal_ReadObjectFromString
   and later marshal.loads. Reading from memory only; the reference list is a
   C array. Dicts take exact-str keys until the kernel's dict exists. */

#include "interp/code.h"

#include "marshal.h"

#include <stdlib.h>
#include <string.h>

#define TYPE_NULL               '0'
#define TYPE_NONE               'N'
#define TYPE_FALSE              'F'
#define TYPE_TRUE               'T'
#define TYPE_STOPITER           'S'
#define TYPE_ELLIPSIS           '.'
#define TYPE_INT                'i'
#define TYPE_INT64              'I'
#define TYPE_FLOAT              'f'
#define TYPE_BINARY_FLOAT       'g'
#define TYPE_COMPLEX            'x'
#define TYPE_BINARY_COMPLEX     'y'
#define TYPE_LONG               'l'
#define TYPE_STRING             's'
#define TYPE_INTERNED           't'
#define TYPE_REF                'r'
#define TYPE_TUPLE              '('
#define TYPE_LIST               '['
#define TYPE_DICT               '{'
#define TYPE_CODE               'c'
#define TYPE_UNICODE            'u'
#define TYPE_UNKNOWN            '?'
#define TYPE_SET                '<'
#define TYPE_FROZENSET          '>'
#define TYPE_SLICE              ':'
#define TYPE_ASCII              'a'
#define TYPE_ASCII_INTERNED     'A'
#define TYPE_SMALL_TUPLE        ')'
#define TYPE_SHORT_ASCII        'z'
#define TYPE_SHORT_ASCII_INTERNED 'Z'
#define FLAG_REF                '\x80'

#define MAX_MARSHAL_STACK_DEPTH 2000
#define SIZE32_MAX 0x7FFFFFFF
#define PyLong_MARSHAL_SHIFT 15
#define PyLong_MARSHAL_BASE ((short)1 << PyLong_MARSHAL_SHIFT)
#define PyLong_MARSHAL_RATIO (PyLong_SHIFT / PyLong_MARSHAL_SHIFT)

typedef struct {
    const char *ptr;
    const char *end;
    int depth;
    PyObject **refs;            /* owned references; NULL marks a reserved slot */
    Py_ssize_t nrefs, refs_cap;
    int allow_code;
} RFILE;

static PyObject *r_object(RFILE *p);

static const char *
r_string(Py_ssize_t n, RFILE *p)
{
    if (p->end - p->ptr < n) {
        PyErr_SetString(PyExc_EOFError, "marshal data too short");
        return NULL;
    }
    const char *res = p->ptr;
    p->ptr += n;
    return res;
}

static int
r_byte(RFILE *p)
{
    if (p->ptr < p->end) {
        return (unsigned char)*p->ptr++;
    }
    PyErr_SetString(PyExc_EOFError, "EOF read where not expected");
    return EOF;
}

static int
r_short(RFILE *p)
{
    const unsigned char *b = (const unsigned char *)r_string(2, p);
    if (b == NULL) {
        return -1;
    }
    short x = b[0] | b[1] << 8;
    return x;
}

static long
r_long(RFILE *p)
{
    const unsigned char *b = (const unsigned char *)r_string(4, p);
    if (b == NULL) {
        return -1;
    }
    long x = b[0] | (long)b[1] << 8 | (long)b[2] << 16 | (long)b[3] << 24;
    return x | -(x & 0x80000000L);
}

/* A little-endian two's-complement integer of n bytes. */
static PyObject *
long_from_bytes(const unsigned char *b, int n)
{
    int negative = (b[n - 1] & 0x80) != 0;
    unsigned char mag[8];
    int carry = 1;
    for (int i = 0; i < n; i++) {
        unsigned v = negative ? (unsigned char)~b[i] + carry : b[i];
        carry = negative && v > 0xff;
        mag[i] = (unsigned char)v;
    }
    digit digits[3] = { 0 };
    for (int bit = 0; bit < n * 8; bit++) {
        if (mag[bit / 8] >> (bit % 8) & 1) {
            digits[bit / PyLong_SHIFT] |= (digit)1 << (bit % PyLong_SHIFT);
        }
    }
    return lu_long_from_digits(negative, 3, digits);
}

static PyObject *
r_PyLong(RFILE *p)
{
    long n = r_long(p);
    if (n == -1 && PyErr_Occurred()) {
        return NULL;
    }
    if (n < -SIZE32_MAX || n > SIZE32_MAX) {
        PyErr_SetString(PyExc_ValueError, "bad marshal data (long size out of range)");
        return NULL;
    }
    Py_ssize_t size = 1 + (labs(n) - 1) / PyLong_MARSHAL_RATIO;
    int shorts_in_top_digit = 1 + (labs(n) - 1) % PyLong_MARSHAL_RATIO;
    digit *digits = PyMem_Malloc(size * sizeof(digit));
    if (digits == NULL) {
        return PyErr_NoMemory();
    }
    PyObject *result = NULL;
    for (Py_ssize_t i = 0; i < size; i++) {
        int shorts = i < size - 1 ? PyLong_MARSHAL_RATIO : shorts_in_top_digit;
        digit d = 0;
        for (int j = 0; j < shorts; j++) {
            int md = r_short(p);
            if (md < 0 || md > PyLong_MARSHAL_BASE) {
                if (!PyErr_Occurred()) {
                    PyErr_SetString(PyExc_ValueError,
                                    "bad marshal data (digit out of range in long)");
                }
                goto done;
            }
            if (md == 0 && i == size - 1 && j == shorts - 1) {
                PyErr_SetString(PyExc_ValueError, "bad marshal data (unnormalized long data)");
                goto done;
            }
            d += (digit)md << j * PyLong_MARSHAL_SHIFT;
        }
        digits[i] = d;
    }
    result = lu_long_from_digits(n < 0, size, digits);
done:
    PyMem_Free(digits);
    return result;
}

static double
r_float_bin(RFILE *p)
{
    const char *buf = r_string(8, p);
    return buf != NULL ? PyFloat_Unpack8(buf, 1) : -1;
}

/* Version 0 floats as repr text. CPython parses them with dtoa.c's
   PyOS_string_to_double; glibc's strtod rounds correctly too, and no
   version-0 data reaches the runtime but marshal.loads of old dumps. */
static double
r_float_str(RFILE *p)
{
    int n = r_byte(p);
    if (n == EOF) {
        return -1;
    }
    const char *ptr = r_string(n, p);
    if (ptr == NULL) {
        return -1;
    }
    char buf[256];
    memcpy(buf, ptr, n);
    buf[n] = '\0';
    char *end;
    double x = strtod(buf, &end);
    if (end == buf || *end != '\0') {
        lu_err_format(PyExc_ValueError, "could not convert string to float: '%s'", buf);
        return -1;
    }
    return x;
}

static int
refs_push(RFILE *p, PyObject *o)
{
    if (p->nrefs >= 0x7ffffffe) {
        PyErr_SetString(PyExc_ValueError, "bad marshal data (index list too large)");
        return -1;
    }
    if (p->nrefs == p->refs_cap) {
        Py_ssize_t cap = p->refs_cap ? p->refs_cap * 2 : 64;
        PyObject **refs = PyMem_Realloc(p->refs, cap * sizeof *refs);
        if (refs == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        p->refs = refs;
        p->refs_cap = cap;
    }
    p->refs[p->nrefs++] = Py_XNewRef(o);
    return 0;
}

/* r_ref_reserve: the slot of an object whose children are read first. */
static Py_ssize_t
r_ref_reserve(int flag, RFILE *p)
{
    if (!flag) {
        return 0;
    }
    Py_ssize_t idx = p->nrefs;
    return refs_push(p, NULL) < 0 ? -1 : idx;
}

static PyObject *
r_ref_insert(PyObject *o, Py_ssize_t idx, int flag, RFILE *p)
{
    if (o != NULL && flag) {
        p->refs[idx] = Py_NewRef(o);
    }
    return o;
}

static PyObject *
r_ref(PyObject *o, int flag, RFILE *p)
{
    if (o != NULL && refs_push(p, o) < 0) {
        Py_DECREF(o);
        return NULL;
    }
    return o;
}

static PyObject *
r_str(RFILE *p, long n, int ascii, int is_interned)
{
    if (n < 0 || n > SIZE32_MAX) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError, "bad marshal data (string size out of range)");
        }
        return NULL;
    }
    const char *ptr = n ? r_string(n, p) : "";
    if (ptr == NULL) {
        return NULL;
    }
    PyObject *v = ascii ? PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, ptr, n)
                        : lu_str_from_utf8(ptr, n, 1);
    if (v != NULL && is_interned) {
        /* Code-related strings are immortal, as in CPython. */
        lu_intern(PyInterpreterState_Get(), &v);
    }
    return v;
}

static long
r_size(RFILE *p, const char *what)
{
    long n = r_long(p);
    if (n < 0 || n > SIZE32_MAX) {
        if (!PyErr_Occurred()) {
            lu_err_format(PyExc_ValueError, "bad marshal data (%s size out of range)", what);
        }
        return -1;
    }
    return n;
}

static PyObject *
r_code(RFILE *p, int flag)
{
    if (!p->allow_code) {
        PyErr_SetString(PyExc_ValueError, "unmarshalling code objects is disallowed");
        return NULL;
    }
    Py_ssize_t idx = r_ref_reserve(flag, p);
    if (idx < 0) {
        return NULL;
    }
    struct lu_code_fields c = { 0 };
    PyObject *v = NULL;
    long ints[5];
    for (int i = 0; i < 5; i++) {
        ints[i] = r_long(p);
        if (ints[i] == -1 && PyErr_Occurred()) {
            goto done;
        }
    }
    c.argcount = (int)ints[0];
    c.posonlyargcount = (int)ints[1];
    c.kwonlyargcount = (int)ints[2];
    c.stacksize = (int)ints[3];
    c.flags = (int)ints[4];
    PyObject **objects[] = { &c.code, &c.consts, &c.names, &c.localsplusnames,
                             &c.localspluskinds, &c.filename, &c.name, &c.qualname };
    for (size_t i = 0; i < sizeof objects / sizeof *objects; i++) {
        if ((*objects[i] = r_object(p)) == NULL) {
            goto done;
        }
    }
    long firstlineno = r_long(p);
    if (firstlineno == -1 && PyErr_Occurred()) {
        goto done;
    }
    c.firstlineno = (int)firstlineno;
    if ((c.linetable = r_object(p)) == NULL || (c.exceptiontable = r_object(p)) == NULL) {
        goto done;
    }
    v = r_ref_insert(lu_code_new(&c), idx, flag, p);
done:
    if (v == NULL && !PyErr_Occurred()) {
        PyErr_SetString(PyExc_TypeError, "NULL object in marshal data for code object");
    }
    Py_XDECREF(c.code);
    Py_XDECREF(c.consts);
    Py_XDECREF(c.names);
    Py_XDECREF(c.localsplusnames);
    Py_XDECREF(c.localspluskinds);
    Py_XDECREF(c.filename);
    Py_XDECREF(c.name);
    Py_XDECREF(c.qualname);
    Py_XDECREF(c.linetable);
    Py_XDECREF(c.exceptiontable);
    return v;
}

/* Tuples and lists: n items into a fresh container. */
static PyObject *
r_items(RFILE *p, PyObject *v, long n, PyObject **items, const char *what)
{
    for (long i = 0; v != NULL && i < n; i++) {
        PyObject *item = r_object(p);
        if (item == NULL) {
            if (!PyErr_Occurred()) {
                lu_err_format(PyExc_TypeError, "NULL object in marshal data for %s", what);
            }
            Py_SETREF(v, NULL);
            break;
        }
        items[i] = item;
    }
    return v;
}

static PyObject *
r_object(RFILE *p)
{
    PyObject *v, *retval = NULL;
    long n;
    int code = r_byte(p);
    int is_interned = 0;
    if (code == EOF) {
        if (PyErr_ExceptionMatches(PyExc_EOFError)) {
            PyErr_SetString(PyExc_EOFError, "EOF read where object expected");
        }
        return NULL;
    }
    if (++p->depth > MAX_MARSHAL_STACK_DEPTH) {
        p->depth--;
        PyErr_SetString(PyExc_ValueError, "recursion limit exceeded");
        return NULL;
    }
    int flag = code & FLAG_REF;
    int type = code & ~FLAG_REF;

#define R_REF(O) do { if (flag) O = r_ref(O, flag, p); } while (0)

    switch (type) {
    case TYPE_NULL:
        break;
    case TYPE_NONE:
        retval = Py_None;
        break;
    case TYPE_STOPITER:
        retval = Py_NewRef(PyExc_StopIteration);
        break;
    case TYPE_ELLIPSIS:
        retval = Py_Ellipsis;
        break;
    case TYPE_FALSE:
        retval = Py_False;
        break;
    case TYPE_TRUE:
        retval = Py_True;
        break;
    case TYPE_INT:
        n = r_long(p);
        if (n == -1 && PyErr_Occurred()) {
            break;
        }
        retval = PyLong_FromLong(n);
        R_REF(retval);
        break;
    case TYPE_INT64: {
        const unsigned char *b = (const unsigned char *)r_string(8, p);
        if (b == NULL) {
            break;
        }
        retval = long_from_bytes(b, 8);
        R_REF(retval);
        break;
    }
    case TYPE_LONG:
        retval = r_PyLong(p);
        R_REF(retval);
        break;
    case TYPE_FLOAT:
    case TYPE_BINARY_FLOAT: {
        double x = type == TYPE_FLOAT ? r_float_str(p) : r_float_bin(p);
        if (x == -1.0 && PyErr_Occurred()) {
            break;
        }
        retval = PyFloat_FromDouble(x);
        R_REF(retval);
        break;
    }
    case TYPE_COMPLEX:
    case TYPE_BINARY_COMPLEX: {
        Py_complex c;
        c.real = type == TYPE_COMPLEX ? r_float_str(p) : r_float_bin(p);
        if (c.real == -1.0 && PyErr_Occurred()) {
            break;
        }
        c.imag = type == TYPE_COMPLEX ? r_float_str(p) : r_float_bin(p);
        if (c.imag == -1.0 && PyErr_Occurred()) {
            break;
        }
        retval = PyComplex_FromCComplex(c);
        R_REF(retval);
        break;
    }
    case TYPE_STRING: {
        n = r_size(p, "bytes object");
        const char *ptr = n >= 0 ? r_string(n, p) : NULL;
        if (ptr == NULL) {
            break;
        }
        retval = PyBytes_FromStringAndSize(ptr, n);
        R_REF(retval);
        break;
    }
    case TYPE_ASCII_INTERNED:
        is_interned = 1;
        /* fall through */
    case TYPE_ASCII:
        retval = r_str(p, r_long(p), 1, is_interned);
        R_REF(retval);
        break;
    case TYPE_SHORT_ASCII_INTERNED:
        is_interned = 1;
        /* fall through */
    case TYPE_SHORT_ASCII:
        n = r_byte(p);
        if (n == EOF) {
            break;
        }
        retval = r_str(p, n, 1, is_interned);
        R_REF(retval);
        break;
    case TYPE_INTERNED:
        is_interned = 1;
        /* fall through */
    case TYPE_UNICODE:
        retval = r_str(p, r_long(p), 0, is_interned);
        R_REF(retval);
        break;
    case TYPE_SMALL_TUPLE:
    case TYPE_TUPLE:
        n = type == TYPE_SMALL_TUPLE ? r_byte(p) : r_size(p, "tuple");
        if (n < 0) {
            break;
        }
        v = PyTuple_New(n);
        R_REF(v);
        retval = v ? r_items(p, v, n, ((PyTupleObject *)v)->ob_item, "tuple") : NULL;
        break;
    case TYPE_LIST:
        n = r_size(p, "list");
        if (n < 0) {
            break;
        }
        v = PyList_New(n);
        R_REF(v);
        retval = v ? r_items(p, v, n, ((PyListObject *)v)->ob_item, "list") : NULL;
        break;
    case TYPE_DICT:
        v = PyDict_New();
        R_REF(v);
        if (v == NULL) {
            break;
        }
        for (;;) {
            PyObject *key = r_object(p);
            if (key == NULL) {
                break;
            }
            PyObject *val = r_object(p);
            int err = val == NULL || lu_ns_insert(v, key, val) < 0;
            Py_DECREF(key);
            Py_XDECREF(val);
            if (err) {
                break;
            }
        }
        if (PyErr_Occurred()) {
            Py_SETREF(v, NULL);
        }
        retval = v;
        break;
    case TYPE_SET:
    case TYPE_FROZENSET: {
        n = r_size(p, "set");
        if (n < 0) {
            break;
        }
        Py_ssize_t idx = 0;
        v = lu_set_new(type == TYPE_SET ? &PySet_Type : &PyFrozenSet_Type);
        if (type == TYPE_SET) {
            R_REF(v);
        }
        else if ((idx = r_ref_reserve(flag, p)) < 0) {
            /* Frozensets register after their items, at refcount 1. */
            Py_CLEAR(v);
        }
        for (long i = 0; v != NULL && i < n; i++) {
            PyObject *item = r_object(p);
            if (item == NULL) {
                if (!PyErr_Occurred()) {
                    PyErr_SetString(PyExc_TypeError, "NULL object in marshal data for set");
                }
                Py_CLEAR(v);
                break;
            }
            if (lu_set_add(v, item) < 0) {
                Py_CLEAR(v);
            }
            Py_DECREF(item);
        }
        retval = type == TYPE_SET ? v : r_ref_insert(v, idx, flag, p);
        break;
    }
    case TYPE_CODE:
        retval = r_code(p, flag);
        break;
    case TYPE_REF:
        n = r_long(p);
        if (n < 0 || n >= p->nrefs || p->refs[n] == NULL) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_ValueError, "bad marshal data (invalid reference)");
            }
            break;
        }
        retval = Py_NewRef(p->refs[n]);
        break;
    case TYPE_SLICE: {
        Py_ssize_t idx = r_ref_reserve(flag, p);
        if (idx < 0) {
            break;
        }
        PyObject *start = r_object(p);
        PyObject *stop = start ? r_object(p) : NULL;
        PyObject *step = stop ? r_object(p) : NULL;
        if (step != NULL) {
            retval = r_ref_insert(PySlice_New(start, stop, step), idx, flag, p);
        }
        Py_XDECREF(start);
        Py_XDECREF(stop);
        Py_XDECREF(step);
        break;
    }
    default:
        PyErr_SetString(PyExc_ValueError, "bad marshal data (unknown type code)");
        break;
    }
#undef R_REF
    p->depth--;
    return retval;
}

PyObject *
PyMarshal_ReadObjectFromString(const char *str, Py_ssize_t len)
{
    RFILE rf = { .ptr = str, .end = str + len, .allow_code = 1 };
    PyObject *result = r_object(&rf);
    if (result == NULL && !PyErr_Occurred()) {
        PyErr_SetString(PyExc_TypeError, "NULL object in marshal data for object");
    }
    for (Py_ssize_t i = 0; i < rf.nrefs; i++) {
        Py_XDECREF(rf.refs[i]);
    }
    PyMem_Free(rf.refs);
    return result;
}

/* The writer (w_object and its helpers). Objects seen more than once, or not
   uniquely referenced, are written with FLAG_REF and later as TYPE_REF, so the
   bytes depend on reference counts exactly as CPython's do. */

#include "interp/specialize.h"
#include "object/dict.h"

#define MAX_WRITE_DEPTH 2000

enum { WFERR_OK, WFERR_UNMARSHALLABLE, WFERR_NESTEDTOODEEP, WFERR_NOMEMORY, WFERR_CODE_NOT_ALLOWED,
       WFERR_SET_CHANGED_SIZE };

struct wref {
    PyObject *obj;
    int incomplete;             /* a code object or slice whose children are being written */
};

typedef struct {
    char *buf;
    size_t len, cap;
    int error, depth, version, allow_code;
    struct wref *refs;          /* by index; strong references */
    Py_ssize_t nrefs, refs_cap;
    Py_ssize_t *index;          /* open addressing over pointers: ref index + 1, 0 empty */
    size_t index_cap;
} WFILE;

static void w_object(PyObject *v, WFILE *p);

static int
w_reserve(WFILE *p, size_t n)
{
    if (p->error != WFERR_OK) {
        return 0;
    }
    if (p->len + n > p->cap) {
        size_t cap = p->cap ? p->cap * 2 : 256;
        while (cap < p->len + n) {
            cap *= 2;
        }
        char *buf = PyMem_Realloc(p->buf, cap);
        if (buf == NULL) {
            p->error = WFERR_NOMEMORY;
            return 0;
        }
        p->buf = buf;
        p->cap = cap;
    }
    return 1;
}

static void
w_string(const void *s, Py_ssize_t n, WFILE *p)
{
    if (n > 0 && w_reserve(p, n)) {
        memcpy(p->buf + p->len, s, n);
        p->len += n;
    }
}

static void
w_byte(char c, WFILE *p)
{
    w_string(&c, 1, p);
}

static void
w_short(int x, WFILE *p)
{
    w_byte((char)(x & 0xff), p);
    w_byte((char)((x >> 8) & 0xff), p);
}

static void
w_long(long x, WFILE *p)
{
    w_short((int)(x & 0xffff), p);
    w_short((int)((x >> 16) & 0xffff), p);
}

#define W_TYPE(t, p) w_byte((char)((t) | flag), (p))

/* A size that must fit in 32 bits; false after setting the error. */
static int
w_size(Py_ssize_t n, WFILE *p)
{
    if (n > SIZE32_MAX) {
        p->error = WFERR_UNMARSHALLABLE;
        return 0;
    }
    w_long((long)n, p);
    return 1;
}

static void
w_pstring(const void *s, Py_ssize_t n, WFILE *p)
{
    if (w_size(n, p)) {
        w_string(s, n, p);
    }
}

static void
w_short_pstring(const void *s, Py_ssize_t n, WFILE *p)
{
    w_byte((char)n, p);
    w_string(s, n, p);
}

/* Python ints are written in base 2**15 whatever their layout. */
static void
w_PyLong(PyObject *v, char flag, WFILE *p)
{
    PyLongObject *ob = (PyLongObject *)v;
    W_TYPE(TYPE_LONG, p);
    Py_ssize_t n = (Py_ssize_t)(ob->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    if (n == 0) {
        w_long(0, p);
        return;
    }
    const digit *digits = ob->long_value.ob_digit;
    Py_ssize_t l = (n - 1) * PyLong_MARSHAL_RATIO;
    for (digit d = digits[n - 1]; d != 0; d >>= PyLong_MARSHAL_SHIFT) {
        l++;
    }
    if (l > SIZE32_MAX) {
        p->error = WFERR_UNMARSHALLABLE;
        return;
    }
    int negative = (ob->long_value.lv_tag & _PyLong_SIGN_MASK) == 2;
    w_long((long)(negative ? -l : l), p);
    for (Py_ssize_t i = 0; i < n - 1; i++) {
        digit d = digits[i];
        for (int j = 0; j < PyLong_MARSHAL_RATIO; j++) {
            w_short(d & (PyLong_MARSHAL_BASE - 1), p);
            d >>= PyLong_MARSHAL_SHIFT;
        }
    }
    digit d = digits[n - 1];
    do {
        w_short(d & (PyLong_MARSHAL_BASE - 1), p);
        d >>= PyLong_MARSHAL_SHIFT;
    } while (d != 0);
}

static void
w_float_bin(double v, WFILE *p)
{
    w_string(&v, 8, p);                 /* little-endian hosts only, like the reader */
}

static void
w_float_str(double v, WFILE *p)
{
    char *buf = PyOS_double_to_string(v, 'g', 17, 0, NULL);
    if (buf == NULL) {
        p->error = WFERR_NOMEMORY;
        return;
    }
    w_short_pstring(buf, strlen(buf), p);
    PyMem_Free(buf);
}

/* The refcount test of _PyObject_IsUniquelyReferenced. */
static int
uniquely_referenced(PyObject *v)
{
    return _Py_IsOwnedByCurrentThread(v) && v->ob_ref_local == 1
        && _Py_atomic_load_ssize_relaxed(&v->ob_ref_shared) == 0;
}

static Py_ssize_t
ref_find(WFILE *p, PyObject *v, size_t *slot)
{
    size_t mask = p->index_cap - 1;
    size_t i = ((uintptr_t)v >> 4) * 0x9E3779B97F4A7C15ull >> 20 & mask;
    while (p->index[i] != 0) {
        if (p->refs[p->index[i] - 1].obj == v) {
            *slot = i;
            return p->index[i] - 1;
        }
        i = (i + 1) & mask;
    }
    *slot = i;
    return -1;
}

static int
ref_grow(WFILE *p)
{
    size_t cap = p->index_cap ? p->index_cap * 2 : 256;
    Py_ssize_t *index = PyMem_Calloc(cap, sizeof *index);
    if (index == NULL) {
        return -1;
    }
    PyMem_Free(p->index);
    p->index = index;
    p->index_cap = cap;
    for (Py_ssize_t k = 0; k < p->nrefs; k++) {
        size_t slot;
        ref_find(p, p->refs[k].obj, &slot);
        p->index[slot] = k + 1;
    }
    return 0;
}

/* w_ref: true when v was written as a reference. `shared` forces the entry for
   an object that is held elsewhere in CPython but not here (co_code). */
static int
w_ref(PyObject *v, char *flag, WFILE *p, int shared)
{
    if (p->version < 3) {
        return 0;
    }
    if (!shared && uniquely_referenced(v)
        && !(PyUnicode_CheckExact(v) && PyUnicode_CHECK_INTERNED(v))) {
        return 0;
    }
    size_t slot;
    if (p->index_cap == 0 && ref_grow(p) < 0) {
        p->error = WFERR_NOMEMORY;
        return 1;
    }
    Py_ssize_t k = ref_find(p, v, &slot);
    if (k >= 0) {
        if (p->refs[k].incomplete) {
            lu_err_format(PyExc_ValueError, "cannot marshal recursion %s objects", Py_TYPE(v)->tp_name);
            p->error = WFERR_UNMARSHALLABLE;
            return 1;
        }
        w_byte(TYPE_REF, p);
        w_long((long)k, p);
        return 1;
    }
    if (p->nrefs >= SIZE32_MAX) {
        PyErr_SetString(PyExc_ValueError, "too many objects");
        p->error = WFERR_UNMARSHALLABLE;
        return 1;
    }
    if (p->nrefs == p->refs_cap) {
        Py_ssize_t cap = p->refs_cap ? p->refs_cap * 2 : 64;
        struct wref *refs = PyMem_Realloc(p->refs, cap * sizeof *refs);
        if (refs == NULL) {
            p->error = WFERR_NOMEMORY;
            return 1;
        }
        p->refs = refs;
        p->refs_cap = cap;
    }
    if ((size_t)(p->nrefs + 1) * 2 > p->index_cap) {
        if (ref_grow(p) < 0) {
            p->error = WFERR_NOMEMORY;
            return 1;
        }
        ref_find(p, v, &slot);
    }
    p->refs[p->nrefs] = (struct wref){ Py_NewRef(v), PyCode_Check(v) || PySlice_Check(v) };
    p->index[slot] = ++p->nrefs;
    *flag |= FLAG_REF;
    return 0;
}

static void
w_complete(PyObject *v, WFILE *p)
{
    if (p->version < 3 || uniquely_referenced(v)) {
        return;
    }
    size_t slot;
    Py_ssize_t k = p->index_cap ? ref_find(p, v, &slot) : -1;
    if (k >= 0) {
        p->refs[k].incomplete = 0;
    }
}

static PyObject *write_to_bytes(PyObject *x, int version, int allow_code);

/* An element of a set, as the bytes it marshals to; sets are written in the
   order of those bytes so that the output is reproducible. */
struct set_item {
    PyObject *dump, *value;
    Py_ssize_t index;
};

static int
set_item_cmp(const void *a, const void *b)
{
    const struct set_item *x = a, *y = b;
    Py_ssize_t nx = PyBytes_GET_SIZE(x->dump), ny = PyBytes_GET_SIZE(y->dump);
    int c = memcmp(PyBytes_AS_STRING(x->dump), PyBytes_AS_STRING(y->dump), nx < ny ? nx : ny);
    if (c != 0) {
        return c;
    }
    return nx != ny ? (nx < ny ? -1 : 1) : (x->index < y->index ? -1 : x->index > y->index);
}

static void
w_set(PyObject *v, char flag, WFILE *p)
{
    PySetObject *so = (PySetObject *)v;
    W_TYPE(PyFrozenSet_CheckExact(v) ? TYPE_FROZENSET : TYPE_SET, p);
    Py_ssize_t n = PySet_GET_SIZE(v);
    if (!w_size(n, p)) {
        return;
    }
    struct set_item *items = PyMem_Calloc(n ? n : 1, sizeof *items);
    if (items == NULL) {
        p->error = WFERR_NOMEMORY;
        return;
    }
    Py_ssize_t count = 0;
    for (Py_ssize_t i = 0; i <= so->mask && p->error == WFERR_OK; i++) {
        PyObject *key = so->table[i].key;
        if (key == NULL || key == LU_SET_DUMMY) {
            continue;
        }
        if (count == n) {
            p->error = WFERR_SET_CHANGED_SIZE;
            break;
        }
        items[count].dump = write_to_bytes(key, p->version, p->allow_code);
        if (items[count].dump == NULL) {
            p->error = WFERR_UNMARSHALLABLE;
            break;
        }
        items[count].value = key;
        items[count].index = count;
        count++;
    }
    if (p->error == WFERR_OK && count != n) {
        p->error = WFERR_SET_CHANGED_SIZE;
    }
    if (p->error == WFERR_OK) {
        qsort(items, n, sizeof *items, set_item_cmp);
        for (Py_ssize_t i = 0; i < n; i++) {
            w_object(items[i].value, p);
        }
    }
    for (Py_ssize_t i = 0; i < count; i++) {
        Py_DECREF(items[i].dump);
    }
    PyMem_Free(items);
}

/* co_code as CPython marshals it: the original opcodes, caches cleared. */
static PyObject *
code_bytes(PyCodeObject *co)
{
    Py_ssize_t n = Py_SIZE(co);
    PyObject *bytes = PyBytes_FromStringAndSize(NULL, n * (Py_ssize_t)sizeof(lu_codeunit));
    if (bytes == NULL) {
        return NULL;
    }
    lu_codeunit *out = (lu_codeunit *)PyBytes_AS_STRING(bytes);
    const lu_codeunit *in = (const lu_codeunit *)co->co_code_adaptive;
    for (Py_ssize_t i = 0; i < n; i++) {
        uint8_t op = in[i].op.code;
        switch (op) {
#define DEOPT(special, generic) case special: op = generic; break;
        LU_SPECIALIZED_OPCODES(DEOPT)
#undef DEOPT
        }
        out[i] = in[i];
        out[i].op.code = op;
        out[i].op.arg = in[i].op.arg;
        for (int j = lu_opcode_caches[op]; j > 0 && i + 1 < n; j--) {
            out[++i].cache = 0;
        }
    }
    return bytes;
}

static void
w_code(PyObject *v, char flag, WFILE *p)
{
    if (!p->allow_code) {
        p->error = WFERR_CODE_NOT_ALLOWED;
        return;
    }
    PyCodeObject *co = (PyCodeObject *)v;
    PyObject *co_code = code_bytes(co);
    if (co_code == NULL) {
        p->error = WFERR_NOMEMORY;
        return;
    }
    W_TYPE(TYPE_CODE, p);
    w_long(co->co_argcount, p);
    w_long(co->co_posonlyargcount, p);
    w_long(co->co_kwonlyargcount, p);
    w_long(co->co_stacksize, p);
    w_long(co->co_flags & ~(LU_CO_KERNEL | LU_CO_HIDDEN), p);
    /* CPython caches co_code in the code object, so it always has two owners. */
    char code_flag = 0;
    if (!w_ref(co_code, &code_flag, p, 1)) {
        w_byte((char)(TYPE_STRING | code_flag), p);
        w_pstring(PyBytes_AS_STRING(co_code), PyBytes_GET_SIZE(co_code), p);
    }
    Py_DECREF(co_code);
    w_object(co->co_consts, p);
    w_object(co->co_names, p);
    w_object(co->co_localsplusnames, p);
    w_object(co->co_localspluskinds, p);
    w_object(co->co_filename, p);
    w_object(co->co_name, p);
    w_object(co->co_qualname, p);
    w_long(co->co_firstlineno, p);
    w_object(co->co_linetable, p);
    w_object(co->co_exceptiontable, p);
    w_complete(v, p);
}

static void
w_str(PyObject *v, char flag, WFILE *p)
{
    int interned = PyUnicode_CHECK_INTERNED(v) != 0;
    Py_ssize_t n = PyUnicode_GET_LENGTH(v);
    if (p->version >= 4 && PyUnicode_IS_ASCII(v)) {
        if (n < 256) {
            W_TYPE(interned ? TYPE_SHORT_ASCII_INTERNED : TYPE_SHORT_ASCII, p);
            w_short_pstring(PyUnicode_DATA(v), n, p);
        }
        else {
            W_TYPE(interned ? TYPE_ASCII_INTERNED : TYPE_ASCII, p);
            w_pstring(PyUnicode_DATA(v), n, p);
        }
        return;
    }
    /* UTF-8 with surrogatepass */
    int kind = PyUnicode_KIND(v);
    const void *data = PyUnicode_DATA(v);
    char *utf8 = PyMem_Malloc(n * 4 + 1), *out = utf8;
    if (utf8 == NULL) {
        p->error = WFERR_NOMEMORY;
        return;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        Py_UCS4 ch = PyUnicode_READ(kind, data, i);
        if (ch < 0x80) {
            *out++ = (char)ch;
        }
        else if (ch < 0x800) {
            *out++ = (char)(0xc0 | (ch >> 6));
            *out++ = (char)(0x80 | (ch & 0x3f));
        }
        else if (ch < 0x10000) {
            *out++ = (char)(0xe0 | (ch >> 12));
            *out++ = (char)(0x80 | ((ch >> 6) & 0x3f));
            *out++ = (char)(0x80 | (ch & 0x3f));
        }
        else {
            *out++ = (char)(0xf0 | (ch >> 18));
            *out++ = (char)(0x80 | ((ch >> 12) & 0x3f));
            *out++ = (char)(0x80 | ((ch >> 6) & 0x3f));
            *out++ = (char)(0x80 | (ch & 0x3f));
        }
    }
    W_TYPE(p->version >= 3 && interned ? TYPE_INTERNED : TYPE_UNICODE, p);
    w_pstring(utf8, out - utf8, p);
    PyMem_Free(utf8);
}

static void
w_complex_object(PyObject *v, char flag, WFILE *p)
{
    if (PyLong_CheckExact(v)) {
        PyLongObject *ob = (PyLongObject *)v;
        Py_ssize_t n = (Py_ssize_t)(ob->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
        int64_t x = 0;
        if (n <= 2) {
            uint64_t magnitude = n == 0 ? 0 : n == 1 ? ob->long_value.ob_digit[0]
                : ob->long_value.ob_digit[0] | (uint64_t)ob->long_value.ob_digit[1] << PyLong_SHIFT;
            x = (ob->long_value.lv_tag & _PyLong_SIGN_MASK) == 2 ? -(int64_t)magnitude : (int64_t)magnitude;
        }
        if (n > 2 || x < INT32_MIN || x > INT32_MAX) {
            w_PyLong(v, flag, p);
        }
        else {
            W_TYPE(TYPE_INT, p);
            w_long((long)x, p);
        }
    }
    else if (PyFloat_CheckExact(v)) {
        W_TYPE(p->version > 1 ? TYPE_BINARY_FLOAT : TYPE_FLOAT, p);
        (p->version > 1 ? w_float_bin : w_float_str)(PyFloat_AS_DOUBLE(v), p);
    }
    else if (PyComplex_CheckExact(v)) {
        Py_complex c = ((PyComplexObject *)v)->cval;
        W_TYPE(p->version > 1 ? TYPE_BINARY_COMPLEX : TYPE_COMPLEX, p);
        (p->version > 1 ? w_float_bin : w_float_str)(c.real, p);
        (p->version > 1 ? w_float_bin : w_float_str)(c.imag, p);
    }
    else if (PyBytes_CheckExact(v)) {
        W_TYPE(TYPE_STRING, p);
        w_pstring(PyBytes_AS_STRING(v), PyBytes_GET_SIZE(v), p);
    }
    else if (PyUnicode_CheckExact(v)) {
        w_str(v, flag, p);
    }
    else if (PyTuple_CheckExact(v)) {
        Py_ssize_t n = PyTuple_GET_SIZE(v);
        if (p->version >= 4 && n < 256) {
            W_TYPE(TYPE_SMALL_TUPLE, p);
            w_byte((char)n, p);
        }
        else {
            W_TYPE(TYPE_TUPLE, p);
            if (!w_size(n, p)) {
                return;
            }
        }
        for (Py_ssize_t i = 0; i < n; i++) {
            w_object(PyTuple_GET_ITEM(v, i), p);
        }
    }
    else if (PyList_CheckExact(v)) {
        W_TYPE(TYPE_LIST, p);
        Py_ssize_t n = PyList_GET_SIZE(v);
        if (!w_size(n, p)) {
            return;
        }
        for (Py_ssize_t i = 0; i < n && i < PyList_GET_SIZE(v); i++) {
            w_object(PyList_GET_ITEM(v, i), p);
        }
    }
    else if (PyDict_CheckExact(v)) {
        W_TYPE(TYPE_DICT, p);
        Py_ssize_t pos = 0;
        PyObject *key, *value;
        while (lu_ns_next(v, &pos, &key, &value)) {
            w_object(key, p);
            w_object(value, p);
        }
        w_object(NULL, p);
    }
    else if (PyAnySet_CheckExact(v)) {
        w_set(v, flag, p);
    }
    else if (PyCode_Check(v)) {
        w_code(v, flag, p);
    }
    else if (PySlice_Check(v)) {
        if (p->version < 5) {
            w_byte(TYPE_UNKNOWN, p);
            p->error = WFERR_UNMARSHALLABLE;
            return;
        }
        PySliceObject *slice = (PySliceObject *)v;
        W_TYPE(TYPE_SLICE, p);
        w_object(slice->start, p);
        w_object(slice->stop, p);
        w_object(slice->step, p);
        w_complete(v, p);
    }
    else if (PyObject_CheckBuffer(v)) {
        Py_buffer view;
        if (PyObject_GetBuffer(v, &view, PyBUF_SIMPLE) != 0) {
            PyErr_Clear();
            w_byte(TYPE_UNKNOWN, p);
            p->error = WFERR_UNMARSHALLABLE;
            return;
        }
        W_TYPE(TYPE_STRING, p);
        w_pstring(view.buf, view.len, p);
        PyBuffer_Release(&view);
    }
    else {
        W_TYPE(TYPE_UNKNOWN, p);
        p->error = WFERR_UNMARSHALLABLE;
    }
}

static void
w_object(PyObject *v, WFILE *p)
{
    char flag = '\0';

    if (p->error != WFERR_OK) {
        return;
    }
    p->depth++;
    if (p->depth > MAX_WRITE_DEPTH) {
        p->error = WFERR_NESTEDTOODEEP;
    }
    else if (v == NULL) {
        w_byte(TYPE_NULL, p);
    }
    else if (v == Py_None) {
        w_byte(TYPE_NONE, p);
    }
    else if (v == PyExc_StopIteration) {
        w_byte(TYPE_STOPITER, p);
    }
    else if (v == Py_Ellipsis) {
        w_byte(TYPE_ELLIPSIS, p);
    }
    else if (v == Py_False) {
        w_byte(TYPE_FALSE, p);
    }
    else if (v == Py_True) {
        w_byte(TYPE_TRUE, p);
    }
    else if (!w_ref(v, &flag, p, 0)) {
        w_complex_object(v, flag, p);
    }
    p->depth--;
}

static PyObject *
write_to_bytes(PyObject *x, int version, int allow_code)
{
    WFILE wf = { .version = version, .allow_code = allow_code };
    w_object(x, &wf);
    for (Py_ssize_t i = 0; i < wf.nrefs; i++) {
        Py_DECREF(wf.refs[i].obj);
    }
    PyMem_Free(wf.refs);
    PyMem_Free(wf.index);
    PyObject *result = NULL;
    switch (wf.error) {
    case WFERR_OK:
        result = PyBytes_FromStringAndSize(wf.buf, (Py_ssize_t)wf.len);
        break;
    case WFERR_NOMEMORY:
        PyErr_NoMemory();
        break;
    case WFERR_NESTEDTOODEEP:
        PyErr_SetString(PyExc_ValueError, "object too deeply nested to marshal");
        break;
    case WFERR_CODE_NOT_ALLOWED:
        PyErr_SetString(PyExc_ValueError, "marshalling code objects is disallowed");
        break;
    case WFERR_SET_CHANGED_SIZE:
        PyErr_SetString(PyExc_RuntimeError, "set changed size during iteration");
        break;
    default:
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_ValueError, "unmarshallable object");
        }
        break;
    }
    PyMem_Free(wf.buf);
    return result;
}

PyObject *
PyMarshal_WriteObjectToString(PyObject *x, int version)
{
    return write_to_bytes(x, version, 1);
}

PyObject *
lu_marshal_dumps(PyObject *x, int version, int allow_code)
{
    return write_to_bytes(x, version, allow_code);
}
