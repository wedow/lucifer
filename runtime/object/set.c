/* The set table (Objects/setobject.c): linear probes, perturbation and the
   resize policy, so iteration order matches the oracle. This is the layout
   half that marshal needs to build frozensets; the kernel's set and
   frozenset reuse it (K1j). Keys compare with lu_eq until the kernel's
   rich comparison exists. */

#include "lucifer.h"

#include <string.h>

#define LINEAR_PROBES 9
#define PERTURB_SHIFT 5

static void
set_dealloc(PyObject *op)
{
    PySetObject *so = (PySetObject *)op;
    for (Py_ssize_t i = 0; i <= so->mask; i++) {
        Py_XDECREF(so->table[i].key);
    }
    if (so->table != so->smalltable) {
        PyMem_Free(so->table);
    }
    lu_object_free(op);
}

void
lu_set_init(void)
{
    PySet_Type.tp_basicsize = PyFrozenSet_Type.tp_basicsize = sizeof(PySetObject);
    PySet_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyFrozenSet_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PySet_Type.tp_dealloc = PyFrozenSet_Type.tp_dealloc = set_dealloc;
}

PyObject *
lu_set_new(PyTypeObject *type)
{
    PySetObject *so = (PySetObject *)lu_object_new(type, sizeof(PySetObject));
    if (so == NULL) {
        return NULL;
    }
    so->mask = PySet_MINSIZE - 1;
    so->table = so->smalltable;
    so->hash = -1;
    return (PyObject *)so;
}

static void
insert_clean(setentry *table, size_t mask, PyObject *key, Py_hash_t hash)
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

/* set_table_resize, for growth: no dummies exist before deletion does. */
static int
resize(PySetObject *so, Py_ssize_t minused)
{
    size_t newsize = PySet_MINSIZE;
    while (newsize <= (size_t)minused) {
        newsize <<= 1;
    }
    setentry *old = so->table;
    Py_ssize_t oldmask = so->mask;
    setentry *table = PyMem_Calloc(newsize, sizeof(setentry));
    if (table == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    so->mask = newsize - 1;
    so->table = table;
    for (Py_ssize_t i = 0; i <= oldmask; i++) {
        if (old[i].key != NULL) {
            insert_clean(table, so->mask, old[i].key, old[i].hash);
        }
    }
    if (old != so->smalltable) {
        PyMem_Free(old);
    }
    return 0;
}

/* set_add_entry. */
int
lu_set_add(PyObject *set, PyObject *key)
{
    PySetObject *so = (PySetObject *)set;
    Py_hash_t hash = lu_hash(key);
    if (hash == -1) {
        return -1;
    }
    size_t mask = so->mask;
    size_t i = (size_t)hash & mask;
    size_t perturb = hash;
    for (;;) {
        setentry *entry = &so->table[i];
        int probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0;
        do {
            if (entry->hash == 0 && entry->key == NULL) {
                so->fill++;
                so->used++;
                entry->key = Py_NewRef(key);
                entry->hash = hash;
                if ((size_t)so->fill * 5 < mask * 3) {
                    return 0;
                }
                return resize(so, so->used > 50000 ? so->used * 2 : so->used * 4);
            }
            if (entry->hash == hash) {
                int cmp = lu_eq(entry->key, key);
                if (cmp != 0) {
                    return cmp < 0 ? -1 : 0;
                }
            }
            entry++;
        } while (probes--);
        perturb >>= PERTURB_SHIFT;
        i = (i * 5 + 1 + perturb) & mask;
    }
}

static Py_uhash_t
shuffle_bits(Py_uhash_t h)
{
    return ((h ^ 89869747UL) ^ (h << 16)) * 3644798167UL;
}

/* frozenset_hash, cached. */
Py_hash_t
lu_frozenset_hash(PyObject *set)
{
    PySetObject *so = (PySetObject *)set;
    if (so->hash != -1) {
        return so->hash;
    }
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
    so->hash = (Py_hash_t)hash;
    return so->hash;
}
