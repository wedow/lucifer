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
    Py_ssize_t dk_refcnt;
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

#endif
