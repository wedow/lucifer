/* Dict objects (Objects/dictobject.c: new_keys_object, the lookups,
   insertdict, dictresize, delitem_common, popitem, PyDict_Clear, PyDict_Copy)
   over the keys table of dict.h, and the namespace primitives that the
   interpreter calls with exact-str keys. Both kinds of table are handled
   here: a unicode table holds exact-str keys (the hash lives in the str), a
   general table stores the hash per entry.

   Nothing in this file runs Python. A probe that meets an equal-hash key
   it cannot judge natively returns that entry (LU_PROBE_CANDIDATE) and the
   kernel compares it with __eq__ outside any commit region, as DESIGN 4.4
   requires. The namespace functions only ever look up exact strs, so they
   treat such an entry as unequal. */

#include "object/dict.h"

#include <string.h>

#define PERTURB_SHIFT 5
#define GROWTH_RATE(mp) ((mp)->ma_used * 3)

static uint8_t
calculate_log2_keysize(Py_ssize_t minsize)
{
    minsize = minsize > PyDict_MINSIZE ? minsize : PyDict_MINSIZE;
    return (uint8_t)(64 - __builtin_clzll((unsigned long long)(minsize - 1)));
}

/* The table that holds n entries with room to grow. */
static uint8_t
estimate_log2_keysize(Py_ssize_t n)
{
    return calculate_log2_keysize((n * 3 + 1) / 2);
}

static size_t
entry_size(int unicode)
{
    return unicode ? sizeof(PyDictUnicodeEntry) : sizeof(PyDictKeyEntry);
}

static size_t
keys_size(PyDictKeysObject *dk)
{
    return sizeof(PyDictKeysObject) + ((size_t)1 << dk->dk_log2_index_bytes)
         + USABLE_FRACTION(DK_SIZE(dk)) * entry_size(lu_dk_is_unicode(dk));
}

static PyDictKeysObject *
new_keys(uint8_t log2_size, int unicode)
{
    uint8_t log2_bytes = log2_size + (log2_size < 8 ? 0 : log2_size < 16 ? 1
                                      : log2_size < 32 ? 2 : 3);
    Py_ssize_t usable = USABLE_FRACTION((Py_ssize_t)1 << log2_size);
    size_t size = sizeof(PyDictKeysObject) + ((size_t)1 << log2_bytes) + usable * entry_size(unicode);
    PyDictKeysObject *dk = PyMem_Malloc(size);
    if (dk == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    memset(dk, 0, sizeof *dk);
    dk->dk_refcnt = 1;
    dk->dk_log2_size = log2_size;
    dk->dk_log2_index_bytes = log2_bytes;
    dk->dk_kind = unicode ? DICT_KEYS_UNICODE : DICT_KEYS_GENERAL;
    dk->dk_usable = usable;
    memset(dk->dk_indices, 0xff, (size_t)1 << log2_bytes);     /* DKIX_EMPTY */
    memset(lu_dk_entries(dk), 0, usable * entry_size(unicode));
    return dk;
}

PyObject *
lu_dict_alloc(PyTypeObject *tp)
{
    PyDictObject *mp = (PyDictObject *)(tp == &PyDict_Type
        ? lu_object_new(&PyDict_Type, sizeof(PyDictObject)) : tp->tp_alloc(tp, 0));
    if (mp != NULL) {
        mp->ma_keys = LU_EMPTY_KEYS;
    }
    return (PyObject *)mp;
}

PyObject *
PyDict_New(void)
{
    return lu_dict_alloc(&PyDict_Type);
}

/* PyDict_WatchEvent, sent to the watchers whose bits are set in
   _ma_watcher_tag. The delivery itself arrives with K1t. */
static void
send_event(int watcher_bits, PyDict_WatchEvent event, PyDictObject *mp, PyObject *key,
           PyObject *value)
{
}

void
lu_dict_notify(PyDictObject *mp, PyDict_WatchEvent event, PyObject *key, PyObject *value)
{
    int bits = (int)(__atomic_load_n(&mp->_ma_watcher_tag, __ATOMIC_ACQUIRE) & ((1 << 8) - 1));
    if (bits) {
        send_event(bits, event, mp, key, value);
    }
}

static void
free_entries(PyDictKeysObject *dk)
{
    if (dk == LU_EMPTY_KEYS) {
        return;
    }
    for (Py_ssize_t i = 0; i < dk->dk_nentries; i++) {
        Py_XDECREF(lu_dk_key(dk, i));
        Py_XDECREF(lu_dk_value(dk, i));
    }
    PyMem_Free(dk);
}

static void
dict_dealloc(PyObject *op)
{
    PyDictObject *mp = (PyDictObject *)op;
    lu_dict_notify(mp, PyDict_EVENT_DEALLOCATED, NULL, NULL);
    free_entries(mp->ma_keys);
    lu_object_free(op);
}

void
lu_dict_init(void)
{
    PyDictKeysObject *dk = LU_EMPTY_KEYS;
    memset(dk, 0, sizeof(PyDictKeysObject));
    dk->dk_refcnt = _Py_IMMORTAL_REFCNT_LOCAL;
    dk->dk_log2_size = dk->dk_log2_index_bytes = 3;
    dk->dk_kind = DICT_KEYS_UNICODE;
    memset(dk->dk_indices, 0xff, PyDict_MINSIZE);
    PyDict_Type.tp_basicsize = sizeof(PyDictObject);
    PyDict_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyDict_Type.tp_dealloc = dict_dealloc;
}

/* Probing. */

/* Whether two distinct keys of an equal hash are equal, when the answer needs
   no Python: 1, 0, or -1 when __eq__ must decide. Exact strs, ints, floats
   and bytes compare by value; an exact int and float are left to their
   methods, and any other pair of those types is unequal. */
enum { EQ_STR, EQ_INT, EQ_FLOAT, EQ_BYTES, EQ_OTHER };

static int
eq_class(PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(v);
    return tp == &PyUnicode_Type ? EQ_STR
         : tp == &PyLong_Type || tp == &PyBool_Type ? EQ_INT
         : tp == &PyFloat_Type ? EQ_FLOAT
         : tp == &PyBytes_Type ? EQ_BYTES : EQ_OTHER;
}

int
lu_native_eq(PyObject *a, PyObject *b)
{
    int ca = eq_class(a), cb = eq_class(b);
    if (ca == EQ_OTHER || cb == EQ_OTHER) {
        return -1;
    }
    if (ca != cb) {
        return (ca == EQ_INT && cb == EQ_FLOAT) || (ca == EQ_FLOAT && cb == EQ_INT) ? -1 : 0;
    }
    return lu_eq(a, b);
}

Py_ssize_t
lu_dict_probe(PyDictObject *mp, PyObject *key, Py_hash_t hash, Py_ssize_t after)
{
    PyDictKeysObject *dk = __atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE);
    size_t mask = DK_MASK(dk);
    size_t perturb = hash;
    size_t i = (size_t)hash & mask;
    int skipping = after >= 0;
    for (;;) {
        Py_ssize_t ix = lu_dk_get_index(dk, i);
        if (ix >= 0) {
            if (skipping) {
                skipping = ix != after;
            }
            else {
                PyObject *k = lu_dk_key(dk, ix);
                if (k == key) {
                    return ix;
                }
                if (lu_dk_hash(dk, ix) == hash) {
                    int eq = lu_native_eq(k, key);
                    if (eq > 0) {
                        return ix;
                    }
                    if (eq < 0) {
                        return LU_PROBE_CANDIDATE(ix);
                    }
                }
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

/* The index slot that holds entry ix, found by its hash. */
static size_t
index_slot(PyDictKeysObject *dk, Py_hash_t hash, Py_ssize_t ix)
{
    size_t mask = DK_MASK(dk);
    size_t i = (size_t)hash & mask;
    for (size_t perturb = hash; lu_dk_get_index(dk, i) != ix; ) {
        perturb >>= PERTURB_SHIFT;
        i = mask & (i * 5 + perturb + 1);
    }
    return i;
}

/* dictresize: a table of 1 << log2_newsize slots holding the live entries
   in order, of the unicode kind when `unicode`. The old table goes to QSBR. */
static int
resize(PyDictObject *mp, uint8_t log2_newsize, int unicode)
{
    PyDictKeysObject *old = mp->ma_keys;
    PyDictKeysObject *dk = new_keys(log2_newsize, unicode);
    if (dk == NULL) {
        return -1;
    }
    Py_ssize_t n = 0;
    for (Py_ssize_t i = 0; i < old->dk_nentries; i++) {
        PyObject *value = lu_dk_value(old, i);
        if (value == NULL) {
            continue;
        }
        PyObject *key = lu_dk_key(old, i);
        Py_hash_t hash = lu_dk_hash(old, i);
        if (unicode) {
            ((PyDictUnicodeEntry *)lu_dk_entries(dk))[n] = (PyDictUnicodeEntry){ key, value };
        }
        else {
            ((PyDictKeyEntry *)lu_dk_entries(dk))[n] = (PyDictKeyEntry){ hash, key, value };
        }
        lu_dk_set_index(dk, find_empty_slot(dk, hash), n);
        n++;
    }
    dk->dk_usable -= n;
    dk->dk_nentries = n;
    __atomic_store_n(&mp->ma_keys, dk, __ATOMIC_RELEASE);
    if (old != LU_EMPTY_KEYS) {
        lu_qsbr_retire(old);
    }
    return 0;
}

static int
insertion_resize(PyDictObject *mp, int unicode)
{
    return resize(mp, calculate_log2_keysize(GROWTH_RATE(mp)), unicode);
}

/* Mutation. */

int
lu_dict_insert(PyDictObject *mp, PyObject *key, Py_hash_t hash, PyObject *value)
{
    int unicode = PyUnicode_CheckExact(key);
    if (mp->ma_keys == LU_EMPTY_KEYS) {
        PyDictKeysObject *dk = new_keys(PyDict_LOG_MINSIZE, unicode);
        if (dk == NULL) {
            return -1;
        }
        __atomic_store_n(&mp->ma_keys, dk, __ATOMIC_RELEASE);
    }
    else if (lu_dk_is_unicode(mp->ma_keys) && !unicode) {
        if (insertion_resize(mp, 0) < 0) {
            return -1;
        }
    }
    else if (mp->ma_keys->dk_usable <= 0 && insertion_resize(mp, lu_dk_is_unicode(mp->ma_keys)) < 0) {
        return -1;
    }
    lu_dict_notify(mp, PyDict_EVENT_ADDED, key, value);
    PyDictKeysObject *dk = mp->ma_keys;
    Py_ssize_t n = dk->dk_nentries;
    if (lu_dk_is_unicode(dk)) {
        ((PyDictUnicodeEntry *)lu_dk_entries(dk))[n] = (PyDictUnicodeEntry){ Py_NewRef(key), Py_NewRef(value) };
    }
    else {
        ((PyDictKeyEntry *)lu_dk_entries(dk))[n] = (PyDictKeyEntry){ hash, Py_NewRef(key), Py_NewRef(value) };
    }
    lu_dk_set_index(dk, find_empty_slot(dk, hash), n);
    dk->dk_version = 0;
    dk->dk_usable--;
    dk->dk_nentries = n + 1;
    __atomic_store_n(&mp->ma_used, mp->ma_used + 1, __ATOMIC_RELAXED);
    return 0;
}

void
lu_dict_replace(PyDictObject *mp, Py_ssize_t ix, PyObject *value)
{
    PyDictKeysObject *dk = mp->ma_keys;
    PyObject **slot = lu_dk_is_unicode(dk) ? &((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix].me_value
                                           : &((PyDictKeyEntry *)lu_dk_entries(dk))[ix].me_value;
    PyObject *old = *slot;
    if (old != value) {
        lu_dict_notify(mp, PyDict_EVENT_MODIFIED, lu_dk_key(dk, ix), value);
        __atomic_store_n(slot, Py_NewRef(value), __ATOMIC_RELEASE);
        Py_XDECREF(old);
    }
}

PyObject *
lu_dict_remove(PyDictObject *mp, Py_ssize_t ix)
{
    PyDictKeysObject *dk = mp->ma_keys;
    PyObject *key = lu_dk_key(dk, ix), *value = lu_dk_value(dk, ix);
    Py_hash_t hash = lu_dk_hash(dk, ix);
    lu_dict_notify(mp, PyDict_EVENT_DELETED, key, NULL);
    lu_dk_set_index(dk, index_slot(dk, hash, ix), DKIX_DUMMY);
    if (lu_dk_is_unicode(dk)) {
        ((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix] = (PyDictUnicodeEntry){ NULL, NULL };
    }
    else {
        ((PyDictKeyEntry *)lu_dk_entries(dk))[ix] = (PyDictKeyEntry){ -1, NULL, NULL };
    }
    dk->dk_version = 0;
    __atomic_store_n(&mp->ma_used, mp->ma_used - 1, __ATOMIC_RELAXED);
    Py_DECREF(key);
    return value;
}

PyObject *
lu_dict_popitem(PyDictObject *mp)
{
    PyDictKeysObject *dk = mp->ma_keys;
    Py_ssize_t ix = lu_dict_prev_entry(mp, dk->dk_nentries - 1);
    PyObject *pair = ix < 0 ? NULL : PyTuple_New(2);
    if (pair == NULL) {
        return NULL;
    }
    PyObject *key = Py_NewRef(lu_dk_key(dk, ix));
    PyObject *value = lu_dict_remove(mp, ix);
    dk->dk_nentries = ix;       /* the entries after it are deleted too */
    PyTuple_SET_ITEM(pair, 0, key);
    PyTuple_SET_ITEM(pair, 1, value);
    return pair;
}

void
lu_dict_clear(PyDictObject *mp)
{
    PyDictKeysObject *old = mp->ma_keys;
    if (old == LU_EMPTY_KEYS) {
        return;
    }
    lu_dict_notify(mp, PyDict_EVENT_CLEARED, NULL, NULL);
    __atomic_store_n(&mp->ma_keys, LU_EMPTY_KEYS, __ATOMIC_RELEASE);
    __atomic_store_n(&mp->ma_used, 0, __ATOMIC_RELAXED);
    for (Py_ssize_t i = 0; i < old->dk_nentries; i++) {
        Py_XDECREF(lu_dk_key(old, i));
        Py_XDECREF(lu_dk_value(old, i));
    }
    lu_qsbr_retire(old);
}

Py_ssize_t
lu_dict_next_entry(PyDictObject *mp, Py_ssize_t ix)
{
    PyDictKeysObject *dk = __atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE);
    for (; ix < dk->dk_nentries; ix++) {
        if (lu_dk_value(dk, ix) != NULL) {
            return ix;
        }
    }
    return -1;
}

Py_ssize_t
lu_dict_prev_entry(PyDictObject *mp, Py_ssize_t ix)
{
    PyDictKeysObject *dk = __atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE);
    if (ix >= dk->dk_nentries) {
        ix = dk->dk_nentries - 1;
    }
    for (; ix >= 0; ix--) {
        if (lu_dk_value(dk, ix) != NULL) {
            return ix;
        }
    }
    return -1;
}

int
lu_dict_str_keys(PyObject *dict)
{
    return lu_dk_is_unicode(((PyDictObject *)dict)->ma_keys);
}

/* Copying and merging. */

/* clone_combined_dict_keys: the same table, entries referenced again. */
static PyDictKeysObject *
clone_keys(PyDictKeysObject *keys)
{
    size_t size = keys_size(keys);
    PyDictKeysObject *dk = PyMem_Malloc(size);
    if (dk == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    memcpy(dk, keys, size);
    dk->dk_refcnt = 1;
    dk->dk_version = 0;
    for (Py_ssize_t i = 0; i < dk->dk_nentries; i++) {
        Py_XINCREF(lu_dk_key(dk, i));
        Py_XINCREF(lu_dk_value(dk, i));
    }
    return dk;
}

/* The case of dict_dict_merge that copies the table: mp is empty and the
   other is clean (no deleted entries) and just allocated. */
static int
can_clone(PyDictObject *other)
{
    PyDictKeysObject *okeys = other->ma_keys;
    return other->ma_used == okeys->dk_nentries
        && (okeys->dk_log2_size == PyDict_LOG_MINSIZE
            || USABLE_FRACTION(DK_SIZE(okeys) / 2) < other->ma_used);
}

static int
clone_into(PyDictObject *mp, PyDictObject *other)
{
    PyDictKeysObject *dk = clone_keys(other->ma_keys);
    if (dk == NULL) {
        return -1;
    }
    lu_dict_notify(mp, PyDict_EVENT_CLONED, (PyObject *)other, NULL);
    __atomic_store_n(&mp->ma_keys, dk, __ATOMIC_RELEASE);
    __atomic_store_n(&mp->ma_used, other->ma_used, __ATOMIC_RELAXED);
    return 0;
}

int
lu_dict_merge_prepare(PyDictObject *mp, PyDictObject *other)
{
    if (other == mp || other->ma_used == 0) {
        return 1;
    }
    if (mp->ma_used == 0 && mp->ma_keys == LU_EMPTY_KEYS && can_clone(other)) {
        return clone_into(mp, other) < 0 ? -1 : 1;
    }
    if (USABLE_FRACTION(DK_SIZE(mp->ma_keys)) < other->ma_used
        && resize(mp, estimate_log2_keysize(mp->ma_used + other->ma_used),
                  lu_dk_is_unicode(other->ma_keys)) < 0) {
        return -1;
    }
    return 0;
}

PyObject *
lu_dict_copy(PyDictObject *o)
{
    PyDictObject *copy = (PyDictObject *)PyDict_New();
    if (copy == NULL || o->ma_used == 0) {
        return (PyObject *)copy;
    }
    if (o->ma_used >= (o->ma_keys->dk_nentries * 2) / 3) {
        if (clone_into(copy, o) < 0) {
            Py_DECREF(copy);
            return NULL;
        }
        return (PyObject *)copy;
    }
    int done = lu_dict_merge_prepare(copy, o);
    for (Py_ssize_t ix = 0; done == 0 && (ix = lu_dict_next_entry(o, ix)) >= 0; ix++) {
        done = lu_dict_insert(copy, lu_dk_key(o->ma_keys, ix), lu_dk_hash(o->ma_keys, ix),
                              lu_dk_value(o->ma_keys, ix)) < 0 ? -1 : 0;
    }
    if (done < 0) {
        Py_CLEAR(copy);
    }
    return (PyObject *)copy;
}

Py_ssize_t
lu_dict_sizeof(PyDictObject *mp)
{
    Py_ssize_t size = Py_TYPE(mp)->tp_basicsize;
    return mp->ma_keys == LU_EMPTY_KEYS ? size : size + (Py_ssize_t)keys_size(mp->ma_keys);
}

/* The namespace primitives: exact-str keys, never raising for a lookup. */

static Py_ssize_t
ns_find(PyObject *dict, PyObject *key)
{
    Py_hash_t hash = lu_str_hash(key);
    Py_ssize_t ix = lu_dict_probe((PyDictObject *)dict, key, hash, -1);
    while (ix <= -3) {
        ix = lu_dict_probe((PyDictObject *)dict, key, hash, -3 - ix);
    }
    return ix;
}

PyObject *
lu_ns_lookup(PyObject *dict, PyObject *key)
{
    Py_ssize_t ix = ns_find(dict, key);
    return ix >= 0 ? lu_dk_value(((PyDictObject *)dict)->ma_keys, ix) : NULL;
}

int
lu_ns_insert(PyObject *dict, PyObject *key, PyObject *value)
{
    if (!PyUnicode_CheckExact(key)) {
        lu_err_format(PyExc_SystemError, "a namespace key must be an exact str, not '%s'",
                      Py_TYPE(key)->tp_name);
        return -1;
    }
    Py_ssize_t ix = ns_find(dict, key);
    if (ix >= 0) {
        lu_dict_replace((PyDictObject *)dict, ix, value);
        return 0;
    }
    return lu_dict_insert((PyDictObject *)dict, key, lu_str_hash(key), value);
}

int
lu_ns_delete(PyObject *dict, PyObject *key)
{
    Py_ssize_t ix = ns_find(dict, key);
    if (ix < 0) {
        return 0;
    }
    Py_DECREF(lu_dict_remove((PyDictObject *)dict, ix));
    return 1;
}

int
lu_ns_next(PyObject *dict, Py_ssize_t *pos, PyObject **key, PyObject **value)
{
    PyDictObject *mp = (PyDictObject *)dict;
    Py_ssize_t ix = lu_dict_next_entry(mp, *pos);
    if (ix < 0) {
        return 0;
    }
    *pos = ix + 1;
    *key = lu_dk_key(mp->ma_keys, ix);
    *value = lu_dk_value(mp->ma_keys, ix);
    return 1;
}
