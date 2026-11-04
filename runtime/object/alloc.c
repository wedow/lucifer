/* Memory allocators (Objects/obmalloc.c). The three domains are tables in
   _PyRuntime that PyMem_SetAllocator replaces. The raw domain defaults to
   libc; mem and obj to the calling carrier's mimalloc heaps, as cp314t keeps
   them per thread state: raw memory, objects, GC objects, and GC objects with
   a pre-header. mimalloc is compiled into this file as CPython does.

   A free page of the object heaps is freed after a QSBR grace period. */

#include "lucifer.h"

/* CPython's mimalloc page hooks (defined below, after mimalloc's own code,
   whose private functions they use). */
static void _PyMem_mi_page_clear_qsbr(mi_page_t *page);
static bool _PyMem_mi_page_is_safe_to_free(mi_page_t *page);
static bool _PyMem_mi_page_maybe_free(mi_page_t *page, mi_page_queue_t *pq, bool force);
static void _PyMem_mi_page_reclaimed(mi_page_t *page);
static void _PyMem_mi_heap_collect_qsbr(mi_heap_t *heap);

#include "static.c"

/* CPython's mimalloc page hooks (Objects/obmalloc.c). A lock-free reader may
   hold a pointer to a freed object and try to take a reference to it (see
   lu_try_incref_compare), so the page of an object must not be reused for
   anything else until every reader has passed a poll: an empty page of the
   object heaps waits for a QSBR goal on its carrier's list before it is freed. */

#define QSBR_PAGE_MEM_LIMIT (4096 * 20)

static struct lu_entry *
entry_of_heap(mi_heap_t *heap)
{
    return (struct lu_entry *)((char *)heap->tld - offsetof(struct lu_entry, mimalloc.tld));
}

static void
_PyMem_mi_page_clear_qsbr(mi_page_t *page)
{
    page->qsbr_goal = 0;
    if (page->qsbr_node.next != NULL) {
        llist_remove(&page->qsbr_node);
    }
}

/* An empty, newly reclaimed page is safe to free now. */
static bool
_PyMem_mi_page_is_safe_to_free(mi_page_t *page)
{
    if (page->use_qsbr && page->qsbr_goal != 0) {
        return lu_qsbr_goal_reached(page->qsbr_goal);
    }
    return true;
}

/* Whether the write sequence should move on for this page: when much memory
   waits, so that it can be reused in another size class. */
static int
should_advance_for_page(struct lu_entry *entry, mi_page_t *page)
{
    struct lu_qsbr *q = &entry->carrier->qsbr;
    size_t page_size = page->capacity * mi_page_block_size(page);
    if (page_size > QSBR_PAGE_MEM_LIMIT) {
        q->deferred_page_memory = 0;
        return 1;
    }
    q->deferred_page_memory += page_size;
    if (q->deferred_page_memory > QSBR_PAGE_MEM_LIMIT) {
        q->deferred_page_memory = 0;
        return 1;
    }
    return 0;
}

static bool
_PyMem_mi_page_maybe_free(mi_page_t *page, mi_page_queue_t *pq, bool force)
{
    /* The process's default heap, which reclaims pages at exit, is not ours. */
    if (page->use_qsbr && mi_page_heap(page)->page_use_qsbr) {
        struct lu_entry *entry = lu_current_entry();
        if (page->qsbr_goal != 0 && lu_qsbr_goal_reached(page->qsbr_goal)) {
            _PyMem_mi_page_clear_qsbr(page);
            _mi_page_free(page, pq, force);
            return true;
        }
        /* Not freed yet: keep it available for allocations. */
        if (mi_page_is_in_full(page)) {
            _mi_page_unfull(page);
        }
        _PyMem_mi_page_clear_qsbr(page);
        page->retire_expire = 0;
        page->qsbr_goal = should_advance_for_page(entry, page) ? lu_qsbr_advance() : lu_qsbr_next_goal();
        /* In a stop-the-world the page may belong to another carrier. */
        llist_insert_tail(&entry_of_heap(mi_page_heap(page))->mimalloc.page_list, &page->qsbr_node);
        return false;
    }
    _PyMem_mi_page_clear_qsbr(page);
    _mi_page_free(page, pq, force);
    return true;
}

static void
_PyMem_mi_page_reclaimed(mi_page_t *page)
{
    if (page->qsbr_goal != 0) {
        if (!mi_page_heap(page)->page_use_qsbr) {
            page->qsbr_goal = 0;
        }
        else if (mi_page_all_free(page)) {
            page->retire_expire = 0;
            llist_insert_tail(&entry_of_heap(mi_page_heap(page))->mimalloc.page_list, &page->qsbr_node);
        }
        else {
            page->qsbr_goal = 0;
        }
    }
}

static void
_PyMem_mi_heap_collect_qsbr(mi_heap_t *heap)
{
    if (!heap->page_use_qsbr) {
        return;
    }
    struct llist_node *head = &lu_current_entry()->mimalloc.page_list;
    for (struct llist_node *node = head->next, *next; node != head; node = next) {
        next = node->next;
        mi_page_t *page = (mi_page_t *)((char *)node - offsetof(mi_page_t, qsbr_node));
        if (!mi_page_all_free(page)) {
            /* Allocated from again since the delayed free. */
            _PyMem_mi_page_clear_qsbr(page);
            continue;
        }
        if (!lu_qsbr_goal_reached(page->qsbr_goal)) {
            return;
        }
        _PyMem_mi_page_clear_qsbr(page);
        _mi_page_free(page, mi_page_queue_of(page), false);
    }
}


/* Default hooks. */

void *
lu_raw_malloc(void *ctx, size_t size)
{
    return malloc(size ? size : 1);
}

void *
lu_raw_calloc(void *ctx, size_t nelem, size_t elsize)
{
    if (nelem == 0 || elsize == 0) {
        nelem = elsize = 1;
    }
    return calloc(nelem, elsize);
}

void *
lu_raw_realloc(void *ctx, void *ptr, size_t size)
{
    return realloc(ptr, size ? size : 1);
}

void
lu_raw_free(void *ctx, void *ptr)
{
    free(ptr);
}

void *
lu_mem_malloc(void *ctx, size_t size)
{
    return mi_heap_malloc(&lu_current_entry()->mimalloc.heaps[LU_HEAP_MEM], size);
}

void *
lu_mem_calloc(void *ctx, size_t nelem, size_t elsize)
{
    return mi_heap_calloc(&lu_current_entry()->mimalloc.heaps[LU_HEAP_MEM], nelem, elsize);
}

void *
lu_mem_realloc(void *ctx, void *ptr, size_t size)
{
    return mi_heap_realloc(&lu_current_entry()->mimalloc.heaps[LU_HEAP_MEM], ptr, size);
}

void
lu_mem_free(void *ctx, void *ptr)
{
    mi_free(ptr);
}

void *
lu_obj_malloc(void *ctx, size_t size)
{
    return mi_heap_malloc(lu_current_entry()->mimalloc.current_object_heap, size);
}

void *
lu_obj_calloc(void *ctx, size_t nelem, size_t elsize)
{
    return mi_heap_calloc(lu_current_entry()->mimalloc.current_object_heap, nelem, elsize);
}

void *
lu_obj_realloc(void *ctx, void *ptr, size_t size)
{
    return mi_heap_realloc(lu_current_entry()->mimalloc.current_object_heap, ptr, size);
}

void
lu_obj_free(void *ctx, void *ptr)
{
    mi_free(ptr);
}

/* Per-carrier heaps: tstate_mimalloc_bind and _PyThreadState_ClearMimallocHeaps
   in Python/pystate.c. */

void
lu_mimalloc_bind(struct lu_entry *entry)
{
    struct lu_mimalloc_state *m = &entry->mimalloc;
    /* The mem heap doubles as the backing heap. */
    _mi_tld_init(&m->tld, &m->heaps[LU_HEAP_MEM]);
    m->tld.segments.subproc = _mi_subproc_from_id(entry->interp->subproc);

    /* Do not fill in the bytes up to ob_type when debugging: lock-free readers
       may still read ob_tid and the refcount of a freed object. */
    size_t base = offsetof(PyObject, ob_type);
    size_t debug_offsets[LU_HEAP_COUNT] = {
        [LU_HEAP_OBJECT] = base,
        [LU_HEAP_GC] = base,
        [LU_HEAP_GC_PRE] = base + 2 * sizeof(PyObject *),
    };
    for (uint8_t i = 0; i < LU_HEAP_COUNT; i++) {
        /* The page tag is the heap id: the GC visits blocks by tag. */
        _mi_heap_init(&m->heaps[i], &m->tld, _mi_arena_id_none(), false, i);
        m->heaps[i].debug_offset = (uint8_t)debug_offsets[i];
    }
    llist_init(&m->page_list);
    m->heaps[LU_HEAP_OBJECT].page_use_qsbr = true;
    m->heaps[LU_HEAP_GC].page_use_qsbr = true;
    m->heaps[LU_HEAP_GC_PRE].page_use_qsbr = true;
    m->current_object_heap = &m->heaps[LU_HEAP_OBJECT];
}

/* Hand every page still in use to the pool, so that objects stay visible to
   the GC and other carriers can reuse the pages. */
void
lu_mimalloc_abandon(struct lu_entry *entry)
{
    for (int i = 0; i < LU_HEAP_COUNT; i++) {
        _mi_heap_collect_abandon(&entry->mimalloc.heaps[i]);
    }
}

/* _PyObject_MallocWithType, _PyObject_GetAllocationHeap and gc_alloc. */
void *
lu_gc_alloc(PyTypeObject *tp, size_t size, size_t presize)
{
    if (size > (size_t)PY_SSIZE_T_MAX - presize) {
        return NULL;
    }
    struct lu_entry *e = lu_current_entry();
    struct lu_mimalloc_state *m = &e->mimalloc;
    m->current_object_heap =
        &m->heaps[(tp->tp_flags & Py_TPFLAGS_PREHEADER) ? LU_HEAP_GC_PRE : LU_HEAP_GC];
    char *mem = PyObject_Malloc(presize + size);
    m->current_object_heap = &m->heaps[LU_HEAP_OBJECT];
    if (mem == NULL) {
        return NULL;
    }
    if (presize) {
        ((PyObject **)mem)[0] = NULL;
        ((PyObject **)mem)[1] = NULL;
    }
    lu_gc_record_allocation(e);
    if (_PyRuntime.stress.gc != 0 && ++e->stress_count >= _PyRuntime.stress.gc) {
        e->stress_count = 0;
        lu_poll_set(e->carrier, LU_POLL_STRESS_TICK);   /* a collection, from M0.2 */
    }
    return mem + presize;
}

/* The public entry points. */

void
PyMem_GetAllocator(PyMemAllocatorDomain domain, PyMemAllocatorEx *allocator)
{
    pthread_mutex_lock(&_PyRuntime.allocators.lock.mutex);
    switch (domain) {
    case PYMEM_DOMAIN_RAW: *allocator = _PyRuntime.allocators.raw; break;
    case PYMEM_DOMAIN_MEM: *allocator = _PyRuntime.allocators.mem; break;
    case PYMEM_DOMAIN_OBJ: *allocator = _PyRuntime.allocators.obj; break;
    default: memset(allocator, 0, sizeof *allocator);
    }
    pthread_mutex_unlock(&_PyRuntime.allocators.lock.mutex);
}

void
PyMem_SetAllocator(PyMemAllocatorDomain domain, PyMemAllocatorEx *allocator)
{
    pthread_mutex_lock(&_PyRuntime.allocators.lock.mutex);
    switch (domain) {
    case PYMEM_DOMAIN_RAW: _PyRuntime.allocators.raw = *allocator; break;
    case PYMEM_DOMAIN_MEM: _PyRuntime.allocators.mem = *allocator; break;
    case PYMEM_DOMAIN_OBJ: _PyRuntime.allocators.obj = *allocator; break;
    default: break;
    }
    pthread_mutex_unlock(&_PyRuntime.allocators.lock.mutex);
}

#define LU_DOMAIN_ENTRY_POINTS(domain, malloc_name, calloc_name, realloc_name, free_name) \
void * \
malloc_name(size_t size) \
{ \
    if (size > (size_t)PY_SSIZE_T_MAX) { \
        return NULL; \
    } \
    return _PyRuntime.allocators.domain.malloc(_PyRuntime.allocators.domain.ctx, size); \
} \
\
void * \
calloc_name(size_t nelem, size_t elsize) \
{ \
    if (elsize != 0 && nelem > (size_t)PY_SSIZE_T_MAX / elsize) { \
        return NULL; \
    } \
    return _PyRuntime.allocators.domain.calloc(_PyRuntime.allocators.domain.ctx, \
                                               nelem, elsize); \
} \
\
void * \
realloc_name(void *ptr, size_t new_size) \
{ \
    if (new_size > (size_t)PY_SSIZE_T_MAX) { \
        return NULL; \
    } \
    return _PyRuntime.allocators.domain.realloc(_PyRuntime.allocators.domain.ctx, \
                                                ptr, new_size); \
} \
\
void \
free_name(void *ptr) \
{ \
    _PyRuntime.allocators.domain.free(_PyRuntime.allocators.domain.ctx, ptr); \
}

LU_DOMAIN_ENTRY_POINTS(raw, PyMem_RawMalloc, PyMem_RawCalloc, PyMem_RawRealloc, PyMem_RawFree)
LU_DOMAIN_ENTRY_POINTS(mem, PyMem_Malloc, PyMem_Calloc, PyMem_Realloc, PyMem_Free)
LU_DOMAIN_ENTRY_POINTS(obj, PyObject_Malloc, PyObject_Calloc, PyObject_Realloc, PyObject_Free)
