/* Layout constructors for the scalar and sequence types the marshal reader
   and the interpreter allocate (Objects/{long,float,complex,bytes,tuple,
   list,slice}object.c, their layout halves only), the static singletons,
   and the layout-level hash and equality used before the kernel exists.
   Python-level behaviour of these types is kernel Python (DESIGN 4.4). */

#include "lucifer.h"

#include <stdarg.h>
#include <string.h>

#define LU_GC_TRACKED 1         /* _PyGC_BITS_TRACKED in ob_gc_bits */

/* Every type whose objects the runtime frees natively gets its size,
   GC flag and dealloc here; the kernel installs everything else. */
static void
set_layout(PyTypeObject *tp, Py_ssize_t basicsize, Py_ssize_t itemsize,
           unsigned long flags, destructor dealloc)
{
    tp->tp_basicsize = basicsize;
    tp->tp_itemsize = itemsize;
    tp->tp_flags |= Py_TPFLAGS_DEFAULT | flags;
    tp->tp_dealloc = dealloc;
}

PyObject *
lu_object_new(PyTypeObject *tp, size_t size)
{
    int gc = (tp->tp_flags & Py_TPFLAGS_HAVE_GC) != 0;
    PyObject *op = gc ? lu_gc_alloc(tp, size, 0) : PyObject_Malloc(size);
    if (op == NULL) {
        return PyErr_NoMemory();
    }
    memset(op, 0, size);
    PyObject_Init(op, tp);
    if (gc) {
        op->ob_gc_bits |= LU_GC_TRACKED;
    }
    return op;
}

void
lu_object_free(PyObject *op)
{
    op->ob_gc_bits &= ~LU_GC_TRACKED;
    /* A heap subclass's instances start at the preheader. */
    size_t presize = (Py_TYPE(op)->tp_flags & Py_TPFLAGS_PREHEADER) ? 2 * sizeof(PyObject *) : 0;
    PyObject_Free((char *)op - presize);
}

static void
immortal_header(PyObject *op, PyTypeObject *tp)
{
    op->ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL;
    op->ob_flags = _Py_STATICALLY_ALLOCATED_FLAG;
    op->ob_type = tp;
}

/* int */

#define LONG_HEADER offsetof(PyLongObject, long_value.ob_digit)

static PyLongObject *
long_alloc(Py_ssize_t ndigits)
{
    return (PyLongObject *)lu_object_new(&PyLong_Type,
        LONG_HEADER + (ndigits > 0 ? ndigits : 1) * sizeof(digit));
}

static void
set_tag(PyLongObject *v, int sign, Py_ssize_t ndigits)
{
    /* TAG_FROM_SIGN_AND_SIZE: 0 positive, 1 zero, 2 negative. */
    v->long_value.lv_tag = ((uintptr_t)ndigits << _PyLong_NON_SIZE_BITS) | (uintptr_t)(1 - sign);
}

PyObject *
lu_small_int(int value)
{
    return (PyObject *)&_PyRuntime.singletons.small_ints[LU_NSMALLNEGINTS + value];
}

PyObject *
PyLong_FromLongLong(long long value)
{
    if (value >= -LU_NSMALLNEGINTS && value < LU_NSMALLPOSINTS) {
        return lu_small_int((int)value);
    }
    unsigned long long abs = value < 0 ? 0ULL - (unsigned long long)value
                                       : (unsigned long long)value;
    digit digits[3];
    Py_ssize_t n = 0;
    for (; abs != 0; abs >>= PyLong_SHIFT) {
        digits[n++] = (digit)(abs & PyLong_MASK);
    }
    return lu_long_from_digits(value < 0, n, digits);
}

PyObject *
PyLong_FromLong(long value)
{
    return PyLong_FromLongLong(value);
}

/* Normalizes away leading zero digits; small values give the cached ints. */
PyObject *
lu_long_from_digits(int negative, Py_ssize_t ndigits, const digit *digits)
{
    while (ndigits > 0 && digits[ndigits - 1] == 0) {
        ndigits--;
    }
    if (ndigits <= 1) {
        long v = ndigits ? (long)digits[0] : 0;
        v = negative ? -v : v;
        if (v >= -LU_NSMALLNEGINTS && v < LU_NSMALLPOSINTS) {
            return lu_small_int((int)v);
        }
    }
    PyLongObject *v = long_alloc(ndigits);
    if (v == NULL) {
        return NULL;
    }
    memcpy(v->long_value.ob_digit, digits, ndigits * sizeof(digit));
    set_tag(v, ndigits == 0 ? 0 : negative ? -1 : 1, ndigits);
    return (PyObject *)v;
}

/* An exact int's value, or -1 with OverflowError set. */
int
lu_long_as_long(PyObject *op, long *result)
{
    PyLongObject *v = (PyLongObject *)op;
    if (_PyLong_IsCompact(v)) {
        *result = (long)_PyLong_CompactValue(v);
        return 0;
    }
    Py_ssize_t n = (Py_ssize_t)(v->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    unsigned long x = 0;
    while (--n >= 0) {
        unsigned long prev = x;
        x = (x << PyLong_SHIFT) | v->long_value.ob_digit[n];
        if ((x >> PyLong_SHIFT) != prev) {
            goto overflow;
        }
    }
    if ((v->long_value.lv_tag & _PyLong_SIGN_MASK) == 2) {
        if (x > (unsigned long)LONG_MAX + 1) {
            goto overflow;
        }
        *result = (long)(0UL - x);
        return 0;
    }
    if (x > (unsigned long)LONG_MAX) {
        goto overflow;
    }
    *result = (long)x;
    return 0;
overflow:
    lu_err_format(PyExc_OverflowError, "Python int too large to convert to C long");
    return -1;
}

static void
plain_dealloc(PyObject *op)
{
    lu_object_free(op);
}

/* float and complex */

PyObject *
PyFloat_FromDouble(double value)
{
    PyFloatObject *op = (PyFloatObject *)lu_object_new(&PyFloat_Type, sizeof(PyFloatObject));
    if (op != NULL) {
        op->ob_fval = value;
    }
    return (PyObject *)op;
}

double
PyFloat_Unpack8(const char *data, int le)
{
    unsigned char bytes[8];
    memcpy(bytes, data, 8);
    if (!le) {
        for (int i = 0; i < 4; i++) {
            unsigned char t = bytes[i];
            bytes[i] = bytes[7 - i];
            bytes[7 - i] = t;
        }
    }
    double x;
    memcpy(&x, bytes, 8);       /* x86-64 and aarch64 are little-endian IEEE */
    return x;
}

PyObject *
PyComplex_FromCComplex(Py_complex value)
{
    PyComplexObject *op = (PyComplexObject *)lu_object_new(&PyComplex_Type,
                                                           sizeof(PyComplexObject));
    if (op != NULL) {
        op->cval = value;
    }
    return (PyObject *)op;
}

/* bytes */

#define BYTES_HEADER offsetof(PyBytesObject, ob_sval)

PyObject *
PyBytes_FromStringAndSize(const char *str, Py_ssize_t size)
{
    if (size < 0) {
        lu_err_format(PyExc_SystemError, "Negative size passed to PyBytes_FromStringAndSize");
        return NULL;
    }
    if (size == 0) {
        return (PyObject *)&_PyRuntime.singletons.empty_bytes;
    }
    if (size == 1 && str != NULL) {
        return (PyObject *)&_PyRuntime.singletons.bytes_chars[(unsigned char)*str];
    }
    PyBytesObject *op = (PyBytesObject *)lu_object_new(&PyBytes_Type, BYTES_HEADER + size + 1);
    if (op == NULL) {
        return NULL;
    }
    Py_SET_SIZE(op, size);
    _Py_COMP_DIAG_PUSH
    _Py_COMP_DIAG_IGNORE_DEPR_DECLS
    op->ob_shash = -1;
    _Py_COMP_DIAG_POP
    if (str != NULL) {
        memcpy(op->ob_sval, str, size);
    }
    return (PyObject *)op;
}

static void
bytes_dealloc(PyObject *op)
{
    lu_object_free(op);
}

/* tuple */

PyObject *
PyTuple_New(Py_ssize_t size)
{
    if (size < 0) {
        lu_err_format(PyExc_SystemError, "Negative size passed to PyTuple_New");
        return NULL;
    }
    if (size == 0) {
        return (PyObject *)&_PyRuntime.singletons.empty_tuple;
    }
    PyTupleObject *op = (PyTupleObject *)lu_object_new(&PyTuple_Type,
        offsetof(PyTupleObject, ob_item) + size * sizeof(PyObject *));
    if (op == NULL) {
        return NULL;
    }
    Py_SET_SIZE(op, size);
    op->ob_hash = -1;
    return (PyObject *)op;
}

PyObject *
PyTuple_Pack(Py_ssize_t n, ...)
{
    PyObject *t = PyTuple_New(n);
    if (t == NULL) {
        return NULL;
    }
    va_list ap;
    va_start(ap, n);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyTuple_SET_ITEM(t, i, Py_NewRef(va_arg(ap, PyObject *)));
    }
    va_end(ap);
    return t;
}

static void
tuple_dealloc(PyObject *op)
{
    PyTupleObject *t = (PyTupleObject *)op;
    for (Py_ssize_t i = Py_SIZE(t); --i >= 0; ) {
        Py_XDECREF(t->ob_item[i]);
    }
    lu_object_free(op);
}

/* list */

PyObject *
PyList_New(Py_ssize_t size)
{
    if (size < 0) {
        lu_err_format(PyExc_SystemError, "Negative size passed to PyList_New");
        return NULL;
    }
    PyListObject *op = (PyListObject *)lu_object_new(&PyList_Type, sizeof(PyListObject));
    if (op == NULL) {
        return NULL;
    }
    if (size > 0) {
        /* Storage is reclaimed through QSBR once lists grow concurrently (K1h). */
        op->ob_item = PyMem_Calloc(size, sizeof(PyObject *));
        if (op->ob_item == NULL) {
            Py_DECREF(op);
            return PyErr_NoMemory();
        }
    }
    Py_SET_SIZE(op, size);
    op->allocated = size;
    return (PyObject *)op;
}

int
lu_list_append(PyObject *op, PyObject *item)
{
    PyListObject *l = (PyListObject *)op;
    Py_ssize_t n = Py_SIZE(l);
    if (n == l->allocated) {
        Py_ssize_t grown = n < 4 ? 4 : 2 * n;
        PyObject **items = PyMem_Realloc(l->ob_item, grown * sizeof(PyObject *));
        if (items == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        l->ob_item = items;
        l->allocated = grown;
    }
    l->ob_item[n] = Py_NewRef(item);
    Py_SET_SIZE(l, n + 1);
    return 0;
}

static void
list_dealloc(PyObject *op)
{
    PyListObject *l = (PyListObject *)op;
    for (Py_ssize_t i = Py_SIZE(l); --i >= 0; ) {
        Py_XDECREF(l->ob_item[i]);
    }
    PyMem_Free(l->ob_item);
    lu_object_free(op);
}

/* slice */

PyObject *
PySlice_New(PyObject *start, PyObject *stop, PyObject *step)
{
    PySliceObject *op = (PySliceObject *)lu_object_new(&PySlice_Type, sizeof(PySliceObject));
    if (op == NULL) {
        return NULL;
    }
    op->start = Py_NewRef(start != NULL ? start : Py_None);
    op->stop = Py_NewRef(stop != NULL ? stop : Py_None);
    op->step = Py_NewRef(step != NULL ? step : Py_None);
    return (PyObject *)op;
}

static void
slice_dealloc(PyObject *op)
{
    PySliceObject *s = (PySliceObject *)op;
    Py_DECREF(s->start);
    Py_DECREF(s->stop);
    Py_DECREF(s->step);
    lu_object_free(op);
}

/* Layout hash and equality. */

Py_hash_t
lu_hash(PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(v);
    if (tp == &PyUnicode_Type) {
        return lu_str_hash(v);
    }
    if (tp == &PyLong_Type || tp == &PyBool_Type) {
        return lu_long_hash((PyLongObject *)v);
    }
    if (tp == &PyBytes_Type) {
        return Py_HashBuffer(((PyBytesObject *)v)->ob_sval, Py_SIZE(v));
    }
    if (tp == &PyFloat_Type) {
        return _Py_HashDouble(v, ((PyFloatObject *)v)->ob_fval);
    }
    if (tp == &PyComplex_Type) {
        /* complex_hash */
        Py_complex c = ((PyComplexObject *)v)->cval;
        Py_uhash_t h = (Py_uhash_t)_Py_HashDouble(v, c.real)
                     + PyHASH_IMAG * (Py_uhash_t)_Py_HashDouble(v, c.imag);
        return h == (Py_uhash_t)-1 ? -2 : (Py_hash_t)h;
    }
    if (tp == &PyTuple_Type) {
        PyTupleObject *t = (PyTupleObject *)v;
        if (t->ob_hash == -1) {
            t->ob_hash = lu_xxhash_items(t->ob_item, Py_SIZE(t), 1);
        }
        return t->ob_hash;
    }
    if (tp == &PySlice_Type) {
        PySliceObject *s = (PySliceObject *)v;
        PyObject *items[3] = { s->start, s->stop, s->step };
        return lu_xxhash_items(items, 3, 0);
    }
    if (tp == &PyFrozenSet_Type) {
        return lu_frozenset_hash(v);
    }
    if (v == Py_None) {
        return 0xFCA86420;
    }
    if (tp == &PyCode_Type || tp == &PyEllipsis_Type || PyType_Check(v)) {
        return Py_HashPointer(v);
    }
    lu_err_format(PyExc_TypeError, "unhashable type: '%s'", tp->tp_name);
    return -1;
}

static int
is_number(PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(v);
    return tp == &PyLong_Type || tp == &PyBool_Type || tp == &PyFloat_Type
        || tp == &PyComplex_Type;
}

/* A number as a complex, exactly for floats and ints of up to 53 bits; wider
   ints compare by value only against other ints. */
static int
number_value(PyObject *v, Py_complex *c)
{
    if (Py_TYPE(v) == &PyFloat_Type) {
        *c = (Py_complex){ ((PyFloatObject *)v)->ob_fval, 0.0 };
        return 1;
    }
    if (Py_TYPE(v) == &PyComplex_Type) {
        *c = ((PyComplexObject *)v)->cval;
        return 1;
    }
    long x;
    if (!_PyLong_IsCompact((PyLongObject *)v) || lu_long_as_long(v, &x) < 0
        || x > (1L << 53) || x < -(1L << 53)) {
        PyErr_Clear();
        return 0;
    }
    *c = (Py_complex){ (double)x, 0.0 };
    return 1;
}

static int
long_eq(PyLongObject *a, PyLongObject *b)
{
    if (a->long_value.lv_tag != b->long_value.lv_tag) {
        return 0;
    }
    Py_ssize_t n = (Py_ssize_t)(a->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    return memcmp(a->long_value.ob_digit, b->long_value.ob_digit, n * sizeof(digit)) == 0;
}

/* 1 if equal, 0 if not, -1 with an error set. */
int
lu_eq(PyObject *a, PyObject *b)
{
    if (a == b) {
        return 1;
    }
    PyTypeObject *ta = Py_TYPE(a), *tb = Py_TYPE(b);
    if (is_number(a) && is_number(b)) {
        int ia = ta == &PyLong_Type || ta == &PyBool_Type;
        int ib = tb == &PyLong_Type || tb == &PyBool_Type;
        if (ia && ib) {
            return long_eq((PyLongObject *)a, (PyLongObject *)b);
        }
        Py_complex x, y;
        if (!number_value(a, &x) || !number_value(b, &y)) {
            return 0;
        }
        return x.real == y.real && x.imag == y.imag;
    }
    if (ta != tb) {
        return 0;
    }
    if (ta == &PyUnicode_Type) {
        return lu_str_eq(a, b);
    }
    if (ta == &PyBytes_Type) {
        return Py_SIZE(a) == Py_SIZE(b)
            && memcmp(((PyBytesObject *)a)->ob_sval, ((PyBytesObject *)b)->ob_sval,
                      Py_SIZE(a)) == 0;
    }
    if (ta == &PyTuple_Type) {
        if (Py_SIZE(a) != Py_SIZE(b)) {
            return 0;
        }
        for (Py_ssize_t i = 0; i < Py_SIZE(a); i++) {
            int r = lu_eq(PyTuple_GET_ITEM(a, i), PyTuple_GET_ITEM(b, i));
            if (r <= 0) {
                return r;
            }
        }
        return 1;
    }
    if (ta == &PySlice_Type) {
        PySliceObject *x = (PySliceObject *)a, *y = (PySliceObject *)b;
        int r = lu_eq(x->start, y->start);
        if (r > 0) {
            r = lu_eq(x->stop, y->stop);
        }
        return r > 0 ? lu_eq(x->step, y->step) : r;
    }
    if (ta == &PyFrozenSet_Type) {
        PySetObject *x = (PySetObject *)a, *y = (PySetObject *)b;
        if (x->used != y->used) {
            return 0;
        }
        for (Py_ssize_t i = 0; i <= x->mask; i++) {
            PyObject *key = x->table[i].key;
            if (key == NULL) {
                continue;
            }
            int found = 0;
            for (Py_ssize_t j = 0; j <= y->mask && !found; j++) {
                if (y->table[j].key != NULL && y->table[j].hash == x->table[i].hash) {
                    found = lu_eq(key, y->table[j].key);
                    if (found < 0) {
                        return -1;
                    }
                }
            }
            if (!found) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

/* Startup: sizes and deallocs first, then the singletons. */
void
lu_layouts_init(void)
{
    set_layout(&PyLong_Type, LONG_HEADER, sizeof(digit), 0, plain_dealloc);
    set_layout(&PyBool_Type, LONG_HEADER, sizeof(digit), 0, NULL);
    set_layout(&PyFloat_Type, sizeof(PyFloatObject), 0, 0, plain_dealloc);
    set_layout(&PyComplex_Type, sizeof(PyComplexObject), 0, 0, plain_dealloc);
    set_layout(&PyBytes_Type, BYTES_HEADER + 1, 1, 0, bytes_dealloc);
    set_layout(&PyTuple_Type, offsetof(PyTupleObject, ob_item), sizeof(PyObject *),
               Py_TPFLAGS_HAVE_GC, tuple_dealloc);
    set_layout(&PyList_Type, sizeof(PyListObject), 0, Py_TPFLAGS_HAVE_GC, list_dealloc);
    set_layout(&PySlice_Type, sizeof(PySliceObject), 0, Py_TPFLAGS_HAVE_GC, slice_dealloc);
    lu_str_init();
    lu_set_init();
    lu_dict_init();
    lu_code_init();
    lu_function_init();
    lu_descr_init();
    lu_storage_init();
    lu_module_init();
    lu_exc_init();

    struct lu_static_objects *s = &_PyRuntime.singletons;
    for (int i = 0; i < LU_NSMALLNEGINTS + LU_NSMALLPOSINTS; i++) {
        PyLongObject *v = &s->small_ints[i];
        int value = i - LU_NSMALLNEGINTS;
        immortal_header((PyObject *)v, &PyLong_Type);
        v->long_value.ob_digit[0] = (digit)(value < 0 ? -value : value);
        set_tag(v, value < 0 ? -1 : value > 0, value != 0);
    }
    for (int i = 0; i < 256; i++) {
        PyBytesObject *b = &s->bytes_chars[i];
        immortal_header((PyObject *)b, &PyBytes_Type);
        Py_SET_SIZE(b, 1);
        b->ob_sval[0] = (char)i;
    }
    immortal_header((PyObject *)&s->empty_bytes, &PyBytes_Type);
    _Py_COMP_DIAG_PUSH
    _Py_COMP_DIAG_IGNORE_DEPR_DECLS
    for (int i = 0; i < 256; i++) {
        s->bytes_chars[i].ob_shash = -1;
    }
    s->empty_bytes.ob_shash = -1;
    _Py_COMP_DIAG_POP
    immortal_header((PyObject *)&s->empty_tuple, &PyTuple_Type);
    s->empty_tuple.ob_hash = -1;
}
