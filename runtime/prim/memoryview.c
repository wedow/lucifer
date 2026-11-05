/* memoryview primitives (DESIGN 4.1): PyMemoryViewObject as cp314t lays it
   out, over a managed buffer shared by every view of one export. The
   managed buffer is a collectable object (mbuf_traverse, as Objects/
   memoryobject.c): it holds the only reference to the exporter, counts its
   views and releases the exporter's buffer when the last one goes. Views are one- or
   multi-dimensional with native single-character formats; indexing,
   slicing and assignment are one-dimensional, as in CPython. */

#include "prim/bytes.h"

#include <string.h>

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)

#define MV(o) ((PyMemoryViewObject *)(o))
#define MB(mv) ((mv)->mbuf)
#define MAX_NDIM 64

static PyObject *
bool_of(int v)
{
    return Py_NewRef(v ? Py_True : Py_False);
}

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

static int
check_mv(PyObject *o)
{
    if (!PyMemoryView_Check(o)) {
        lu_err_format(PyExc_TypeError, "a memoryview is required, not '%.200s'", Py_TYPE(o)->tp_name);
        return -1;
    }
    return 0;
}

static int
released(PyObject *o)
{
    if (MV(o)->flags & _Py_MEMORYVIEW_RELEASED) {
        PyErr_SetString(PyExc_ValueError, "operation forbidden on released memoryview object");
        return 1;
    }
    return 0;
}

#define SELF(var) PyMemoryViewObject *var = MV(args[0]); \
    do { if (check_mv(args[0]) < 0 || released(args[0])) return NULL; } while (0)

/* Native single-character formats ----------------------------------- */

static const struct { char c; Py_ssize_t size; } native[] = {
    { '?', 1 }, { 'c', 1 }, { 'b', 1 }, { 'B', 1 }, { 'h', 2 }, { 'H', 2 }, { 'i', 4 },
    { 'I', 4 }, { 'l', 8 }, { 'L', 8 }, { 'q', 8 }, { 'Q', 8 }, { 'n', 8 }, { 'N', 8 },
    { 'e', 2 }, { 'f', 4 }, { 'd', 8 }, { 'P', 8 }, { 0, 0 },
};

/* The static format string of a native character, or NULL. */
static const char *
native_fmt(char c)
{
    static const char all[] = "?\0c\0b\0B\0h\0H\0i\0I\0l\0L\0q\0Q\0n\0N\0e\0f\0d\0P";
    for (const char *p = all; p < all + sizeof all; p += 2) {
        if (*p == c) {
            return p;
        }
    }
    return NULL;
}

static Py_ssize_t
fmt_size(char c)
{
    for (int i = 0; native[i].c; i++) {
        if (native[i].c == c) {
            return native[i].size;
        }
    }
    return -1;
}

/* The character of a view's format when it is native (optionally '@'). */
static int
fmt_char(const char *fmt, char *out)
{
    if (fmt[0] == '@') {
        fmt++;
    }
    if (fmt[0] == '\0' || fmt[1] != '\0' || fmt_size(fmt[0]) < 0) {
        return -1;
    }
    *out = fmt[0];
    return 0;
}

static PyObject *
from_u64(unsigned long long v)
{
    digit d[3] = { (digit)(v & PyLong_MASK), (digit)((v >> PyLong_SHIFT) & PyLong_MASK), (digit)(v >> (2 * PyLong_SHIFT)) };
    Py_ssize_t n = d[2] ? 3 : d[1] ? 2 : d[0] ? 1 : 0;
    return lu_long_from_digits(0, n, d);
}

static PyObject *
unpack_item(const char *p, char c)
{
    switch (c) {
    case '?': return bool_of(*p != 0);
    case 'c': return PyBytes_FromStringAndSize(p, 1);
    case 'b': return PyLong_FromLong(*(signed char *)p);
    case 'B': return PyLong_FromLong(*(unsigned char *)p);
    case 'h': { short v; memcpy(&v, p, sizeof v); return PyLong_FromLong(v); }
    case 'H': { unsigned short v; memcpy(&v, p, sizeof v); return PyLong_FromLong(v); }
    case 'i': { int v; memcpy(&v, p, sizeof v); return PyLong_FromLong(v); }
    case 'I': { unsigned int v; memcpy(&v, p, sizeof v); return PyLong_FromLongLong(v); }
    case 'l': case 'q': case 'n': { long v; memcpy(&v, p, sizeof v); return PyLong_FromLong(v); }
    case 'L': case 'Q': case 'N': case 'P': {
        unsigned long v; memcpy(&v, p, sizeof v); return from_u64(v);
    }
    case 'e': return PyFloat_FromDouble(PyFloat_Unpack2(p, 1));
    case 'f': { float v; memcpy(&v, p, sizeof v); return PyFloat_FromDouble(v); }
    case 'd': { double v; memcpy(&v, p, sizeof v); return PyFloat_FromDouble(v); }
    }
    return NULL;
}

/* A non-negative int below 2**64. */
static int
as_u64(PyObject *v, unsigned long long *out)
{
    uintptr_t tag = ((PyLongObject *)v)->long_value.lv_tag;
    size_t n = tag >> _PyLong_NON_SIZE_BITS;
    if (n > 3 || (n == 3 && ((PyLongObject *)v)->long_value.ob_digit[2] >= 16)) {
        return -1;
    }
    unsigned long long u = 0;
    for (size_t i = n; i-- > 0; ) {
        u = (u << PyLong_SHIFT) | ((PyLongObject *)v)->long_value.ob_digit[i];
    }
    *out = u;
    return 0;
}

static int
pack_fail(const char *fmt, int type_error)
{
    lu_err_format(type_error ? PyExc_TypeError : PyExc_ValueError,
                  "memoryview: invalid %s for format '%s'", type_error ? "type" : "value", fmt);
    return -1;
}

/* A failed conversion becomes the format's error, as fix_error_int does. */
static int
pack_convert_fail(const char *fmt)
{
    if (PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_Clear();
        return pack_fail(fmt, 1);
    }
    if (PyErr_ExceptionMatches(PyExc_OverflowError) || PyErr_ExceptionMatches(PyExc_ValueError)) {
        PyErr_Clear();
        return pack_fail(fmt, 0);
    }
    return -1;
}

/* The item packed from an int or an __index__ object (pylong_as_ld and its
   kin): range errors are ValueErrors of the format. */
static int
pack_int(char *p, char c, PyObject *v, const char *fmt)
{
    if (c == 'P' && !PyLong_Check(v)) {     /* PyLong_AsVoidPtr takes ints only */
        return pack_fail(fmt, 1);
    }
    PyObject *n = PyNumber_Index(v);
    if (n == NULL) {
        return pack_convert_fail(fmt);
    }
    int r = -1;
    Py_ssize_t size = fmt_size(c);
    long long x;
    int negative = (((PyLongObject *)n)->long_value.lv_tag & 3) == 2;
    if (c == 'P' && negative) {             /* a pointer also takes the signed range */
        c = 'q';
    }
    if (c == 'B' || c == 'H' || c == 'I' || c == 'L' || c == 'Q' || c == 'N' || c == 'P') {
        unsigned long long u;
        if (negative || as_u64(n, &u) < 0
            || (size < 8 && u >= (1ULL << (size * 8)))) {
            r = pack_fail(fmt, 0);
            goto done;
        }
        x = (long long)u;
    }
    else {
        long l;
        if (lu_long_as_long(n, &l) < 0) {
            PyErr_Clear();
            r = pack_fail(fmt, 0);
            goto done;
        }
        x = l;
        if (size < 8 && (x < -(1LL << (size * 8 - 1)) || x > (1LL << (size * 8 - 1)) - 1)) {
            r = pack_fail(fmt, 0);
            goto done;
        }
    }
    memcpy(p, &x, size);                /* little-endian hosts */
    r = 0;
done:
    Py_DECREF(n);
    return r;
}

/* Packs v as the item of format c at p. The conversion may run Python code
   that releases mv, which is then an error. */
static int
pack_item(PyMemoryViewObject *mv, char *p, char c, PyObject *v, const char *fmt)
{
    int r;
    switch (c) {
    case '?': {
        int t = lu_truth_exact(v);
        if (t < 0) {
            return pack_fail(fmt, 1);
        }
        *p = (char)t;
        return 0;
    }
    case 'c':
        if (!PyBytes_Check(v) || PyBytes_GET_SIZE(v) != 1) {
            return pack_fail(fmt, !PyBytes_Check(v));
        }
        *p = PyBytes_AS_STRING(v)[0];
        return 0;
    case 'e': case 'f': case 'd': {
        double d = PyFloat_AsDouble(v);
        if (d == -1.0 && PyErr_Occurred()) {
            return pack_convert_fail(fmt);
        }
        if (released((PyObject *)mv)) {
            return -1;
        }
        if (c == 'e') {
            return PyFloat_Pack2(d, p, 1) < 0 ? pack_convert_fail(fmt) : 0;
        }
        if (c == 'f') { float f = (float)d; memcpy(p, &f, sizeof f); }
        else memcpy(p, &d, sizeof d);
        return 0;
    }
    }
    char buf[8];
    r = pack_int(buf, c, v, fmt);
    if (r < 0 || released((PyObject *)mv)) {
        return -1;
    }
    memcpy(p, buf, fmt_size(c));
    return 0;
}

/* Allocation and sharing --------------------------------------------- */

static PyMemoryViewObject *
mv_alloc(int ndim)
{
    Py_ssize_t slots = 3 * (ndim > 0 ? ndim : 1);
    PyMemoryViewObject *mv = (PyMemoryViewObject *)lu_object_new(&PyMemoryView_Type,
        offsetof(PyMemoryViewObject, ob_array) + slots * sizeof(Py_ssize_t));
    if (mv != NULL) {
        Py_SET_SIZE(mv, slots);
        mv->view.shape = mv->ob_array;
        mv->view.strides = mv->ob_array + ndim;
        mv->view.suboffsets = mv->ob_array + 2 * ndim;
    }
    return mv;
}

static void
init_flags(PyMemoryViewObject *mv)
{
    Py_buffer *view = &mv->view;
    int flags = 0;
    switch (view->ndim) {
    case 0:
        flags |= _Py_MEMORYVIEW_SCALAR | _Py_MEMORYVIEW_C | _Py_MEMORYVIEW_FORTRAN;
        break;
    case 1:
        if (view->shape[0] == 1 || view->strides[0] == view->itemsize) {
            flags |= _Py_MEMORYVIEW_C | _Py_MEMORYVIEW_FORTRAN;
        }
        break;
    default:
        if (PyBuffer_IsContiguous(view, 'C')) flags |= _Py_MEMORYVIEW_C;
        if (PyBuffer_IsContiguous(view, 'F')) flags |= _Py_MEMORYVIEW_FORTRAN;
    }
    if (view->suboffsets) {
        flags |= _Py_MEMORYVIEW_PIL;
        flags &= ~(_Py_MEMORYVIEW_C | _Py_MEMORYVIEW_FORTRAN);
    }
    mv->flags = flags;
}

static void
init_len(Py_buffer *view)
{
    Py_ssize_t n = 1;
    for (int i = 0; i < view->ndim; i++) {
        n *= view->shape[i];
    }
    view->len = n * view->itemsize;
}

/* The managed buffer: mbuf_release, mbuf_dealloc, mbuf_traverse and mbuf_clear
   of Objects/memoryobject.c. */

static PyTypeObject managed_buffer_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "managedbuffer",
    .tp_basicsize = sizeof(_PyManagedBufferObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
};

static void
mbuf_release(_PyManagedBufferObject *mb)
{
    if (!(mb->flags & _Py_MANAGED_BUFFER_RELEASED)) {
        mb->flags |= _Py_MANAGED_BUFFER_RELEASED;
        PyBuffer_Release(&mb->master);
    }
}

static void
mbuf_dealloc(PyObject *op)
{
    mbuf_release((_PyManagedBufferObject *)op);
    lu_object_free(op);
}

static int
mbuf_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(((_PyManagedBufferObject *)op)->master.obj);
    return 0;
}

static int
mbuf_clear(PyObject *op)
{
    mbuf_release((_PyManagedBufferObject *)op);
    return 0;
}

/* A managed buffer with no exporter yet. */
static _PyManagedBufferObject *
mbuf_new(void)
{
    return (_PyManagedBufferObject *)lu_object_new(&managed_buffer_type, sizeof(_PyManagedBufferObject));
}

/* A view on mb with the layout of src (the master when NULL). */
static PyMemoryViewObject *
add_view(_PyManagedBufferObject *mb, const Py_buffer *src, int ndim)
{
    if (src == NULL) {
        src = &mb->master;
    }
    PyMemoryViewObject *mv = mv_alloc(ndim);
    if (mv == NULL) {
        return NULL;
    }
    mv->mbuf = (_PyManagedBufferObject *)Py_NewRef(mb);
    __atomic_add_fetch(&mb->exports, 1, __ATOMIC_ACQ_REL);
    Py_buffer *d = &mv->view;
    d->obj = src->obj;
    d->buf = src->buf;
    d->len = src->len;
    d->itemsize = src->itemsize;
    d->readonly = src->readonly;
    d->format = src->format ? src->format : "B";
    d->ndim = ndim;
    d->internal = src->internal;
    mv->hash = -1;
    d->suboffsets = src->suboffsets ? d->suboffsets : NULL;
    if (src->shape == NULL) {
        d->shape[0] = src->len / src->itemsize;
        d->strides[0] = src->itemsize;
    }
    else {
        for (int i = 0; i < ndim; i++) {
            d->shape[i] = src->shape[i];
            if (src->strides) {
                d->strides[i] = src->strides[i];
            }
            if (d->suboffsets) {
                d->suboffsets[i] = src->suboffsets[i];
            }
        }
        if (src->strides == NULL) {
            PyBuffer_FillContiguousStrides(ndim, d->shape, d->strides, d->itemsize, 'C');
        }
    }
    init_flags(mv);
    return mv;
}

/* Releases a view's hold; the last one releases the exporter's buffer. */
static void
drop_view(PyMemoryViewObject *mv)
{
    mv->flags |= _Py_MEMORYVIEW_RELEASED;
    mv->view.obj = NULL;
    if (__atomic_sub_fetch(&MB(mv)->exports, 1, __ATOMIC_ACQ_REL) == 0) {
        mbuf_release(MB(mv));
    }
}

static void
memory_dealloc(PyObject *op)
{
    PyMemoryViewObject *mv = MV(op);
    if (!(mv->flags & _Py_MEMORYVIEW_RELEASED)) {
        drop_view(mv);
    }
    Py_CLEAR(mv->mbuf);
    lu_object_free(op);
}

static int
memory_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(MV(op)->mbuf);
    return 0;
}

static int
memory_clear(PyObject *op)
{
    PyMemoryViewObject *mv = MV(op);
    if (mv->exports == 0) {
        if (!(mv->flags & _Py_MEMORYVIEW_RELEASED)) {
            drop_view(mv);
        }
        Py_CLEAR(mv->mbuf);
    }
    return 0;
}

/* memoryview(obj) for any exporter, and a view sharing a memoryview's. */
PyObject *
PyMemoryView_FromObject(PyObject *v)
{
    if (PyMemoryView_Check(v)) {
        if (released(v)) {
            return NULL;
        }
        return (PyObject *)add_view(MB(MV(v)), &MV(v)->view, MV(v)->view.ndim);
    }
    if (!PyObject_CheckBuffer(v)) {
        lu_err_format(PyExc_TypeError, "memoryview: a bytes-like object is required, not '%.200s'",
                      Py_TYPE(v)->tp_name);
        return NULL;
    }
    _PyManagedBufferObject *mb = mbuf_new();
    if (mb == NULL) {
        return NULL;
    }
    PyMemoryViewObject *mv = NULL;
    if (PyObject_GetBuffer(v, &mb->master, PyBUF_FULL_RO) == 0) {
        mv = add_view(mb, NULL, mb->master.ndim);
    }
    Py_DECREF(mb);
    return (PyObject *)mv;
}

PyObject *
PyMemoryView_FromBuffer(const Py_buffer *info)
{
    if (info->buf == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyMemoryView_FromBuffer(): info->buf must not be NULL");
        return NULL;
    }
    _PyManagedBufferObject *mb = mbuf_new();
    if (mb == NULL) {
        return NULL;
    }
    mb->master = *info;
    mb->master.obj = NULL;
    /* The shape and strides point into the caller's buffer: copy them where
       the views will find them, as add_view does for each view. */
    PyMemoryViewObject *mv = add_view(mb, info, info->ndim);
    if (mv != NULL) {
        mv->view.obj = NULL;
    }
    Py_DECREF(mb);
    return (PyObject *)mv;
}

PyObject *
PyMemoryView_FromMemory(char *mem, Py_ssize_t size, int flags)
{
    Py_buffer info;
    if (mem == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyMemoryView_FromMemory(): mem must not be NULL");
        return NULL;
    }
    if (PyBuffer_FillInfo(&info, NULL, mem, size, !(flags & PyBUF_WRITE), PyBUF_FULL_RO) < 0) {
        return NULL;
    }
    return PyMemoryView_FromBuffer(&info);
}

/* mv_new(obj) */
static PyObject *
prim_mv_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyMemoryView_FromObject(args[0]);
}

/* mv_release(mv) */
static PyObject *
prim_mv_release(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (check_mv(args[0]) < 0) {
        return NULL;
    }
    PyMemoryViewObject *mv = MV(args[0]);
    if (mv->flags & _Py_MEMORYVIEW_RELEASED) {
        Py_RETURN_NONE;
    }
    if (mv->exports > 0) {
        lu_err_format(PyExc_BufferError, "memoryview has %zd exported buffer%s", mv->exports,
                      mv->exports == 1 ? "" : "s");
        return NULL;
    }
    drop_view(mv);
    Py_RETURN_NONE;
}

/* mv_released(mv) */
static PyObject *
prim_mv_released(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (check_mv(args[0]) < 0) {
        return NULL;
    }
    return bool_of(MV(args[0])->flags & _Py_MEMORYVIEW_RELEASED);
}

/* mv_attr(mv, name) */
static PyObject *
prim_mv_attr(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    SELF(mv);
    Py_buffer *v = &mv->view;
    const char *name = PyUnicode_AsUTF8(args[1]);
    if (!strcmp(name, "obj")) return v->obj ? Py_NewRef(v->obj) : Py_NewRef(Py_None);
    if (!strcmp(name, "nbytes")) return PyLong_FromLongLong(v->len);
    if (!strcmp(name, "readonly")) return bool_of(v->readonly);
    if (!strcmp(name, "itemsize")) return PyLong_FromLongLong(v->itemsize);
    if (!strcmp(name, "format")) return PyUnicode_FromString(v->format);
    if (!strcmp(name, "ndim")) return PyLong_FromLong(v->ndim);
    if (!strcmp(name, "c_contiguous")) return bool_of(mv->flags & _Py_MEMORYVIEW_C);
    if (!strcmp(name, "f_contiguous")) return bool_of(mv->flags & _Py_MEMORYVIEW_FORTRAN);
    if (!strcmp(name, "contiguous")) return bool_of(mv->flags & (_Py_MEMORYVIEW_C | _Py_MEMORYVIEW_FORTRAN));
    const Py_ssize_t *a = !strcmp(name, "shape") ? v->shape : !strcmp(name, "strides") ? v->strides
                        : !strcmp(name, "suboffsets") ? v->suboffsets : NULL;
    if (a == NULL) {
        return PyTuple_New(0);
    }
    PyObject *t = PyTuple_New(v->ndim);
    for (int i = 0; t != NULL && i < v->ndim; i++) {
        PyObject *x = PyLong_FromLongLong(a[i]);
        if (x == NULL) {
            Py_CLEAR(t);
            break;
        }
        PyTuple_SET_ITEM(t, i, x);
    }
    return t;
}

/* mv_len(mv) */
static PyObject *
prim_mv_len(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    SELF(mv);
    if (mv->view.ndim == 0) {
        PyErr_SetString(PyExc_TypeError, "0-dim memory has no length");
        return NULL;
    }
    return PyLong_FromLongLong(mv->view.shape[0]);
}

/* Copying ---------------------------------------------------------------- */

/* The bytes of a view in C order. */
static PyObject *
tobytes(PyMemoryViewObject *mv, char order)
{
    Py_buffer *v = &mv->view;
    PyObject *r = PyBytes_FromStringAndSize(NULL, v->len);
    if (r != NULL && v->len && PyBuffer_ToContiguous(PyBytes_AS_STRING(r), v, v->len, order) < 0) {
        Py_CLEAR(r);
    }
    return r;
}

/* mv_tobytes(mv, order) */
static PyObject *
prim_mv_tobytes(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    SELF(mv);
    IDX(1, order);
    return tobytes(mv, (char)order);
}

static PyObject *
list_rec(const char *buf, const Py_buffer *v, int dim, char c)
{
    Py_ssize_t n = v->shape[dim];
    PyObject *list = PyList_New(0);
    for (Py_ssize_t i = 0; list != NULL && i < n; i++) {
        const char *p = buf + i * v->strides[dim];
        if (v->suboffsets && v->suboffsets[dim] >= 0) {
            p = *(const char *const *)p + v->suboffsets[dim];
        }
        PyObject *x = dim == v->ndim - 1 ? unpack_item(p, c) : list_rec(p, v, dim + 1, c);
        if (x == NULL || lu_list_append(list, x) < 0) {
            Py_XDECREF(x);
            Py_CLEAR(list);
            break;
        }
        Py_DECREF(x);
    }
    return list;
}

/* mv_tolist(mv) */
static PyObject *
prim_mv_tolist(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    SELF(mv);
    char c;
    if (fmt_char(mv->view.format, &c) < 0) {
        lu_err_format(PyExc_NotImplementedError, "memoryview: format %s not supported", mv->view.format);
        return NULL;
    }
    if (mv->view.ndim == 0) {
        return unpack_item(mv->view.buf, c);
    }
    return list_rec(mv->view.buf, &mv->view, 0, c);
}

/* mv_item(mv, i): the 1-D item at the normalized index. */
static PyObject *
prim_mv_item(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    SELF(mv);
    IDX(1, i);
    Py_buffer *v = &mv->view;
    char c;
    if (v->ndim != 1) {
        PyErr_SetString(PyExc_NotImplementedError, "multi-dimensional sub-views are not implemented");
        return NULL;
    }
    if (i < 0 || i >= v->shape[0]) {
        PyErr_SetString(PyExc_IndexError, "index out of bounds on dimension 1");
        return NULL;
    }
    if (fmt_char(v->format, &c) < 0) {
        lu_err_format(PyExc_NotImplementedError, "memoryview: format %s not supported", v->format);
        return NULL;
    }
    const char *p = (const char *)v->buf + i * v->strides[0];
    if (v->suboffsets && v->suboffsets[0] >= 0) {
        p = *(const char *const *)p + v->suboffsets[0];
    }
    return unpack_item(p, c);
}

/* mv_set_item(mv, i, value) */
static PyObject *
prim_mv_set_item(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    SELF(mv);
    IDX(1, i);
    Py_buffer *v = &mv->view;
    char c;
    if (v->readonly) {
        PyErr_SetString(PyExc_TypeError, "cannot modify read-only memory");
        return NULL;
    }
    if (v->ndim != 1) {
        PyErr_SetString(PyExc_NotImplementedError, "multi-dimensional sub-views are not implemented");
        return NULL;
    }
    if (i < 0 || i >= v->shape[0]) {
        PyErr_SetString(PyExc_IndexError, "index out of bounds on dimension 1");
        return NULL;
    }
    if (fmt_char(v->format, &c) < 0) {
        lu_err_format(PyExc_NotImplementedError, "memoryview: format %s not supported", v->format);
        return NULL;
    }
    char *p = (char *)v->buf + i * v->strides[0];
    if (v->suboffsets && v->suboffsets[0] >= 0) {
        p = *(char **)p + v->suboffsets[0];
    }
    if (pack_item(mv, p, c, args[2], v->format) < 0) {
        return NULL;
    }
    Py_RETURN_NONE;
}

/* mv_slice(mv, start, step, count): a view on the 1-D items. */
static PyObject *
prim_mv_slice(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4);
    SELF(mv);
    IDX(1, start); IDX(2, step); IDX(3, count);
    Py_buffer *v = &mv->view;
    PyMemoryViewObject *r = add_view(MB(mv), v, v->ndim);
    if (r == NULL) {
        return NULL;
    }
    if (count > 0) {
        r->view.buf = (char *)v->buf + v->strides[0] * start;
    }
    r->view.shape[0] = count;
    r->view.strides[0] = v->strides[0] * step;
    init_len(&r->view);
    init_flags(r);
    return (PyObject *)r;
}

/* mv_set_slice(mv, start, step, count, src): mv[start::step] = src, count items. */
static PyObject *
prim_mv_set_slice(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(5);
    SELF(mv);
    IDX(1, start); IDX(2, step); IDX(3, count);
    Py_buffer *v = &mv->view;
    if (v->readonly) {
        PyErr_SetString(PyExc_TypeError, "cannot modify read-only memory");
        return NULL;
    }
    if (v->ndim != 1) {
        PyErr_SetString(PyExc_NotImplementedError,
                        "memoryview slice assignments are currently restricted to ndim = 1");
        return NULL;
    }
    Py_buffer src;
    if (PyObject_GetBuffer(args[4], &src, PyBUF_FULL_RO) < 0) {
        return NULL;
    }
    PyObject *r = NULL;
    const char *sf = src.format ? src.format : "B";
    if (src.ndim != 1 || src.itemsize != v->itemsize || strcmp(sf, v->format) != 0
        || (src.shape ? src.shape[0] : src.len / src.itemsize) != count) {
        PyErr_SetString(PyExc_ValueError,
                        "memoryview assignment: lvalue and rvalue have different structures");
    }
    else {
        Py_ssize_t sz = v->itemsize;
        char *tmp = PyMem_Malloc(count * sz + 1);
        if (tmp == NULL) {
            PyErr_NoMemory();
        }
        else {
            Py_ssize_t sstride = src.strides ? src.strides[0] : sz;
            for (Py_ssize_t i = 0; i < count; i++) {
                memcpy(tmp + i * sz, (char *)src.buf + i * sstride, sz);
            }
            for (Py_ssize_t i = 0; i < count; i++) {
                memcpy((char *)v->buf + (start + i * step) * v->strides[0], tmp + i * sz, sz);
            }
            PyMem_Free(tmp);
            r = Py_NewRef(Py_None);
        }
    }
    PyBuffer_Release(&src);
    return r;
}

/* mv_cast(mv, format, shape): shape is None or a tuple of ints. */
static PyObject *
prim_mv_cast(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    SELF(mv);
    Py_buffer *v = &mv->view;
    PyObject *fmt = args[1];
    if (!PyUnicode_Check(fmt) || !PyUnicode_IS_ASCII(fmt)) {
        PyErr_SetString(PyExc_ValueError,
                        "memoryview: destination format must be a native single character format prefixed with an optional '@'");
        return NULL;
    }
    char dc, sc = 0;
    const char *f = PyUnicode_AsUTF8(fmt);
    if (fmt_char(f, &dc) < 0) {
        PyErr_SetString(PyExc_ValueError,
                        "memoryview: destination format must be a native single character format prefixed with an optional '@'");
        return NULL;
    }
    if (!(mv->flags & _Py_MEMORYVIEW_C)) {
        PyErr_SetString(PyExc_TypeError, "memoryview: casts are restricted to C-contiguous views");
        return NULL;
    }
    if ((v->shape && v->ndim > 0 && v->len == 0 && 0) || (v->ndim > 0 && v->len == 0 && v->shape[0] == 0 && v->ndim > 1)) {
        PyErr_SetString(PyExc_TypeError, "memoryview: cannot cast view with zeros in shape or strides");
        return NULL;
    }
    int srcbyte = fmt_char(v->format, &sc) == 0 && (sc == 'B' || sc == 'b' || sc == 'c');
    int destbyte = dc == 'B' || dc == 'b' || dc == 'c';
    Py_ssize_t itemsize = fmt_size(dc);
    PyObject *shape = args[2];
    int nd = shape == Py_None ? 1 : (int)PyTuple_GET_SIZE(shape);
    if (shape != Py_None) {
        if (v->ndim != 1 && nd != 1) {
            PyErr_SetString(PyExc_TypeError, "memoryview: cast must be 1D -> ND or ND -> 1D");
            return NULL;
        }
        if (nd > MAX_NDIM) {
            lu_err_format(PyExc_ValueError, "memoryview: number of dimensions must not exceed %d", MAX_NDIM);
            return NULL;
        }
    }
    if (!srcbyte && !destbyte) {
        PyErr_SetString(PyExc_TypeError, "memoryview: cannot cast between two non-byte formats");
        return NULL;
    }
    if (v->len % itemsize) {
        PyErr_SetString(PyExc_TypeError, "memoryview: length is not a multiple of itemsize");
        return NULL;
    }
    PyMemoryViewObject *r = add_view(MB(mv), v, nd);
    if (r == NULL) {
        return NULL;
    }
    r->view.format = (char *)native_fmt(dc);
    r->view.itemsize = itemsize;
    r->view.ndim = nd;
    r->view.suboffsets = NULL;
    r->view.buf = v->buf;
    if (shape == Py_None) {
        r->view.shape[0] = v->len / itemsize;
        r->view.strides[0] = itemsize;
    }
    else {
        Py_ssize_t prod = 1;
        for (int i = 0; i < nd; i++) {
            PyObject *x = PyTuple_GET_ITEM(shape, i);
            Py_ssize_t d;
            if (!PyLong_Check(x)) {
                PyErr_SetString(PyExc_TypeError, "memoryview.cast(): elements of shape must be integers");
                goto fail;
            }
            if (index_arg(x, &d) < 0) goto fail;
            if (d <= 0) {
                PyErr_SetString(PyExc_ValueError, "memoryview.cast(): elements of shape must be integers > 0");
                goto fail;
            }
            if (prod > PY_SSIZE_T_MAX / d) {
                PyErr_SetString(PyExc_ValueError, "memoryview.cast(): product(shape) > SSIZE_MAX");
                goto fail;
            }
            prod *= d;
            r->view.shape[i] = d;
        }
        if (prod * itemsize != v->len) {
            PyErr_SetString(PyExc_TypeError, "memoryview: product(shape) * itemsize != buffer size");
            goto fail;
        }
        PyBuffer_FillContiguousStrides(nd, r->view.shape, r->view.strides, itemsize, 'C');
    }
    init_len(&r->view);
    init_flags(r);
    return (PyObject *)r;
fail:
    Py_DECREF(r);
    return NULL;
}

/* mv_readonly_view(mv) */
static PyObject *
prim_mv_toreadonly(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    SELF(mv);
    PyMemoryViewObject *r = add_view(MB(mv), &mv->view, mv->view.ndim);
    if (r != NULL) {
        r->view.readonly = 1;
    }
    return (PyObject *)r;
}

/* mv_eq(a, b): True, False, or NotImplemented when b has no buffer. */
static PyObject *
prim_mv_eq(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (check_mv(args[0]) < 0) {
        return NULL;
    }
    if (args[0] == args[1]) {
        Py_RETURN_TRUE;
    }
    if (MV(args[0])->flags & _Py_MEMORYVIEW_RELEASED) {
        return bool_of(PyMemoryView_Check(args[1]) && (MV(args[1])->flags & _Py_MEMORYVIEW_RELEASED));
    }
    if (PyMemoryView_Check(args[1]) && released(args[1])) {
        PyErr_Clear();
        Py_RETURN_FALSE;
    }
    if (!PyObject_CheckBuffer(args[1])) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    Py_buffer a = MV(args[0])->view, b;
    if (PyObject_GetBuffer(args[1], &b, PyBUF_FULL_RO) < 0) {
        PyErr_Clear();
        Py_RETURN_NOTIMPLEMENTED;
    }
    int eq = a.ndim == b.ndim;
    for (int i = 0; eq && i < a.ndim; i++) {
        eq = a.shape[i] == (b.shape ? b.shape[i] : b.len / b.itemsize);
    }
    char ca, cb;
    const char *bf = b.format ? b.format : "B";
    if (eq && (fmt_char(a.format, &ca) < 0 || fmt_char(bf, &cb) < 0)) {
        eq = strcmp(a.format, bf) == 0 && a.len == b.len && a.ndim <= 1
             && (a.ndim == 0 || a.strides[0] == a.itemsize) && (b.ndim == 0 || !b.strides || b.strides[0] == b.itemsize)
             && (a.len == 0 || memcmp(a.buf, b.buf, a.len) == 0);
    }
    else if (eq && a.ndim == 1) {
        for (Py_ssize_t i = 0; eq && i < a.shape[0]; i++) {
            PyObject *x = unpack_item((char *)a.buf + i * a.strides[0], ca);
            PyObject *y = unpack_item((char *)b.buf + i * (b.strides ? b.strides[0] : b.itemsize), cb);
            int r = x && y ? lu_eq(x, y) : -1;
            Py_XDECREF(x);
            Py_XDECREF(y);
            if (r < 0) {
                PyBuffer_Release(&b);
                return NULL;
            }
            eq = r;
        }
    }
    else if (eq && a.ndim > 1) {
        eq = a.len == b.len && memcmp(a.buf, b.buf, a.len) == 0;
    }
    else if (eq && a.ndim == 0) {
        PyObject *x = unpack_item(a.buf, ca), *y = unpack_item(b.buf, cb);
        int r = x && y ? lu_eq(x, y) : -1;
        Py_XDECREF(x);
        Py_XDECREF(y);
        eq = r;
    }
    PyBuffer_Release(&b);
    return eq < 0 ? NULL : bool_of(eq);
}

/* mv_hash(mv) */
static PyObject *
prim_mv_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    SELF(mv);
    if (mv->hash != -1) {
        return PyLong_FromLongLong(mv->hash);
    }
    if (!mv->view.readonly) {
        PyErr_SetString(PyExc_ValueError, "cannot hash writable memoryview object");
        return NULL;
    }
    const char *f = mv->view.format;
    if (f[0] == '@') f++;
    if (!(f[1] == '\0' && (f[0] == 'B' || f[0] == 'b' || f[0] == 'c'))) {
        PyErr_SetString(PyExc_ValueError, "memoryview: hashing is restricted to formats 'B', 'b' or 'c'");
        return NULL;
    }
    PyObject *bytes = tobytes(mv, 'C');
    if (bytes == NULL) {
        return NULL;
    }
    mv->hash = Py_HashBuffer(PyBytes_AS_STRING(bytes), PyBytes_GET_SIZE(bytes));
    Py_DECREF(bytes);
    return PyLong_FromLongLong(mv->hash);
}

/* The buffer protocol of memoryview (memory_getbuf). */
static int
memory_getbuf(PyObject *self, Py_buffer *view, int flags)
{
    PyMemoryViewObject *mv = MV(self);
    Py_buffer *base = &mv->view;
    if (released(self)) {
        return -1;
    }
    if ((flags & PyBUF_WRITABLE) == PyBUF_WRITABLE && base->readonly) {
        PyErr_SetString(PyExc_BufferError, "memoryview: underlying buffer is not writable");
        return -1;
    }
    if (!(flags & PyBUF_FORMAT)) {
        /* the data is read as unsigned bytes */
    }
    /* start with complete information; obj is taken only on success, so a
       failed getbuffer leaves a view PyBuffer_Release can safely ignore */
    *view = *base;
    view->obj = NULL;
    if (!(flags & PyBUF_FORMAT)) {
        view->format = NULL;
    }
    if ((flags & PyBUF_C_CONTIGUOUS) == PyBUF_C_CONTIGUOUS && !(mv->flags & _Py_MEMORYVIEW_C)) {
        PyErr_SetString(PyExc_BufferError, "memoryview: underlying buffer is not C-contiguous");
        return -1;
    }
    if ((flags & PyBUF_F_CONTIGUOUS) == PyBUF_F_CONTIGUOUS && !(mv->flags & _Py_MEMORYVIEW_FORTRAN)) {
        PyErr_SetString(PyExc_BufferError, "memoryview: underlying buffer is not Fortran contiguous");
        return -1;
    }
    if ((flags & PyBUF_ANY_CONTIGUOUS) == PyBUF_ANY_CONTIGUOUS
        && !(mv->flags & (_Py_MEMORYVIEW_C | _Py_MEMORYVIEW_FORTRAN))) {
        PyErr_SetString(PyExc_BufferError, "memoryview: underlying buffer is not contiguous");
        return -1;
    }
    if (!(flags & PyBUF_INDIRECT) && (mv->flags & _Py_MEMORYVIEW_PIL)) {
        PyErr_SetString(PyExc_BufferError, "memoryview: underlying buffer requires suboffsets");
        return -1;
    }
    if (!(flags & PyBUF_STRIDES)) {
        if (!(mv->flags & _Py_MEMORYVIEW_C)) {
            PyErr_SetString(PyExc_BufferError, "memoryview: underlying buffer is not C-contiguous");
            return -1;
        }
        view->strides = NULL;
    }
    if (!(flags & PyBUF_ND)) {
        if (view->format != NULL) {
            PyErr_SetString(PyExc_BufferError,
                            "memoryview: cannot cast to unsigned bytes if the format flag is present");
            return -1;
        }
        view->ndim = 1;
        view->shape = NULL;
    }
    view->obj = Py_NewRef(self);
    __atomic_add_fetch(&mv->exports, 1, __ATOMIC_ACQ_REL);
    return 0;
}

static void
memory_releasebuf(PyObject *self, Py_buffer *view)
{
    __atomic_sub_fetch(&MV(self)->exports, 1, __ATOMIC_ACQ_REL);
}

static PyBufferProcs memory_as_buffer = { memory_getbuf, memory_releasebuf };

void
lu_memoryview_init(void)
{
    PyMemoryView_Type.tp_basicsize = offsetof(PyMemoryViewObject, ob_array);
    PyMemoryView_Type.tp_itemsize = sizeof(Py_ssize_t);
    PyMemoryView_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyMemoryView_Type.tp_dealloc = memory_dealloc;
    PyMemoryView_Type.tp_traverse = memory_traverse;
    PyMemoryView_Type.tp_clear = memory_clear;
    managed_buffer_type.tp_dealloc = mbuf_dealloc;
    managed_buffer_type.tp_traverse = mbuf_traverse;
    managed_buffer_type.tp_clear = mbuf_clear;
    PyMemoryView_Type.tp_as_buffer = &memory_as_buffer;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_mv_methods[] = {
    FAST(mv_new), FAST(mv_release), FAST(mv_released), FAST(mv_attr), FAST(mv_len),
    FAST(mv_tobytes), FAST(mv_tolist), FAST(mv_item), FAST(mv_set_item), FAST(mv_slice),
    FAST(mv_set_slice), FAST(mv_cast), FAST(mv_toreadonly), FAST(mv_eq), FAST(mv_hash),
    { NULL, NULL, 0, NULL },
};
