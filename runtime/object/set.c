/* The set table (Objects/setobject.c): set_lookkey, set_add_entry,
   set_insert_clean, set_table_resize and frozenset_hash. The probe and the
   insertion are split so that the kernel can compare a candidate key with
   __eq__ between them (see set.h); the marshal reader's frozenset build
   (lu_set_add) compares natively with lu_eq. */

#include "object/set.h"

#include <string.h>

#define LINEAR_PROBES 9
#define PERTURB_SHIFT 5

static void
set_dealloc(PyObject *op)
{
    PySetObject *so = (PySetObject *)op;
    for (Py_ssize_t i = 0; i <= so->mask; i++) {
        PyObject *key = so->table[i].key;
        if (key != NULL && key != LU_SET_DUMMY) {
            Py_DECREF(key);
        }
    }
    if (so->table != so->smalltable) {
        PyMem_Free(so->table);
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

setentry *
lu_set_probe(PySetObject *so, PyObject *key, Py_hash_t hash, setentry *after, int *verdict)
{
    size_t perturb = hash;
    size_t mask = so->mask;
    size_t i = (size_t)hash & mask;
    int skipping = after != NULL;
    for (;;) {
        setentry *entry = &so->table[i];
        int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
        do {
            if (entry->hash == 0 && entry->key == NULL) {
                *verdict = LU_SET_ABSENT;
                return entry;
            }
            if (skipping) {
                skipping = entry != after;
            }
            else if (entry->hash == hash) {
                PyObject *startkey = entry->key;
                int eq = startkey == key ? 1 : lu_native_eq(startkey, key);
                if (eq != 0) {
                    *verdict = eq > 0 ? LU_SET_FOUND : LU_SET_CANDIDATE;
                    return entry;
                }
            }
            entry++;
        } while (probes--);
        perturb >>= PERTURB_SHIFT;
        i = (i * 5 + 1 + perturb) & mask;
    }
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

/* set_table_resize. */
int
lu_set_resize(PySetObject *so, Py_ssize_t minused)
{
    Py_ssize_t oldmask = so->mask;
    size_t newsize = PySet_MINSIZE;
    while (newsize <= (size_t)minused) {
        newsize <<= 1;
    }
    setentry *oldtable = so->table, *newtable;
    setentry small_copy[PySet_MINSIZE];
    int is_oldtable_malloced = oldtable != so->smalltable;
    if (newsize == PySet_MINSIZE) {
        /* A large table is shrinking, or we cannot get any smaller. */
        newtable = so->smalltable;
        if (newtable == oldtable) {
            if (so->fill == so->used) {
                return 0;               /* no dummies, so nothing to do */
            }
            memcpy(small_copy, oldtable, sizeof small_copy);
            oldtable = small_copy;
        }
    }
    else {
        newtable = PyMem_Malloc(newsize * sizeof(setentry));
        if (newtable == NULL) {
            PyErr_NoMemory();
            return -1;
        }
    }
    memset(newtable, 0, sizeof(setentry) * newsize);
    so->mask = newsize - 1;
    so->table = newtable;
    if (so->fill != so->used) {
        so->fill = so->used;
    }
    for (setentry *entry = oldtable; entry <= oldtable + oldmask; entry++) {
        if (entry->key != NULL && entry->key != LU_SET_DUMMY) {
            lu_set_insert_clean(newtable, so->mask, entry->key, entry->hash);
        }
    }
    if (is_oldtable_malloced) {
        lu_qsbr_retire(oldtable);
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

/* The marshal reader's add: keys compare with lu_eq. */
int
lu_set_add(PyObject *set, PyObject *key)
{
    PySetObject *so = (PySetObject *)set;
    Py_hash_t hash = lu_hash(key);
    if (hash == -1) {
        return -1;
    }
    setentry *after = NULL;
    for (;;) {
        int verdict;
        setentry *entry = lu_set_probe(so, key, hash, after, &verdict);
        if (verdict == LU_SET_FOUND) {
            return 0;
        }
        if (verdict == LU_SET_ABSENT) {
            return lu_set_insert(so, key, hash);
        }
        int cmp = lu_eq(entry->key, key);
        if (cmp != 0) {
            return cmp < 0 ? -1 : 0;
        }
        after = entry;
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
