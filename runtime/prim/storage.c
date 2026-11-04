/* Storage primitives under the kernel's tuple, list, range and slice
   (DESIGN 4.4, storage family): C over the cp314t PyTupleObject and
   PyListObject layouts, which extensions read directly, plus the layouts of
   the iterators and of range. Python semantics (indices, errors, protocols,
   sorting) are lib/kernel/{tuple,list,range,slice,iter}.py.

   Every primitive leaves its object traversable and structurally valid. Each
   list mutator takes the list's critical section, as CPython's @critical_section
   methods do, so extension code and kernel code exclude each other. A
   replaced ob_item array goes to lu_qsbr_retire, which frees it only when no
   lock-free reader can still hold it. */

#include "prim/prim.h"

#include <string.h>

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__ + 5, (n)); \
        return NULL; } } while (0)
#define SEQ(i) do { if (!PyTuple_Check(args[i]) && !PyList_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() needs a tuple or list", __func__ + 5); \
        return NULL; } } while (0)
#define LIST(i) do { if (!PyList_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() needs a list", __func__ + 5); return NULL; } } while (0)
#define SSIZE(i, var) Py_ssize_t var; do { long v_; if (!PyLong_CheckExact(args[i]) \
        || lu_long_as_long(args[i], &v_) < 0) { \
        if (!PyErr_Occurred()) lu_err_format(PyExc_TypeError, "%s() needs an int", __func__ + 5); \
        return NULL; } var = v_; } while (0)

static PyObject **
seq_items(PyObject *seq)
{
    return PyTuple_Check(seq) ? ((PyTupleObject *)seq)->ob_item : ((PyListObject *)seq)->ob_item;
}

/* A new strong reference to each item of a tuple or list, in a PyMem
   array the caller frees; the count in *n. */
static PyObject **
snapshot(PyObject *seq, Py_ssize_t *n)
{
    PyObject **copy = NULL;
    if (PyList_Check(seq)) {
        Py_BEGIN_CRITICAL_SECTION(seq);
        *n = Py_SIZE(seq);
        copy = PyMem_Malloc((*n > 0 ? *n : 1) * sizeof(PyObject *));
        for (Py_ssize_t i = 0; copy != NULL && i < *n; i++) {
            copy[i] = Py_NewRef(((PyListObject *)seq)->ob_item[i]);
        }
        Py_END_CRITICAL_SECTION();
    }
    else {
        *n = Py_SIZE(seq);
        copy = PyMem_Malloc((*n > 0 ? *n : 1) * sizeof(PyObject *));
        for (Py_ssize_t i = 0; copy != NULL && i < *n; i++) {
            copy[i] = Py_NewRef(((PyTupleObject *)seq)->ob_item[i]);
        }
    }
    if (copy == NULL) {
        PyErr_NoMemory();
    }
    return copy;
}

static void
release(PyObject **items, Py_ssize_t n)
{
    for (Py_ssize_t i = 0; i < n; i++) {
        Py_DECREF(items[i]);
    }
    PyMem_Free(items);
}

/* An exact tuple (copied; the empty tuple is shared) or list (adopted) of
   the n items in a PyMem array. The items and the array are consumed, also
   on failure. */
static PyObject *
seq_take(PyObject **items, Py_ssize_t n, int as_tuple)
{
    PyObject *r = as_tuple ? PyTuple_New(n) : PyList_New(0);
    if (r == NULL) {
        release(items, n);
        return NULL;
    }
    if (as_tuple) {
        if (n > 0) {
            memcpy(((PyTupleObject *)r)->ob_item, items, n * sizeof(PyObject *));
        }
        PyMem_Free(items);
    }
    else if (n > 0) {
        PyMem_Free(((PyListObject *)r)->ob_item);
        ((PyListObject *)r)->ob_item = items;
        ((PyListObject *)r)->allocated = n;
        Py_SET_SIZE(r, n);
    }
    else {
        PyMem_Free(items);
    }
    return r;
}

/* list_resize (Objects/listobject.c): the array is replaced, never grown in
   place, so a lock-free reader sees the old one or the new, whole. Items
   past the old size are left for the caller to fill. */
static int
list_resize(PyListObject *self, Py_ssize_t newsize)
{
    Py_ssize_t allocated = self->allocated;
    if (allocated >= newsize && newsize >= (allocated >> 1)) {
        Py_SET_SIZE(self, newsize);
        return 0;
    }
    size_t new_allocated = ((size_t)newsize + (newsize >> 3) + 6) & ~(size_t)3;
    if (newsize - Py_SIZE(self) > (Py_ssize_t)(new_allocated - newsize)) {
        new_allocated = ((size_t)newsize + 3) & ~(size_t)3;
    }
    if (newsize == 0) {
        new_allocated = 0;
    }
    PyObject **items = NULL;
    if (new_allocated > 0) {
        if (new_allocated > (size_t)PY_SSIZE_T_MAX / sizeof(PyObject *)
            || (items = PyMem_Malloc(new_allocated * sizeof(PyObject *))) == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        Py_ssize_t keep = Py_SIZE(self) < newsize ? Py_SIZE(self) : newsize;
        if (keep > 0) {
            memcpy(items, self->ob_item, keep * sizeof(PyObject *));
        }
    }
    PyObject **old = self->ob_item;
    _Py_atomic_store_ptr_release(&self->ob_item, items);
    Py_SET_SIZE(self, newsize);
    self->allocated = (Py_ssize_t)new_allocated;
    if (old != NULL) {
        lu_qsbr_retire(old);
    }
    return 0;
}

/* seq_get(seq, i): item i of a tuple or list, 0 <= i < len. */
static PyObject *
prim_seq_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SEQ(0); SSIZE(1, i);
    PyObject *v = NULL;
    if (PyList_Check(args[0])) {
        Py_BEGIN_CRITICAL_SECTION(args[0]);
        if (0 <= i && i < Py_SIZE(args[0])) {
            v = Py_NewRef(((PyListObject *)args[0])->ob_item[i]);
        }
        Py_END_CRITICAL_SECTION();
    }
    else if (0 <= i && i < Py_SIZE(args[0])) {
        v = Py_NewRef(((PyTupleObject *)args[0])->ob_item[i]);
    }
    if (v == NULL && !PyErr_Occurred()) {
        lu_err_format(PyExc_IndexError, "index out of range");
    }
    return v;
}

/* seq_slice(seq, start, step, count, as_tuple): the count items from start
   by step, as an exact tuple or list. The caller took the bounds from a
   length that another thread may have shrunk since: positions past the end
   are left out. */
static PyObject *
prim_seq_slice(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(5); SEQ(0); SSIZE(1, start); SSIZE(2, step); SSIZE(3, count);
    PyObject **items = PyMem_Malloc((count > 0 ? count : 1) * sizeof(PyObject *));
    if (items == NULL) {
        return PyErr_NoMemory();
    }
    PyObject *seq = args[0];
    Py_ssize_t n = 0;
    Py_BEGIN_CRITICAL_SECTION(seq);
    PyObject **src = seq_items(seq);
    for (Py_ssize_t k = 0; k < count; k++) {
        Py_ssize_t at = start + k * step;
        if (at < Py_SIZE(seq)) {
            items[n++] = Py_NewRef(src[at]);
        }
    }
    Py_END_CRITICAL_SECTION();
    return seq_take(items, n, args[4] == Py_True);
}

/* seq_concat(a, b, as_tuple) */
static PyObject *
prim_seq_concat(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); SEQ(0); SEQ(1);
    Py_ssize_t na, nb;
    PyObject **a = snapshot(args[0], &na);
    PyObject **b = a != NULL ? snapshot(args[1], &nb) : NULL;
    if (b == NULL) {
        if (a != NULL) {
            release(a, na);
        }
        return NULL;
    }
    PyObject **all = PyMem_Realloc(a, (na + nb > 0 ? na + nb : 1) * sizeof(PyObject *));
    if (all == NULL) {
        release(a, na);
        release(b, nb);
        return PyErr_NoMemory();
    }
    memcpy(all + na, b, nb * sizeof(PyObject *));
    PyMem_Free(b);
    return seq_take(all, na + nb, args[2] == Py_True);
}

/* seq_repeat(seq, n, as_tuple): n >= 0 copies. */
static PyObject *
prim_seq_repeat(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); SEQ(0); SSIZE(1, n);
    Py_ssize_t size;
    PyObject **src = snapshot(args[0], &size);
    if (src == NULL) {
        return NULL;
    }
    if (size > 0 && n > PY_SSIZE_T_MAX / (Py_ssize_t)sizeof(PyObject *) / size) {
        release(src, size);
        return PyErr_NoMemory();
    }
    Py_ssize_t total = size * n;
    PyObject **all = PyMem_Malloc((total > 0 ? total : 1) * sizeof(PyObject *));
    if (all == NULL) {
        release(src, size);
        return PyErr_NoMemory();
    }
    for (Py_ssize_t k = 0; k < total; k++) {
        all[k] = Py_NewRef(src[k % size]);
    }
    release(src, size);
    return seq_take(all, total, args[2] == Py_True);
}

/* tuple_from(seq, cls): the items of a tuple or list as an instance of the
   tuple class cls; an exact tuple stays itself. */
static PyObject *
prim_tuple_from(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SEQ(0);
    if (!PyType_Check(args[1]) || !PyType_IsSubtype((PyTypeObject *)args[1], &PyTuple_Type)) {
        lu_err_format(PyExc_TypeError, "tuple_from() needs a tuple class");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[1];
    if (tp == &PyTuple_Type && PyTuple_CheckExact(args[0])) {
        return Py_NewRef(args[0]);
    }
    Py_ssize_t n;
    PyObject **items = snapshot(args[0], &n);
    if (items == NULL) {
        return NULL;
    }
    if (tp == &PyTuple_Type) {
        return seq_take(items, n, 1);
    }
    PyObject *t = tp->tp_alloc(tp, n);
    if (t == NULL) {
        release(items, n);
        return NULL;
    }
    memcpy(((PyTupleObject *)t)->ob_item, items, n * sizeof(PyObject *));
    ((PyTupleObject *)t)->ob_hash = -1;
    PyMem_Free(items);
    return t;
}

/* xxh_hash(hashes, mangle): the xxHash fold of tuple_hash (mangle true) or
   slice_hash (false) over the hashes of the items, ints. */
static PyObject *
prim_xxh_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SEQ(0);
    Py_ssize_t n = Py_SIZE(args[0]);
    Py_uhash_t acc = 2870177450012600261ULL;            /* XXPRIME_5 */
    for (Py_ssize_t i = 0; i < n; i++) {
        long lane;
        PyObject *h = seq_items(args[0])[i];
        if (!PyLong_CheckExact(h) || lu_long_as_long(h, &lane) < 0) {
            return NULL;
        }
        acc += (Py_uhash_t)lane * 14029467366897019727ULL;      /* XXPRIME_2 */
        acc = (acc << 31) | (acc >> 33);
        acc *= 11400714785074694791ULL;                 /* XXPRIME_1 */
    }
    if (args[1] == Py_True) {
        acc += (Py_uhash_t)n ^ (2870177450012600261ULL ^ 3527539UL);
    }
    return PyLong_FromLong(acc == (Py_uhash_t)-1 ? 1546275796 : (Py_hash_t)acc);
}

/* repr_enter(obj): whether obj is already being repr'd by this task, else
   marks it; repr_leave(obj) unmarks (Py_ReprEnter, Py_ReprLeave). */
static PyObject *
repr_stack(int create)
{
    PyThreadState *ts = PyThreadState_Get();
    if (ts->dict == NULL && (!create || (ts->dict = PyDict_New()) == NULL)) {
        return NULL;
    }
    PyObject *key = PyUnicode_InternFromString("Py_Repr");
    PyObject *list = key != NULL ? lu_ns_lookup(ts->dict, key) : NULL;
    if (list == NULL && create && key != NULL) {
        PyObject *fresh = PyList_New(0);
        if (fresh != NULL && lu_ns_insert(ts->dict, key, fresh) == 0) {
            list = fresh;
        }
        Py_XDECREF(fresh);
    }
    Py_XDECREF(key);
    return list;
}

static PyObject *
prim_repr_enter(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *list = repr_stack(1);
    if (list == NULL) {
        return PyErr_Occurred() ? NULL : PyErr_NoMemory();
    }
    for (Py_ssize_t i = Py_SIZE(list); --i >= 0; ) {
        if (((PyListObject *)list)->ob_item[i] == args[0]) {
            return Py_NewRef(Py_True);
        }
    }
    return lu_list_append(list, args[0]) < 0 ? NULL : Py_NewRef(Py_False);
}

static PyObject *
prim_repr_leave(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *list = repr_stack(0);
    for (Py_ssize_t i = list != NULL ? Py_SIZE(list) : 0; --i >= 0; ) {
        PyListObject *l = (PyListObject *)list;
        if (l->ob_item[i] == args[0]) {
            PyObject *gone = l->ob_item[i];
            memmove(l->ob_item + i, l->ob_item + i + 1, (Py_SIZE(l) - i - 1) * sizeof(PyObject *));
            Py_SET_SIZE(l, Py_SIZE(l) - 1);
            Py_DECREF(gone);
            break;
        }
    }
    return Py_NewRef(Py_None);
}

/* list */

/* list_alloc(cls): an empty list of the list class cls. */
static PyObject *
prim_list_alloc(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyList_Type)) {
        lu_err_format(PyExc_TypeError, "list_alloc() needs a list class");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[0];
    return tp == &PyList_Type ? PyList_New(0) : tp->tp_alloc(tp, 0);
}

/* list_append(list, item) */
static PyObject *
prim_list_append(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); LIST(0);
    PyListObject *l = (PyListObject *)args[0];
    int err = 0;
    Py_BEGIN_CRITICAL_SECTION(l);
    Py_ssize_t n = Py_SIZE(l);
    if (n < l->allocated) {
        l->ob_item[n] = Py_NewRef(args[1]);
        Py_SET_SIZE(l, n + 1);
    }
    else if (list_resize(l, n + 1) < 0) {
        err = -1;
    }
    else {
        l->ob_item[n] = Py_NewRef(args[1]);
    }
    Py_END_CRITICAL_SECTION();
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* list_set(list, i, item): 0 <= i < len. */
static PyObject *
prim_list_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); LIST(0); SSIZE(1, i);
    PyListObject *l = (PyListObject *)args[0];
    PyObject *old = NULL;
    Py_BEGIN_CRITICAL_SECTION(l);
    if (0 <= i && i < Py_SIZE(l)) {
        old = l->ob_item[i];
        _Py_atomic_store_ptr_release(&l->ob_item[i], Py_NewRef(args[2]));
    }
    Py_END_CRITICAL_SECTION();
    if (old == NULL) {
        lu_err_format(PyExc_IndexError, "list assignment index out of range");
        return NULL;
    }
    Py_DECREF(old);
    return Py_NewRef(Py_None);
}

/* list_ass_slice: replaces l[lo:hi] with the items of a tuple or list. Bounds
   are 0 <= lo <= hi <= len, or with `clamp` brought into range as slice
   assignment does. A result with no items clears the list, which resets its
   storage. */
static int
list_splice(PyListObject *l, Py_ssize_t lo, Py_ssize_t hi, PyObject *seq, int clamp)
{
    Py_ssize_t d;
    PyObject **fresh = snapshot(seq, &d);
    if (fresh == NULL) {
        return -1;
    }
    PyObject **recycle = NULL;
    Py_ssize_t norig = 0;
    int err = 0, cleared = 0;
    Py_BEGIN_CRITICAL_SECTION(l);
    Py_ssize_t size = Py_SIZE(l);
    if (clamp) {
        lo = lo < 0 ? 0 : lo > size ? size : lo;
        hi = hi < lo ? lo : hi > size ? size : hi;
    }
    if (lo < 0 || lo > hi || hi > size) {
        lu_err_format(PyExc_SystemError, "list_splice() range out of bounds");
        err = -1;
    }
    else if (size - (hi - lo) + d == 0) {
        /* list_clear: the old array and its items go once the list is empty;
           a list without storage is left as it is, a sort's marker included. */
        recycle = l->ob_item;
        if (recycle != NULL) {
            norig = size;
            Py_SET_SIZE(l, 0);
            _Py_atomic_store_ptr_release(&l->ob_item, NULL);
            l->allocated = 0;
            cleared = 1;
        }
    }
    else {
        norig = hi - lo;
        recycle = PyMem_Malloc((norig > 0 ? norig : 1) * sizeof(PyObject *));
        if (recycle == NULL) {
            PyErr_NoMemory();
            err = -1;
        }
        else {
            memcpy(recycle, l->ob_item + lo, norig * sizeof(PyObject *));
            Py_ssize_t tail = size - hi;
            Py_ssize_t newsize = size - norig + d;
            if (d < norig) {
                memmove(l->ob_item + lo + d, l->ob_item + hi, tail * sizeof(PyObject *));
                if (list_resize(l, newsize) < 0) {
                    Py_SET_SIZE(l, newsize);    /* the shrink failed; the items are in place */
                    PyErr_Clear();
                }
            }
            else if (d > norig) {
                err = list_resize(l, newsize);
                if (err == 0) {
                    memmove(l->ob_item + lo + d, l->ob_item + hi, tail * sizeof(PyObject *));
                }
            }
            if (err == 0) {
                memcpy(l->ob_item + lo, fresh, d * sizeof(PyObject *));
                PyMem_Free(fresh);
                fresh = NULL;
            }
        }
    }
    Py_END_CRITICAL_SECTION();
    if (err < 0) {
        PyMem_Free(recycle);
        release(fresh, d);
        return -1;
    }
    if (fresh != NULL) {
        PyMem_Free(fresh);              /* empty: nothing was inserted */
    }
    for (Py_ssize_t k = norig - 1; k >= 0; --k) {
        Py_DECREF(recycle[k]);
    }
    if (cleared) {
        lu_qsbr_retire(recycle);        /* the old array, which readers may still hold */
    }
    else {
        PyMem_Free(recycle);
    }
    return 0;
}

static PyObject *
prim_list_splice(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); LIST(0); SSIZE(1, lo); SSIZE(2, hi); SEQ(3);
    return list_splice((PyListObject *)args[0], lo, hi, args[3], 0) < 0 ? NULL : Py_NewRef(Py_None);
}

/* The list C-API (Objects/listobject.c). */

int
PyList_SetSlice(PyObject *a, Py_ssize_t ilow, Py_ssize_t ihigh, PyObject *v)
{
    if (!PyList_Check(a)) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyObject *seq = v == NULL ? Py_NewRef((PyObject *)&_PyRuntime.singletons.empty_tuple)
        : PyTuple_Check(v) || PyList_Check(v) ? Py_NewRef(v) : lu_call_kernel(LU_K_tuple_of, &v, 1);
    if (seq == NULL) {
        return -1;
    }
    int r = list_splice((PyListObject *)a, ilow, ihigh, seq, 1);
    Py_DECREF(seq);
    return r;
}

int
PyList_Insert(PyObject *op, Py_ssize_t where, PyObject *newitem)
{
    if (!PyList_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyListObject *l = (PyListObject *)op;
    int err = 0;
    Py_BEGIN_CRITICAL_SECTION(l);
    Py_ssize_t n = Py_SIZE(l);
    if (list_resize(l, n + 1) < 0) {
        err = -1;
    }
    else {
        where = where < 0 ? (where + n < 0 ? 0 : where + n) : where > n ? n : where;
        memmove(l->ob_item + where + 1, l->ob_item + where, (n - where) * sizeof(PyObject *));
        l->ob_item[where] = Py_NewRef(newitem);
    }
    Py_END_CRITICAL_SECTION();
    return err;
}

/* _PyList_AppendTakeRef's slow path: the list is full and the caller holds its lock. */
LU_EXPORT int
_PyList_AppendTakeRefListResize(PyListObject *self, PyObject *newitem)
{
    Py_ssize_t len = Py_SIZE(self);
    if (list_resize(self, len + 1) < 0) {
        Py_DECREF(newitem);
        return -1;
    }
    _Py_atomic_store_ptr_release(&self->ob_item[len], newitem);
    return 0;
}

/* list_set_stride(list, start, step, seq): list[start + k * step] = seq[k]
   for each item of seq, positions in range. */
static PyObject *
prim_list_set_stride(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); LIST(0); SSIZE(1, start); SSIZE(2, step); SEQ(3);
    PyListObject *l = (PyListObject *)args[0];
    Py_ssize_t n;
    PyObject **fresh = snapshot(args[3], &n);
    if (fresh == NULL) {
        return NULL;
    }
    PyObject **old = PyMem_Malloc((n > 0 ? n : 1) * sizeof(PyObject *));
    if (old == NULL) {
        release(fresh, n);
        return PyErr_NoMemory();
    }
    int bad = 0;
    Py_BEGIN_CRITICAL_SECTION(l);
    for (Py_ssize_t k = 0; k < n; k++) {
        Py_ssize_t at = start + k * step;
        if (at < 0 || at >= Py_SIZE(l)) {
            bad = 1;
            break;
        }
        old[k] = l->ob_item[at];
        l->ob_item[at] = fresh[k];
    }
    Py_END_CRITICAL_SECTION();
    if (bad) {
        lu_err_format(PyExc_SystemError, "list_set_stride() position out of bounds");
        PyMem_Free(old);
        PyMem_Free(fresh);
        return NULL;
    }
    PyMem_Free(fresh);
    release(old, n);
    return Py_NewRef(Py_None);
}

/* list_del_stride(list, start, step, count): removes the count items at
   start + k * step, in either direction (the loop of list_ass_subscript). */
static PyObject *
prim_list_del_stride(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); LIST(0); SSIZE(1, start); SSIZE(2, step); SSIZE(3, count);
    PyListObject *l = (PyListObject *)args[0];
    PyObject **garbage = PyMem_Malloc((count > 0 ? count : 1) * sizeof(PyObject *));
    if (garbage == NULL) {
        return PyErr_NoMemory();
    }
    int err = 0;
    Py_BEGIN_CRITICAL_SECTION(l);
    Py_ssize_t size = Py_SIZE(l);
    if (step < 0) {
        start += (count - 1) * step;
        step = -step;
    }
    if (count > 0 && (start < 0 || start + (count - 1) * step >= size)) {
        lu_err_format(PyExc_SystemError, "list_del_stride() position out of bounds");
        err = -1;
    }
    else if (count > 0) {
        PyObject **items = l->ob_item;
        /* size_t arithmetic, as list_ass_subscript's: the step may be near PY_SSIZE_T_MAX. */
        Py_ssize_t cur = start, i = 0;
        for (; i < count; cur = (Py_ssize_t)((size_t)cur + (size_t)step), i++) {
            garbage[i] = items[cur];
            Py_ssize_t lim = step - 1;
            if ((size_t)cur + (size_t)step >= (size_t)size) {
                lim = size - cur - 1;
            }
            memmove(items + cur - i, items + cur + 1, lim * sizeof(PyObject *));
        }
        size_t after = (size_t)start + (size_t)count * (size_t)step;
        if (after < (size_t)size) {
            memmove(items + after - count, items + after, (size - after) * sizeof(PyObject *));
        }
        if (list_resize(l, size - count) < 0) {
            Py_SET_SIZE(l, size - count);   /* the shrink failed; the items are in place */
            PyErr_Clear();
        }
    }
    Py_END_CRITICAL_SECTION();
    if (err < 0) {
        PyMem_Free(garbage);
        return NULL;
    }
    release(garbage, count);
    return Py_NewRef(Py_None);
}

/* list_pop(list, i): removes and returns item i, or MISSING when i is out of range. */
static PyObject *
prim_list_pop(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); LIST(0); SSIZE(1, i);
    PyListObject *l = (PyListObject *)args[0];
    PyObject *v = NULL;
    Py_BEGIN_CRITICAL_SECTION(l);
    Py_ssize_t size = Py_SIZE(l);
    if (0 <= i && i < size) {
        v = l->ob_item[i];
        memmove(l->ob_item + i, l->ob_item + i + 1, (size - i - 1) * sizeof(PyObject *));
        if (list_resize(l, size - 1) < 0) {
            Py_SET_SIZE(l, size - 1);       /* the shrink failed; the items are in place */
            PyErr_Clear();
        }
    }
    Py_END_CRITICAL_SECTION();
    return v != NULL ? v : Py_NewRef(LU_MISSING);
}

/* list_reverse(list) */
static PyObject *
prim_list_reverse(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); LIST(0);
    PyListObject *l = (PyListObject *)args[0];
    Py_BEGIN_CRITICAL_SECTION(l);
    for (Py_ssize_t i = 0, j = Py_SIZE(l) - 1; i < j; i++, j--) {
        PyObject *t = l->ob_item[i];
        l->ob_item[i] = l->ob_item[j];
        l->ob_item[j] = t;
    }
    Py_END_CRITICAL_SECTION();
    return Py_NewRef(Py_None);
}

/* list_sort_begin(list): moves the items to a new list and leaves the
   original empty with allocated -1, which any resize resets
   (list_sort_impl); list_sort_end(list, items) puts the items back, drops
   whatever the original grew meanwhile, and says whether it grew anything. */
static PyObject *
prim_list_sort_begin(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); LIST(0);
    PyListObject *l = (PyListObject *)args[0];
    PyObject *items = PyList_New(0);
    if (items == NULL) {
        return NULL;
    }
    PyListObject *held = (PyListObject *)items;
    Py_BEGIN_CRITICAL_SECTION(l);
    PyMem_Free(held->ob_item);
    held->ob_item = l->ob_item;
    held->allocated = l->allocated;
    Py_SET_SIZE(held, Py_SIZE(l));
    Py_SET_SIZE(l, 0);
    _Py_atomic_store_ptr_release(&l->ob_item, NULL);
    l->allocated = -1;
    Py_END_CRITICAL_SECTION();
    return items;
}

static PyObject *
prim_list_sort_end(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); LIST(0); LIST(1);
    PyListObject *l = (PyListObject *)args[0], *held = (PyListObject *)args[1];
    PyObject **grown;
    Py_ssize_t n, modified;
    Py_BEGIN_CRITICAL_SECTION(l);
    modified = l->allocated != -1;
    grown = l->ob_item;
    n = Py_SIZE(l);
    Py_SET_SIZE(l, Py_SIZE(held));
    _Py_atomic_store_ptr_release(&l->ob_item, held->ob_item);
    l->allocated = held->allocated;
    held->ob_item = NULL;
    held->allocated = 0;
    Py_SET_SIZE(held, 0);
    Py_END_CRITICAL_SECTION();
    if (grown != NULL) {
        while (--n >= 0) {
            Py_XDECREF(grown[n]);
        }
        lu_qsbr_retire(grown);
    }
    return Py_NewRef(modified ? Py_True : Py_False);
}

/* Iterators. A list_iterator, tuple_iterator, list_reverseiterator and the
   sequence-protocol "iterator" share one layout: the sequence, or NULL once
   exhausted, and the next index. */

/* iter_new(type, seq): an iterator of one of those types; a reverse iterator
   starts at the last index. */
static PyObject *
prim_iter_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    if (!PyType_Check(args[0]) || (tp != &PyTupleIter_Type && tp != &PyListIter_Type
                                   && tp != &PyListRevIter_Type && tp != &PySeqIter_Type)) {
        lu_err_format(PyExc_TypeError, "iter_new() needs a sequence iterator type");
        return NULL;
    }
    seqiterobject *it = (seqiterobject *)lu_object_new(tp, sizeof(seqiterobject));
    if (it != NULL) {
        it->it_index = tp == &PyListRevIter_Type ? Py_SIZE(args[1]) - 1 : 0;
        it->it_seq = Py_NewRef(args[1]);
    }
    return (PyObject *)it;
}

/* The next item of a tuple, list or reverse list iterator, or NULL when
   exhausted. */
PyObject *
lu_seqiter_next(PyObject *iter)
{
    seqiterobject *it = (seqiterobject *)iter;
    PyTypeObject *tp = Py_TYPE(iter);
    /* The iterator's state under its own lock and a list's array and size,
       which change apart, under the list's: both at once, on a reference
       taken first so the sequence outlives a racing exhaustion. */
    PyObject *v = NULL, *dead = NULL, *seq;
    Py_BEGIN_CRITICAL_SECTION(iter);
    seq = Py_XNewRef(it->it_seq);
    Py_END_CRITICAL_SECTION();
    if (seq == NULL) {
        return NULL;
    }
    Py_BEGIN_CRITICAL_SECTION2(iter, seq);
    if (it->it_seq == seq) {
        if (tp == &PyListRevIter_Type) {
            if (it->it_index >= 0 && it->it_index < Py_SIZE(seq)) {
                v = Py_NewRef(((PyListObject *)seq)->ob_item[it->it_index--]);
            }
            else {
                it->it_index = -1;
            }
        }
        else if (it->it_index < Py_SIZE(seq)) {
            v = Py_NewRef(tp == &PyTupleIter_Type ? PyTuple_GET_ITEM(seq, it->it_index)
                                                  : ((PyListObject *)seq)->ob_item[it->it_index]);
            it->it_index++;
        }
        if (v == NULL) {
            it->it_seq = NULL;
            dead = seq;
        }
    }
    Py_END_CRITICAL_SECTION2();
    Py_XDECREF(dead);
    Py_DECREF(seq);
    return v;
}

static PyObject *
prim_seqiter_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *v = lu_seqiter_next(args[0]);
    return v != NULL ? v : Py_NewRef(LU_MISSING);
}

/* iter_state(it): (seq or None, index); iter_set(it, seq or None, index). */
static PyObject *
prim_iter_state(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    seqiterobject *it = (seqiterobject *)args[0];
    PyObject *seq, *r = NULL;
    Py_ssize_t i;
    Py_BEGIN_CRITICAL_SECTION(it);
    seq = Py_XNewRef(it->it_seq);
    i = it->it_index;
    Py_END_CRITICAL_SECTION();
    PyObject *index = PyLong_FromSsize_t(i);
    if (index != NULL) {
        r = PyTuple_Pack(2, seq != NULL ? seq : Py_None, index);
    }
    Py_XDECREF(index);
    Py_XDECREF(seq);
    return r;
}

static PyObject *
prim_iter_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); SSIZE(2, index);
    seqiterobject *it = (seqiterobject *)args[0];
    PyObject *old;
    Py_BEGIN_CRITICAL_SECTION(it);
    old = it->it_seq;
    it->it_seq = args[1] == Py_None ? NULL : Py_NewRef(args[1]);
    it->it_index = index;
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old);
    return Py_NewRef(Py_None);
}

LU_GC_FIELDS(seqiter, seqiterobject, it_seq, it_seq)

static void
seqiter_dealloc(PyObject *op)
{
    Py_XDECREF(((seqiterobject *)op)->it_seq);
    lu_object_free(op);
}

/* range: four ints, as rangeobject. */

typedef struct {
    PyObject_HEAD
    PyObject *start, *stop, *step, *length;
} rangeobject;

/* range_iterator: the values as C longs; longrange_iterator: as ints. */
typedef struct {
    PyObject_HEAD
    long start, step, len;
} rangeiterobject;

typedef struct {
    PyObject_HEAD
    PyObject *start, *step, *len;
} longrangeiterobject;

static void
range_dealloc(PyObject *op)
{
    rangeobject *r = (rangeobject *)op;
    Py_XDECREF(r->start);
    Py_XDECREF(r->stop);
    Py_XDECREF(r->step);
    Py_XDECREF(r->length);
    lu_object_free(op);
}

static void
longrangeiter_dealloc(PyObject *op)
{
    longrangeiterobject *it = (longrangeiterobject *)op;
    Py_XDECREF(it->start);
    Py_XDECREF(it->step);
    Py_XDECREF(it->len);
    lu_object_free(op);
}

static void
plain_dealloc(PyObject *op)
{
    lu_object_free(op);
}

/* range_new(start, stop, step, length): the four ints, already validated. */
static PyObject *
prim_range_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4);
    for (int i = 0; i < 4; i++) {
        if (!PyLong_CheckExact(args[i])) {
            lu_err_format(PyExc_TypeError, "range_new() needs ints");
            return NULL;
        }
    }
    rangeobject *r = (rangeobject *)lu_object_new(&PyRange_Type, sizeof(rangeobject));
    if (r != NULL) {
        r->start = Py_NewRef(args[0]);
        r->stop = Py_NewRef(args[1]);
        r->step = Py_NewRef(args[2]);
        r->length = Py_NewRef(args[3]);
    }
    return (PyObject *)r;
}

/* range_get(r, i): start, stop, step or length for i = 0..3. */
static PyObject *
prim_range_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SSIZE(1, i);
    if (!PyRange_Check(args[0]) || i < 0 || i > 3) {
        lu_err_format(PyExc_TypeError, "range_get(range, 0..3)");
        return NULL;
    }
    rangeobject *r = (rangeobject *)args[0];
    return Py_NewRef(i == 0 ? r->start : i == 1 ? r->stop : i == 2 ? r->step : r->length);
}

static PyObject *
fast_range_iter(long start, long step, long len)
{
    rangeiterobject *it = (rangeiterobject *)lu_object_new(&PyRangeIter_Type, sizeof(rangeiterobject));
    if (it != NULL) {
        it->start = start;
        it->step = step;
        it->len = len;
    }
    return (PyObject *)it;
}

static PyObject *
long_range_iter(PyObject *start, PyObject *step, PyObject *len)
{
    longrangeiterobject *it = (longrangeiterobject *)lu_object_new(&PyLongRangeIter_Type,
                                                                   sizeof(longrangeiterobject));
    if (it != NULL) {
        it->start = Py_NewRef(start);
        it->step = Py_NewRef(step);
        it->len = Py_NewRef(len);
    }
    return (PyObject *)it;
}

/* range_iter_new(start, step, len): the iterator over len values from
   start by step, a range_iterator when the values fit C longs and a
   longrange_iterator otherwise (range_iter and range_reverse). */
static PyObject *
prim_range_iter_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    for (int i = 0; i < 3; i++) {
        if (!PyLong_CheckExact(args[i])) {
            lu_err_format(PyExc_TypeError, "range_iter_new() needs ints");
            return NULL;
        }
    }
    long start, step, len, last;
    if (lu_long_as_long(args[0], &start) == 0 && lu_long_as_long(args[1], &step) == 0
        && lu_long_as_long(args[2], &len) == 0
        && (len == 0 || (!__builtin_mul_overflow(len - 1, step, &last)
                         && !__builtin_add_overflow(start, last, &last)
                         && !__builtin_add_overflow(last, step, &last)))) {
        return fast_range_iter(start, step, len);
    }
    PyErr_Clear();
    return long_range_iter(args[0], args[1], args[2]);
}

/* rangeiter_next(it): the next value, or MISSING when exhausted. */
static PyObject *
prim_rangeiter_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *it = args[0];
    if (Py_TYPE(it) == &PyRangeIter_Type) {
        rangeiterobject *r = (rangeiterobject *)it;
        if (r->len <= 0) {
            return Py_NewRef(LU_MISSING);
        }
        long v = r->start;
        r->start += r->step;
        r->len--;
        return PyLong_FromLong(v);
    }
    longrangeiterobject *r = (longrangeiterobject *)it;
    if (lu_long_compare(r->len, lu_small_int(0)) <= 0) {
        return Py_NewRef(LU_MISSING);
    }
    PyObject *next = lu_long_add(r->start, r->step);
    PyObject *left = next != NULL ? lu_long_sub(r->len, lu_small_int(1)) : NULL;
    if (left == NULL) {
        Py_XDECREF(next);
        return NULL;
    }
    PyObject *v = r->start;
    r->start = next;
    Py_SETREF(r->len, left);
    return v;
}

/* rangeiter_len(it): the values left, as an int. */
static PyObject *
prim_rangeiter_len(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (Py_TYPE(args[0]) == &PyRangeIter_Type) {
        long n = ((rangeiterobject *)args[0])->len;
        return PyLong_FromLong(n > 0 ? n : 0);
    }
    return Py_NewRef(((longrangeiterobject *)args[0])->len);
}

/* The iteration of a range_iterator the loop does itself: the next long, or
   NULL when exhausted. */
PyObject *
lu_rangeiter_next(PyObject *iter)
{
    rangeiterobject *r = (rangeiterobject *)iter;
    if (r->len <= 0) {
        return NULL;
    }
    long v = r->start;
    r->start += r->step;
    r->len--;
    return PyLong_FromLong(v);
}

/* The iterator of a range (range_iter): a range_iterator when every value
   fits a C long, else a longrange_iterator. */
PyObject *
lu_range_iter(PyObject *seq)
{
    rangeobject *r = (rangeobject *)seq;
    long start, stop, step, len;
    if (lu_long_as_long(r->start, &start) == 0 && lu_long_as_long(r->stop, &stop) == 0
        && lu_long_as_long(r->step, &step) == 0 && lu_long_as_long(r->length, &len) == 0
        && (len == 0 || (step > 0 ? stop <= LONG_MAX - (step - 1) : stop >= LONG_MIN + (-1 - step)))) {
        return fast_range_iter(start, step, len);
    }
    PyErr_Clear();
    return long_range_iter(r->start, r->step, r->length);
}

static PyObject *
prim_range_iter(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyRange_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "range_iter() needs a range");
        return NULL;
    }
    return lu_range_iter(args[0]);
}

/* slice: start, stop, step. slice_new(a, b, c); slice_get(s, i). */
static PyObject *
prim_slice_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    return PySlice_New(args[0], args[1], args[2]);
}

static PyObject *
prim_slice_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SSIZE(1, i);
    if (!PySlice_Check(args[0]) || i < 0 || i > 2) {
        lu_err_format(PyExc_TypeError, "slice_get(slice, 0..2)");
        return NULL;
    }
    PySliceObject *s = (PySliceObject *)args[0];
    return Py_NewRef(i == 0 ? s->start : i == 1 ? s->stop : s->step);
}

/* type_has_sequence(type): whether the type has a PySequenceMethods table,
   which PyObject_DelItem consults for its message. */
static PyObject *
prim_type_has_sequence(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyType_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "type_has_sequence() needs a type");
        return NULL;
    }
    return Py_NewRef(((PyTypeObject *)args[0])->tp_as_sequence != NULL ? Py_True : Py_False);
}

void
lu_storage_init(void)
{
    PyTypeObject *iters[] = { &PyTupleIter_Type, &PyListIter_Type, &PyListRevIter_Type,
                              &PySeqIter_Type };
    for (int i = 0; i < 4; i++) {
        iters[i]->tp_basicsize = sizeof(seqiterobject);
        iters[i]->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        iters[i]->tp_dealloc = seqiter_dealloc;
        iters[i]->tp_traverse = seqiter_traverse;
        iters[i]->tp_clear = seqiter_clear;
    }
    PyRange_Type.tp_basicsize = sizeof(rangeobject);
    PyRange_Type.tp_flags |= Py_TPFLAGS_DEFAULT;
    PyRange_Type.tp_dealloc = range_dealloc;
    PyRangeIter_Type.tp_basicsize = sizeof(rangeiterobject);
    PyRangeIter_Type.tp_flags |= Py_TPFLAGS_DEFAULT;
    PyRangeIter_Type.tp_dealloc = plain_dealloc;
    PyLongRangeIter_Type.tp_basicsize = sizeof(longrangeiterobject);
    PyLongRangeIter_Type.tp_flags |= Py_TPFLAGS_DEFAULT;
    PyLongRangeIter_Type.tp_dealloc = longrangeiter_dealloc;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_storage_methods[] = {
    FAST(seq_get), FAST(seq_slice), FAST(seq_concat), FAST(seq_repeat), FAST(tuple_from),
    FAST(xxh_hash), FAST(repr_enter), FAST(repr_leave), FAST(list_alloc), FAST(list_append),
    FAST(list_set), FAST(list_splice), FAST(list_set_stride), FAST(list_del_stride),
    FAST(list_reverse), FAST(list_pop), FAST(list_sort_begin), FAST(list_sort_end), FAST(iter_new),
    FAST(seqiter_next), FAST(iter_state), FAST(iter_set), FAST(range_new), FAST(range_get),
    FAST(range_iter_new), FAST(rangeiter_next), FAST(rangeiter_len), FAST(range_iter),
    FAST(slice_new), FAST(slice_get), FAST(type_has_sequence),
    { NULL, NULL, 0, NULL },
};
