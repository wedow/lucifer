/* Layout constructors for the scalar and sequence types the marshal reader
   and the interpreter allocate (Objects/{long,float,complex,bytes,tuple,
   list,slice}object.c, their layout halves only), the static singletons,
   and the layout-level hash and equality used before the kernel exists.
   Python-level behaviour of these types is kernel Python (DESIGN 4.4). */

#include "lucifer.h"
#include "prim/bytes.h"

#include <stdarg.h>
#include <string.h>

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
    if (Py_TYPE(op)->tp_flags & Py_TPFLAGS_HAVE_GC) {
        lu_gc_record_deallocation(lu_current_entry());
    }
    op->ob_gc_bits &= ~LU_GC_TRACKED;
    /* A heap subclass's instances start at the preheader. */
    size_t presize = (Py_TYPE(op)->tp_flags & Py_TPFLAGS_PREHEADER) ? 2 * sizeof(PyObject *) : 0;
    PyObject_Free((char *)op - presize);
}

int
lu_fields_traverse(PyObject *op, Py_ssize_t first, Py_ssize_t end, visitproc visit, void *arg)
{
    for (Py_ssize_t off = first; off < end; off += sizeof(PyObject *)) {
        Py_VISIT(*(PyObject **)((char *)op + off));
    }
    return 0;
}

int
lu_fields_clear(PyObject *op, Py_ssize_t first, Py_ssize_t end)
{
    for (Py_ssize_t off = first; off < end; off += sizeof(PyObject *)) {
        Py_CLEAR(*(PyObject **)((char *)op + off));
    }
    return 0;
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

/* Floats and one-digit ints come and go in arithmetic: up to FREE_MAX
   freed ones of each per carrier wait for reuse, linked through the word
   after the header. A lock-free reader of a stale pointer finds the same
   type's header there, as it would in a reused mimalloc block. */
#define FREE_MAX 100

static PyObject *
freelist_pop(struct lu_freelist *fl)
{
    PyObject *op = fl->head;
    if (op != NULL) {
        fl->head = LU_FREE_NEXT(op);
        fl->n--;
        _Py_NewReference(op);
    }
    return op;
}

static int
freelist_push(struct lu_freelist *fl, PyObject *op)
{
    if (fl->n >= FREE_MAX) {
        return 0;
    }
    LU_FREE_NEXT(op) = fl->head;
    fl->head = op;
    fl->n++;
    return 1;
}

static void
freelist_clear(struct lu_freelist *fl)
{
    while (fl->head != NULL) {
        PyObject *op = fl->head;
        fl->head = LU_FREE_NEXT(op);
        lu_object_free(op);
    }
    fl->n = 0;
}

void
lu_freelists_clear(struct lu_entry *e)
{
    freelist_clear(&e->free_floats);
    freelist_clear(&e->free_ints);
}

static void
set_tag(PyLongObject *v, int sign, Py_ssize_t ndigits)
{
    /* TAG_FROM_SIGN_AND_SIZE: 0 positive, 1 zero, 2 negative. */
    v->long_value.lv_tag = ((uintptr_t)ndigits << _PyLong_NON_SIZE_BITS) | (uintptr_t)(1 - sign);
}

/* long_alloc: an int with ndigits digits, positive sign (zero when ndigits
   is 0) and a zeroed first digit (CPython's long_alloc). */
PyLongObject *
lu_long_alloc(Py_ssize_t ndigits)
{
    PyLongObject *v = ndigits <= 1 ? (PyLongObject *)freelist_pop(&lu_current_entry()->free_ints) : NULL;
    if (v != NULL) {
        v->long_value.ob_digit[0] = 0;
    }
    else {
        v = (PyLongObject *)lu_object_new(&PyLong_Type, LONG_HEADER + (ndigits > 0 ? ndigits : 1) * sizeof(digit));
    }
    if (v != NULL) {
        set_tag(v, ndigits != 0, ndigits);
    }
    return v;
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
    PyLongObject *v = lu_long_alloc(ndigits);
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

Py_ssize_t
PyLong_AsSsize_t(PyObject *op)
{
    long value;
    if (op == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (!PyLong_Check(op)) {
        PyErr_SetString(PyExc_TypeError, "an integer is required");
        return -1;
    }
    if (lu_long_as_long(op, &value) < 0) {
        PyErr_Clear();
        PyErr_SetString(PyExc_OverflowError, "Python int too large to convert to C ssize_t");
        return -1;
    }
    return value;
}

PyObject *
PyLong_FromSsize_t(Py_ssize_t value)
{
    return PyLong_FromLongLong(value);
}

/* An exact int with the digits of src, a subclass instance included. */
PyObject *
_PyLong_Copy(PyLongObject *src)
{
    Py_ssize_t n = (Py_ssize_t)(src->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    return lu_long_from_digits((src->long_value.lv_tag & _PyLong_SIGN_MASK) == 2, n,
                               src->long_value.ob_digit);
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
    PyObject *op = freelist_pop(&lu_current_entry()->free_floats);
    if (op == NULL && (op = lu_object_new(&PyFloat_Type, sizeof(PyFloatObject))) == NULL) {
        return NULL;
    }
    ((PyFloatObject *)op)->ob_fval = value;
    return op;
}

static void
float_dealloc(PyObject *op)
{
    if (!PyFloat_CheckExact(op) || !freelist_push(&lu_current_entry()->free_floats, op)) {
        lu_object_free(op);
    }
}

static void
long_dealloc(PyObject *op)
{
    if (!PyLong_CheckExact(op) || !_PyLong_IsCompact((PyLongObject *)op)
        || !freelist_push(&lu_current_entry()->free_ints, op)) {
        lu_object_free(op);
    }
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
    if ((size_t)size > (size_t)PY_SSIZE_T_MAX - (BYTES_HEADER + 1)) {
        PyErr_SetString(PyExc_OverflowError, "byte string is too large");
        return NULL;
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
    if (size > (PY_SSIZE_T_MAX - sizeof(PyTupleObject) - sizeof(PyObject *)) / sizeof(PyObject *)) {
        return PyErr_NoMemory();
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
    for (Py_ssize_t i = 0; i < size; i++) {
        op->ob_item[i] = NULL;
    }
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
    int track = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = va_arg(ap, PyObject *);
        track |= PyType_IS_GC(Py_TYPE(item));
        PyTuple_SET_ITEM(t, i, Py_NewRef(item));
    }
    va_end(ap);
    if (!track) {
        t->ob_gc_bits &= ~LU_GC_TRACKED;    /* all items atomic: CPython's PyTuple_Pack */
    }
    return t;
}

/* _PyTuple_Resize: only a tuple this thread alone holds may move (its slots
   elsewhere would dangle); the empty tuple, never. Items are kept, new
   slots arrive NULL, a shrinking tail loses its references. */
int
_PyTuple_Resize(PyObject **pv, Py_ssize_t newsize)
{
    PyTupleObject *v = (PyTupleObject *)*pv;
    PyObject *ob = *pv;
    int unique = v != NULL && Py_IS_TYPE(v, &PyTuple_Type)
        && _Py_IsOwnedByCurrentThread(ob) && ob->ob_ref_local == 1
        && _Py_atomic_load_ssize_relaxed(&ob->ob_ref_shared) == 0;
    if (v == NULL || !Py_IS_TYPE(v, &PyTuple_Type) || newsize < 0
        || (Py_SIZE(v) != 0 && !unique)) {
        Py_XDECREF(*pv);
        *pv = NULL;
        PyErr_BadInternalCall();
        return -1;
    }
    Py_ssize_t oldsize = Py_SIZE(v);
    if (oldsize == newsize) {
        return 0;
    }
    if (newsize == 0) {
        Py_DECREF(v);
        *pv = (PyObject *)&_PyRuntime.singletons.empty_tuple;
        return 0;
    }
    if (oldsize == 0) {
        *pv = PyTuple_New(newsize);
        return *pv != NULL ? 0 : -1;
    }
    if (newsize > (PY_SSIZE_T_MAX - sizeof(PyTupleObject) - sizeof(PyObject *)) / sizeof(PyObject *)) {
        PyErr_NoMemory();
        Py_DECREF(v);           /* the caller's stolen reference, as every failure takes it */
        *pv = NULL;
        return -1;
    }
    PyTupleObject *resized = (PyTupleObject *)lu_object_new(&PyTuple_Type,
        offsetof(PyTupleObject, ob_item) + newsize * sizeof(PyObject *));
    if (resized == NULL) {
        return -1;
    }
    Py_SET_SIZE(resized, newsize);
    resized->ob_hash = -1;
    for (Py_ssize_t i = 0; i < newsize; i++) {
        resized->ob_item[i] = i < oldsize ? v->ob_item[i] : NULL;
    }
    for (Py_ssize_t i = newsize; i < oldsize; i++) {
        Py_XDECREF(v->ob_item[i]);
    }
    lu_object_free((PyObject *)v);   /* unique: its single reference moves to *pv */
    *pv = (PyObject *)resized;
    return 0;
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

static int
tuple_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyTupleObject *t = (PyTupleObject *)op;
    for (Py_ssize_t i = Py_SIZE(t); --i >= 0; ) {
        Py_VISIT(t->ob_item[i]);
    }
    return 0;
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
        op->ob_item = lu_list_array_new(size);
        if (op->ob_item == NULL) {
            Py_DECREF(op);
            return PyErr_NoMemory();
        }
    }
    Py_SET_SIZE(op, size);
    op->allocated = size;
    return (PyObject *)op;
}

static void
list_dealloc(PyObject *op)
{
    PyListObject *l = (PyListObject *)op;
    for (Py_ssize_t i = Py_SIZE(l); --i >= 0; ) {
        Py_XDECREF(l->ob_item[i]);
    }
    lu_list_array_free(l->ob_item);
    lu_object_free(op);
}

static int
list_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyListObject *l = (PyListObject *)op;
    for (Py_ssize_t i = Py_SIZE(l); --i >= 0; ) {
        Py_VISIT(l->ob_item[i]);
    }
    return 0;
}

/* _list_clear: the items go once the list is empty. */
static int
list_clear(PyObject *op)
{
    PyListObject *l = (PyListObject *)op;
    PyObject **items = l->ob_item;
    if (items != NULL) {
        Py_ssize_t i = Py_SIZE(l);
        Py_SET_SIZE(l, 0);
        _Py_atomic_store_ptr_release(&l->ob_item, NULL);
        l->allocated = 0;
        while (--i >= 0) {
            Py_XDECREF(items[i]);
        }
        lu_list_array_retire(items);
    }
    return 0;
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

static int
slice_traverse(PyObject *op, visitproc visit, void *arg)
{
    PySliceObject *s = (PySliceObject *)op;
    Py_VISIT(s->start);
    Py_VISIT(s->stop);
    Py_VISIT(s->step);
    return 0;
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

/* Equality by value (long_richcompare): the small ints carry the
   immortality bit in lv_tag, so equal ints may differ in the tag (1 == True). */
static int
long_eq(PyLongObject *a, PyLongObject *b)
{
    if (_PyLong_IsCompact(a) && _PyLong_IsCompact(b)) {
        return _PyLong_CompactValue(a) == _PyLong_CompactValue(b);
    }
    if (_PyLong_IsCompact(a) || _PyLong_IsCompact(b)) {
        return 0;       /* a non-compact int has at least two digits */
    }
    if ((a->long_value.lv_tag & _PyLong_SIGN_MASK) != (b->long_value.lv_tag & _PyLong_SIGN_MASK)) {
        return 0;
    }
    Py_ssize_t na = (Py_ssize_t)(a->long_value.lv_tag >> _PyLong_NON_SIZE_BITS);
    if (na != (Py_ssize_t)(b->long_value.lv_tag >> _PyLong_NON_SIZE_BITS)) {
        return 0;
    }
    return memcmp(a->long_value.ob_digit, b->long_value.ob_digit, na * sizeof(digit)) == 0;
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
    set_layout(&PyLong_Type, LONG_HEADER, sizeof(digit), 0, long_dealloc);
    set_layout(&PyBool_Type, LONG_HEADER, sizeof(digit), 0, NULL);
    set_layout(&PyFloat_Type, sizeof(PyFloatObject), 0, 0, float_dealloc);
    set_layout(&PyComplex_Type, sizeof(PyComplexObject), 0, 0, plain_dealloc);
    set_layout(&PyBytes_Type, BYTES_HEADER + 1, 1, 0, bytes_dealloc);
    set_layout(&PyTuple_Type, offsetof(PyTupleObject, ob_item), sizeof(PyObject *),
               Py_TPFLAGS_HAVE_GC, tuple_dealloc);
    set_layout(&PyList_Type, sizeof(PyListObject), 0, Py_TPFLAGS_HAVE_GC, list_dealloc);
    set_layout(&PySlice_Type, sizeof(PySliceObject), 0, Py_TPFLAGS_HAVE_GC, slice_dealloc);
    PyTuple_Type.tp_traverse = tuple_traverse;
    PyList_Type.tp_traverse = list_traverse;
    PyList_Type.tp_clear = list_clear;
    PySlice_Type.tp_traverse = slice_traverse;
    lu_str_init();
    lu_bytes_init();
    lu_set_init();
    lu_dict_init();
    lu_dict_views_init();
    lu_set_iter_init();
    lu_code_init();
    lu_function_init();
    lu_descr_init();
    lu_storage_init();
    lu_module_init();
    lu_exc_init();
    lu_gen_init();
    lu_frame_init();
    lu_weakref_init();
    lu_capsule_init();

    struct lu_static_objects *s = &_PyRuntime.singletons;
    for (int i = 0; i < LU_NSMALLNEGINTS + LU_NSMALLPOSINTS; i++) {
        PyLongObject *v = &s->small_ints[i];
        int value = i - LU_NSMALLNEGINTS;
        immortal_header((PyObject *)v, &PyLong_Type);
        v->long_value.ob_digit[0] = (digit)(value < 0 ? -value : value);
        set_tag(v, value < 0 ? -1 : value > 0, value != 0);
        v->long_value.lv_tag |= _PyLong_IMMORTALITY_BIT_MASK;
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
    immortal_header(&s->missing, &PyBaseObject_Type);   /* a tuple may hold it: the collector reads its type */
}
