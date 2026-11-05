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
    LU_SET_CHANGED,         /* only inside the probe: a key was being freed */
};

/* The probe of set_lookkey for `key`: FOUND, ABSENT, or CANDIDATE with the
   key to compare in *candidate (a new reference). It holds no lock (a discard
   may free a key under it: the probe takes a reference first, and redoes
   itself under the critical section when it cannot). `judged` is a tuple of
   keys the caller has found unequal, or NULL: the probe passes over them.
   Naming them by identity keeps them right when the table changes. */
int lu_set_probe(PySetObject *so, PyObject *key, Py_hash_t hash, PyObject *judged, PyObject **candidate);
/* set_add_entry and set_discard_entry, each in one critical section from the
   probe to the commit, so that two threads cannot both add a key, nor a
   resize land between the probe and the commit. Answers as the probe;
   FOUND after an add means the set holds an equal key, after a discard that
   a key was removed. -1 on error. */
int lu_set_add_native(PySetObject *so, PyObject *key, Py_hash_t hash, PyObject *judged, PyObject **candidate);
int lu_set_discard_native(PySetObject *so, PyObject *key, Py_hash_t hash, PyObject *judged, int exact,
                          PyObject **candidate);
/* set_add_entry's last step, for a key known to be absent; under the critical section. */
int lu_set_insert(PySetObject *so, PyObject *key, Py_hash_t hash);
void lu_set_insert_clean(setentry *table, size_t mask, PyObject *key, Py_hash_t hash);
int lu_set_resize(PySetObject *so, Py_ssize_t minused);
/* set_clear_internal: empties the set, releasing its keys. */
void lu_set_clear(PySetObject *so);
/* set_swap_bodies, for an `a` that lock-free readers may probe and a `b`
   that only the caller holds. */
int lu_set_swap(PySetObject *a, PySetObject *b);
/* frozenset_hash_impl: the hash from the entries' hashes, uncached. */
Py_hash_t lu_set_hash_impl(PySetObject *so);

#endif
