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
#include "interp/exceptions.h"

#include <string.h>

#define PERTURB_SHIFT 5
#define GROWTH_RATE(mp) ((mp)->ma_used * 3)

/* Table serials: never reused, and dict.h says what they are for. */
static int64_t next_serial_counter;

static Py_ssize_t
next_serial(void)
{
    return (Py_ssize_t)(__atomic_add_fetch(&next_serial_counter, 1, __ATOMIC_RELAXED) & 0x7fffffff);
}

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
    dk->dk_serial = next_serial();
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
   _ma_watcher_tag (_PyDict_SendEvent). */
static const char *
event_name(PyDict_WatchEvent event)
{
    switch (event) {
    case PyDict_EVENT_ADDED: return "PyDict_EVENT_ADDED";
    case PyDict_EVENT_MODIFIED: return "PyDict_EVENT_MODIFIED";
    case PyDict_EVENT_DELETED: return "PyDict_EVENT_DELETED";
    case PyDict_EVENT_CLONED: return "PyDict_EVENT_CLONED";
    case PyDict_EVENT_CLEARED: return "PyDict_EVENT_CLEARED";
    case PyDict_EVENT_DEALLOCATED: return "PyDict_EVENT_DEALLOCATED";
    }
    return "PyDict_EVENT_?";
}

static void
send_event(int watcher_bits, PyDict_WatchEvent event, PyDictObject *mp, PyObject *key,
           PyObject *value)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    for (int i = 0; watcher_bits != 0; i++, watcher_bits >>= 1) {
        PyDict_WatchCallback cb = (PyDict_WatchCallback)interp->dict_watchers[i];
        if ((watcher_bits & 1) && cb != NULL && cb(event, (PyObject *)mp, key, value) < 0) {
            /* Not the dict as context: a hook could keep it alive. */
            char message[128];
            snprintf(message, sizeof message, "Exception ignored in %s watcher callback for <dict at %p>",
                     event_name(event), (void *)mp);
            lu_err_unraisable_text(message, NULL);
        }
    }
}

/* After a change to a dict compiled code relies on (dict watcher id 0):
   its keys (ix < 0), or the value of entry ix, named by its word's offset
   in the keys table, which only a change of keys moves. */
static void
changed(PyDictObject *mp, Py_ssize_t ix)
{
    if (__atomic_load_n(&mp->_ma_watcher_tag, __ATOMIC_ACQUIRE) & 1) {
        PyDictKeysObject *dk = mp->ma_keys;
        if (ix >= 0) {
            lu_jit_invalidate(LU_DEP_VALUE, (uintptr_t)mp, (uint32_t)((char *)lu_dk_value_slot(dk, ix) - (char *)dk));
        }
        else {
            lu_jit_invalidate(LU_DEP_KEYS, (uintptr_t)mp, 0);
        }
    }
}

void
lu_dict_notify(PyDictObject *mp, PyDict_WatchEvent event, PyObject *key, PyObject *value)
{
    int bits = (int)(__atomic_load_n(&mp->_ma_watcher_tag, __ATOMIC_ACQUIRE) & ((1 << LU_MAX_WATCHERS) - 1));
    if (bits) {
        send_event(bits, event, mp, key, value);
    }
}

/* The item calls of the dict API that watchers are tested through. They go
   to the dict's own implementation, which hashes and compares keys; a
   subclass's __setitem__ or __delitem__ is not consulted, as in CPython. */

int
PyDict_SetItem(PyObject *op, PyObject *key, PyObject *value)
{
    if (!PyDict_Check(op)) {
        lu_err_format(PyExc_SystemError, "bad argument to internal function");
        return -1;
    }
    PyObject *args[3] = { op, key, value };
    PyObject *r = lu_call_kernel(LU_K_dict_setitem, args, 3);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PyDict_DelItem(PyObject *op, PyObject *key)
{
    if (!PyDict_Check(op)) {
        lu_err_format(PyExc_SystemError, "bad argument to internal function");
        return -1;
    }
    PyObject *args[2] = { op, key };
    PyObject *r = lu_call_kernel(LU_K_dict_delitem, args, 2);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

void
PyDict_Clear(PyObject *op)
{
    if (PyDict_Check(op)) {
        lu_dict_clear((PyDictObject *)op);
    }
}

/* The watcher API (dictobject.c). Id 0 is CPython's own, in use from startup
   (its optimizer watches globals); id 1 is reserved. */

static lu_watcher *
dict_watchers(void)
{
    return PyInterpreterState_Get()->dict_watchers;
}

int
PyDict_AddWatcher(PyDict_WatchCallback callback)
{
    return lu_watcher_add(dict_watchers(), 2, (lu_watcher)callback, "dict");
}

static int
check_dict_watcher(int id)
{
    return lu_watcher_check(dict_watchers(), id, "Invalid dict watcher ID %d",
                            "No dict watcher set for ID %d");
}

int
PyDict_ClearWatcher(int id)
{
    if (check_dict_watcher(id) < 0) {
        return -1;
    }
    dict_watchers()[id] = NULL;
    return 0;
}

int
PyDict_Watch(int id, PyObject *dict)
{
    if (!PyDict_Check(dict)) {
        lu_err_format(PyExc_ValueError, "Cannot watch non-dictionary");
        return -1;
    }
    if (check_dict_watcher(id) < 0) {
        return -1;
    }
    __atomic_or_fetch(&((PyDictObject *)dict)->_ma_watcher_tag, 1ULL << id, __ATOMIC_RELEASE);
    return 0;
}

int
PyDict_Unwatch(int id, PyObject *dict)
{
    if (!PyDict_Check(dict)) {
        lu_err_format(PyExc_ValueError, "Cannot watch non-dictionary");
        return -1;
    }
    if (check_dict_watcher(id) < 0) {
        return -1;
    }
    __atomic_and_fetch(&((PyDictObject *)dict)->_ma_watcher_tag, ~(1ULL << id), __ATOMIC_RELEASE);
    return 0;
}

/* Keys versions (get_next_dict_keys_version). A cache word holds 16 bits, so
   an asker that stores it there gets 0 past 65535, as CPython's specializer
   does; the shared empty table is never versioned. */
uint32_t
lu_dict_keys_version(PyDictObject *mp)
{
    PyDictKeysObject *dk = __atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE);
    if (dk == LU_EMPTY_KEYS) {
        return 0;
    }
    uint32_t version = __atomic_load_n(&dk->dk_version, __ATOMIC_RELAXED);
    if (version != 0) {
        return version;
    }
    uint32_t *next = &PyInterpreterState_Get()->next_keys_version;
    version = __atomic_load_n(next, __ATOMIC_RELAXED);
    do {
        if (version == 0 || version > UINT16_MAX) {
            return 0;
        }
    } while (!__atomic_compare_exchange_n(next, &version, version + 1, 0,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    __atomic_store_n(&dk->dk_version, version, __ATOMIC_RELEASE);
    return version;
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

static int
dict_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyDictKeysObject *dk = ((PyDictObject *)op)->ma_keys;
    if (dk == LU_EMPTY_KEYS) {
        return 0;
    }
    for (Py_ssize_t i = 0; i < dk->dk_nentries; i++) {
        Py_VISIT(lu_dk_key(dk, i));
        Py_VISIT(lu_dk_value(dk, i));
    }
    return 0;
}

static int
dict_clear(PyObject *op)
{
    lu_dict_clear((PyDictObject *)op);
    return 0;
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
    dk->dk_log2_size = dk->dk_log2_index_bytes = 3;
    dk->dk_kind = DICT_KEYS_UNICODE;
    memset(dk->dk_indices, 0xff, PyDict_MINSIZE);
    PyDict_Type.tp_basicsize = sizeof(PyDictObject);
    PyDict_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyDict_Type.tp_dealloc = dict_dealloc;
    PyDict_Type.tp_traverse = dict_traverse;
    PyDict_Type.tp_clear = dict_clear;
}

/* Probing. */

/* How deep native hashing and equality follow nested tuples. */
#define LU_NATIVE_DEPTH 16

/* Whether two distinct keys of an equal hash are equal, when the answer needs
   no Python: 1, 0, or -1 when __eq__ must decide. Exact strs, ints, floats
   and bytes compare by value, None and classes by identity, tuples item by
   item (an identical pair of items is equal); an exact int and float are
   left to their methods, and any other pair of those types is unequal. */
enum { EQ_STR, EQ_INT, EQ_FLOAT, EQ_BYTES, EQ_TUPLE, EQ_IDENT, EQ_OTHER };

static int
eq_class(PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(v);
    return tp == &PyUnicode_Type ? EQ_STR
         : tp == &PyLong_Type || tp == &PyBool_Type ? EQ_INT
         : tp == &PyFloat_Type ? EQ_FLOAT
         : tp == &PyBytes_Type ? EQ_BYTES
         : tp == &PyTuple_Type ? EQ_TUPLE
         : v == Py_None || tp == &PyType_Type ? EQ_IDENT : EQ_OTHER;
}

static int
native_eq(PyObject *a, PyObject *b, int depth)
{
    int ca = eq_class(a), cb = eq_class(b);
    if (ca == EQ_OTHER || cb == EQ_OTHER) {
        return -1;
    }
    if (ca != cb) {
        return (ca == EQ_INT && cb == EQ_FLOAT) || (ca == EQ_FLOAT && cb == EQ_INT) ? -1 : 0;
    }
    if (ca == EQ_IDENT) {
        return a == b;
    }
    if (ca != EQ_TUPLE) {
        return lu_eq(a, b);
    }
    if (depth >= LU_NATIVE_DEPTH) {
        return -1;
    }
    Py_ssize_t n = Py_MIN(Py_SIZE(a), Py_SIZE(b));
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *x = PyTuple_GET_ITEM(a, i), *y = PyTuple_GET_ITEM(b, i);
        int eq = x == y ? 1 : native_eq(x, y, depth + 1);
        if (eq != 1) {
            return eq;
        }
    }
    return Py_SIZE(a) == Py_SIZE(b);
}

int
lu_native_eq(PyObject *a, PyObject *b)
{
    return native_eq(a, b, 0);
}

/* The probe of lu_dict_probe in one keys table. Without the lock a concurrent
   deletion may have cleared an entry (its key is NULL: passed over like any
   unequal key) or be freeing its key, which a probe reads only after a
   try-incref; a key that is already gone answers LU_PROBE_CHANGED and the
   caller probes again under the lock, as CPython's DKIX_KEY_CHANGED.
   The first *steps slots of the probe sequence were judged already and are
   passed by position, since the sequence can visit a slot twice; a
   candidate answer sets *steps to its own position. */
static Py_ssize_t
probe_keys(PyDictKeysObject *dk, PyObject *key, Py_hash_t hash, Py_ssize_t *steps, int locked)
{
    size_t mask = DK_MASK(dk);
    size_t perturb = hash;
    size_t i = (size_t)hash & mask;
    for (Py_ssize_t step = 1;; step++) {
        Py_ssize_t ix = lu_dk_get_index(dk, i);
        if (ix >= 0) {
            if (step > *steps) {
                PyObject **slot = lu_dk_key_slot(dk, ix);
                PyObject *k = _Py_atomic_load_ptr(slot);
                if (k == key) {
                    return ix;
                }
                if (k != NULL) {
                    int held = !locked && !_Py_IsImmortal(k);
                    if (held && !lu_try_incref_compare(slot, k)) {
                        return LU_PROBE_CHANGED;
                    }
                    int eq = 0;
                    if (lu_dk_hash_of(dk, ix, k) == hash) {
                        eq = lu_native_eq(k, key);
                    }
                    if (held) {
                        Py_DECREF(k);
                    }
                    if (eq > 0) {
                        return ix;
                    }
                    if (eq < 0) {
                        *steps = step;
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

Py_ssize_t
lu_dict_probe(PyDictObject *mp, PyObject *key, Py_hash_t hash, Py_ssize_t *steps)
{
    Py_ssize_t ix = probe_keys(__atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE), key, hash, steps, 0);
    if (ix == LU_PROBE_CHANGED) {
        Py_BEGIN_CRITICAL_SECTION(mp);
        ix = probe_keys(mp->ma_keys, key, hash, steps, 1);
        Py_END_CRITICAL_SECTION();
    }
    return ix;
}

/* The hash of a key that needs no Python: an exact str, int, bool, float or
   bytes, None, a class (object's hash, equal only to itself), or a tuple of
   those. 0 when Python must hash it. */
static int
hash_native(PyObject *key, Py_hash_t *hash, int depth)
{
    PyTypeObject *kt = Py_TYPE(key);
    if (kt == &PyUnicode_Type || kt == &PyLong_Type || kt == &PyBool_Type || kt == &PyFloat_Type
        || kt == &PyBytes_Type || key == Py_None) {
        *hash = lu_hash(key);
    }
    else if (kt == &PyType_Type) {
        *hash = Py_HashPointer(key);
    }
    else if (kt == &PyTuple_Type && depth < LU_NATIVE_DEPTH) {
        PyTupleObject *t = (PyTupleObject *)key;
        if (t->ob_hash == -1) {
            Py_uhash_t acc = LU_XXHASH_START;
            for (Py_ssize_t i = 0; i < Py_SIZE(t); i++) {
                Py_hash_t lane;
                if (!hash_native(t->ob_item[i], &lane, depth + 1)) {
                    return 0;
                }
                acc = lu_xxhash_step(acc, lane);
            }
            t->ob_hash = lu_xxhash_end(acc, Py_SIZE(t), 1);
        }
        *hash = t->ob_hash;
    }
    else {
        return 0;
    }
    return 1;
}

static int
native_hash(PyObject *key, Py_hash_t *hash)
{
    return hash_native(key, hash, 0);
}

/* The entry number of a native key in the table dk: -1 when absent, -2 when
   Python must judge, -3 when a lock-free probe must be redone under the lock. */
static Py_ssize_t
probe_native(PyDictKeysObject *dk, PyObject *key, Py_hash_t *hash, int locked)
{
    if (!native_hash(key, hash)) {
        return -2;
    }
    Py_ssize_t steps = 0;
    Py_ssize_t ix = probe_keys(dk, key, *hash, &steps, locked);
    return ix == LU_PROBE_CHANGED ? -3 : ix <= -3 ? -2 : ix;
}

int
lu_dict_find_native(PyObject *d, PyObject *key, PyObject **value)
{
    PyDictObject *mp = (PyDictObject *)d;
    Py_hash_t hash;
    /* One table for the probe and the read, which a resize may replace. */
    PyDictKeysObject *dk = __atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE);
    Py_ssize_t ix = probe_native(dk, key, &hash, 0);
    if (ix == -1) {
        return 0;
    }
    if (ix == -2) {
        return -1;
    }
    if (ix >= 0) {
        PyObject *v = lu_try_xget_ref(lu_dk_value_slot(dk, ix));
        if (v != NULL && dk == __atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE)) {
            *value = v;
            return 1;
        }
        Py_XDECREF(v);
    }
    int result = -1;
    Py_BEGIN_CRITICAL_SECTION(d);
    dk = mp->ma_keys;
    ix = probe_native(dk, key, &hash, 1);
    if (ix == -1) {
        result = 0;
    }
    else if (ix >= 0 && *lu_dk_value_slot(dk, ix) != NULL) {
        *value = lu_new_ref_with_lock(*lu_dk_value_slot(dk, ix));
        result = 1;
    }
    Py_END_CRITICAL_SECTION();
    return result;
}

int
lu_dict_put_native(PyObject *d, PyObject *key, PyObject *value)
{
    int result = -1;
    Py_hash_t hash;
    Py_BEGIN_CRITICAL_SECTION(d);
    Py_ssize_t ix = probe_native(((PyDictObject *)d)->ma_keys, key, &hash, 1);
    if (ix >= 0) {
        lu_dict_replace((PyDictObject *)d, ix, value);
        result = 1;
    }
    else if (ix == -1) {
        result = lu_dict_insert((PyDictObject *)d, key, hash, value) < 0 ? -2 : 1;
    }
    Py_END_CRITICAL_SECTION();
    return result;
}

int
lu_dict_del_native(PyObject *d, PyObject *key)
{
    PyObject *old = NULL;
    Py_hash_t hash;
    Py_BEGIN_CRITICAL_SECTION(d);
    Py_ssize_t ix = probe_native(((PyDictObject *)d)->ma_keys, key, &hash, 1);
    if (ix >= 0) {
        old = lu_dict_remove((PyDictObject *)d, ix);
    }
    Py_END_CRITICAL_SECTION();
    Py_XDECREF(old);
    return old != NULL ? 1 : -1;
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
    changed(mp, -1);
    return 0;
}

uint64_t
lu_dict_stamp(PyDictObject *mp)
{
    PyDictKeysObject *dk = __atomic_load_n(&mp->ma_keys, __ATOMIC_ACQUIRE);
    return ((uint64_t)dk->dk_serial << 32) | (uint64_t)dk->dk_nentries;
}

int
lu_dict_stamp_holds(PyDictObject *mp, uint64_t stamp, Py_hash_t *absent_hash)
{
    PyDictKeysObject *dk = mp->ma_keys;
    if (LU_STAMP_SERIAL((uint64_t)dk->dk_serial << 32) != LU_STAMP_SERIAL(stamp)) {
        return 0;
    }
    /* A key is absent still if none added since has its hash. */
    for (Py_ssize_t ix = (Py_ssize_t)(stamp & 0xffffffff); absent_hash != NULL && ix < dk->dk_nentries; ix++) {
        if (lu_dk_key(dk, ix) != NULL && lu_dk_hash(dk, ix) == *absent_hash) {
            return 0;
        }
    }
    return 1;
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
        changed(mp, ix);
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
    changed(mp, -1);
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
    dk->dk_serial = next_serial();  /* the numbers it frees are used again */
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
    changed(mp, -1);
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
    dk->dk_serial = next_serial();
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
    changed(mp, -1);
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
    Py_ssize_t steps = 0, ix;
    do {
        ix = lu_dict_probe((PyDictObject *)dict, key, hash, &steps);
    } while (ix <= -3);
    return ix;
}

Py_ssize_t
lu_ns_index(PyObject *dict, PyObject *key)
{
    Py_ssize_t ix = ns_find(dict, key);
    return ix >= 0 ? ix : -1;
}

PyObject *
lu_ns_lookup(PyObject *dict, PyObject *key)
{
    /* A name that could not be made (out of memory, the error set) is in
       no namespace: callers look up a fresh PyUnicode_InternFromString. */
    if (key == NULL) {
        return NULL;
    }
    Py_ssize_t ix = ns_find(dict, key);
    return ix >= 0 ? lu_dk_value(((PyDictObject *)dict)->ma_keys, ix) : NULL;
}

PyObject *
lu_ns_get(PyObject *dict, PyObject *key)
{
    PyObject *value;
    return lu_dict_find_native(dict, key, &value) > 0 ? value : NULL;
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
lu_ns_store(PyObject *dict, PyObject *key, PyObject *value)
{
    int err;
    Py_BEGIN_CRITICAL_SECTION(dict);
    err = lu_ns_insert(dict, key, value);
    Py_END_CRITICAL_SECTION();
    return err;
}

int
lu_ns_delete(PyObject *dict, PyObject *key)
{
    int found = 0;
    Py_BEGIN_CRITICAL_SECTION(dict);
    Py_ssize_t ix = ns_find(dict, key);
    if (ix >= 0) {
        Py_DECREF(lu_dict_remove((PyDictObject *)dict, ix));
        found = 1;
    }
    Py_END_CRITICAL_SECTION();
    return found;
}

int
lu_ns_exchange(PyObject *dict, PyObject *key, PyObject *value, PyObject **old)
{
    int r;
    Py_BEGIN_CRITICAL_SECTION(dict);
    Py_ssize_t ix = ns_find(dict, key);
    *old = ix >= 0 ? Py_NewRef(lu_dk_value(((PyDictObject *)dict)->ma_keys, ix)) : NULL;
    if (value != NULL) {
        r = lu_ns_insert(dict, key, value) < 0 ? -1 : 1;
    }
    else {
        r = ix >= 0;
        if (r) {
            Py_DECREF(lu_dict_remove((PyDictObject *)dict, ix));
        }
    }
    Py_END_CRITICAL_SECTION();
    return r;
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
