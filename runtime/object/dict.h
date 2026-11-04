/* The dict keys table (Include/internal/pycore_dict.h), defined once for the
   namespace primitives and the kernel's dict code (DESIGN 4.4). A combined
   table: dk_indices maps hash slots to entry numbers, and the entries that
   follow it keep insertion order. Unicode tables hold exact-str keys whose
   hash is cached in the str; general tables store the hash per entry. */
#ifndef LU_DICT_H
#define LU_DICT_H

#include "lucifer.h"

#define DKIX_EMPTY (-1)
#define DKIX_DUMMY (-2)
#define PyDict_LOG_MINSIZE 3
#define PyDict_MINSIZE 8

enum {
    DICT_KEYS_GENERAL = 0,
    DICT_KEYS_UNICODE = 1,
    DICT_KEYS_SPLIT = 2,
};

struct _dictkeysobject {
    Py_ssize_t dk_serial;               /* names the table: no other table of this runtime
                                           has it (the slot of CPython's dk_refcnt, which
                                           only shared split tables use) */
    uint8_t dk_log2_size;               /* dk_indices has 1 << dk_log2_size slots */
    uint8_t dk_log2_index_bytes;        /* and 1 << dk_log2_index_bytes bytes */
    uint8_t dk_kind;
    PyMutex dk_mutex;                   /* shared (split) keys */
    uint32_t dk_version;                /* 0 after any change */
    Py_ssize_t dk_usable;               /* entries left before a resize */
    Py_ssize_t dk_nentries;             /* entries used, including deleted */
    char dk_indices[];                  /* int8/16/32/64 slots, then the entries */
};

typedef struct {
    Py_hash_t me_hash;
    PyObject *me_key;
    PyObject *me_value;
} PyDictKeyEntry;

typedef struct {
    PyObject *me_key;                   /* an exact str */
    PyObject *me_value;
} PyDictUnicodeEntry;

#define DK_SIZE(dk) ((Py_ssize_t)1 << (dk)->dk_log2_size)
#define DK_MASK(dk) (DK_SIZE(dk) - 1)
#define USABLE_FRACTION(n) (((n) << 1) / 3)

static inline void *
lu_dk_entries(PyDictKeysObject *dk)
{
    return &dk->dk_indices[(size_t)1 << dk->dk_log2_index_bytes];
}

static inline Py_ssize_t
lu_dk_get_index(PyDictKeysObject *dk, size_t i)
{
    switch (dk->dk_log2_index_bytes - dk->dk_log2_size) {
    case 0: return ((int8_t *)dk->dk_indices)[i];
    case 1: return ((int16_t *)dk->dk_indices)[i];
    case 2: return ((int32_t *)dk->dk_indices)[i];
    default: return ((int64_t *)dk->dk_indices)[i];
    }
}

static inline void
lu_dk_set_index(PyDictKeysObject *dk, size_t i, Py_ssize_t ix)
{
    switch (dk->dk_log2_index_bytes - dk->dk_log2_size) {
    case 0: ((int8_t *)dk->dk_indices)[i] = (int8_t)ix; break;
    case 1: ((int16_t *)dk->dk_indices)[i] = (int16_t)ix; break;
    case 2: ((int32_t *)dk->dk_indices)[i] = (int32_t)ix; break;
    default: ((int64_t *)dk->dk_indices)[i] = ix; break;
    }
}

/* The object layer of dicts (runtime/object/dict.c), under both the
   namespace primitives and the kernel's dict code (runtime/prim/dict.c and
   lib/kernel/dict.py). Nothing here calls Python: a probe that meets a
   key it cannot compare natively hands the entry back, and the kernel
   compares it outside any commit region and probes again. */

#define LU_PROBE_CANDIDATE(ix) (-3 - (ix))      /* a probe's answer: compare entry ix */
#define LU_PROBE_CHANGED (-2)                   /* a lock-free probe's: redo it under the lock */
#define LU_EMPTY_KEYS ((PyDictKeysObject *)_PyRuntime.singletons.empty_dict_keys)

static inline int
lu_dk_is_unicode(PyDictKeysObject *dk)
{
    return dk->dk_kind != DICT_KEYS_GENERAL;
}

/* The address of entry ix's key and value. */
static inline PyObject **
lu_dk_key_slot(PyDictKeysObject *dk, Py_ssize_t ix)
{
    return lu_dk_is_unicode(dk) ? &((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix].me_key
                                : &((PyDictKeyEntry *)lu_dk_entries(dk))[ix].me_key;
}

static inline PyObject **
lu_dk_value_slot(PyDictKeysObject *dk, Py_ssize_t ix)
{
    return lu_dk_is_unicode(dk) ? &((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix].me_value
                                : &((PyDictKeyEntry *)lu_dk_entries(dk))[ix].me_value;
}

/* Entry ix of a combined table: its key, value (NULL once deleted) and hash. */
static inline PyObject *
lu_dk_key(PyDictKeysObject *dk, Py_ssize_t ix)
{
    return lu_dk_is_unicode(dk) ? ((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix].me_key
                                : ((PyDictKeyEntry *)lu_dk_entries(dk))[ix].me_key;
}

static inline PyObject *
lu_dk_value(PyDictKeysObject *dk, Py_ssize_t ix)
{
    return lu_dk_is_unicode(dk) ? ((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix].me_value
                                : ((PyDictKeyEntry *)lu_dk_entries(dk))[ix].me_value;
}

static inline Py_hash_t
lu_dk_hash(PyDictKeysObject *dk, Py_ssize_t ix)
{
    return lu_dk_is_unicode(dk) ? lu_str_hash(((PyDictUnicodeEntry *)lu_dk_entries(dk))[ix].me_key)
                                : ((PyDictKeyEntry *)lu_dk_entries(dk))[ix].me_hash;
}

/* The hash of entry ix whose key is k, which the caller holds: a lock-free
   reader must not read the key again from the entry. */
static inline Py_hash_t
lu_dk_hash_of(PyDictKeysObject *dk, Py_ssize_t ix, PyObject *k)
{
    return lu_dk_is_unicode(dk) ? lu_str_hash(k) : ((PyDictKeyEntry *)lu_dk_entries(dk))[ix].me_hash;
}

/* The entry number of `key` in mp, DKIX_EMPTY when it is absent, or
   LU_PROBE_CANDIDATE(ix) when entry ix has an equal hash and a key that only
   __eq__ can judge. `after` >= 0 resumes the probe past the candidate it
   named, which the caller found unequal. */
Py_ssize_t lu_dict_probe(PyDictObject *mp, PyObject *key, Py_hash_t hash, Py_ssize_t after);
/* A caller that finds an entry without the lock commits under the critical
   section with a stamp read before it looked: the serial of the table says
   that the entry numbers it found are still that table's (entries never move
   in a table, and a resize, clear or popitem names a new serial), and the
   entry count says which entries were added since. lu_dict_stamp_holds is
   enough to replace or remove an entry; to add a key it also takes the key's
   hash, and fails if an entry added since has it: the key may be that one.
   Under the critical section only. */
uint64_t lu_dict_stamp(PyDictObject *mp);
int lu_dict_stamp_holds(PyDictObject *mp, uint64_t stamp, Py_hash_t *absent_hash);
/* The serial part of the stamp, which a lookup that compares with __eq__
   uses to see its table replaced. */
#define LU_STAMP_SERIAL(stamp) ((stamp) >> 32)
/* Adds key (known to be absent) or replaces the value of entry ix. */
int lu_dict_insert(PyDictObject *mp, PyObject *key, Py_hash_t hash, PyObject *value);
void lu_dict_replace(PyDictObject *mp, Py_ssize_t ix, PyObject *value);
/* Deletes live entry ix; the old value is returned (new reference). */
PyObject *lu_dict_remove(PyDictObject *mp, Py_ssize_t ix);
/* Deletes the newest entry; the (key, value) pair is returned, or NULL when empty. */
PyObject *lu_dict_popitem(PyDictObject *mp);
void lu_dict_clear(PyDictObject *mp);
/* The next live entry number at or after ix, or -1. */
Py_ssize_t lu_dict_next_entry(PyDictObject *mp, Py_ssize_t ix);
/* The previous live entry number at or before ix, or -1. */
Py_ssize_t lu_dict_prev_entry(PyDictObject *mp, Py_ssize_t ix);
/* PyDict_Copy's table: an exact dict with the same entries. */
PyObject *lu_dict_copy(PyDictObject *mp);
/* The first step of updating mp from a dict: clones the table when mp is empty
   and the other is clean (1), else makes room for the entries (0); -1 on error. */
int lu_dict_merge_prepare(PyDictObject *mp, PyDictObject *other);
/* The entry number of exact-str `key` in a namespace dict, or -1. */
Py_ssize_t lu_ns_index(PyObject *dict, PyObject *key);
/* Lookup and store of an exact str or int key in any dict, decided without
   Python: find answers 1 with *value a new reference, 0 absent, or -1 when
   the key is another type or an equal-hash entry needs __eq__; put answers
   1 stored, -1 undecided, -2 with an exception set. Find holds no lock: it
   takes the reference only if the value is alive (lu_try_xget_ref) and else
   reads under the dict's critical section. */
int lu_dict_find_native(PyObject *d, PyObject *key, PyObject **value);
int lu_dict_put_native(PyObject *d, PyObject *key, PyObject *value);
/* A new dict of type tp with no entries. */
PyObject *lu_dict_alloc(PyTypeObject *tp);
Py_ssize_t lu_dict_sizeof(PyDictObject *mp);

#endif
