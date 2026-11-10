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

/* Whether entry i is in use; the caller holds the set's critical section. */
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

/* The answer of a probe, add or discard to the kernel: whether the key was
   found (an add: is there; a discard: was removed), or the 1-tuple of a
   candidate that only __eq__ can judge. */
static PyObject *
verdict_result(int verdict, PyObject *candidate)
{
    if (verdict < 0) {
        return NULL;
    }
    if (verdict != LU_SET_CANDIDATE) {
        return bool_of(verdict == LU_SET_FOUND);
    }
    PyObject *one = PyTuple_Pack(1, candidate);
    Py_DECREF(candidate);
    return one;
}

/* set_probe(s, key, hash, judged) -> whether s holds an equal key, or the
   1-tuple of a candidate to compare. judged is the tuple of candidates the
   caller found unequal. */
static PyObject *
prim_set_probe(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); SET(0); SSIZE(2, hash);
    PyObject *candidate = NULL;
    int verdict = lu_set_probe((PySetObject *)args[0], args[1], hash, args[3], &candidate);
    return verdict_result(verdict, candidate);
}

/* set_add_entry(s, key, hash, judged): adds key unless s holds an equal one; True
   when it does, or the 1-tuple of a candidate to compare first. */
static PyObject *
prim_set_add_entry(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); SET(0); SSIZE(2, hash);
    PyObject *candidate = NULL;
    int verdict = lu_set_add_native((PySetObject *)args[0], args[1], hash, args[3], &candidate);
    return verdict_result(verdict, candidate);
}

/* set_discard_entry(s, key, hash, judged, exact): removes an equal key; whether it
   did, or the 1-tuple of a candidate to compare first. exact removes only
   key itself. */
static PyObject *
prim_set_discard_entry(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(5); SET(0); SSIZE(2, hash);
    PyObject *candidate = NULL;
    int verdict = lu_set_discard_native((PySetObject *)args[0], args[1], hash, args[3], args[4] == Py_True,
                                        &candidate);
    return verdict_result(verdict, candidate);
}

/* set_key(s, i), set_hash(s, i): an active entry's key or hash, else MISSING.
   Read under the set's critical section: a discard frees keys, and the table
   is replaced apart from its mask. */
static PyObject *
prim_set_key(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, i);
    PySetObject *so = (PySetObject *)args[0];
    PyObject *key = NULL;
    Py_BEGIN_CRITICAL_SECTION(so);
    key = active(so, i) ? Py_NewRef(so->table[i].key) : NULL;
    Py_END_CRITICAL_SECTION();
    return key != NULL ? key : Py_NewRef(LU_MISSING);
}

static PyObject *
prim_set_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, i);
    PySetObject *so = (PySetObject *)args[0];
    Py_hash_t hash = 0;
    int is_active;
    Py_BEGIN_CRITICAL_SECTION(so);
    is_active = active(so, i);
    if (is_active) {
        hash = so->table[i].hash;
    }
    Py_END_CRITICAL_SECTION();
    return is_active ? int_of(hash) : Py_NewRef(LU_MISSING);
}

/* set_next(s, i): the active entry at or after i, or -1. */
static PyObject *
prim_set_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, i);
    PySetObject *so = (PySetObject *)args[0];
    Py_ssize_t found = -1;
    Py_BEGIN_CRITICAL_SECTION(so);
    for (; i <= so->mask; i++) {
        if (active(so, i)) {
            found = i;
            break;
        }
    }
    Py_END_CRITICAL_SECTION();
    return int_of(found);
}

/* set_reserve(s, n): room for n more keys, sized from s under its lock. */
static PyObject *
prim_set_reserve(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SSIZE(1, n);
    int err;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    err = lu_set_reserve((PySetObject *)args[0], n);
    Py_END_CRITICAL_SECTION();
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* set_compact(s): set_difference_update_internal's last step, a new table
   once more than a quarter of the entries are dummies. */
static PyObject *
prim_set_compact(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); SET(0);
    PySetObject *so = (PySetObject *)args[0];
    int err = 0;
    Py_BEGIN_CRITICAL_SECTION(so);
    if (so->fill - so->used > (Py_ssize_t)so->mask / 4) {
        err = lu_set_rebuild(so);
    }
    Py_END_CRITICAL_SECTION();
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* set_clear(s): set_clear_internal. */
static PyObject *
prim_set_clear(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); SET(0);
    lu_set_clear((PySetObject *)args[0]);
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
    return lu_set_swap((PySetObject *)args[0], (PySetObject *)args[1]) < 0 ? NULL : Py_NewRef(Py_None);
}

/* set_merge_clean(so, other): the first two cases of set_merge_lock_held,
   for a so with no entries (fill 0) and an other with some: room is made for
   other's keys (a clear since the caller's presize leaves the small table),
   then the table is copied when the sizes agree and there are no dummies,
   else each key is inserted. False,
   with nothing done, when another thread has added to so since the caller
   looked. */
static PyObject *
prim_set_merge_clean(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); SET(0); SET(1);
    PySetObject *so = (PySetObject *)args[0], *other = (PySetObject *)args[1];
    if (so == other) {
        lu_err_format(PyExc_SystemError, "set_merge_clean() needs two sets");
        return NULL;
    }
    int merged = 0;
    Py_BEGIN_CRITICAL_SECTION2(so, other);
    if (so->fill != 0) {
        /* not clean */
    }
    else if (lu_set_reserve(so, other->used) < 0) {
        merged = -1;
    }
    else if (so->mask == other->mask && other->fill == other->used) {
        merged = 1;
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
        merged = 1;
        so->fill = so->used = other->used;
        for (Py_ssize_t i = 0; i <= other->mask; i++) {
            PyObject *key = other->table[i].key;
            if (key != NULL && key != LU_SET_DUMMY) {
                lu_set_insert_clean(so->table, so->mask, Py_NewRef(key), other->table[i].hash);
            }
        }
    }
    Py_END_CRITICAL_SECTION2();
    return merged < 0 ? NULL : bool_of(merged);
}

/* set_info(s, which): 0 fill, 1 used, 2 mask, 3 finger, 4 the cached hash. */
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
    default: return int_of(so->hash);
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
    if (so->mask + 1 > PySet_MINSIZE) {
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

static int
setiter_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(((setiterobject *)op)->si_set);
    return 0;
}

static int
setiter_clear(PyObject *op)
{
    Py_CLEAR(((setiterobject *)op)->si_set);
    return 0;
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
    PySetIter_Type.tp_traverse = setiter_traverse;
    PySetIter_Type.tp_clear = setiter_clear;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_set_methods[] = {
    FAST(set_new), FAST(set_probe), FAST(set_add_entry), FAST(set_discard_entry), FAST(set_key), FAST(set_hash),
    FAST(set_next), FAST(set_reserve), FAST(set_compact), FAST(set_clear), FAST(set_pop),
    FAST(set_swap), FAST(set_merge_clean), FAST(set_info), FAST(set_hash_impl),
    FAST(set_reset_hash), FAST(set_sizeof), FAST(setiter_new), FAST(setiter_next),
    FAST(setiter_len), FAST(setiter_copy),
    { NULL, NULL, 0, NULL },
};
