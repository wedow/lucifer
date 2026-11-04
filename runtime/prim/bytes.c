/* bytes and bytearray primitives (DESIGN 4.4, storage): the loops under the
   kernel's bytes and bytearray, over the PyBytesObject and PyByteArrayObject
   layouts of cp314t, and the buffer protocol both export through. The
   methods, argument parsing and messages are lib/kernel/bytes.py.

   Every primitive that mutates a bytearray, or reads one as its first
   argument, runs in a critical section on it, as the 3.14t methods do, so a
   method that is one primitive is atomic. Resizing while buffers are
   exported raises BufferError. */

#include "prim/bytes.h"

#include <stdlib.h>
#include <string.h>

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)

/* An int argument, saturated to the Py_ssize_t range. */
static int
index_arg(PyObject *o, Py_ssize_t *out)
{
    long v;
    if (!PyLong_Check(o)) {
        lu_err_format(PyExc_TypeError, "an int is required, not '%.200s'", Py_TYPE(o)->tp_name);
        return -1;
    }
    if (lu_long_as_long(o, &v) < 0) {
        PyErr_Clear();
        v = (((PyLongObject *)o)->long_value.lv_tag & 3) == 2 ? PY_SSIZE_T_MIN : PY_SSIZE_T_MAX;
    }
    *out = (Py_ssize_t)v;
    return 0;
}
#define IDX(i, var) Py_ssize_t var; do { if (index_arg(args[i], &var) < 0) return NULL; } while (0)

static PyObject *
bool_of(int v)
{
    return Py_NewRef(v ? Py_True : Py_False);
}

/* Data of any bytes-like object. */
int
lu_bv_get(PyObject *o, lu_bv *v)
{
    if (PyBytes_Check(o)) {
        v->p = (const unsigned char *)PyBytes_AS_STRING(o);
        v->n = PyBytes_GET_SIZE(o);
        v->held = 0;
        return 0;
    }
    if (PyObject_GetBuffer(o, &v->view, PyBUF_SIMPLE) < 0) {
        return -1;
    }
    v->p = v->view.buf;
    v->n = v->view.len;
    v->held = 1;
    return 0;
}

void
lu_bv_put(lu_bv *v)
{
    if (v->held) {
        PyBuffer_Release(&v->view);
    }
}

#define BV(var, i) lu_bv var; do { if (lu_bv_get(args[i], &var) < 0) return NULL; } while (0)

static PyObject *
mk(const void *p, Py_ssize_t n)
{
    return PyBytes_FromStringAndSize(p, n);
}

/* A growing byte buffer, the result of the transformations. */
typedef struct {
    unsigned char *p;
    Py_ssize_t n, cap;
} wbuf;

static int
wb_room(wbuf *b, Py_ssize_t more)
{
    if (b->n + more <= b->cap) {
        return 0;
    }
    Py_ssize_t cap = b->cap ? b->cap : 64;
    while (cap < b->n + more) {
        cap *= 2;
    }
    unsigned char *p = PyMem_Realloc(b->p, cap);
    if (p == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    b->p = p;
    b->cap = cap;
    return 0;
}

static int
wb_put(wbuf *b, const void *p, Py_ssize_t n)
{
    if (wb_room(b, n) < 0) {
        return -1;
    }
    if (n) {
        memcpy(b->p + b->n, p, n);
    }
    b->n += n;
    return 0;
}

static PyObject *
wb_finish(wbuf *b, int err)
{
    PyObject *r = err ? NULL : mk(b->p, b->n);
    PyMem_Free(b->p);
    return r;
}

/* Critical sections: a primitive whose first argument is a bytearray runs
   locked on it. */

typedef PyObject *(*impl_fn)(PyObject *const *args);

static PyObject *
locked(impl_fn f, PyObject *const *args)
{
    if (!PyByteArray_Check(args[0])) {
        return f(args);
    }
    PyObject *r;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    r = f(args);
    Py_END_CRITICAL_SECTION();
    return r;
}

#define PRIM(name, n) \
    static PyObject *impl_##name(PyObject *const *args); \
    static PyObject *prim_##name(PyObject *m, PyObject *const *args, Py_ssize_t nargs) \
    { ARGS(n); return locked(impl_##name, args); } \
    static PyObject *impl_##name(PyObject *const *args)

/* bytearray storage ------------------------------------------------- */

#define BA(o) ((PyByteArrayObject *)(o))

static void
bytearray_dealloc(PyObject *op)
{
    PyByteArrayObject *self = BA(op);
    if (self->ob_bytes != NULL) {
        PyObject_Free(self->ob_bytes);
    }
    lu_object_free(op);
}

static int
can_resize(PyObject *op)
{
    if (BA(op)->ob_exports > 0) {
        PyErr_SetString(PyExc_BufferError, "Existing exports of data: object cannot be re-sized");
        return 0;
    }
    return 1;
}

static int
ba_resize(PyObject *op, Py_ssize_t size)
{
    PyByteArrayObject *self = BA(op);
    if (size == Py_SIZE(op)) {
        return 0;
    }
    if (!can_resize(op)) {
        return -1;
    }
    Py_ssize_t alloc = self->ob_alloc;
    if (size + 1 <= alloc) {
        if (size < alloc / 2) {
            alloc = size + 1;               /* a major downsize resizes to fit */
        }
        else {
            Py_SET_SIZE(op, size);
            self->ob_bytes[size] = '\0';
            return 0;
        }
    }
    else if (size <= alloc * 1.125) {
        alloc = size + (size >> 3) + (size < 9 ? 3 : 6);
    }
    else {
        alloc = size + 1;
    }
    if (size < 0 || alloc < size + 1) {
        PyErr_NoMemory();
        return -1;
    }
    char *p = self->ob_bytes == NULL ? PyObject_Malloc(alloc)
                                     : PyObject_Realloc(self->ob_bytes, alloc);
    if (p == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    self->ob_bytes = self->ob_start = p;
    self->ob_alloc = alloc;
    Py_SET_SIZE(op, size);
    p[size] = '\0';
    return 0;
}

int
lu_bytearray_append(PyObject *op, int byte)
{
    int err;
    Py_BEGIN_CRITICAL_SECTION(op);
    Py_ssize_t n = Py_SIZE(op);
    err = ba_resize(op, n + 1);
    if (err == 0) {
        BA(op)->ob_bytes[n] = (char)byte;
    }
    Py_END_CRITICAL_SECTION();
    return err;
}

/* The data of a bytes-like argument that a bytearray may be about to
   overwrite: a private copy when it is the bytearray itself. */
static int
src_get(PyObject *dst, PyObject *o, lu_bv *v, unsigned char **copy)
{
    *copy = NULL;
    if (lu_bv_get(o, v) < 0) {
        return -1;
    }
    if (o == dst && v->n) {
        *copy = PyMem_Malloc(v->n);
        if (*copy == NULL) {
            lu_bv_put(v);
            PyErr_NoMemory();
            return -1;
        }
        memcpy(*copy, v->p, v->n);
        v->p = *copy;
    }
    return 0;
}

static Py_ssize_t
clamp(Py_ssize_t i, Py_ssize_t n)
{
    return i < 0 ? 0 : i > n ? n : i;
}

/* ba_splice(ba, start, stop, data): replaces ba[start:stop] by data. */
PRIM(ba_splice, 4)
{
    PyObject *ba = args[0];
    IDX(1, start); IDX(2, stop);
    lu_bv src;
    unsigned char *copy;
    if (src_get(ba, args[3], &src, &copy) < 0) {
        return NULL;
    }
    Py_ssize_t n = Py_SIZE(ba);
    start = clamp(start, n);
    stop = clamp(stop, n);
    if (stop < start) {
        stop = start;
    }
    Py_ssize_t growth = src.n - (stop - start);
    PyObject *r = Py_None;
    if (growth > 0) {
        if (ba_resize(ba, n + growth) < 0) {
            r = NULL;
        }
        else {
            memmove(BA(ba)->ob_bytes + stop + growth, BA(ba)->ob_bytes + stop, n - stop);
        }
    }
    else if (growth < 0) {
        if (!can_resize(ba)) {
            r = NULL;
        }
        else {
            /* move the tail down first: shrinking may cut the allocation */
            memmove(BA(ba)->ob_bytes + start + src.n, BA(ba)->ob_bytes + stop, n - stop);
            if (ba_resize(ba, n + growth) < 0) {
                r = NULL;
            }
        }
    }
    if (r != NULL && src.n) {
        memcpy(BA(ba)->ob_bytes + start, src.p, src.n);
    }
    PyMem_Free(copy);
    lu_bv_put(&src);
    return Py_XNewRef(r);
}

/* ba_set_step(ba, start, step, count, data): ba[start::step] = data, count
   items. */
PRIM(ba_set_step, 5)
{
    PyObject *ba = args[0];
    IDX(1, start); IDX(2, step); IDX(3, count);
    lu_bv src;
    unsigned char *copy;
    if (src_get(ba, args[4], &src, &copy) < 0) {
        return NULL;
    }
    Py_ssize_t n = Py_SIZE(ba);
    PyObject *r = Py_None;
    if (src.n != count) {
        PyErr_SetString(PyExc_IndexError, "bytearray slice assignment changed size");
        r = NULL;
    }
    else if (count > 0 && (start < 0 || start >= n || start + (count - 1) * step < 0
                           || start + (count - 1) * step >= n)) {
        PyErr_SetString(PyExc_IndexError, "bytearray index out of range");
        r = NULL;
    }
    else {
        for (Py_ssize_t i = 0; i < count; i++) {
            BA(ba)->ob_bytes[start + i * step] = src.p[i];
        }
    }
    PyMem_Free(copy);
    lu_bv_put(&src);
    return Py_XNewRef(r);
}

/* ba_del_step(ba, start, step, count): del ba[start::step], count items. */
PRIM(ba_del_step, 4)
{
    PyObject *ba = args[0];
    IDX(1, start); IDX(2, step); IDX(3, count);
    Py_ssize_t n = Py_SIZE(ba);
    if (count <= 0) {
        Py_RETURN_NONE;
    }
    if (start < 0 || start >= n || start + (count - 1) * step < 0 || start + (count - 1) * step >= n) {
        PyErr_SetString(PyExc_IndexError, "bytearray index out of range");
        return NULL;
    }
    if (step < 0) {                     /* the same items, ascending */
        start += (count - 1) * step;
        step = -step;
    }
    if (!can_resize(ba)) {
        return NULL;
    }
    char *b = BA(ba)->ob_bytes;
    Py_ssize_t keep = 0;
    Py_ssize_t next = start;
    Py_ssize_t removed = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (i == next && removed < count) {
            removed++;
            next += step;
            continue;
        }
        b[keep++] = b[i];
    }
    return ba_resize(ba, keep) < 0 ? NULL : Py_NewRef(Py_None);
}

/* ba_set_item(ba, i, v) with i already in range. */
PRIM(ba_set_item, 3)
{
    IDX(1, i); IDX(2, v);
    if (i < 0 || i >= Py_SIZE(args[0])) {
        PyErr_SetString(PyExc_IndexError, "bytearray index out of range");
        return NULL;
    }
    BA(args[0])->ob_bytes[i] = (char)v;
    Py_RETURN_NONE;
}

/* ba_pop(ba, i) */
PRIM(ba_pop, 2)
{
    IDX(1, i);
    Py_ssize_t n = Py_SIZE(args[0]);
    if (n == 0) {
        PyErr_SetString(PyExc_IndexError, "pop from empty bytearray");
        return NULL;
    }
    if (i < 0) {
        i += n;
    }
    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "pop index out of range");
        return NULL;
    }
    char *b = BA(args[0])->ob_bytes;
    unsigned char c = (unsigned char)b[i];
    if (!can_resize(args[0])) {
        return NULL;
    }
    memmove(b + i, b + i + 1, n - i - 1);
    if (ba_resize(args[0], n - 1) < 0) {
        return NULL;
    }
    return PyLong_FromLong(c);
}

/* ba_remove(ba, v): removes the first byte equal to v. */
PRIM(ba_remove, 2)
{
    IDX(1, v);
    Py_ssize_t n = Py_SIZE(args[0]);
    char *b = BA(args[0])->ob_bytes;
    for (Py_ssize_t i = 0; i < n; i++) {
        if ((unsigned char)b[i] == v) {
            if (!can_resize(args[0])) {
                return NULL;
            }
            memmove(b + i, b + i + 1, n - i - 1);
            return ba_resize(args[0], n - 1) < 0 ? NULL : Py_NewRef(Py_None);
        }
    }
    PyErr_SetString(PyExc_ValueError, "value not found in bytearray");
    return NULL;
}

PRIM(ba_reverse, 1)
{
    Py_ssize_t n = Py_SIZE(args[0]);
    char *b = BA(args[0])->ob_bytes;
    for (Py_ssize_t i = 0, j = n - 1; i < j; i++, j--) {
        char t = b[i];
        b[i] = b[j];
        b[j] = t;
    }
    Py_RETURN_NONE;
}

/* ba_resize(ba, n): the new bytes are zero. */
PRIM(ba_resize, 2)
{
    IDX(1, size);
    Py_ssize_t n = Py_SIZE(args[0]);
    if (size < 0) {
        lu_err_format(PyExc_ValueError, "Can only resize to positive sizes, got %zd", size);
        return NULL;
    }
    if (ba_resize(args[0], size) < 0) {
        return NULL;
    }
    if (size > n) {
        memset(BA(args[0])->ob_bytes + n, 0, size - n);
    }
    Py_RETURN_NONE;
}

/* ba_irepeat(ba, count) */
PRIM(ba_irepeat, 2)
{
    IDX(1, count);
    Py_ssize_t n = Py_SIZE(args[0]);
    if (count < 0) {
        count = 0;
    }
    if (count > 0 && n > PY_SSIZE_T_MAX / count) {
        return PyErr_NoMemory();
    }
    Py_ssize_t size = n * count;
    if (size == n) {
        Py_RETURN_NONE;
    }
    if (ba_resize(args[0], size) < 0) {
        return NULL;
    }
    char *b = BA(args[0])->ob_bytes;
    for (Py_ssize_t i = n; i < size; i++) {
        b[i] = b[i % n];
    }
    Py_RETURN_NONE;
}

/* ba_alloc(ba) -> allocated size */
PRIM(ba_alloc, 1)
{
    return PyLong_FromLongLong(BA(args[0])->ob_alloc);
}

/* ba_new(cls): an empty bytearray of the class. */
static PyObject *
prim_ba_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyByteArray_Type)) {
        lu_err_format(PyExc_TypeError, "ba_new() needs a bytearray class");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyObject *ba = tp->tp_alloc(tp, 0);
    if (ba != NULL) {
        BA(ba)->ob_start = BA(ba)->ob_bytes = NULL;
    }
    return ba;
}

/* ba_exports(ba) */
PRIM(ba_exports, 1)
{
    return PyLong_FromLongLong(BA(args[0])->ob_exports);
}

/* bytes storage ----------------------------------------------------- */

/* b_len(x): the length of bytes, bytearray or any buffer. */
static PyObject *
prim_b_len(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (PyByteArray_Check(args[0])) {
        return PyLong_FromLongLong(PyByteArray_GET_SIZE(args[0]));
    }
    BV(v, 0);
    Py_ssize_t n = v.n;
    lu_bv_put(&v);
    return PyLong_FromLongLong(n);
}

/* b_check(x): whether x supports the buffer protocol. */
static PyObject *
prim_b_check(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return bool_of(PyObject_CheckBuffer(args[0]));
}

/* b_bytes(x): an exact bytes with the data of a bytes-like object. */
PRIM(b_bytes, 1)
{
    if (PyBytes_CheckExact(args[0])) {
        return Py_NewRef(args[0]);
    }
    if (PyBytes_Check(args[0])) {
        return mk(PyBytes_AS_STRING(args[0]), PyBytes_GET_SIZE(args[0]));
    }
    Py_buffer view;
    if (PyObject_GetBuffer(args[0], &view, PyBUF_FULL_RO) < 0) {
        return NULL;
    }
    PyObject *r = PyBytes_FromStringAndSize(NULL, view.len);
    if (r != NULL && view.len && PyBuffer_ToContiguous(PyBytes_AS_STRING(r), &view, view.len, 'C') < 0) {
        Py_CLEAR(r);
    }
    PyBuffer_Release(&view);
    return r;
}

/* b_from_ints(seq): bytes from a tuple or list of ints. */
static PyObject *
prim_b_from_ints(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *seq = args[0];
    if (!PyTuple_Check(seq) && !PyList_Check(seq)) {
        lu_err_format(PyExc_TypeError, "b_from_ints() needs a tuple or a list");
        return NULL;
    }
    Py_ssize_t n = Py_SIZE(seq);
    PyObject *r = PyBytes_FromStringAndSize(NULL, n);
    if (r == NULL) {
        return NULL;
    }
    char *p = PyBytes_AS_STRING(r);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *x = PyTuple_Check(seq) ? PyTuple_GET_ITEM(seq, i) : PyList_GET_ITEM(seq, i);
        Py_ssize_t v;
        if (index_arg(x, &v) < 0) {
            Py_DECREF(r);
            return NULL;
        }
        if (v < 0 || v > 255) {
            Py_DECREF(r);
            PyErr_SetString(PyExc_ValueError, "bytes must be in range(0, 256)");
            return NULL;
        }
        p[i] = (char)v;
    }
    if (n == 1) {
        /* _PyBytesWriter_Finish: a one-byte result is the cached object. */
        char only = p[0];
        Py_DECREF(r);
        return PyBytes_FromStringAndSize(&only, 1);
    }
    return r;
}

/* b_zeros(n) */
static PyObject *
prim_b_zeros(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    IDX(0, n);
    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "negative count");
        return NULL;
    }
    PyObject *r = PyBytes_FromStringAndSize(NULL, n);
    if (r != NULL) {
        memset(PyBytes_AS_STRING(r), 0, n);
    }
    return r;
}

/* b_subtype(cls, data) and ba_wrap(cls, data): an instance of the class
   with the data. */
static PyObject *
prim_b_subtype(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyBytes_Type)) {
        lu_err_format(PyExc_TypeError, "b_subtype() needs a bytes subclass");
        return NULL;
    }
    BV(v, 1);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyBytesObject *r = (PyBytesObject *)tp->tp_alloc(tp, v.n);
    if (r != NULL) {
        memcpy(r->ob_sval, v.p, v.n);
        r->ob_sval[v.n] = '\0';
        _Py_COMP_DIAG_PUSH
        _Py_COMP_DIAG_IGNORE_DEPR_DECLS
        r->ob_shash = -1;
        _Py_COMP_DIAG_POP
    }
    lu_bv_put(&v);
    return (PyObject *)r;
}

static PyObject *
prim_ba_wrap(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyObject *ba = prim_ba_new(m, args, 1);
    if (ba == NULL) {
        return NULL;
    }
    PyObject *splice[4] = { ba, lu_small_int(0), lu_small_int(0), args[1] };
    PyObject *r = prim_ba_splice(m, splice, 4);
    if (r == NULL) {
        Py_DECREF(ba);
        return NULL;
    }
    Py_DECREF(r);
    return ba;
}

/* b_item(data, i) with i in range. */
static PyObject *
prim_b_item(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    IDX(1, i);
    BV(v, 0);
    PyObject *r = NULL;
    if (i < 0 || i >= v.n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
    }
    else {
        r = PyLong_FromLong(v.p[i]);
    }
    lu_bv_put(&v);
    return r;
}

/* b_slice(data, start, stop) with 0 <= start <= stop <= len; clamped. */
PRIM(b_slice, 3)
{
    IDX(1, start); IDX(2, stop);
    BV(v, 0);
    start = clamp(start, v.n);
    stop = clamp(stop, v.n);
    PyObject *r = mk(v.p + start, stop > start ? stop - start : 0);
    lu_bv_put(&v);
    return r;
}

/* b_slice_step(data, start, step, count) */
PRIM(b_slice_step, 4)
{
    IDX(1, start); IDX(2, step); IDX(3, count);
    BV(v, 0);
    PyObject *r = NULL;
    if (count <= 0) {
        r = mk(NULL, 0);
    }
    else if (start < 0 || start >= v.n || start + (count - 1) * step < 0
             || start + (count - 1) * step >= v.n) {
        PyErr_SetString(PyExc_IndexError, "index out of range");
    }
    else {
        r = PyBytes_FromStringAndSize(NULL, count);
        if (r != NULL) {
            char *p = PyBytes_AS_STRING(r);
            for (Py_ssize_t i = 0; i < count; i++) {
                p[i] = (char)v.p[start + i * step];
            }
        }
    }
    lu_bv_put(&v);
    return r;
}

/* b_cmp(a, b) -> -1, 0, 1 */
PRIM(b_cmp, 2)
{
    BV(a, 0);
    lu_bv b;
    if (lu_bv_get(args[1], &b) < 0) {
        lu_bv_put(&a);
        return NULL;
    }
    Py_ssize_t m = a.n < b.n ? a.n : b.n;
    int c = m ? memcmp(a.p, b.p, m) : 0;
    if (c == 0) {
        c = a.n < b.n ? -1 : a.n > b.n;
    }
    lu_bv_put(&a);
    lu_bv_put(&b);
    return PyLong_FromLong(c < 0 ? -1 : c > 0);
}

/* b_hash(data) */
PRIM(b_hash, 1)
{
    BV(v, 0);
    Py_hash_t h = Py_HashBuffer(v.p, v.n);
    lu_bv_put(&v);
    return PyLong_FromLongLong(h);
}

/* b_keyed_hash(key, data): the 8 bytes _imp.source_hash returns. */
PRIM(b_keyed_hash, 2)
{
    long key;
    if (lu_long_as_long(args[0], &key) < 0) {
        return NULL;
    }
    BV(v, 1);
    uint64_t h = lu_keyed_hash((uint64_t)key, v.p, v.n);
    lu_bv_put(&v);
    return PyBytes_FromStringAndSize((const char *)&h, sizeof h);
}

/* b_concat(a, b) */
PRIM(b_concat, 2)
{
    BV(a, 0);
    lu_bv b;
    if (lu_bv_get(args[1], &b) < 0) {
        lu_bv_put(&a);
        return NULL;
    }
    PyObject *r = NULL;
    if (a.n > PY_SSIZE_T_MAX - b.n) {
        PyErr_NoMemory();
    }
    else {
        r = PyBytes_FromStringAndSize(NULL, a.n + b.n);
        if (r != NULL) {
            memcpy(PyBytes_AS_STRING(r), a.p, a.n);
            memcpy(PyBytes_AS_STRING(r) + a.n, b.p, b.n);
        }
    }
    lu_bv_put(&a);
    lu_bv_put(&b);
    return r;
}

/* b_repeat(data, n) */
PRIM(b_repeat, 2)
{
    IDX(1, count);
    BV(v, 0);
    PyObject *r = NULL;
    if (count <= 0 || v.n == 0) {
        r = mk(NULL, 0);
    }
    else if (v.n > PY_SSIZE_T_MAX / count) {
        PyErr_SetString(PyExc_OverflowError, "repeated bytes are too long");
    }
    else {
        r = PyBytes_FromStringAndSize(NULL, v.n * count);
        if (r != NULL) {
            char *p = PyBytes_AS_STRING(r);
            if (v.n == 1) {
                memset(p, v.p[0], count);
            }
            else {
                for (Py_ssize_t i = 0; i < count; i++) {
                    memcpy(p + i * v.n, v.p, v.n);
                }
            }
        }
    }
    lu_bv_put(&v);
    return r;
}

/* Searching --------------------------------------------------------- */

static void
adjust(Py_ssize_t n, Py_ssize_t *start, Py_ssize_t *end)
{
    if (*end > n) {
        *end = n;
    }
    else if (*end < 0) {
        *end += n;
        if (*end < 0) {
            *end = 0;
        }
    }
    if (*start < 0) {
        *start += n;
        if (*start < 0) {
            *start = 0;
        }
    }
}

static Py_ssize_t
find_in(const unsigned char *s, Py_ssize_t n, const unsigned char *sub, Py_ssize_t m, int last)
{
    if (m > n) {
        return -1;
    }
    if (m == 0) {
        return last ? n : 0;
    }
    if (!last) {
        const unsigned char *r = memmem(s, n, sub, m);
        return r == NULL ? -1 : r - s;
    }
    for (Py_ssize_t i = n - m; i >= 0; i--) {
        if (s[i] == sub[0] && memcmp(s + i, sub, m) == 0) {
            return i;
        }
    }
    return -1;
}

/* b_find(data, sub, start, end, last) */
PRIM(b_find, 5)
{
    IDX(2, start); IDX(3, end);
    BV(v, 0);
    lu_bv sub;
    if (lu_bv_get(args[1], &sub) < 0) {
        lu_bv_put(&v);
        return NULL;
    }
    adjust(v.n, &start, &end);
    Py_ssize_t r = -1;
    if (start <= v.n && end - start >= sub.n) {
        r = find_in(v.p + start, end - start, sub.p, sub.n, args[4] == Py_True);
        if (r >= 0) {
            r += start;
        }
    }
    lu_bv_put(&v);
    lu_bv_put(&sub);
    return PyLong_FromLongLong(r);
}

/* b_count(data, sub, start, end) */
PRIM(b_count, 4)
{
    IDX(2, start); IDX(3, end);
    BV(v, 0);
    lu_bv sub;
    if (lu_bv_get(args[1], &sub) < 0) {
        lu_bv_put(&v);
        return NULL;
    }
    adjust(v.n, &start, &end);
    Py_ssize_t count = 0;
    if (start > v.n || end - start < sub.n) {
        count = 0;
    }
    else if (sub.n == 0) {
        count = end - start + 1;
    }
    else {
        const unsigned char *s = v.p + start;
        Py_ssize_t n = end - start;
        for (;;) {
            Py_ssize_t at = find_in(s, n, sub.p, sub.n, 0);
            if (at < 0) {
                break;
            }
            count++;
            s += at + sub.n;
            n -= at + sub.n;
        }
    }
    lu_bv_put(&v);
    lu_bv_put(&sub);
    return PyLong_FromLongLong(count);
}

/* b_match(data, sub, start, end, at_end): startswith and endswith. */
PRIM(b_match, 5)
{
    IDX(2, start); IDX(3, end);
    BV(v, 0);
    lu_bv sub;
    if (lu_bv_get(args[1], &sub) < 0) {
        lu_bv_put(&v);
        return NULL;
    }
    adjust(v.n, &start, &end);
    int r = 0;
    if (start <= v.n && end - start >= sub.n) {
        const unsigned char *at = args[4] == Py_True ? v.p + end - sub.n : v.p + start;
        r = sub.n == 0 || memcmp(at, sub.p, sub.n) == 0;
    }
    lu_bv_put(&v);
    lu_bv_put(&sub);
    return bool_of(r);
}

/* b_replace(data, old, new, count) */
PRIM(b_replace, 4)
{
    IDX(3, count);
    BV(v, 0);
    lu_bv old, new;
    if (lu_bv_get(args[1], &old) < 0) {
        lu_bv_put(&v);
        return NULL;
    }
    if (lu_bv_get(args[2], &new) < 0) {
        lu_bv_put(&v);
        lu_bv_put(&old);
        return NULL;
    }
    wbuf b = { 0 };
    int err = 0;
    Py_ssize_t i = 0;
    if (count < 0) {
        count = PY_SSIZE_T_MAX;
    }
    if (old.n == 0) {
        for (Py_ssize_t k = 0; k <= v.n && !err; k++) {
            if (count > 0) {
                err = wb_put(&b, new.p, new.n);
                count--;
            }
            if (k < v.n && !err) {
                err = wb_put(&b, v.p + k, 1);
            }
        }
    }
    else {
        while (count > 0 && !err) {
            Py_ssize_t at = find_in(v.p + i, v.n - i, old.p, old.n, 0);
            if (at < 0) {
                break;
            }
            err = wb_put(&b, v.p + i, at) || wb_put(&b, new.p, new.n);
            i += at + old.n;
            count--;
        }
        if (!err) {
            err = wb_put(&b, v.p + i, v.n - i);
        }
    }
    PyObject *r = wb_finish(&b, err);
    lu_bv_put(&v);
    lu_bv_put(&old);
    lu_bv_put(&new);
    return r;
}

/* b_join(sep, items): items is a tuple of bytes-like objects. */
PRIM(b_join, 2)
{
    BV(sep, 0);
    PyObject *items = args[1];
    wbuf b = { 0 };
    int err = 0;
    Py_ssize_t n = Py_SIZE(items);
    for (Py_ssize_t i = 0; i < n && !err; i++) {
        PyObject *x = PyTuple_GET_ITEM(items, i);
        lu_bv v;
        if (!PyObject_CheckBuffer(x)) {
            lu_err_format(PyExc_TypeError, "sequence item %zd: expected a bytes-like object, %.80s found",
                          i, Py_TYPE(x)->tp_name);
            err = 1;
            break;
        }
        if (lu_bv_get(x, &v) < 0) {
            err = 1;
            break;
        }
        err = (i > 0 && wb_put(&b, sep.p, sep.n)) || wb_put(&b, v.p, v.n);
        lu_bv_put(&v);
    }
    PyObject *r = wb_finish(&b, err);
    lu_bv_put(&sep);
    return r;
}

/* Splitting --------------------------------------------------------- */

#define SPACE(c) ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))

static int
add(PyObject **list, const unsigned char *p, Py_ssize_t n)
{
    PyObject *x = mk(p, n);
    if (x == NULL) {
        return -1;
    }
    int err = lu_list_append(*list, x);
    Py_DECREF(x);
    return err;
}

/* The pieces, as a tuple (reversed first when rsplit built them backwards). */
static PyObject *
pieces_tuple(PyObject *list, int reverse)
{
    if (list == NULL) {
        return NULL;
    }
    Py_ssize_t n = Py_SIZE(list);
    PyObject *t = PyTuple_New(n);
    if (t != NULL) {
        for (Py_ssize_t i = 0; i < n; i++) {
            PyTuple_SET_ITEM(t, i, Py_NewRef(PyList_GET_ITEM(list, reverse ? n - 1 - i : i)));
        }
    }
    Py_DECREF(list);
    return t;
}

/* b_split(data, sep, maxsplit, rsplit): sep is None for whitespace. */
PRIM(b_split, 4)
{
    IDX(2, maxsplit);
    int rsplit = args[3] == Py_True;
    BV(v, 0);
    lu_bv sep = { 0 };
    int have_sep = args[1] != Py_None;
    if (have_sep && lu_bv_get(args[1], &sep) < 0) {
        lu_bv_put(&v);
        return NULL;
    }
    if (have_sep && sep.n == 0) {
        PyErr_SetString(PyExc_ValueError, "empty separator");
        lu_bv_put(&v);
        lu_bv_put(&sep);
        return NULL;
    }
    if (maxsplit < 0) {
        maxsplit = PY_SSIZE_T_MAX;
    }
    PyObject *list = PyList_New(0);
    int err = list == NULL;
    const unsigned char *s = v.p;
    Py_ssize_t n = v.n;
    if (!err && !rsplit && !have_sep) {
        Py_ssize_t i = 0, j;
        while (maxsplit-- > 0 && !err) {
            while (i < n && SPACE(s[i])) i++;
            if (i == n) break;
            j = i++;
            while (i < n && !SPACE(s[i])) i++;
            err = add(&list, s + j, i - j);
        }
        if (!err && i < n) {
            while (i < n && SPACE(s[i])) i++;
            if (i != n) err = add(&list, s + i, n - i);
        }
    }
    else if (!err && rsplit && !have_sep) {
        Py_ssize_t i = n - 1, j;
        while (maxsplit-- > 0 && !err) {
            while (i >= 0 && SPACE(s[i])) i--;
            if (i < 0) break;
            j = i--;
            while (i >= 0 && !SPACE(s[i])) i--;
            err = add(&list, s + i + 1, j - i);
        }
        if (!err && i >= 0) {
            while (i >= 0 && SPACE(s[i])) i--;
            if (i >= 0) err = add(&list, s, i + 1);
        }
    }
    else if (!err && !rsplit) {
        Py_ssize_t i = 0;
        while (maxsplit-- > 0 && !err) {
            Py_ssize_t at = find_in(s + i, n - i, sep.p, sep.n, 0);
            if (at < 0) break;
            err = add(&list, s + i, at);
            i += at + sep.n;
        }
        if (!err) err = add(&list, s + i, n - i);
    }
    else if (!err) {
        Py_ssize_t end = n;
        while (maxsplit-- > 0 && !err) {
            Py_ssize_t at = find_in(s, end, sep.p, sep.n, 1);
            if (at < 0) break;
            err = add(&list, s + at + sep.n, end - at - sep.n);
            end = at;
        }
        if (!err) err = add(&list, s, end);
    }
    PyObject *r = err ? (Py_XDECREF(list), NULL) : pieces_tuple(list, rsplit);
    lu_bv_put(&v);
    if (have_sep) lu_bv_put(&sep);
    return r;
}

/* b_splitlines(data, keepends) */
PRIM(b_splitlines, 2)
{
    int keepends = args[1] == Py_True;
    BV(v, 0);
    PyObject *list = PyList_New(0);
    int err = list == NULL;
    Py_ssize_t i = 0;
    while (!err && i < v.n) {
        Py_ssize_t j = i;
        while (j < v.n && v.p[j] != '\n' && v.p[j] != '\r') j++;
        Py_ssize_t eol = j;
        if (j < v.n) {
            if (v.p[j] == '\r' && j + 1 < v.n && v.p[j + 1] == '\n') j += 2;
            else j++;
            if (keepends) eol = j;
        }
        err = add(&list, v.p + i, eol - i);
        i = j;
    }
    PyObject *r = err ? (Py_XDECREF(list), NULL) : pieces_tuple(list, 0);
    lu_bv_put(&v);
    return r;
}

/* b_strip(data, chars, mode): chars None for whitespace; mode 1 left, 2 right, 3 both. */
PRIM(b_strip, 3)
{
    IDX(2, mode);
    BV(v, 0);
    lu_bv chars = { 0 };
    int have = args[1] != Py_None;
    if (have && lu_bv_get(args[1], &chars) < 0) {
        lu_bv_put(&v);
        return NULL;
    }
    Py_ssize_t i = 0, j = v.n;
#define STRIPPED(c) (have ? memchr(chars.p, (c), chars.n) != NULL : SPACE(c))
    if (mode & 1) {
        while (i < j && STRIPPED(v.p[i])) i++;
    }
    if (mode & 2) {
        while (j > i && STRIPPED(v.p[j - 1])) j--;
    }
#undef STRIPPED
    PyObject *r = mk(v.p + i, j - i);
    lu_bv_put(&v);
    if (have) lu_bv_put(&chars);
    return r;
}

/* ASCII case and predicates ------------------------------------------ */

#define LOWER(c) ((c) >= 'a' && (c) <= 'z')
#define UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define ALPHA(c) (LOWER(c) || UPPER(c))
#define DIGIT(c) ((c) >= '0' && (c) <= '9')

/* b_case(data, mode): 0 lower, 1 upper, 2 swapcase, 3 title, 4 capitalize. */
PRIM(b_case, 2)
{
    IDX(1, mode);
    BV(v, 0);
    PyObject *r = PyBytes_FromStringAndSize(NULL, v.n);
    if (r != NULL) {
        unsigned char *o = (unsigned char *)PyBytes_AS_STRING(r);
        int prev = 0;
        for (Py_ssize_t i = 0; i < v.n; i++) {
            unsigned char c = v.p[i];
            switch (mode) {
            case 0: if (UPPER(c)) c += 32; break;
            case 1: if (LOWER(c)) c -= 32; break;
            case 2: if (UPPER(c)) c += 32; else if (LOWER(c)) c -= 32; break;
            case 3:
                if (LOWER(c)) { if (!prev) c -= 32; prev = 1; }
                else if (UPPER(c)) { if (prev) c += 32; prev = 1; }
                else prev = 0;
                break;
            default:
                if (i == 0) { if (LOWER(c)) c -= 32; }
                else if (UPPER(c)) c += 32;
            }
            o[i] = c;
        }
    }
    lu_bv_put(&v);
    return r;
}

/* b_is(data, mode): 0 alpha, 1 alnum, 2 digit, 3 space, 4 lower, 5 upper, 6 title, 7 ascii. */
PRIM(b_is, 2)
{
    IDX(1, mode);
    BV(v, 0);
    int r = 1;
    if (mode == 7) {
        for (Py_ssize_t i = 0; i < v.n && r; i++) r = v.p[i] < 128;
    }
    else if (v.n == 0) {
        r = 0;
    }
    else if (mode <= 3) {
        for (Py_ssize_t i = 0; i < v.n && r; i++) {
            unsigned char c = v.p[i];
            r = mode == 0 ? ALPHA(c) : mode == 1 ? ALPHA(c) || DIGIT(c) : mode == 2 ? DIGIT(c) : SPACE(c);
        }
    }
    else if (mode == 4 || mode == 5) {
        int cased = 0;
        for (Py_ssize_t i = 0; i < v.n && r; i++) {
            unsigned char c = v.p[i];
            if (mode == 4 ? UPPER(c) : LOWER(c)) r = 0;
            else if (mode == 4 ? LOWER(c) : UPPER(c)) cased = 1;
        }
        r = r && cased;
    }
    else {
        int cased = 0, prev = 0;
        for (Py_ssize_t i = 0; i < v.n && r; i++) {
            unsigned char c = v.p[i];
            if (UPPER(c)) { if (prev) r = 0; prev = cased = 1; }
            else if (LOWER(c)) { if (!prev) r = 0; prev = cased = 1; }
            else prev = 0;
        }
        r = r && cased;
    }
    lu_bv_put(&v);
    return bool_of(r);
}

/* b_expandtabs(data, tabsize) */
PRIM(b_expandtabs, 2)
{
    IDX(1, tabsize);
    BV(v, 0);
    Py_ssize_t i = 0, j = 0;
    int overflow = 0;
    for (Py_ssize_t k = 0; k < v.n && !overflow; k++) {
        if (v.p[k] == '\t') {
            if (tabsize > 0) {
                Py_ssize_t incr = tabsize - (j % tabsize);
                if (j > PY_SSIZE_T_MAX - incr) overflow = 1;
                j += incr;
            }
        }
        else {
            if (j > PY_SSIZE_T_MAX - 1) overflow = 1;
            j++;
            if (v.p[k] == '\n' || v.p[k] == '\r') {
                if (i > PY_SSIZE_T_MAX - j) overflow = 1;
                i += j;
                j = 0;
            }
        }
    }
    if (overflow || i > PY_SSIZE_T_MAX - j) {
        lu_bv_put(&v);
        PyErr_SetString(PyExc_OverflowError, "result too long");
        return NULL;
    }
    PyObject *r = PyBytes_FromStringAndSize(NULL, i + j);
    if (r != NULL) {
        char *q = PyBytes_AS_STRING(r);
        j = 0;
        for (Py_ssize_t k = 0; k < v.n; k++) {
            if (v.p[k] == '\t') {
                if (tabsize > 0) {
                    Py_ssize_t pad = tabsize - (j % tabsize);
                    j += pad;
                    while (pad--) *q++ = ' ';
                }
            }
            else {
                j++;
                *q++ = (char)v.p[k];
                if (v.p[k] == '\n' || v.p[k] == '\r') j = 0;
            }
        }
    }
    lu_bv_put(&v);
    return r;
}

/* b_translate(data, table, delete): table is None or 256 bytes; delete is bytes-like. */
PRIM(b_translate, 3)
{
    BV(v, 0);
    lu_bv table = { 0 }, del;
    int have = args[1] != Py_None;
    if (have && lu_bv_get(args[1], &table) < 0) {
        lu_bv_put(&v);
        return NULL;
    }
    if (lu_bv_get(args[2], &del) < 0) {
        lu_bv_put(&v);
        if (have) lu_bv_put(&table);
        return NULL;
    }
    char gone[256] = { 0 };
    for (Py_ssize_t i = 0; i < del.n; i++) gone[del.p[i]] = 1;
    wbuf b = { 0 };
    int err = wb_room(&b, v.n);
    for (Py_ssize_t i = 0; i < v.n && !err; i++) {
        if (!gone[v.p[i]]) {
            b.p[b.n++] = have ? table.p[v.p[i]] : v.p[i];
        }
    }
    PyObject *r = wb_finish(&b, err);
    lu_bv_put(&v);
    if (have) lu_bv_put(&table);
    lu_bv_put(&del);
    return r;
}

/* Hex ----------------------------------------------------------------- */

/* b_hex(data, sep, bytes_per_sep): sep is a byte value or -1. */
PRIM(b_hex, 3)
{
    IDX(1, sep); IDX(2, group);
    BV(v, 0);
    static const char digits[] = "0123456789abcdef";
    Py_ssize_t g = group < 0 ? -group : group;
    int seps = sep >= 0 && g > 0 && v.n > 0;
    Py_ssize_t nsep = seps ? (v.n - 1) / g : 0;
    PyObject *r = PyUnicode_New(v.n * 2 + nsep, 127);
    if (r != NULL) {
        char *q = (char *)PyUnicode_DATA(r);
        for (Py_ssize_t i = 0; i < v.n; i++) {
            if (seps && i > 0 && (group > 0 ? (v.n - i) % g == 0 : i % g == 0)) {
                *q++ = (char)sep;
            }
            *q++ = digits[v.p[i] >> 4];
            *q++ = digits[v.p[i] & 15];
        }
    }
    lu_bv_put(&v);
    return r;
}

static int
hexval(unsigned char c)
{
    return DIGIT(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
}

/* b_fromhex(x): bytes from a str or bytes-like object of hex digits. */
static PyObject *
prim_b_fromhex(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *x = args[0];
    const unsigned char *s;
    Py_ssize_t n;
    lu_bv v = { .held = 0 };
    if (PyUnicode_Check(x)) {
        n = PyUnicode_GET_LENGTH(x);
        if (!PyUnicode_IS_ASCII(x)) {
            Py_ssize_t i = 0;
            while (PyUnicode_READ_CHAR(x, i) < 128) i++;
            lu_err_format(PyExc_ValueError, "non-hexadecimal number found in fromhex() arg at position %zd", i);
            return NULL;
        }
        s = PyUnicode_1BYTE_DATA(x);
    }
    else if (PyObject_CheckBuffer(x)) {
        if (lu_bv_get(x, &v) < 0) return NULL;
        s = v.p;
        n = v.n;
    }
    else {
        lu_err_format(PyExc_TypeError, "fromhex() argument must be str or bytes-like, not %.200s",
                      Py_TYPE(x)->tp_name);
        return NULL;
    }
    wbuf b = { 0 };
    Py_ssize_t bad = -2, i = 0;
    int err = wb_room(&b, n / 2);
    while (!err && i < n) {
        while (i < n && SPACE(s[i])) i++;
        if (i >= n) break;
        int top = hexval(s[i]);
        if (top < 0) { bad = i; break; }
        if (++i >= n) { bad = -1; break; }
        int bot = hexval(s[i]);
        if (bot < 0) { bad = i; break; }
        i++;
        b.p[b.n++] = (unsigned char)(top << 4 | bot);
    }
    PyObject *r = NULL;
    if (!err && bad == -1) {
        PyErr_SetString(PyExc_ValueError, "fromhex() arg must contain an even number of hexadecimal digits");
    }
    else if (!err && bad >= 0) {
        lu_err_format(PyExc_ValueError, "non-hexadecimal number found in fromhex() arg at position %zd", bad);
    }
    else {
        r = wb_finish(&b, err);
        b.p = NULL;
    }
    PyMem_Free(b.p);
    lu_bv_put(&v);
    return r;
}

/* Iteration ------------------------------------------------------------ */

typedef struct {
    PyObject_HEAD
    Py_ssize_t index;
    PyObject *seq;
} biterobject;

static PyObject *
biter_next(PyObject *op)
{
    biterobject *it = (biterobject *)op;
    PyObject *result = NULL, *dead = NULL;
    Py_BEGIN_CRITICAL_SECTION(op);
    PyObject *seq = it->seq;
    if (seq != NULL) {
        Py_ssize_t n = PyBytes_Check(seq) ? PyBytes_GET_SIZE(seq) : PyByteArray_GET_SIZE(seq);
        if (it->index < n) {
            unsigned char c = PyBytes_Check(seq) ? (unsigned char)PyBytes_AS_STRING(seq)[it->index]
                                                 : (unsigned char)BA(seq)->ob_bytes[it->index];
            it->index++;
            result = PyLong_FromLong(c);
        }
        else {
            it->seq = NULL;
            dead = seq;
        }
    }
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(dead);
    return result;
}

static PyObject *
biter_self(PyObject *op)
{
    return Py_NewRef(op);
}

static void
biter_dealloc(PyObject *op)
{
    Py_XDECREF(((biterobject *)op)->seq);
    lu_object_free(op);
}

static int
biter_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(((biterobject *)op)->seq);
    return 0;
}

/* b_iter(x): an iterator over the bytes of bytes or bytearray. */
static PyObject *
prim_b_iter(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyTypeObject *tp = PyBytes_Check(args[0]) ? &PyBytesIter_Type : &PyByteArrayIter_Type;
    if (!PyBytes_Check(args[0]) && !PyByteArray_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "b_iter() needs bytes or bytearray");
        return NULL;
    }
    biterobject *it = (biterobject *)lu_object_new(tp, sizeof(biterobject));
    if (it != NULL) {
        it->seq = Py_NewRef(args[0]);
    }
    return (PyObject *)it;
}

/* b_next(it): the next byte, or MISSING; b_hint(it): how many are left. */
static PyObject *
prim_b_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *r = biter_next(args[0]);
    return r != NULL ? r : PyErr_Occurred() ? NULL : Py_NewRef(LU_MISSING);
}

static PyObject *
prim_b_hint(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    biterobject *it = (biterobject *)args[0];
    Py_ssize_t left = 0;
    Py_BEGIN_CRITICAL_SECTION(it);
    if (it->seq != NULL) {
        Py_ssize_t n = PyBytes_Check(it->seq) ? PyBytes_GET_SIZE(it->seq) : PyByteArray_GET_SIZE(it->seq);
        left = n > it->index ? n - it->index : 0;
    }
    Py_END_CRITICAL_SECTION();
    return PyLong_FromSsize_t(left);
}

/* b_sizeof(x) */
static PyObject *
prim_b_sizeof(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyTypeObject *tp = Py_TYPE(args[0]);
    if (PyByteArray_Check(args[0])) {
        return PyLong_FromLongLong(tp->tp_basicsize + BA(args[0])->ob_alloc);
    }
    return PyLong_FromLongLong(tp->tp_basicsize + PyBytes_GET_SIZE(args[0]) * tp->tp_itemsize);
}

/* The buffer protocol of bytes and bytearray ------------------------------ */

static int
bytes_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    return PyBuffer_FillInfo(view, self, PyBytes_AS_STRING(self), PyBytes_GET_SIZE(self), 1, flags);
}

static int
bytearray_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    PyByteArrayObject *obj = BA(self);
    int ret;
    Py_BEGIN_CRITICAL_SECTION(self);
    if (view == NULL) {
        PyErr_SetString(PyExc_BufferError, "bytearray_getbuffer: view==NULL argument is obsolete");
        ret = -1;
    }
    else {
        ret = PyBuffer_FillInfo(view, self, obj->ob_bytes == NULL ? _PyByteArray_empty_string : obj->ob_bytes,
                                Py_SIZE(self), 0, flags);
        if (ret >= 0) {
            obj->ob_exports++;
        }
    }
    Py_END_CRITICAL_SECTION();
    return ret;
}

static void
bytearray_releasebuffer(PyObject *self, Py_buffer *view)
{
    Py_BEGIN_CRITICAL_SECTION(self);
    BA(self)->ob_exports--;
    Py_END_CRITICAL_SECTION();
}

static PyBufferProcs bytes_as_buffer = { bytes_getbuffer, NULL };
static PyBufferProcs bytearray_as_buffer = { bytearray_getbuffer, bytearray_releasebuffer };

void
lu_bytes_init(void)
{
    PyByteArray_Type.tp_basicsize = sizeof(PyByteArrayObject);
    PyByteArray_Type.tp_flags |= Py_TPFLAGS_DEFAULT;
    PyByteArray_Type.tp_dealloc = bytearray_dealloc;
    PyByteArray_Type.tp_alloc = PyType_GenericAlloc;
    PyBytes_Type.tp_as_buffer = &bytes_as_buffer;
    PyByteArray_Type.tp_as_buffer = &bytearray_as_buffer;
    PyTypeObject *iters[] = { &PyBytesIter_Type, &PyByteArrayIter_Type };
    for (int i = 0; i < 2; i++) {
        iters[i]->tp_basicsize = sizeof(biterobject);
        iters[i]->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        iters[i]->tp_dealloc = biter_dealloc;
        iters[i]->tp_traverse = biter_traverse;
        iters[i]->tp_iter = biter_self;
        iters[i]->tp_iternext = biter_next;
    }
    lu_memoryview_init();
}

/* The C API over the layouts ------------------------------------------- */

char *
PyBytes_AsString(PyObject *op)
{
    if (!PyBytes_Check(op)) {
        lu_err_format(PyExc_TypeError, "expected bytes, %.200s found", Py_TYPE(op)->tp_name);
        return NULL;
    }
    return PyBytes_AS_STRING(op);
}

int
PyBytes_AsStringAndSize(PyObject *obj, char **s, Py_ssize_t *len)
{
    if (s == NULL) {
        lu_err_format(PyExc_SystemError, "bad argument to internal function");
        return -1;
    }
    if (!PyBytes_Check(obj)) {
        lu_err_format(PyExc_TypeError, "expected bytes, %.200s found", Py_TYPE(obj)->tp_name);
        return -1;
    }
    *s = PyBytes_AS_STRING(obj);
    if (len != NULL) {
        *len = PyBytes_GET_SIZE(obj);
    }
    else if (strlen(*s) != (size_t)PyBytes_GET_SIZE(obj)) {
        PyErr_SetString(PyExc_ValueError, "embedded null byte");
        return -1;
    }
    return 0;
}

Py_ssize_t
PyBytes_Size(PyObject *op)
{
    if (!PyBytes_Check(op)) {
        lu_err_format(PyExc_TypeError, "expected bytes, %.200s found", Py_TYPE(op)->tp_name);
        return -1;
    }
    return PyBytes_GET_SIZE(op);
}

PyObject *
PyBytes_FromString(const char *str)
{
    return PyBytes_FromStringAndSize(str, (Py_ssize_t)strlen(str));
}

/* Objects/bytesobject.c PyBytes_Repr. */
PyObject *
PyBytes_Repr(PyObject *obj, int smartquotes)
{
    PyBytesObject *op = (PyBytesObject *)obj;
    Py_ssize_t i, length = Py_SIZE(op);
    Py_ssize_t newsize, squotes, dquotes;
    PyObject *v;
    unsigned char quote;
    const unsigned char *s;

    /* Compute size of output string */
    squotes = dquotes = 0;
    newsize = 3; /* b'' */
    s = (const unsigned char *)op->ob_sval;
    for (i = 0; i < length; i++) {
        Py_ssize_t incr = 1;
        switch (s[i]) {
        case '\'': squotes++; break;
        case '"':  dquotes++; break;
        case '\\': case '\t': case '\n': case '\r':
            incr = 2; break; /* \C */
        default:
            if (s[i] < ' ' || s[i] >= 0x7f)
                incr = 4; /* \xHH */
        }
        if (newsize > PY_SSIZE_T_MAX - incr)
            goto overflow;
        newsize += incr;
    }
    quote = '\'';
    if (smartquotes && squotes && !dquotes)
        quote = '"';
    if (squotes && quote == '\'') {
        if (newsize > PY_SSIZE_T_MAX - squotes)
            goto overflow;
        newsize += squotes;
    }

    v = PyUnicode_New(newsize, 127);
    if (v == NULL) {
        return NULL;
    }
    Py_UCS1 *p = PyUnicode_1BYTE_DATA(v);

    *p++ = 'b', *p++ = quote;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)op->ob_sval[i];
        if (c == quote || c == '\\')
            *p++ = '\\', *p++ = c;
        else if (c == '\t')
            *p++ = '\\', *p++ = 't';
        else if (c == '\n')
            *p++ = '\\', *p++ = 'n';
        else if (c == '\r')
            *p++ = '\\', *p++ = 'r';
        else if (c < ' ' || c >= 0x7f) {
            *p++ = '\\';
            *p++ = 'x';
            *p++ = Py_hexdigits[(c & 0xf0) >> 4];
            *p++ = Py_hexdigits[c & 0xf];
        }
        else
            *p++ = c;
    }
    *p++ = quote;
    return v;

  overflow:
    PyErr_SetString(PyExc_OverflowError,
                    "bytes object is too large to make repr");
    return NULL;
}

/* Objects/bytesobject.c PyBytes_DecodeEscape (over _PyBytes_DecodeEscape2).
   The unescaped result is never longer than the input, so len bytes are
   allocated up front and trimmed after. */
PyObject *
PyBytes_DecodeEscape(const char *s, Py_ssize_t len, const char *errors,
                     Py_ssize_t Py_UNUSED(unicode), const char *Py_UNUSED(recode_encoding))
{
    PyObject *r = PyBytes_FromStringAndSize(NULL, len);
    if (r == NULL) {
        return NULL;
    }
    char *p = PyBytes_AS_STRING(r);
    const char *start = s, *end = s + len;
    int c;
    int bad_escape = -1;

    while (s < end) {
        if (*s != '\\') {
            *p++ = *s++;
            continue;
        }

        s++;
        if (s == end) {
            PyErr_SetString(PyExc_ValueError,
                            "Trailing \\ in string");
            goto failed;
        }

        switch (*s++) {
        /* XXX This assumes ASCII! */
        case '\n': break;
        case '\\': *p++ = '\\'; break;
        case '\'': *p++ = '\''; break;
        case '\"': *p++ = '\"'; break;
        case 'b': *p++ = '\b'; break;
        case 'f': *p++ = '\014'; break; /* FF */
        case 't': *p++ = '\t'; break;
        case 'n': *p++ = '\n'; break;
        case 'r': *p++ = '\r'; break;
        case 'v': *p++ = '\013'; break; /* VT */
        case 'a': *p++ = '\007'; break; /* BEL, not classic C */
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7':
            c = s[-1] - '0';
            if (s < end && '0' <= *s && *s <= '7') {
                c = (c << 3) + *s++ - '0';
                if (s < end && '0' <= *s && *s <= '7')
                    c = (c << 3) + *s++ - '0';
            }
            if (c > 0377) {
                if (bad_escape == -1) {
                    bad_escape = c;
                }
            }
            *p++ = (char)c;
            break;
        case 'x':
            if (s + 1 < end) {
                int digit1, digit2;
                digit1 = hexval(Py_CHARMASK(s[0]));
                digit2 = hexval(Py_CHARMASK(s[1]));
                if (digit1 >= 0 && digit2 >= 0) {
                    *p++ = (char)((digit1 << 4) + digit2);
                    s += 2;
                    break;
                }
            }
            /* invalid hexadecimal digits */

            if (!errors || strcmp(errors, "strict") == 0) {
                PyErr_Format(PyExc_ValueError,
                             "invalid \\x escape at position %zd",
                             s - 2 - start);
                goto failed;
            }
            if (strcmp(errors, "replace") == 0) {
                *p++ = '?';
            }
            else if (strcmp(errors, "ignore") == 0) {
                /* do nothing */
            }
            else {
                PyErr_Format(PyExc_ValueError,
                             "decoding error; unknown "
                             "error handling code: %.400s",
                             errors);
                goto failed;
            }
            /* skip \x */
            if (s < end && Py_ISXDIGIT(s[0]))
                s++; /* and a hexdigit */
            break;

        default:
            if (bad_escape == -1) {
                bad_escape = (unsigned char)s[-1];
            }
            *p++ = '\\';
            s--;
        }
    }

    if (_PyBytes_Resize(&r, p - PyBytes_AS_STRING(r)) < 0) {
        return NULL;
    }
    if (bad_escape != -1) {
        if (bad_escape > 0xff) {
            if (PyErr_WarnFormat(PyExc_DeprecationWarning, 1,
                                 "b\"\\%o\" is an invalid octal escape sequence. "
                                 "Such sequences will not work in the future. ",
                                 bad_escape) < 0)
            {
                Py_DECREF(r);
                return NULL;
            }
        }
        else {
            if (PyErr_WarnFormat(PyExc_DeprecationWarning, 1,
                                 "b\"\\%c\" is an invalid escape sequence. "
                                 "Such sequences will not work in the future. ",
                                 bad_escape) < 0)
            {
                Py_DECREF(r);
                return NULL;
            }
        }
    }
    return r;

  failed:
    Py_DECREF(r);
    return NULL;
}

/* An iterator of ints, as bytes: the _PyBytes_FromIterator tail of
   Objects/bytesobject.c PyBytes_FromObject. */
static PyObject *
bytes_from_iterator(PyObject *it)
{
    wbuf b = { 0 };
    int err = 0;
    PyObject *item;
    while (!err && (item = PyIter_Next(it)) != NULL) {
        Py_ssize_t value = PyNumber_AsSsize_t(item, NULL);
        if (value == -1 && PyErr_Occurred()) {
            err = 1;
        }
        else if (value < 0 || value >= 256) {
            PyErr_SetString(PyExc_ValueError,
                            "bytes must be in range(0, 256)");
            err = 1;
        }
        else {
            unsigned char c = (unsigned char)value;
            err = wb_put(&b, &c, 1);
        }
        Py_DECREF(item);
    }
    if (PyErr_Occurred()) {
        err = 1;
    }
    return wb_finish(&b, err);
}

/* Objects/bytesobject.c PyBytes_FromObject. */
PyObject *
PyBytes_FromObject(PyObject *x)
{
    if (x == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }

    if (PyBytes_CheckExact(x)) {
        return Py_NewRef(x);
    }

    /* Use the modern buffer interface */
    if (PyObject_CheckBuffer(x)) {
        PyObject *args[1] = { x };
        return prim_b_bytes(NULL, args, 1);
    }

    if (PyList_CheckExact(x) || PyTuple_CheckExact(x)) {
        PyObject *args[1] = { x };
        return prim_b_from_ints(NULL, args, 1);
    }

    if (!PyUnicode_Check(x)) {
        PyObject *it = PyObject_GetIter(x);
        if (it != NULL) {
            PyObject *result = bytes_from_iterator(it);
            Py_DECREF(it);
            return result;
        }
        if (!PyErr_ExceptionMatches(PyExc_TypeError)) {
            return NULL;
        }
    }

    lu_err_format(PyExc_TypeError,
                  "cannot convert '%.200s' object to bytes",
                  Py_TYPE(x)->tp_name);
    return NULL;
}

/* Objects/bytesobject.c PyBytes_Join (over stringlib/join.h): the joining
   itself is prim b_join over the items as a tuple. */
PyObject *
PyBytes_Join(PyObject *sep, PyObject *iterable)
{
    if (sep == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (!PyBytes_Check(sep)) {
        PyErr_Format(PyExc_TypeError,
                     "sep: expected bytes, got %T", sep);
        return NULL;
    }

    PyObject *seq = PySequence_Fast(iterable, "can only join an iterable");
    if (seq == NULL) {
        return NULL;
    }
    PyObject *items = PyTuple_CheckExact(seq) ? Py_NewRef(seq) : PySequence_Tuple(seq);
    Py_DECREF(seq);
    if (items == NULL) {
        return NULL;
    }
    PyObject *args[2] = { sep, items };
    PyObject *r = prim_b_join(NULL, args, 2);
    Py_DECREF(items);
    return r;
}

/* Objects/bytesobject.c PyBytes_Concat over bytes_concat: raw buffers on
   both sides, so a strided view or a non-buffer raises TypeError rather
   than going through the constructor.  CPython appends in place when the
   left side is uniquely referenced; lucifer's allocator cannot realloc an
   object, so the result is always fresh, which the contract allows. */
void
PyBytes_Concat(PyObject **pv, PyObject *w)
{
    if (*pv == NULL) {
        return;
    }
    if (w == NULL) {
        Py_CLEAR(*pv);
        return;
    }
    Py_buffer va, vb;
    PyObject *result = NULL;
    va.len = -1;
    vb.len = -1;
    if (PyObject_GetBuffer(*pv, &va, PyBUF_SIMPLE) != 0 ||
        PyObject_GetBuffer(w, &vb, PyBUF_SIMPLE) != 0) {
        PyErr_Format(PyExc_TypeError, "can't concat %.100s to %.100s",
                     Py_TYPE(w)->tp_name, Py_TYPE(*pv)->tp_name);
        goto done;
    }
    /* Optimize end cases */
    if (va.len == 0 && PyBytes_CheckExact(w)) {
        result = Py_NewRef(w);
        goto done;
    }
    if (vb.len == 0 && PyBytes_CheckExact(*pv)) {
        result = Py_NewRef(*pv);
        goto done;
    }
    if (va.len > PY_SSIZE_T_MAX - vb.len) {
        PyErr_NoMemory();
        goto done;
    }
    result = PyBytes_FromStringAndSize(NULL, va.len + vb.len);
    if (result != NULL) {
        memcpy(PyBytes_AS_STRING(result), va.buf, va.len);
        memcpy(PyBytes_AS_STRING(result) + va.len, vb.buf, vb.len);
    }
  done:
    if (va.len != -1)
        PyBuffer_Release(&va);
    if (vb.len != -1)
        PyBuffer_Release(&vb);
    Py_SETREF(*pv, result);
}

void
PyBytes_ConcatAndDel(PyObject **pv, PyObject *w)
{
    PyBytes_Concat(pv, w);
    Py_XDECREF(w);
}

/* Objects/bytesobject.c _PyBytes_Resize: the contract allows the result to
   be the same object or a copy, so this always copies (lucifer's allocator
   cannot realloc an object in place).  A copy is required anyway whenever
   the object is shared or its hash was computed (gh-156995), and a fresh
   object is never one of the 0- and 1-byte singletons. */
int
_PyBytes_Resize(PyObject **pv, Py_ssize_t size)
{
    PyObject *v = *pv;
    if (!PyBytes_Check(v) || size < 0) {
        *pv = NULL;
        Py_DECREF(v);
        PyErr_BadInternalCall();
        return -1;
    }
    Py_ssize_t oldsize = PyBytes_GET_SIZE(v);
    if (oldsize == size) {
        /* Leave the object unchanged if the new size is the same as the old
           size, even if the object is not uniquely referenced or if the hash
           value was already computed. */
        return 0;
    }
    PyObject *r = PyBytes_FromStringAndSize(NULL, size);
    if (r != NULL && oldsize > 0) {
        memcpy(PyBytes_AS_STRING(r), PyBytes_AS_STRING(v),
               oldsize < size ? oldsize : size);
    }
    *pv = r;
    Py_DECREF(v);
    return r == NULL ? -1 : 0;
}

PyObject *
PyByteArray_FromStringAndSize(const char *bytes, Py_ssize_t size)
{
    if (size < 0) {
        PyErr_SetString(PyExc_SystemError, "Negative size passed to PyByteArray_FromStringAndSize");
        return NULL;
    }
    if (size == PY_SSIZE_T_MAX) {
        return PyErr_NoMemory();
    }
    PyObject *ba = PyByteArray_Type.tp_alloc(&PyByteArray_Type, 0);
    if (ba == NULL) {
        return NULL;
    }
    if (size > 0 && (ba_resize(ba, size) < 0)) {
        Py_DECREF(ba);
        return NULL;
    }
    if (size > 0 && bytes != NULL) {
        memcpy(BA(ba)->ob_bytes, bytes, size);
    }
    return ba;
}

Py_ssize_t
PyByteArray_Size(PyObject *self)
{
    return PyByteArray_GET_SIZE(self);
}

char *
PyByteArray_AsString(PyObject *self)
{
    return PyByteArray_AS_STRING(self);
}

int
PyByteArray_Resize(PyObject *self, Py_ssize_t size)
{
    if (size < 0) {
        PyErr_Format(PyExc_ValueError,
                     "Can only resize to positive sizes, got %zd", size);
        return -1;
    }
    int r;
    Py_BEGIN_CRITICAL_SECTION(self);
    r = ba_resize(self, size);
    Py_END_CRITICAL_SECTION();
    return r;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_bytes_methods[] = {
    FAST(ba_splice), FAST(ba_set_step), FAST(ba_del_step), FAST(ba_set_item), FAST(ba_pop),
    FAST(ba_remove), FAST(ba_reverse), FAST(ba_resize), FAST(ba_irepeat), FAST(ba_alloc),
    FAST(ba_new), FAST(ba_wrap), FAST(ba_exports), FAST(b_len), FAST(b_check), FAST(b_bytes),
    FAST(b_from_ints), FAST(b_zeros), FAST(b_subtype), FAST(b_item), FAST(b_slice),
    FAST(b_slice_step), FAST(b_cmp), FAST(b_hash), FAST(b_keyed_hash), FAST(b_concat), FAST(b_repeat), FAST(b_find),
    FAST(b_count), FAST(b_match), FAST(b_replace), FAST(b_join), FAST(b_split),
    FAST(b_splitlines), FAST(b_strip), FAST(b_case), FAST(b_is), FAST(b_expandtabs),
    FAST(b_translate), FAST(b_hex), FAST(b_fromhex), FAST(b_iter), FAST(b_next), FAST(b_hint), FAST(b_sizeof),
    { NULL, NULL, 0, NULL },
};
