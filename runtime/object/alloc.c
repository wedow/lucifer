/* Memory allocators (Objects/obmalloc.c): PyMem_Raw* on libc, PyMem_* and
   PyObject_* on mimalloc, compiled into this file as CPython does.

   cp314t gives each PyThreadState its own mimalloc heaps (raw memory,
   objects, GC objects, GC objects with a preheader). Until thread states
   exist, PyMem_* and PyObject_* share mimalloc's default per-thread heap, and
   no page waits for a QSBR grace period. */

#include "Python.h"

/* What CPython's pycore_mimalloc.h configures. */
struct llist_node {             /* pycore_llist.h; embedded in mi_page_t */
    struct llist_node *next;
    struct llist_node *prev;
};

static inline void
llist_remove(struct llist_node *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node->next = NULL;
}
#define MI_PRIM_THREAD_ID   _Py_ThreadId
#define MI_VISIT_ABANDONED  1
#define MI_DEBUG_UNINIT     0xCD    /* PYMEM_CLEANBYTE */
#define MI_DEBUG_FREED      0xDD    /* PYMEM_DEADBYTE */
#define MI_DEBUG_PADDING    0xFD    /* PYMEM_FORBIDDENBYTE */
#define MI_NO_THP           1
#define MI_GUARDED          0
#define MI_DEBUG            0

#include "mimalloc.h"
#include "mimalloc/types.h"
#include "mimalloc/internal.h"

/* CPython's mimalloc page hooks, without QSBR: a free page is freed now. */
static void
_PyMem_mi_page_clear_qsbr(mi_page_t *page)
{
}

static bool
_PyMem_mi_page_is_safe_to_free(mi_page_t *page)
{
    return true;
}

static bool
_PyMem_mi_page_maybe_free(mi_page_t *page, mi_page_queue_t *pq, bool force)
{
    _mi_page_free(page, pq, force);
    return true;
}

static void
_PyMem_mi_page_reclaimed(mi_page_t *page)
{
}

static void
_PyMem_mi_heap_collect_qsbr(mi_heap_t *heap)
{
}

#include "static.c"

/* A preloaded extension's constructor runs before this executable's, and may
   allocate on worker threads. mimalloc takes any thread to be the main one
   until it is initialized, so the first worker would share the main heap.
   ld.so runs .preinit_array before every constructor, so initialize there. */
static void
init_mimalloc(void)
{
    mi_process_init();
}

__attribute__((section(".preinit_array"), used))
static void (*const init_mimalloc_entry)(void) = init_mimalloc;

void *
PyMem_RawMalloc(size_t size)
{
    if (size > (size_t)PY_SSIZE_T_MAX) {
        return NULL;
    }
    return malloc(size ? size : 1);
}

void *
PyMem_RawCalloc(size_t nelem, size_t elsize)
{
    if (elsize != 0 && nelem > (size_t)PY_SSIZE_T_MAX / elsize) {
        return NULL;
    }
    if (nelem == 0 || elsize == 0) {
        nelem = elsize = 1;
    }
    return calloc(nelem, elsize);
}

void *
PyMem_RawRealloc(void *ptr, size_t new_size)
{
    if (new_size > (size_t)PY_SSIZE_T_MAX) {
        return NULL;
    }
    return realloc(ptr, new_size ? new_size : 1);
}

void
PyMem_RawFree(void *ptr)
{
    free(ptr);
}

void *
PyMem_Malloc(size_t size)
{
    if (size > (size_t)PY_SSIZE_T_MAX) {
        return NULL;
    }
    return mi_malloc(size);
}

void *
PyMem_Calloc(size_t nelem, size_t elsize)
{
    if (elsize != 0 && nelem > (size_t)PY_SSIZE_T_MAX / elsize) {
        return NULL;
    }
    return mi_calloc(nelem, elsize);
}

void *
PyMem_Realloc(void *ptr, size_t new_size)
{
    if (new_size > (size_t)PY_SSIZE_T_MAX) {
        return NULL;
    }
    return mi_realloc(ptr, new_size);
}

void
PyMem_Free(void *ptr)
{
    mi_free(ptr);
}

void *
PyObject_Malloc(size_t size)
{
    return PyMem_Malloc(size);
}

void *
PyObject_Calloc(size_t nelem, size_t elsize)
{
    return PyMem_Calloc(nelem, elsize);
}

void *
PyObject_Realloc(void *ptr, size_t new_size)
{
    return PyMem_Realloc(ptr, new_size);
}

void
PyObject_Free(void *ptr)
{
    mi_free(ptr);
}
