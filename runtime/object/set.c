/* The set table (Objects/setobject.c): set_lookkey, set_add_entry,
   set_insert_clean, set_table_resize and frozenset_hash. The probe and the
   insertion are split so that the kernel can compare a candidate key with
   __eq__ between them (see set.h); the marshal reader's frozenset build
   (lu_set_add) compares natively with lu_eq. */

#include "object/set.h"

#include <string.h>

#define LINEAR_PROBES 9
#define PERTURB_SHIFT 5

/* A malloced table keeps its mask in a header before the entries, so that a
   reader that holds no lock gets the table and its mask from one load of
   so->table: the mask in the set object is replaced apart from the table.
   The smalltable's mask is PySet_MINSIZE - 1. */
#define TABLE_HEADER 16

static setentry *
table_new(size_t size)
{
    char *block = PyMem_Malloc(TABLE_HEADER + size * sizeof(setentry));
    if (block == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    *(size_t *)block = size - 1;
    return (setentry *)(block + TABLE_HEADER);
}

static size_t
table_mask(PySetObject *so, setentry *table)
{
    return table == so->smalltable ? PySet_MINSIZE - 1 : ((size_t *)table)[-TABLE_HEADER / sizeof(size_t)];
}

static void *
table_block(setentry *table)
{
    return (char *)table - TABLE_HEADER;
}

/* set_clear_internal. */
void
lu_set_clear(PySetObject *so)
{
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
        _Py_atomic_store_ptr_release(&so->table, so->smalltable);
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
        lu_qsbr_retire(table_block(table));
    }
}

/* Readers of `a` probe its table without the lock, so a's table is never
   written in place: a gets a whole table, b's malloced one or a new copy of
   b's smalltable, and b takes a's old one, or a copy of a's smalltable. A
   table b takes may still be probed: set_dealloc frees it through QSBR. */
int
lu_set_swap(PySetObject *a, PySetObject *b)
{
    int err = 0;
    Py_BEGIN_CRITICAL_SECTION2(a, b);
    setentry *at = a->table, *bt = b->table;
    if (bt == b->smalltable) {
        bt = table_new(PySet_MINSIZE);
        if (bt == NULL) {
            err = -1;
        }
        else {
            memcpy(bt, b->smalltable, sizeof b->smalltable);
        }
    }
    if (err == 0) {
        if (at == a->smalltable) {
            memcpy(b->smalltable, a->smalltable, sizeof b->smalltable);
            at = b->smalltable;
        }
        b->table = at;
        _Py_atomic_store_ptr_release(&a->table, bt);
        Py_ssize_t t = a->fill; a->fill = b->fill; b->fill = t;
        t = a->used; a->used = b->used; b->used = t;
        t = a->mask; a->mask = b->mask; b->mask = t;
        if (PyType_IsSubtype(Py_TYPE(a), &PyFrozenSet_Type) && PyType_IsSubtype(Py_TYPE(b), &PyFrozenSet_Type)) {
            Py_hash_t h = a->hash; a->hash = b->hash; b->hash = h;
        }
        else {
            a->hash = b->hash = -1;
        }
    }
    Py_END_CRITICAL_SECTION2();
    return err;
}

static int
set_clear(PyObject *op)
{
    lu_set_clear((PySetObject *)op);
    return 0;
}

static int
set_traverse(PyObject *op, visitproc visit, void *arg)
{
    PySetObject *so = (PySetObject *)op;
    for (Py_ssize_t i = 0; i <= so->mask; i++) {
        PyObject *key = so->table[i].key;
        if (key != NULL && key != LU_SET_DUMMY) {
            Py_VISIT(key);
        }
    }
    return 0;
}

static void
set_dealloc(PyObject *op)
{
    PySetObject *so = (PySetObject *)op;
    lu_clear_weakrefs(op);
    for (Py_ssize_t i = 0; i <= so->mask; i++) {
        PyObject *key = so->table[i].key;
        if (key != NULL && key != LU_SET_DUMMY) {
            Py_DECREF(key);
        }
    }
    if (so->table != so->smalltable) {
        lu_qsbr_retire(table_block(so->table));     /* it may have been swapped from a set readers probe */
    }
    lu_object_free(op);
}

void
lu_set_init(void)
{
    PyObject *dummy = LU_SET_DUMMY;
    dummy->ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL;
    dummy->ob_flags = _Py_STATICALLY_ALLOCATED_FLAG;
    dummy->ob_type = &PyBaseObject_Type;
    PySet_Type.tp_basicsize = PyFrozenSet_Type.tp_basicsize = sizeof(PySetObject);
    PySet_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyFrozenSet_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PySet_Type.tp_dealloc = PyFrozenSet_Type.tp_dealloc = set_dealloc;
    PySet_Type.tp_traverse = PyFrozenSet_Type.tp_traverse = set_traverse;
    PySet_Type.tp_clear = PyFrozenSet_Type.tp_clear = set_clear;
    PySet_Type.tp_weaklistoffset = PyFrozenSet_Type.tp_weaklistoffset = offsetof(PySetObject, weakreflist);
}

PyObject *
lu_set_new(PyTypeObject *type)
{
    PySetObject *so = (PySetObject *)(type == &PySet_Type || type == &PyFrozenSet_Type
        ? lu_object_new(type, sizeof(PySetObject)) : type->tp_alloc(type, 0));
    if (so == NULL) {
        return NULL;
    }
    so->mask = PySet_MINSIZE - 1;
    so->table = so->smalltable;
    so->hash = -1;
    return (PyObject *)so;
}

static int
judged_has(PyObject *judged, PyObject *key)
{
    for (Py_ssize_t i = 0; judged != NULL && i < PyTuple_GET_SIZE(judged); i++) {
        if (PyTuple_GET_ITEM(judged, i) == key) {
            return 1;
        }
    }
    return 0;
}

/* The probe of set_lookkey. Without the lock an entry's key may be being
   freed by a discard, so a key is read only after a try-incref; one that is
   gone answers LU_SET_CHANGED (the locked probe below redoes it). The table
   and its mask come from one load. A candidate whose key is in the tuple
   `judged` (or any key but `key` itself, when `exact`) is passed over; the
   candidate that stops the probe is returned as a new reference. */
static int
probe(PySetObject *so, PyObject *key, Py_hash_t hash, PyObject *judged, int exact, PyObject **candidate,
      setentry **at, int locked)
{
    setentry *table = _Py_atomic_load_ptr_acquire(&so->table);
    size_t perturb = hash;
    size_t mask = table_mask(so, table);
    size_t i = (size_t)hash & mask;
    for (;;) {
        setentry *entry = &table[i];
        int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
        do {
            if (entry->hash == 0 && entry->key == NULL) {
                return LU_SET_ABSENT;
            }
            if (entry->hash == hash) {
                PyObject *startkey = _Py_atomic_load_ptr(&entry->key);
                if (startkey == key) {
                    *at = entry;
                    return LU_SET_FOUND;
                }
                if (startkey != NULL && startkey != LU_SET_DUMMY && !exact && !judged_has(judged, startkey)) {
                    int held = !locked && !_Py_IsImmortal(startkey);
                    if (held && !lu_try_incref_compare(&entry->key, startkey)) {
                        return LU_SET_CHANGED;
                    }
                    int eq = lu_native_eq(startkey, key);
                    if (eq > 0) {
                        if (held) {
                            Py_DECREF(startkey);
                        }
                        *at = entry;
                        return LU_SET_FOUND;
                    }
                    if (eq < 0) {
                        *candidate = held ? startkey : Py_NewRef(startkey);
                        return LU_SET_CANDIDATE;
                    }
                    if (held) {
                        Py_DECREF(startkey);
                    }
                }
            }
            entry++;
        } while (probes--);
        perturb >>= PERTURB_SHIFT;
        i = (i * 5 + 1 + perturb) & mask;
    }
}

int
lu_set_probe(PySetObject *so, PyObject *key, Py_hash_t hash, PyObject *judged, PyObject **candidate)
{
    setentry *at;
    int verdict = probe(so, key, hash, judged, 0, candidate, &at, 0);
    if (verdict == LU_SET_CHANGED) {
        Py_BEGIN_CRITICAL_SECTION(so);
        verdict = probe(so, key, hash, judged, 0, candidate, &at, 1);
        Py_END_CRITICAL_SECTION();
    }
    return verdict;
}

void
lu_set_insert_clean(setentry *table, size_t mask, PyObject *key, Py_hash_t hash)
{
    size_t perturb = hash;
    size_t i = (size_t)hash & mask;
    for (;;) {
        setentry *entry = &table[i];
        if (entry->key == NULL) {
            goto found;
        }
        if (i + LINEAR_PROBES <= mask) {
            for (int j = 0; j < LINEAR_PROBES; j++) {
                entry++;
                if (entry->key == NULL) {
                    goto found;
                }
            }
        }
        perturb >>= PERTURB_SHIFT;
        i = (i * 5 + 1 + perturb) & mask;
        continue;
    found:
        entry->key = key;
        entry->hash = hash;
        return;
    }
}

/* set_table_resize. The new table is filled before it is published, and the
   smalltable is never refilled under a reader: a table is always a new one. */
int
lu_set_resize(PySetObject *so, Py_ssize_t minused)
{
    size_t newsize = PySet_MINSIZE;
    while (newsize <= (size_t)minused) {
        newsize <<= 1;
    }
    setentry *oldtable = so->table;
    if (newsize == PySet_MINSIZE && oldtable == so->smalltable && so->fill == so->used) {
        return 0;               /* we cannot get any smaller, and no dummies */
    }
    setentry *newtable = table_new(newsize);
    if (newtable == NULL) {
        return -1;
    }
    memset(newtable, 0, sizeof(setentry) * newsize);
    for (setentry *entry = oldtable; entry <= oldtable + so->mask; entry++) {
        if (entry->key != NULL && entry->key != LU_SET_DUMMY) {
            lu_set_insert_clean(newtable, newsize - 1, entry->key, entry->hash);
        }
    }
    so->fill = so->used;
    _Py_atomic_store_ptr_release(&so->table, newtable);
    so->mask = newsize - 1;
    if (oldtable != so->smalltable) {
        lu_qsbr_retire(table_block(oldtable));
    }
    return 0;
}

/* set_add_entry from the point where the probe found no equal key: the key
   takes the last dummy met before the unused slot, else the unused slot. */
int
lu_set_insert(PySetObject *so, PyObject *key, Py_hash_t hash)
{
    size_t mask = so->mask;
    size_t perturb = hash;
    size_t i = (size_t)hash & mask;
    setentry *freeslot = NULL;
    for (;;) {
        setentry *entry = &so->table[i];
        int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
        do {
            if (entry->hash == 0 && entry->key == NULL) {
                if (freeslot != NULL) {
                    so->used++;
                    freeslot->key = Py_NewRef(key);
                    freeslot->hash = hash;
                    return 0;
                }
                so->fill++;
                so->used++;
                entry->key = Py_NewRef(key);
                entry->hash = hash;
                if ((size_t)so->fill * 5 < mask * 3) {
                    return 0;
                }
                return lu_set_resize(so, so->used > 50000 ? so->used * 2 : so->used * 4);
            }
            if (entry->hash == -1) {
                freeslot = entry;
            }
            entry++;
        } while (probes--);
        perturb >>= PERTURB_SHIFT;
        i = (i * 5 + 1 + perturb) & mask;
    }
}

/* set_add_entry under the critical section: the key is inserted when the
   probe meets nothing equal. A candidate that only __eq__ can judge is
   returned (a new reference) with the set unchanged; the caller judges it
   and comes back with it in `judged`, so that what it commits is decided
   against the table as it is then. FOUND: the set holds an equal key now. */
int
lu_set_add_native(PySetObject *so, PyObject *key, Py_hash_t hash, PyObject *judged, PyObject **candidate)
{
    setentry *at;
    int verdict;
    Py_BEGIN_CRITICAL_SECTION(so);
    verdict = probe(so, key, hash, judged, 0, candidate, &at, 1);
    if (verdict == LU_SET_ABSENT) {
        verdict = lu_set_insert(so, key, hash) < 0 ? -1 : LU_SET_FOUND;
    }
    Py_END_CRITICAL_SECTION();
    return verdict;
}

/* set_discard_entry under the critical section, with the answers of the add:
   FOUND is a key removed. `exact` removes only the entry holding `key` itself
   (the candidate a caller has judged equal). */
int
lu_set_discard_native(PySetObject *so, PyObject *key, Py_hash_t hash, PyObject *judged, int exact,
                      PyObject **candidate)
{
    setentry *at;
    PyObject *old = NULL;
    int verdict;
    Py_BEGIN_CRITICAL_SECTION(so);
    verdict = probe(so, key, hash, judged, exact, candidate, &at, 1);
    if (verdict == LU_SET_FOUND) {
        old = at->key;
        at->key = LU_SET_DUMMY;
        at->hash = -1;
        so->used--;
    }
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old);
    return verdict;
}

/* The tuple `judged` with `key` appended; NULL with an error set. */
static PyObject *
judged_push(PyObject *judged, PyObject *key)
{
    Py_ssize_t n = judged == NULL ? 0 : PyTuple_GET_SIZE(judged);
    PyObject *more = PyTuple_New(n + 1);
    if (more == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        PyTuple_SET_ITEM(more, i, Py_NewRef(PyTuple_GET_ITEM(judged, i)));
    }
    PyTuple_SET_ITEM(more, n, Py_NewRef(key));
    return more;
}

/* The marshal reader's add: keys compare with lu_eq. */
int
lu_set_add(PyObject *set, PyObject *key)
{
    Py_hash_t hash = lu_hash(key);
    if (hash == -1) {
        return -1;
    }
    PyObject *judged = NULL;
    for (;;) {
        PyObject *candidate;
        int verdict = lu_set_add_native((PySetObject *)set, key, hash, judged, &candidate);
        if (verdict != LU_SET_CANDIDATE) {
            Py_XDECREF(judged);
            return verdict < 0 ? -1 : 0;
        }
        int cmp = lu_eq(candidate, key);
        PyObject *more = cmp == 0 ? judged_push(judged, candidate) : NULL;
        Py_DECREF(candidate);
        Py_XDECREF(judged);
        if (cmp != 0) {
            return cmp < 0 ? -1 : 0;
        }
        if ((judged = more) == NULL) {
            return -1;
        }
    }
}

static Py_uhash_t
shuffle_bits(Py_uhash_t h)
{
    return ((h ^ 89869747UL) ^ (h << 16)) * 3644798167UL;
}

Py_hash_t
lu_set_hash_impl(PySetObject *so)
{
    Py_uhash_t hash = 0;
    for (Py_ssize_t i = 0; i <= so->mask; i++) {
        hash ^= shuffle_bits(so->table[i].hash);
    }
    if ((so->mask + 1 - so->fill) & 1) {
        hash ^= shuffle_bits(0);
    }
    if ((so->fill - so->used) & 1) {
        hash ^= shuffle_bits(-1);
    }
    hash ^= ((Py_uhash_t)so->used + 1) * 1927868237UL;
    hash ^= (hash >> 11) ^ (hash >> 25);
    hash = hash * 69069U + 907133923UL;
    if (hash == (Py_uhash_t)-1) {
        hash = 590923713UL;
    }
    return (Py_hash_t)hash;
}

/* frozenset_hash, cached. */
Py_hash_t
lu_frozenset_hash(PyObject *set)
{
    PySetObject *so = (PySetObject *)set;
    if (so->hash == -1) {
        so->hash = lu_set_hash_impl(so);
    }
    return so->hash;
}


/* _PySet_NextEntryRef (setobject.c): the entry at pos onward, its key as a
   new reference, under the set's lock: a concurrent discard cannot take the
   key between the read and the reference. */
LU_EXPORT int
_PySet_NextEntryRef(PyObject *set, Py_ssize_t *pos, PyObject **key, Py_hash_t *hash)
{
    if (!PyAnySet_Check(set)) {
        PyErr_BadInternalCall();
        return -1;
    }
    PySetObject *so = (PySetObject *)set;
    int found = 0;
    Py_BEGIN_CRITICAL_SECTION(set);
    while (*pos <= so->mask) {
        setentry *entry = &so->table[(*pos)++];
        if (entry->key != NULL && entry->key != LU_SET_DUMMY) {
            *key = Py_NewRef(entry->key);
            *hash = entry->hash;
            found = 1;
            break;
        }
    }
    Py_END_CRITICAL_SECTION();
    return found;
}

/* _PySet_Update (set_update_internal): every item of iterable into the set. */
LU_EXPORT int
_PySet_Update(PyObject *set, PyObject *iterable)
{
    if (!PySet_Check(set)) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyObject *it = PyObject_GetIter(iterable);
    if (it == NULL) {
        return -1;
    }
    PyObject *item;
    while ((item = PyIter_Next(it)) != NULL) {
        int err = lu_set_add(set, item);
        Py_DECREF(item);
        if (err < 0) {
            Py_DECREF(it);
            return -1;
        }
    }
    Py_DECREF(it);
    return PyErr_Occurred() ? -1 : 0;
}
