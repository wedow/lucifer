/* PyMem_SetAllocator and PyMem_GetAllocator are atomic with respect to each
   other, as cp314t's ALLOCATORS_MUTEX makes them: a reader racing writers
   that install one of two hooks always gets one whole hook. */

#include "Python.h"

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

enum { ROUNDS = 200000 };

static PyMemAllocatorEx prev;

static void *a_malloc(void *ctx, size_t n) { return prev.malloc(prev.ctx, n); }
static void *a_calloc(void *ctx, size_t m, size_t n) { return prev.calloc(prev.ctx, m, n); }
static void *a_realloc(void *ctx, void *p, size_t n) { return prev.realloc(prev.ctx, p, n); }
static void a_free(void *ctx, void *p) { prev.free(prev.ctx, p); }
static void *b_malloc(void *ctx, size_t n) { return prev.malloc(prev.ctx, n); }
static void *b_calloc(void *ctx, size_t m, size_t n) { return prev.calloc(prev.ctx, m, n); }
static void *b_realloc(void *ctx, void *p, size_t n) { return prev.realloc(prev.ctx, p, n); }
static void b_free(void *ctx, void *p) { prev.free(prev.ctx, p); }

static int ctx_a, ctx_b;
static PyMemAllocatorEx hook_a = { &ctx_a, a_malloc, a_calloc, a_realloc, a_free };
static PyMemAllocatorEx hook_b = { &ctx_b, b_malloc, b_calloc, b_realloc, b_free };
static volatile int done;

static void *
writer(void *arg)
{
    PyMemAllocatorEx *h = arg;
    while (!done) {
        PyMem_SetAllocator(PYMEM_DOMAIN_MEM, h);
    }
    return NULL;
}

static int
whole(const PyMemAllocatorEx *g, const PyMemAllocatorEx *h)
{
    return g->ctx == h->ctx && g->malloc == h->malloc && g->calloc == h->calloc
           && g->realloc == h->realloc && g->free == h->free;
}

static struct PyModuleDef module = { PyModuleDef_HEAD_INIT, .m_name = "alloc_setget" };

PyMODINIT_FUNC
PyInit_alloc_setget(void)
{
    return PyModuleDef_Init(&module);
}

__attribute__((constructor)) static void
check(void)
{
    PyMem_GetAllocator(PYMEM_DOMAIN_MEM, &prev);
    PyMem_SetAllocator(PYMEM_DOMAIN_MEM, &hook_a);
    pthread_t ta, tb;
    pthread_create(&ta, NULL, writer, &hook_a);
    pthread_create(&tb, NULL, writer, &hook_b);
    int torn = 0;
    for (int i = 0; i < ROUNDS; i++) {
        PyMemAllocatorEx g;
        PyMem_GetAllocator(PYMEM_DOMAIN_MEM, &g);
        torn += !whole(&g, &hook_a) && !whole(&g, &hook_b);
    }
    done = 1;
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    PyMem_SetAllocator(PYMEM_DOMAIN_MEM, &prev);
    if (torn == 0) {
        puts("capi alloc_setget ok");
    }
    else {
        fprintf(stderr, "alloc_setget: %d torn reads of %d\n", torn, ROUNDS);
    }
    fflush(stdout);
    _exit(torn != 0);
}
