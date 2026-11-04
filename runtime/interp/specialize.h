/* Inline caches (DESIGN 6.1, FOUNDATIONS 2.4). A specializing instruction
   keeps its guard and its answer in the CACHE words after it, as in
   CPython's Python/specialize.c, and the opcode names the shape of the answer.
   The guards are tp_version_tag and dict keys versions, which change exactly
   when what was cached could be different; a guard that fails runs the
   generic instruction, whose counter decides when to try again. */
#ifndef LU_SPECIALIZE_H
#define LU_SPECIALIZE_H

#include "interp/frame.h"
#include "opcode_ids.h"

/* The shapes: X(specialized opcode, the generic opcode it stands for). */
#define LU_SPECIALIZED_OPCODES(X) \
    X(LOAD_GLOBAL_MODULE, LOAD_GLOBAL) \
    X(LOAD_GLOBAL_BUILTIN, LOAD_GLOBAL) \
    X(LOAD_ATTR_MODULE, LOAD_ATTR) \
    X(LOAD_ATTR_CLASS, LOAD_ATTR) \
    X(LOAD_ATTR_CLASS_WITH_METACLASS_CHECK, LOAD_ATTR) \
    X(LOAD_ATTR_WITH_HINT, LOAD_ATTR) \
    X(LOAD_ATTR_METHOD_NO_DICT, LOAD_ATTR) \
    X(LOAD_ATTR_METHOD_WITH_VALUES, LOAD_ATTR) \
    X(LOAD_ATTR_NONDESCRIPTOR_NO_DICT, LOAD_ATTR) \
    X(LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES, LOAD_ATTR) \
    X(LOAD_ATTR_SLOT, LOAD_ATTR) \
    X(STORE_ATTR_SLOT, STORE_ATTR) \
    X(STORE_ATTR_WITH_HINT, STORE_ATTR)

/* LOAD_GLOBAL's cache words (_PyLoadGlobalCache). */
enum {
    LG_COUNTER = 0,
    LG_MODULE_KEYS_VERSION = 1,
    LG_BUILTIN_KEYS_VERSION = 2,
    LG_INDEX = 3,
};

/* LOAD_ATTR's (_PyLoadMethodCache): a 32-bit version, the guard; a second
   32-bit word, a metaclass version or, for a keyed lookup, a 16-bit entry
   index; and a pointer to the answer. */
enum {
    LA_COUNTER = 0,
    LA_VERSION = 1,
    LA_KEYS_VERSION = 3,
    LA_INDEX = 3,
    LA_DESCR = 5,
};

/* The adaptive counter (pycore_backoff.h): the count in the high 12 bits, the
   backoff exponent in the low four. A counter below 16 is spent. */
#define LU_COUNTER_SPENT(c) ((c) < (1 << 4))
#define LU_COUNTER_STEP (1 << 4)

/* Words are written and read with relaxed atomics: a site is rewritten while
   other carriers run it. A reader takes a word pair or a pointer only after
   the version it checked, and re-reads that version and its own opcode
   afterwards (lu_site_unchanged); the writer makes the site generic, clears
   the version, writes the rest, sets the version, and sets the opcode last. */
static inline uint16_t
lu_cache_load(const lu_codeunit *w)
{
    return __atomic_load_n(&w->cache, __ATOMIC_RELAXED);
}

static inline void
lu_cache_store(lu_codeunit *w, uint16_t v)
{
    __atomic_store_n(&w->cache, v, __ATOMIC_RELAXED);
}

static inline uint32_t
lu_cache_load32(const lu_codeunit *w)
{
    return lu_cache_load(w) | (uint32_t)lu_cache_load(w + 1) << 16;
}

static inline void
lu_cache_store32(lu_codeunit *w, uint32_t v)
{
    lu_cache_store(w, (uint16_t)v);
    lu_cache_store(w + 1, (uint16_t)(v >> 16));
}

static inline void *
lu_cache_load_ptr(const lu_codeunit *w)
{
    uintptr_t p = 0;
    for (int i = 0; i < 4; i++) {
        p |= (uintptr_t)lu_cache_load(w + i) << (16 * i);
    }
    return (void *)p;
}

static inline void
lu_cache_store_ptr(lu_codeunit *w, const void *ptr)
{
    for (int i = 0; i < 4; i++) {
        lu_cache_store(w + i, (uint16_t)((uintptr_t)ptr >> (16 * i)));
    }
}

/* Whether the instruction at `site` is still `opcode`: no one has rewritten
   it since the reader took its opcode. Called after the reader has loaded the
   cache, with a re-read of the guard word. */
static inline int
lu_site_unchanged(const lu_codeunit *site, int opcode)
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return __atomic_load_n(&site->op.code, __ATOMIC_RELAXED) == opcode;
}

/* Tries to turn the generic LOAD_GLOBAL or LOAD_ATTR at `site` into a
   specialized one from what it has just met. Never fails an execution: a
   site that cannot be specialized backs off. */
void lu_specialize_load_global(PyObject *globals, PyObject *builtins, lu_codeunit *site, PyObject *name);
void lu_specialize_load_attr(PyObject *owner, lu_codeunit *site, PyObject *name);
void lu_specialize_store_attr(PyObject *owner, lu_codeunit *site, PyObject *name);

#endif
