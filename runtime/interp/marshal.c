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
