/* Memory allocators (Objects/obmalloc.c). The three domains are tables in
   _PyRuntime that PyMem_SetAllocator replaces. The raw domain defaults to
   libc; mem and obj to the calling carrier's mimalloc heaps, as cp314t keeps
   them per thread state: raw memory, objects, GC objects, and GC objects with
   a pre-header. mimalloc is compiled into this file as CPython does.

   There is no QSBR yet: a free page is freed at once, not after a grace
   period. */

#include "lucifer.h"

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

/* Default hooks. */

void *
lu_raw_malloc(void *ctx, size_t size)
{
    return malloc(size ? size : 1);
}

void *
lu_raw_calloc(void *ctx, size_t nelem, size_t elsize)
{
    return calloc(nelem ? nelem : 1, elsize ? elsize : 1);
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
    struct lu_mimalloc_state *m = &lu_current_entry()->mimalloc;
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
    return mem + presize;
}

/* The public entry points. */

void
PyMem_GetAllocator(PyMemAllocatorDomain domain, PyMemAllocatorEx *allocator)
{
    switch (domain) {
    case PYMEM_DOMAIN_RAW: *allocator = _PyRuntime.allocators.raw; break;
    case PYMEM_DOMAIN_MEM: *allocator = _PyRuntime.allocators.mem; break;
    case PYMEM_DOMAIN_OBJ: *allocator = _PyRuntime.allocators.obj; break;
    default: memset(allocator, 0, sizeof *allocator);
    }
}

void
PyMem_SetAllocator(PyMemAllocatorDomain domain, PyMemAllocatorEx *allocator)
{
    switch (domain) {
    case PYMEM_DOMAIN_RAW: _PyRuntime.allocators.raw = *allocator; break;
    case PYMEM_DOMAIN_MEM: _PyRuntime.allocators.mem = *allocator; break;
    case PYMEM_DOMAIN_OBJ: _PyRuntime.allocators.obj = *allocator; break;
    default: break;
    }
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
