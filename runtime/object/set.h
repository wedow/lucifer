/* The set table (Objects/setobject.c): linear probes, perturbation, dummies
   and the resize policy, so iteration order equals the oracle's. Defined
   once for the marshal reader's frozenset build (lu_set_add) and the
   kernel's set and frozenset (runtime/prim/set.c, lib/kernel/set.py).
   Like the dict probe, nothing here runs Python: a key that only __eq__ can
   judge comes back as a candidate. */
#ifndef LU_SET_H
#define LU_SET_H

#include "lucifer.h"

enum {
    LU_SET_FOUND,           /* the entry holds an equal key */
    LU_SET_ABSENT,          /* the entry is the unused slot that ends the probe */
    LU_SET_CANDIDATE,       /* the entry has an equal hash and a key to compare */
};

/* The probe of set_lookkey for `key`. `skip` is the number of candidates the
   caller has found unequal since the table last changed: the probe resumes
   past them, counted in the order it meets them (the windows of the probe
   sequence overlap, so an entry can be met twice). */
setentry *lu_set_probe(PySetObject *so, PyObject *key, Py_hash_t hash, Py_ssize_t skip, int *verdict);
/* set_add_entry's last step, for a key known to be absent. */
int lu_set_insert(PySetObject *so, PyObject *key, Py_hash_t hash);
void lu_set_insert_clean(setentry *table, size_t mask, PyObject *key, Py_hash_t hash);
int lu_set_resize(PySetObject *so, Py_ssize_t minused);
/* frozenset_hash_impl: the hash from the entries' hashes, uncached. */
Py_hash_t lu_set_hash_impl(PySetObject *so);

#endif
