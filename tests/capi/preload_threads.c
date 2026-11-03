/* Worker threads that allocate before the runtime has started. The
   constructor of a preloaded extension runs before the executable's own, so
   the allocator must already be initialized: otherwise the first worker is
   taken for the main thread and shares the main heap with the real one.
   Blocks cross threads in both directions and are verified for corruption. */

#include "Python.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

enum { THREADS = 8, BLOCKS = 4000 };

static unsigned char *blocks[THREADS][BLOCKS];
static int failures;
static pthread_barrier_t start;

static size_t
size_of(int t, int i)
{
    return 8 + (size_t)((t * 7919 + i * 31) % 2000);
}

static void *
fill(void *arg)
{
    int t = (int)(intptr_t)arg;
    pthread_barrier_wait(&start);
    for (int i = 0; i < BLOCKS; i++) {
        unsigned char *p = (i & 1) ? PyMem_Malloc(size_of(t, i))
                                   : PyObject_Malloc(size_of(t, i));
        if (p == NULL) {
            __atomic_add_fetch(&failures, 1, __ATOMIC_SEQ_CST);
            return NULL;
        }
        memset(p, t + 1, size_of(t, i));
        blocks[t][i] = p;
    }
    return NULL;
}

/* Frees the blocks of the next thread, then allocates again. */
static void *
drain(void *arg)
{
    int t = (int)(intptr_t)arg;
    int from = (t + 1) % THREADS;
    pthread_barrier_wait(&start);
    for (int i = 0; i < BLOCKS; i++) {
        unsigned char *p = blocks[from][i];
        for (size_t k = 0; k < size_of(from, i); k++) {
            if (p[k] != from + 1) {
                __atomic_add_fetch(&failures, 1, __ATOMIC_SEQ_CST);
                break;
            }
        }
        if (i & 1) {
            PyMem_Free(p);
        }
        else {
            PyObject_Free(p);
        }
    }
    return NULL;
}

static void
run_all(void *(*fn)(void *))
{
    pthread_t th[THREADS];
    for (int t = 0; t < THREADS; t++) {
        pthread_create(&th[t], NULL, fn, (void *)(intptr_t)t);
    }
    for (int t = 0; t < THREADS; t++) {
        pthread_join(th[t], NULL);
    }
}

static struct PyModuleDef preload_module = {
    PyModuleDef_HEAD_INIT, .m_name = "preload_threads",
};

PyMODINIT_FUNC
PyInit_preload_threads(void)
{
    return PyModuleDef_Init(&preload_module);
}

__attribute__((constructor)) static void
check(void)
{
    pthread_barrier_init(&start, NULL, THREADS);
    for (int round = 0; round < 3; round++) {
        run_all(fill);
        run_all(drain);
    }
    void *p = PyMem_Malloc(100);
    failures += p == NULL;
    PyMem_Free(p);
    if (failures == 0) {
        puts("capi preload_threads ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
