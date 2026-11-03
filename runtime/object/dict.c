/* Dict objects and the namespace primitives (Objects/dictobject.c: new_keys_object,
   unicodekeys_lookup_unicode, insertdict, dictresize). Only exact-str keys
   are handled natively; general keys, deletion and views are the kernel's
   dict (K1j) over this same table format. Replaced tables are freed at once
   until lock-free readers exist, then through QSBR. */

#include "object/dict.h"

#include <string.h>

#define PERTURB_SHIFT 5

static uint8_t
log2_keysize(Py_ssize_t minsize)
{
    minsize = minsize > PyDict_MINSIZE ? minsize : PyDict_MINSIZE;
    return (uint8_t)(64 - __builtin_clzll((unsigned long long)(minsize - 1)));
}

static PyDictKeysObject *
new_keys(uint8_t log2_size)
{
    uint8_t log2_bytes = log2_size + (log2_size < 8 ? 0 : log2_size < 16 ? 1
                                      : log2_size < 32 ? 2 : 3);
    Py_ssize_t usable = USABLE_FRACTION((Py_ssize_t)1 << log2_size);
    size_t size = sizeof(PyDictKeysObject) + ((size_t)1 << log2_bytes)
                + usable * sizeof(PyDictUnicodeEntry);
    PyDictKeysObject *dk = PyMem_Malloc(size);
    if (dk == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    memset(dk, 0, sizeof *dk);
    dk->dk_refcnt = 1;
    dk->dk_log2_size = log2_size;
    dk->dk_log2_index_bytes = log2_bytes;
    dk->dk_kind = DICT_KEYS_UNICODE;
    dk->dk_usable = usable;
    memset(dk->dk_indices, 0xff, (size_t)1 << log2_bytes);     /* DKIX_EMPTY */
    memset(lu_dk_entries(dk), 0, usable * sizeof(PyDictUnicodeEntry));
    return dk;
}

PyObject *
PyDict_New(void)
{
    PyDictKeysObject *dk = new_keys(PyDict_LOG_MINSIZE);
    if (dk == NULL) {
        return NULL;
    }
    PyDictObject *mp = (PyDictObject *)lu_object_new(&PyDict_Type, sizeof(PyDictObject));
    if (mp == NULL) {
        PyMem_Free(dk);
        return NULL;
    }
    mp->ma_keys = dk;
    return (PyObject *)mp;
}

static void
dict_dealloc(PyObject *op)
{
    PyDictKeysObject *dk = ((PyDictObject *)op)->ma_keys;
    PyDictUnicodeEntry *ep = lu_dk_entries(dk);
    for (Py_ssize_t i = 0; i < dk->dk_nentries; i++) {
        Py_XDECREF(ep[i].me_key);
        Py_XDECREF(ep[i].me_value);
    }
    PyMem_Free(dk);
    lu_object_free(op);
}

void
lu_dict_init(void)
{
    PyDict_Type.tp_basicsize = sizeof(PyDictObject);
    PyDict_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyDict_Type.tp_dealloc = dict_dealloc;
}

/* The entry number of `key`, or DKIX_EMPTY; *slot_out, when given, is its
   index slot. */
static Py_ssize_t
lookup(PyDictKeysObject *dk, PyObject *key, Py_hash_t hash, size_t *slot_out)
{
    PyDictUnicodeEntry *ep0 = lu_dk_entries(dk);
    size_t mask = DK_MASK(dk);
    size_t perturb = hash;
    size_t i = (size_t)hash & mask;
    for (;;) {
        Py_ssize_t ix = lu_dk_get_index(dk, i);
        if (ix >= 0) {
            PyObject *k = ep0[ix].me_key;
            if (k == key || (lu_str_hash(k) == hash && lu_str_eq(k, key))) {
                if (slot_out != NULL) {
                    *slot_out = i;
                }
                return ix;
            }
        }
        else if (ix == DKIX_EMPTY) {
            return DKIX_EMPTY;
        }
        perturb >>= PERTURB_SHIFT;
        i = mask & (i * 5 + perturb + 1);
    }
}

static size_t
find_empty_slot(PyDictKeysObject *dk, Py_hash_t hash)
{
    size_t mask = DK_MASK(dk);
    size_t i = (size_t)hash & mask;
    for (size_t perturb = hash; lu_dk_get_index(dk, i) >= 0; ) {
        perturb >>= PERTURB_SHIFT;
        i = mask & (i * 5 + perturb + 1);
    }
    return i;
}

PyObject *
lu_ns_lookup(PyObject *dict, PyObject *key)
{
    PyDictKeysObject *dk = ((PyDictObject *)dict)->ma_keys;
    Py_ssize_t ix = lookup(dk, key, lu_str_hash(key), NULL);
    return ix >= 0 ? ((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix].me_value : NULL;
}

/* dictresize to a table for `minsize` slots, compacting the entries. */
static int
resize(PyDictObject *mp, Py_ssize_t minsize)
{
    PyDictKeysObject *old = mp->ma_keys;
    PyDictKeysObject *dk = new_keys(log2_keysize(minsize));
    if (dk == NULL) {
        return -1;
    }
    PyDictUnicodeEntry *from = lu_dk_entries(old), *to = lu_dk_entries(dk);
    Py_ssize_t n = 0;
    for (Py_ssize_t i = 0; i < old->dk_nentries; i++) {
        if (from[i].me_value != NULL) {
            to[n] = from[i];
            lu_dk_set_index(dk, find_empty_slot(dk, lu_str_hash(to[n].me_key)), n);
            n++;
        }
    }
    dk->dk_nentries = n;
    dk->dk_usable -= n;
    mp->ma_keys = dk;
    PyMem_Free(old);
    return 0;
}

int
lu_ns_insert(PyObject *dict, PyObject *key, PyObject *value)
{
    if (!PyUnicode_CheckExact(key)) {
        lu_err_format(PyExc_SystemError, "a namespace key must be an exact str, not '%s'",
                      Py_TYPE(key)->tp_name);
        return -1;
    }
    PyDictObject *mp = (PyDictObject *)dict;
    Py_hash_t hash = lu_str_hash(key);
    Py_ssize_t ix = lookup(mp->ma_keys, key, hash, NULL);
    if (ix >= 0) {
        PyDictUnicodeEntry *ep = &((PyDictUnicodeEntry *)lu_dk_entries(mp->ma_keys))[ix];
        PyObject *old = ep->me_value;
        ep->me_value = Py_NewRef(value);
        Py_DECREF(old);
        return 0;
    }
    if (mp->ma_keys->dk_usable <= 0 && resize(mp, mp->ma_used * 3) < 0) {   /* GROWTH_RATE */
        return -1;
    }
    PyDictKeysObject *dk = mp->ma_keys;
    Py_ssize_t n = dk->dk_nentries;
    PyDictUnicodeEntry *ep = &((PyDictUnicodeEntry *)lu_dk_entries(dk))[n];
    ep->me_key = Py_NewRef(key);
    ep->me_value = Py_NewRef(value);
    lu_dk_set_index(dk, find_empty_slot(dk, hash), n);
    dk->dk_version = 0;
    dk->dk_usable--;
    dk->dk_nentries = n + 1;
    mp->ma_used++;
    return 0;
}

int
lu_ns_delete(PyObject *dict, PyObject *key)
{
    PyDictObject *mp = (PyDictObject *)dict;
    PyDictKeysObject *dk = mp->ma_keys;
    size_t slot;
    Py_ssize_t ix = lookup(dk, key, lu_str_hash(key), &slot);
    if (ix < 0) {
        return 0;
    }
    PyDictUnicodeEntry *ep = &((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix];
    PyObject *old_key = ep->me_key, *old_value = ep->me_value;
    lu_dk_set_index(dk, slot, DKIX_DUMMY);
    ep->me_key = ep->me_value = NULL;
    dk->dk_version = 0;
    mp->ma_used--;
    Py_DECREF(old_key);
    Py_DECREF(old_value);
    return 1;
}

int
lu_ns_next(PyObject *dict, Py_ssize_t *pos, PyObject **key, PyObject **value)
{
    PyDictKeysObject *dk = ((PyDictObject *)dict)->ma_keys;
    PyDictUnicodeEntry *ep = lu_dk_entries(dk);
    for (Py_ssize_t i = *pos; i < dk->dk_nentries; i++) {
        if (ep[i].me_value != NULL) {
            *pos = i + 1;
            *key = ep[i].me_key;
            *value = ep[i].me_value;
            return 1;
        }
    }
    return 0;
}
