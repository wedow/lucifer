/* mimalloc as CPython 3.14t configures it (Include/internal/pycore_mimalloc.h),
   and the per-(carrier, interpreter) state it needs (_mimalloc_thread_state).
   The mimalloc types are laid out by these macros, so every file that sees
   them includes this header, never mimalloc.h directly. */
#ifndef LU_MIMALLOC_H
#define LU_MIMALLOC_H

#include "Python.h"

struct llist_node {             /* pycore_llist.h; embedded in mi_page_t */
    struct llist_node *next;
    struct llist_node *prev;
};

static inline void
llist_init(struct llist_node *head)
{
    head->next = head->prev = head;
}

static inline int
llist_empty(struct llist_node *head)
{
    return head->next == head;
}

static inline void
llist_insert_tail(struct llist_node *head, struct llist_node *node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

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

/* CPython's _Py_mimalloc_heap_id. */
enum {
    LU_HEAP_MEM,        /* PyMem_Malloc() and friends; also the backing heap */
    LU_HEAP_OBJECT,     /* non-GC objects */
    LU_HEAP_GC,         /* GC objects without a pre-header */
    LU_HEAP_GC_PRE,     /* GC objects with a pre-header */
    LU_HEAP_COUNT
};

struct lu_mimalloc_state {
    mi_heap_t *current_object_heap;     /* where PyObject_Malloc allocates */
    mi_heap_t heaps[LU_HEAP_COUNT];
    mi_tld_t tld;
    struct llist_node page_list;        /* empty pages waiting for their QSBR goal */
};

#endif
