/* The storage of collections.deque (lib/modules/_collections.py keeps the
   protocol: arguments, errors, comparison, iteration): a ring buffer of
   strong references, so both ends grow and shrink in O(1). Each primitive is
   one critical section on the ring, as each method of 3.14t's deque is one
   on the deque, and counts a mutation in `state` where that deque does, for
   the iterators. Items a primitive drops are released after it unlocks, so
   no destructor runs under the lock. Readers take the lock too, except
   ring_len and ring_state, which load one word. */

#include "prim/prim.h"

#include <string.h>

typedef struct {
    PyObject_HEAD
    PyObject **items;           /* cap slots, cap a power of two or 0 */
    Py_ssize_t head;            /* the slot of item 0 */
    Py_ssize_t len;
    Py_ssize_t cap;
    Py_ssize_t state;           /* mutations so far */
} ring;

#define MIN_CAP 8
#define SLOT(r, i) ((r)->items[((r)->head + (i)) & ((r)->cap - 1)])

/* Moves the items into a new array of cap slots (a power of two >= len). */
static int
resize(ring *r, Py_ssize_t cap)
{
    PyObject **items = PyMem_Malloc(cap * sizeof(PyObject *));
    if (items == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    for (Py_ssize_t i = 0; i < r->len; i++) {
        items[i] = SLOT(r, i);
    }
    memset(items + r->len, 0, (cap - r->len) * sizeof(PyObject *));
    PyMem_Free(r->items);
    r->items = items;
    r->head = 0;
    r->cap = cap;
    return 0;
}

static int
reserve(ring *r, Py_ssize_t need)
{
    if (need <= r->cap) {
        return 0;
    }
    Py_ssize_t cap = r->cap ? r->cap : MIN_CAP;
    while (cap < need) {
        if (cap > PY_SSIZE_T_MAX / 2 / (Py_ssize_t)sizeof(PyObject *)) {
            PyErr_NoMemory();
            return -1;
        }
        cap *= 2;
    }
    return resize(r, cap);
}

/* Gives back half the array once three quarters of it are empty. */
static void
shrink(ring *r)
{
    if (r->cap > 4 * MIN_CAP && r->len < r->cap / 4 && resize(r, r->cap / 2) < 0) {
        PyErr_Clear();          /* the items stay where they are */
    }
}

/* Removes and returns the item at one end; the ring is not empty. */
static PyObject *
take(ring *r, int left)
{
    Py_ssize_t at = (r->head + (left ? 0 : r->len - 1)) & (r->cap - 1);
    PyObject *v = r->items[at];
    r->items[at] = NULL;
    if (left) {
        r->head = (r->head + 1) & (r->cap - 1);
    }
    r->len--;
    r->state++;
    shrink(r);
    return v;
}

/* deque_append_lock_held / deque_appendleft_lock_held: adds item (a new
   reference) at one end; past maxlen (>= 0) the other end's item goes to
   *dropped, else NULL. */
static int
put(ring *r, PyObject *item, int left, Py_ssize_t maxlen, PyObject **dropped)
{
    *dropped = NULL;
    if (reserve(r, r->len + 1) < 0) {
        Py_DECREF(item);
        return -1;
    }
    if (left) {
        r->head = (r->head - 1) & (r->cap - 1);
        r->items[r->head] = item;
    }
    else {
        SLOT(r, r->len) = item;
    }
    r->len++;
    if (maxlen >= 0 && r->len > maxlen) {
        *dropped = take(r, !left);
    }
    else {
        r->state++;
    }
    return 0;
}

#define ARGS(n) do { if (nargs != (n) || !Py_IS_TYPE(args[0], &ring_type)) { \
        lu_err_format(PyExc_TypeError, "%s() takes a ring and %d more arguments", __func__ + 5, (n) - 1); \
        return NULL; } } while (0)

static PyTypeObject ring_type;

/* An index or count argument; a value past a C long is an OverflowError. */
static int
ssize_arg(PyObject *o, Py_ssize_t *v)
{
    long x;
    if (!PyLong_CheckExact(o)) {
        lu_err_format(PyExc_TypeError, "ring index must be an int");
        return -1;
    }
    if (lu_long_as_long(o, &x) < 0) {
        PyErr_Clear();
        lu_err_format(PyExc_OverflowError, "Python int too large to convert to C ssize_t");
        return -1;
    }
    *v = x;
    return 0;
}

/* maxlen: None for none (-1), else a non-negative int. */
static int
maxlen_arg(PyObject *o, Py_ssize_t *v)
{
    if (o == Py_None) {
        *v = -1;
        return 0;
    }
    return ssize_arg(o, v);
}

/* ring_new() */
static PyObject *
prim_ring_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 0) {
        lu_err_format(PyExc_TypeError, "ring_new() takes no arguments");
        return NULL;
    }
    return lu_object_new(&ring_type, sizeof(ring));
}

/* ring_push(r, item, left, maxlen) */
static PyObject *
prim_ring_push(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4);
    Py_ssize_t maxlen;
    if (maxlen_arg(args[3], &maxlen) < 0) {
        return NULL;
    }
    ring *r = (ring *)args[0];
    PyObject *dropped = NULL;
    int err;
    Py_BEGIN_CRITICAL_SECTION(r);
    err = put(r, Py_NewRef(args[1]), args[2] == Py_True, maxlen, &dropped);
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(dropped);
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* ring_extend(r, seq, left, maxlen): every item of a list or tuple, pushed
   in one critical section (deque_extend over its copy of a list). */
static PyObject *
prim_ring_extend(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4);
    Py_ssize_t maxlen;
    if (maxlen_arg(args[3], &maxlen) < 0) {
        return NULL;
    }
    PyObject *seq = args[1];
    if (!PyList_CheckExact(seq) && !PyTuple_CheckExact(seq)) {
        lu_err_format(PyExc_TypeError, "ring_extend() needs a list or tuple");
        return NULL;
    }
    /* The items, owned here: each slot then holds what its push dropped. */
    Py_ssize_t n = 0;
    PyObject **items = NULL;
    Py_BEGIN_CRITICAL_SECTION(seq);
    n = Py_SIZE(seq);
    items = PyMem_Malloc((n > 0 ? n : 1) * sizeof(PyObject *));
    for (Py_ssize_t i = 0; items != NULL && i < n; i++) {
        items[i] = Py_NewRef(PySequence_Fast_ITEMS(seq)[i]);
    }
    Py_END_CRITICAL_SECTION();
    if (items == NULL) {
        return PyErr_NoMemory();
    }
    ring *r = (ring *)args[0];
    Py_ssize_t done = 0;
    Py_BEGIN_CRITICAL_SECTION(r);
    while (done < n && put(r, items[done], args[2] == Py_True, maxlen, &items[done]) == 0) {
        done++;
    }
    Py_END_CRITICAL_SECTION();
    for (Py_ssize_t i = 0; i < n; i++) {
        /* a failed push released its item */
        if (i != done) {
            Py_XDECREF(items[i]);
        }
    }
    PyMem_Free(items);
    return done < n ? NULL : Py_NewRef(Py_None);
}

/* ring_pop(r, left): the item, or MISSING when the ring is empty. */
static PyObject *
prim_ring_pop(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    ring *r = (ring *)args[0];
    PyObject *v = NULL;
    Py_BEGIN_CRITICAL_SECTION(r);
    if (r->len > 0) {
        v = take(r, args[1] == Py_True);
    }
    Py_END_CRITICAL_SECTION();
    return v != NULL ? v : Py_NewRef(LU_MISSING);
}

/* The slot of index i (negative from the end), or -1 when out of range. */
static Py_ssize_t
position(ring *r, Py_ssize_t i)
{
    i += i < 0 ? r->len : 0;
    return 0 <= i && i < r->len ? i : -1;
}

/* An item index; one that fits no C long raises exc, as PyNumber_AsSsize_t. */
static int
index_arg(PyObject *o, Py_ssize_t *v, PyObject *exc)
{
    if (ssize_arg(o, v) < 0) {
        if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
            PyErr_Clear();
            lu_err_format(exc, "cannot fit 'int' into an index-sized integer");
        }
        return -1;
    }
    return 0;
}

/* ring_get(r, i, state): item i (negative from the end); MISSING when out of
   range, or when state >= 0 and the ring has changed since that state. */
static PyObject *
prim_ring_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    Py_ssize_t i, state;
    if (index_arg(args[1], &i, PyExc_IndexError) < 0 || ssize_arg(args[2], &state) < 0) {
        return NULL;
    }
    ring *r = (ring *)args[0];
    PyObject *v = NULL;
    Py_BEGIN_CRITICAL_SECTION(r);
    if (state < 0 || state == r->state) {
        i = position(r, i);
        v = i >= 0 ? Py_NewRef(SLOT(r, i)) : NULL;
    }
    Py_END_CRITICAL_SECTION();
    return v != NULL ? v : Py_NewRef(LU_MISSING);
}

/* ring_set(r, i, value): False when i is out of range. */
static PyObject *
prim_ring_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    Py_ssize_t i;
    if (index_arg(args[1], &i, PyExc_OverflowError) < 0) {
        return NULL;
    }
    ring *r = (ring *)args[0];
    PyObject *old = NULL;
    Py_BEGIN_CRITICAL_SECTION(r);
    i = position(r, i);
    if (i >= 0) {
        old = SLOT(r, i);
        SLOT(r, i) = Py_NewRef(args[2]);
    }
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old);
    return Py_NewRef(old != NULL ? Py_True : Py_False);
}

/* ring_del(r, i): False when i is out of range. The shorter side moves. */
static PyObject *
prim_ring_del(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    Py_ssize_t i;
    if (index_arg(args[1], &i, PyExc_OverflowError) < 0) {
        return NULL;
    }
    ring *r = (ring *)args[0];
    PyObject *old = NULL;
    Py_BEGIN_CRITICAL_SECTION(r);
    i = position(r, i);
    if (i >= 0) {
        /* move it to the nearer end, then take it from there */
        PyObject *v = SLOT(r, i);
        int left = i < r->len / 2;
        if (left) {
            for (Py_ssize_t j = i; j > 0; j--) {
                SLOT(r, j) = SLOT(r, j - 1);
            }
            SLOT(r, 0) = v;
        }
        else {
            for (Py_ssize_t j = i; j < r->len - 1; j++) {
                SLOT(r, j) = SLOT(r, j + 1);
            }
            SLOT(r, r->len - 1) = v;
        }
        old = take(r, left);
    }
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old);
    return Py_NewRef(i >= 0 ? Py_True : Py_False);
}

/* ring_insert(r, i, value, maxlen): value before index i (clamped, negative
   from the end); False when the ring holds maxlen items already. */
static PyObject *
prim_ring_insert(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4);
    Py_ssize_t i, maxlen;
    if (ssize_arg(args[1], &i) < 0 || maxlen_arg(args[3], &maxlen) < 0) {
        return NULL;
    }
    ring *r = (ring *)args[0];
    int full = 0, err = 0;
    Py_BEGIN_CRITICAL_SECTION(r);
    if (r->len == maxlen) {
        full = 1;
    }
    else if (reserve(r, r->len + 1) < 0) {
        err = -1;
    }
    else {
        i += i < 0 ? r->len : 0;
        i = i < 0 ? 0 : i > r->len ? r->len : i;
        r->len++;
        for (Py_ssize_t j = r->len - 1; j > i; j--) {
            SLOT(r, j) = SLOT(r, j - 1);
        }
        SLOT(r, i) = Py_NewRef(args[2]);
        r->state++;
    }
    Py_END_CRITICAL_SECTION();
    return err < 0 ? NULL : Py_NewRef(full ? Py_False : Py_True);
}

/* ring_rotate(r, n): n steps to the right, left when negative; the fewer
   items move. */
static PyObject *
prim_ring_rotate(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    Py_ssize_t n;
    if (ssize_arg(args[1], &n) < 0) {
        return NULL;
    }
    ring *r = (ring *)args[0];
    Py_BEGIN_CRITICAL_SECTION(r);
    if (r->len > 1) {
        r->state++;
        Py_ssize_t mask = r->cap - 1;
        n %= r->len;
        n += n < 0 ? r->len : 0;
        if (n <= r->len / 2) {
            for (; n > 0; n--) {
                Py_ssize_t last = (r->head + r->len - 1) & mask;
                PyObject *v = r->items[last];
                r->items[last] = NULL;
                r->head = (r->head - 1) & mask;
                r->items[r->head] = v;
            }
        }
        else {
            for (n = r->len - n; n > 0; n--) {
                PyObject *v = r->items[r->head];
                r->items[r->head] = NULL;
                r->items[(r->head + r->len) & mask] = v;
                r->head = (r->head + 1) & mask;
            }
        }
    }
    Py_END_CRITICAL_SECTION();
    return Py_NewRef(Py_None);
}

/* ring_reverse(r) */
static PyObject *
prim_ring_reverse(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    ring *r = (ring *)args[0];
    Py_BEGIN_CRITICAL_SECTION(r);
    for (Py_ssize_t i = 0, j = r->len - 1; i < j; i++, j--) {
        PyObject *t = SLOT(r, i);
        SLOT(r, i) = SLOT(r, j);
        SLOT(r, j) = t;
    }
    Py_END_CRITICAL_SECTION();
    return Py_NewRef(Py_None);
}

/* Empties the ring; its old array and count go to the caller to release. */
static PyObject **
detach(ring *r, Py_ssize_t *n, Py_ssize_t *head, Py_ssize_t *cap)
{
    PyObject **items = r->items;
    *n = r->len;
    *head = r->head;
    *cap = r->cap;
    if (r->len > 0) {
        r->state++;
    }
    r->items = NULL;
    r->len = r->head = r->cap = 0;
    return items;
}

static void
release(PyObject **items, Py_ssize_t n, Py_ssize_t head, Py_ssize_t cap)
{
    for (Py_ssize_t i = 0; i < n; i++) {
        Py_DECREF(items[(head + i) & (cap - 1)]);
    }
    PyMem_Free(items);
}

/* ring_clear(r) */
static PyObject *
prim_ring_clear(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    ring *r = (ring *)args[0];
    PyObject **items;
    Py_ssize_t n, head, cap;
    Py_BEGIN_CRITICAL_SECTION(r);
    items = detach(r, &n, &head, &cap);
    Py_END_CRITICAL_SECTION();
    release(items, n, head, cap);
    return Py_NewRef(Py_None);
}

/* ring_items(r): the items as a new list. */
static PyObject *
prim_ring_items(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    ring *r = (ring *)args[0];
    PyObject *list = NULL;
    Py_BEGIN_CRITICAL_SECTION(r);
    list = PyList_New(r->len);
    for (Py_ssize_t i = 0; list != NULL && i < r->len; i++) {
        PyList_SET_ITEM(list, i, Py_NewRef(SLOT(r, i)));
    }
    Py_END_CRITICAL_SECTION();
    return list;
}

/* ring_len(r), ring_state(r): one word each, without the lock. */
static PyObject *
prim_ring_len(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyLong_FromSsize_t(__atomic_load_n(&((ring *)args[0])->len, __ATOMIC_RELAXED));
}

static PyObject *
prim_ring_state(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyLong_FromSsize_t(__atomic_load_n(&((ring *)args[0])->state, __ATOMIC_RELAXED));
}

static int
ring_traverse(PyObject *op, visitproc visit, void *arg)
{
    ring *r = (ring *)op;
    for (Py_ssize_t i = 0; i < r->len; i++) {
        Py_VISIT(SLOT(r, i));
    }
    return 0;
}

static int
ring_clear(PyObject *op)
{
    Py_ssize_t n, head, cap;
    PyObject **items = detach((ring *)op, &n, &head, &cap);
    release(items, n, head, cap);
    return 0;
}

static void
ring_dealloc(PyObject *op)
{
    ring_clear(op);
    lu_object_free(op);
}

static PyTypeObject ring_type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "deque_ring",
    .tp_basicsize = sizeof(ring),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_dealloc = ring_dealloc,
    .tp_traverse = ring_traverse,
    .tp_clear = ring_clear,
};

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_deque_methods[] = {
    FAST(ring_new), FAST(ring_push), FAST(ring_extend), FAST(ring_pop), FAST(ring_get), FAST(ring_set),
    FAST(ring_del), FAST(ring_insert), FAST(ring_rotate), FAST(ring_reverse), FAST(ring_clear),
    FAST(ring_items), FAST(ring_len), FAST(ring_state),
    { NULL, NULL, 0, NULL },
};
