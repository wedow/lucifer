/* The allocator domains and the reference tracer are hookable. Each domain is
   wrapped by a counting allocator that delegates to the previous one; the
   defaults are restored and no longer counted. A tracer sees the creation
   and the destruction of an object made by PyObject_Init and released by
   Py_DECREF. */

#include "Python.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "alloc_hooks.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

struct counting {
    PyMemAllocatorEx prev;
    int mallocs, callocs, reallocs, frees;
};

static void *
count_malloc(void *ctx, size_t size)
{
    struct counting *c = ctx;
    c->mallocs++;
    return c->prev.malloc(c->prev.ctx, size);
}

static void *
count_calloc(void *ctx, size_t nelem, size_t elsize)
{
    struct counting *c = ctx;
    c->callocs++;
    return c->prev.calloc(c->prev.ctx, nelem, elsize);
}

static void *
count_realloc(void *ctx, void *ptr, size_t size)
{
    struct counting *c = ctx;
    c->reallocs++;
    return c->prev.realloc(c->prev.ctx, ptr, size);
}

static void
count_free(void *ctx, void *ptr)
{
    struct counting *c = ctx;
    c->frees++;
    c->prev.free(c->prev.ctx, ptr);
}

static struct counting counts[3];
static const PyMemAllocatorDomain domains[3] = {
    PYMEM_DOMAIN_RAW, PYMEM_DOMAIN_MEM, PYMEM_DOMAIN_OBJ,
};

static void
wrap_all(void)
{
    for (int i = 0; i < 3; i++) {
        PyMemAllocatorEx hook = { &counts[i], count_malloc, count_calloc,
                                  count_realloc, count_free };
        PyMem_GetAllocator(domains[i], &counts[i].prev);
        PyMem_SetAllocator(domains[i], &hook);
    }
}

static void
unwrap_all(void)
{
    for (int i = 0; i < 3; i++) {
        PyMem_SetAllocator(domains[i], &counts[i].prev);
    }
}

static void
use(void *(*malloc_fn)(size_t), void *(*calloc_fn)(size_t, size_t),
    void *(*realloc_fn)(void *, size_t), void (*free_fn)(void *))
{
    char *p = malloc_fn(16);
    p = realloc_fn(p, 4096);
    free_fn(p);
    free_fn(calloc_fn(2, 8));
}

static void
use_all(void)
{
    use(PyMem_RawMalloc, PyMem_RawCalloc, PyMem_RawRealloc, PyMem_RawFree);
    use(PyMem_Malloc, PyMem_Calloc, PyMem_Realloc, PyMem_Free);
    use(PyObject_Malloc, PyObject_Calloc, PyObject_Realloc, PyObject_Free);
}

static void
check_domains(void)
{
    PyMemAllocatorEx before[3], after[3];
    for (int i = 0; i < 3; i++) {
        PyMem_GetAllocator(domains[i], &before[i]);
        CHECK(before[i].malloc != NULL && before[i].free != NULL);
    }
    CHECK(before[0].malloc != before[1].malloc);

    wrap_all();
    use_all();
    for (int i = 0; i < 3; i++) {
        /* Domains are independent: one use of each, nothing from the others. */
        CHECK(counts[i].mallocs == 1 && counts[i].callocs == 1);
        CHECK(counts[i].reallocs == 1 && counts[i].frees == 2);
    }
    unwrap_all();

    use_all();
    for (int i = 0; i < 3; i++) {
        CHECK(counts[i].mallocs == 1 && counts[i].frees == 2);
        PyMem_GetAllocator(domains[i], &after[i]);
        CHECK(memcmp(&before[i], &after[i], sizeof after[i]) == 0);
    }
}

static void
plain_dealloc(PyObject *op)
{
    PyObject_Free(op);
}

static PyTypeObject Plain_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "plain",
    .tp_basicsize = sizeof(PyObject),
    .tp_dealloc = plain_dealloc,
};

static int creates, destroys;

static int
tracer(PyObject *op, PyRefTracerEvent event, void *data)
{
    CHECK(data == &failures);
    if (Py_TYPE(op) == &Plain_Type) {
        if (event == PyRefTracer_CREATE) {
            creates++;
        }
        else if (event == PyRefTracer_DESTROY) {
            destroys++;
        }
    }
    return 0;
}

static void
check_tracer(void)
{
    void *data = NULL;
    CHECK(PyRefTracer_GetTracer(&data) == NULL);
    CHECK(PyRefTracer_SetTracer(tracer, &failures) == 0);
    CHECK(PyRefTracer_GetTracer(&data) == tracer && data == &failures);

    PyObject *traced = PyObject_Init(PyObject_Malloc(sizeof(PyObject)), &Plain_Type);
    CHECK(creates == 1 && destroys == 0);
    Py_DECREF(traced);
    CHECK(creates == 1 && destroys == 1);

    CHECK(PyRefTracer_SetTracer(NULL, NULL) == 0);
    PyObject *o = PyObject_Init(PyObject_Malloc(sizeof(PyObject)), &Plain_Type);
    Py_DECREF(o);
    CHECK(creates == 1 && destroys == 1);
}

static struct PyModuleDef hooks_module = {
    PyModuleDef_HEAD_INIT, .m_name = "alloc_hooks",
};

PyMODINIT_FUNC
PyInit_alloc_hooks(void)
{
    return PyModuleDef_Init(&hooks_module);
}

__attribute__((constructor)) static void
check(void)
{
    check_domains();
    check_tracer();
    if (failures == 0) {
        puts("capi alloc_hooks ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
