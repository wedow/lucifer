/* Set primitives under the kernel's set and frozenset (DESIGN 4.4, set
   family): the steps of Objects/setobject.c that touch the table, over
   runtime/object/set.h. Entries are named by their table index. The
   kernel (lib/kernel/set.py) compares candidate keys with __eq__ between
   probes and commits through these; every mutator takes the set's critical
   section. */

#include "prim/prim.h"
#include "object/set.h"

#include <string.h>

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__ + 5, (n)); \
        return NULL; } } while (0)
#define SET(i) do { if (!PyAnySet_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() needs a set", __func__ + 5); return NULL; } } while (0)
#define SSIZE(i, var) Py_ssize_t var; do { long v_; if (!PyLong_CheckExact(args[i]) \
        || lu_long_as_long(args[i], &v_) < 0) { \
        if (!PyErr_Occurred()) lu_err_format(PyExc_TypeError, "%s() needs an int", __func__ + 5); \
        return NULL; } var = v_; } while (0)

static PyObject *
int_of(Py_ssize_t n)
{
    return PyLong_FromLongLong(n);
}

static PyObject *
bool_of(int v)
{
    return Py_NewRef(v ? Py_True : Py_False);
}

static int
active(PySetObject *so, Py_ssize_t i)
{
    return i >= 0 && i <= so->mask && so->table[i].key != NULL && so->table[i].key != LU_SET_DUMMY;
}

/* set_new(cls): an empty set or frozenset of class cls. */
static PyObject *
prim_set_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyType_Check(args[0]) || (!PyType_IsSubtype((PyTypeObject *)args[0], &PySet_Type)
                                   && !PyType_IsSubtype((PyTypeObject *)args[0], &PyFrozenSet_Type))) {
        lu_err_format(PyExc_TypeError, "set_new() needs a set class");
        return NULL;
    }
    return lu_set_new((PyTypeObject *)args[0]);
}

/* set_probe(s, key, hash, skip) -> entry index of an equal key, -1 when
   absent, or -3 - the index of a candidate to compare. skip is the
   number of candidates already judged unequal. */
static PyObject *
prim_set_probe(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); SET(0); SSIZE(2, hash); SSIZE(3, after);
    PySetObject *so = (PySetObject *)args[0];
    int verdict;
    setentry *entry = lu_set_probe(so, args[1], hash, after > 0 ? after : 0, &verdict);
    Py_ssize_t i = entry - so->table;
    return int_of(verdict == LU_SET_FOUND ? i : verdict == LU_SET_ABSENT ? -1 : -3 - i);
}

/* set_insert(s, key, hash): adds a key that is not in s. */
static PyObject *
prim_set_insert(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); SET(0); SSIZE(2, hash);
    int err;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    err = lu_set_insert((PySetObject *)args[0], args[1], hash);
    Py_END_CRITICAL_SECTION();
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* set_key(s, i), set_hash(s, i): an active entry's key or hash, else MISSING. */
static PyObject *
prim_set_key(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, i);
    PySetObject *so = (PySetObject *)args[0];
    return Py_NewRef(active(so, i) ? so->table[i].key : LU_MISSING);
}

static PyObject *
prim_set_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, i);
    PySetObject *so = (PySetObject *)args[0];
    return active(so, i) ? int_of(so->table[i].hash) : Py_NewRef(LU_MISSING);
}

/* set_next(s, i): the active entry at or after i, or -1. */
static PyObject *
prim_set_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, i);
    PySetObject *so = (PySetObject *)args[0];
    for (; i <= so->mask; i++) {
        if (active(so, i)) {
            return int_of(i);
        }
    }
    return int_of(-1);
}

/* set_discard_at(s, i): turns the active entry i into a dummy; False if it
   is not active. */
static PyObject *
prim_set_discard_at(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, i);
    PySetObject *so = (PySetObject *)args[0];
    PyObject *old = NULL;
    Py_BEGIN_CRITICAL_SECTION(so);
    if (active(so, i)) {
        old = so->table[i].key;
        so->table[i].key = LU_SET_DUMMY;
        so->table[i].hash = -1;
        so->used--;
    }
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old);
    return bool_of(old != NULL);
}

/* set_resize(s, minused). */
static PyObject *
prim_set_resize(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, n);
    int err;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    err = lu_set_resize((PySetObject *)args[0], n);
    Py_END_CRITICAL_SECTION();
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* set_clear(s): set_clear_internal. */
static PyObject *
prim_set_clear(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); SET(0);
    PySetObject *so = (PySetObject *)args[0];
    setentry *table, small_copy[PySet_MINSIZE];
    Py_ssize_t used;
    int malloced;
    Py_BEGIN_CRITICAL_SECTION(so);
    table = so->table;
    used = so->used;
    malloced = table != so->smalltable;
    if (!malloced && so->fill > 0) {
        memcpy(small_copy, table, sizeof small_copy);
        table = small_copy;
    }
    if (malloced || so->fill > 0) {
        memset(so->smalltable, 0, sizeof so->smalltable);
        so->fill = so->used = 0;
        so->mask = PySet_MINSIZE - 1;
        so->table = so->smalltable;
        so->hash = -1;
    }
    Py_END_CRITICAL_SECTION();
    for (setentry *e = table; used > 0; e++) {
        if (e->key != NULL && e->key != LU_SET_DUMMY) {
            used--;
            Py_DECREF(e->key);
        }
    }
    if (malloced) {
        lu_qsbr_retire(table);
    }
    return Py_NewRef(Py_None);
}

/* set_pop(s): set_pop_impl's key, MISSING when empty. */
static PyObject *
prim_set_pop(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); SET(0);
    PySetObject *so = (PySetObject *)args[0];
    PyObject *key = NULL;
    Py_BEGIN_CRITICAL_SECTION(so);
    if (so->used > 0) {
        setentry *entry = so->table + (so->finger & so->mask);
        setentry *limit = so->table + so->mask;
        while (entry->key == NULL || entry->key == LU_SET_DUMMY) {
            entry++;
            if (entry > limit) {
                entry = so->table;
            }
        }
        key = entry->key;
        entry->key = LU_SET_DUMMY;
        entry->hash = -1;
        so->used--;
        so->finger = entry - so->table + 1;
    }
    Py_END_CRITICAL_SECTION();
    return key != NULL ? key : Py_NewRef(LU_MISSING);
}

/* set_swap(a, b): set_swap_bodies. */
static PyObject *
prim_set_swap(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SET(1);
    PySetObject *a = (PySetObject *)args[0], *b = (PySetObject *)args[1];
    Py_BEGIN_CRITICAL_SECTION2(a, b);
    Py_ssize_t t = a->fill; a->fill = b->fill; b->fill = t;
    t = a->used; a->used = b->used; b->used = t;
    t = a->mask; a->mask = b->mask; b->mask = t;
    setentry *u = a->table;
    if (a->table == a->smalltable) {
        u = b->smalltable;
    }
    a->table = b->table;
    if (b->table == b->smalltable) {
        a->table = a->smalltable;
    }
    b->table = u;
    if (a->table == a->smalltable || b->table == b->smalltable) {
        setentry tab[PySet_MINSIZE];
        memcpy(tab, a->smalltable, sizeof tab);
        memcpy(a->smalltable, b->smalltable, sizeof tab);
        memcpy(b->smalltable, tab, sizeof tab);
    }
    if (PyType_IsSubtype(Py_TYPE(a), &PyFrozenSet_Type) && PyType_IsSubtype(Py_TYPE(b), &PyFrozenSet_Type)) {
        Py_hash_t h = a->hash; a->hash = b->hash; b->hash = h;
    }
    else {
        a->hash = b->hash = -1;
    }
    Py_END_CRITICAL_SECTION2();
    return Py_NewRef(Py_None);
}

/* set_merge_clean(so, other): the first two cases of set_merge_lock_held,
   for a so with no entries (fill 0) and an other with some: copy the table
   when the sizes agree and there are no dummies, else insert each key. */
static PyObject *
prim_set_merge_clean(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SET(1);
    PySetObject *so = (PySetObject *)args[0], *other = (PySetObject *)args[1];
    if (so->fill != 0 || so == other) {
        lu_err_format(PyExc_SystemError, "set_merge_clean() needs an empty set");
        return NULL;
    }
    Py_BEGIN_CRITICAL_SECTION2(so, other);
    if (so->mask == other->mask && other->fill == other->used) {
        for (Py_ssize_t i = 0; i <= other->mask; i++) {
            if (other->table[i].key != NULL) {
                so->table[i].key = Py_NewRef(other->table[i].key);
                so->table[i].hash = other->table[i].hash;
            }
        }
        so->fill = other->fill;
        so->used = other->used;
    }
    else {
        so->fill = so->used = other->used;
        for (Py_ssize_t i = 0; i <= other->mask; i++) {
            PyObject *key = other->table[i].key;
            if (key != NULL && key != LU_SET_DUMMY) {
                lu_set_insert_clean(so->table, so->mask, Py_NewRef(key), other->table[i].hash);
            }
        }
    }
    Py_END_CRITICAL_SECTION2();
    return Py_NewRef(Py_None);
}

/* set_info(s, which): 0 fill, 1 used, 2 mask, 3 finger, 4 the cached hash, 5 a
   table id that changes when the table is replaced. */
static PyObject *
prim_set_info(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, which);
    PySetObject *so = (PySetObject *)args[0];
    switch (which) {
    case 0: return int_of(so->fill);
    case 1: return int_of(so->used);
    case 2: return int_of(so->mask);
    case 3: return int_of(so->finger);
    case 4: return int_of(so->hash);
    default: return PyLong_FromLongLong((long long)(intptr_t)(so->table));
    }
}

/* set_hash_impl(s, cache): frozenset_hash_impl, stored as the hash when cache is true. */
static PyObject *
prim_set_hash_impl(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0);
    PySetObject *so = (PySetObject *)args[0];
    Py_hash_t h = lu_set_hash_impl(so);
    if (args[1] == Py_True) {
        so->hash = h;
    }
    return int_of(h);
}

/* set_reset_hash(s): the cached hash is dropped (set.__init__). */
static PyObject *
prim_set_reset_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); SET(0);
    ((PySetObject *)args[0])->hash = -1;
    return Py_NewRef(Py_None);
}

static PyObject *
prim_set_sizeof(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); SET(0);
    PySetObject *so = (PySetObject *)args[0];
    Py_ssize_t size = Py_TYPE(so)->tp_basicsize;
    if (so->table != so->smalltable) {
        size += (so->mask + 1) * (Py_ssize_t)sizeof(setentry);
    }
    return int_of(size);
}

/* Iterator: setiterobject. */
typedef struct {
    PyObject_HEAD
    PySetObject *si_set;        /* NULL once exhausted */
    Py_ssize_t si_used;
    Py_ssize_t si_pos;
    Py_ssize_t len;
} setiterobject;

static void
setiter_dealloc(PyObject *op)
{
    Py_XDECREF(((setiterobject *)op)->si_set);
    lu_object_free(op);
}

static PyObject *
prim_setiter_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); SET(0);
    setiterobject *si = (setiterobject *)lu_object_new(&PySetIter_Type, sizeof(setiterobject));
    if (si != NULL) {
        PySetObject *so = (PySetObject *)args[0];
        si->si_set = (PySetObject *)Py_NewRef(so);
        si->si_used = si->len = so->used;
    }
    return (PyObject *)si;
}

/* setiter_next(it): the next key, MISSING when exhausted, RuntimeError when
   the set changed size or lost the entries the iterator counted on. */
static PyObject *
prim_setiter_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    setiterobject *si = (setiterobject *)args[0];
    PySetObject *so = si->si_set;
    if (so == NULL) {
        return Py_NewRef(LU_MISSING);
    }
    PyObject *key = NULL;
    Py_BEGIN_CRITICAL_SECTION(so);
    if (si->si_used != so->used) {
        PyErr_SetString(PyExc_RuntimeError, "Set changed size during iteration");
        si->si_used = -1;       /* sticky */
    }
    else {
        Py_ssize_t i = si->si_pos;
        while (i <= so->mask && !active(so, i)) {
            i++;
        }
        si->si_pos = i + 1;
        if (i > so->mask) {
            si->si_set = NULL;
            Py_DECREF(so);
            key = Py_NewRef(LU_MISSING);
        }
        else {
            si->len--;
            key = Py_NewRef(so->table[i].key);
        }
    }
    Py_END_CRITICAL_SECTION();
    return key;
}

static PyObject *
prim_setiter_len(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    setiterobject *si = (setiterobject *)args[0];
    return int_of(si->si_set != NULL && si->si_used == si->si_set->used ? si->len : 0);
}

/* setiter_copy(it): an iterator at the same place. */
static PyObject *
prim_setiter_copy(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    setiterobject *si = (setiterobject *)args[0];
    setiterobject *copy = (setiterobject *)lu_object_new(&PySetIter_Type, sizeof(setiterobject));
    if (copy != NULL) {
        copy->si_set = (PySetObject *)Py_XNewRef((PyObject *)si->si_set);
        copy->si_used = si->si_used;
        copy->si_pos = si->si_pos;
        copy->len = si->len;
    }
    return (PyObject *)copy;
}

void
lu_set_iter_init(void)
{
    PySetIter_Type.tp_basicsize = sizeof(setiterobject);
    PySetIter_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PySetIter_Type.tp_dealloc = setiter_dealloc;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_set_methods[] = {
    FAST(set_new), FAST(set_probe), FAST(set_insert), FAST(set_key), FAST(set_hash),
    FAST(set_next), FAST(set_discard_at), FAST(set_resize), FAST(set_clear), FAST(set_pop),
    FAST(set_swap), FAST(set_merge_clean), FAST(set_info), FAST(set_hash_impl),
    FAST(set_reset_hash), FAST(set_sizeof), FAST(setiter_new), FAST(setiter_next),
    FAST(setiter_len), FAST(setiter_copy),
    { NULL, NULL, 0, NULL },
};
