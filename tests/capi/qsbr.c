/* Storage retired with lu_qsbr_retire is freed only after every attached
   thread has been quiescent since. A reader stays attached, polling nothing
   and reading every block, while the main thread retires 300 blocks and goes
   offline and online (which is a quiescent state) and polls: nothing is
   freed, and nothing the reader reads has been touched. Once the reader
   leaves, more retires and polls free every block exactly once. */

#include "Python.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern void lu_qsbr_retire(void *p);
extern int _Py_HandlePending(PyThreadState *tstate);

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "qsbr.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

#define BLOCKS 2000
#define MAGIC 0x5eed5eedu

static unsigned *blocks[BLOCKS];
static int nblocks;                     /* allocated and tracked */
#define FIRST 300                       /* retired while the reader is attached */
static int freed[BLOCKS];
static int double_frees;
static PyMemAllocatorEx previous;

/* Counts the frees of tracked blocks and poisons them first. */
static void
tracking_free(void *ctx, void *ptr)
{
    for (int i = 0; i < nblocks; i++) {
        if (blocks[i] == ptr) {
            if (__atomic_fetch_add(&freed[i], 1, __ATOMIC_SEQ_CST) != 0) {
                __atomic_fetch_add(&double_frees, 1, __ATOMIC_SEQ_CST);
                return;
            }
            memset(ptr, 0xdd, 64);
            break;
        }
    }
    previous.free(previous.ctx, ptr);
}

static int
count_freed(void)
{
    int n = 0;
    for (int i = 0; i < nblocks; i++) {
        n += __atomic_load_n(&freed[i], __ATOMIC_SEQ_CST);
    }
    return n;
}

static int reading, leave, left, damaged;

/* Attached and polling nothing until told to leave; every block read must be
   intact while it is. */
static void *
reader(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    __atomic_store_n(&reading, 1, __ATOMIC_SEQ_CST);
    while (!__atomic_load_n(&leave, __ATOMIC_SEQ_CST)) {
        for (int i = 0; i < FIRST; i++) {
            if (*blocks[i] != MAGIC) {
                __atomic_fetch_add(&damaged, 1, __ATOMIC_SEQ_CST);
            }
        }
        sched_yield();
    }
    Py_BEGIN_ALLOW_THREADS
    __atomic_store_n(&left, 1, __ATOMIC_SEQ_CST);
    for (int spins = 0; spins < 100000000; spins++) {
        sched_yield();
    }
    Py_END_ALLOW_THREADS
    PyGILState_Release(gil);
    return NULL;
}

/* Quiescent: offline and online again, which takes the current sequence. */
static void
quiesce(PyThreadState *ts)
{
    Py_BEGIN_ALLOW_THREADS
    Py_END_ALLOW_THREADS
    _Py_HandlePending(ts);
}

__attribute__((constructor)) static void
check(void)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    PyThreadState *ts = PyThreadState_Get();
    PyMemAllocatorEx hook;
    PyMem_GetAllocator(PYMEM_DOMAIN_MEM, &previous);
    hook = previous;
    hook.free = tracking_free;
    PyMem_SetAllocator(PYMEM_DOMAIN_MEM, &hook);

    /* All allocated first, so that no freed block's address is reused. */
    for (nblocks = 0; nblocks < BLOCKS; nblocks++) {
        blocks[nblocks] = PyMem_Malloc(64);
        *blocks[nblocks] = MAGIC;
    }
    pthread_t th;
    pthread_create(&th, NULL, reader, NULL);
    Py_BEGIN_ALLOW_THREADS
    while (!__atomic_load_n(&reading, __ATOMIC_SEQ_CST)) {
        sched_yield();
    }
    Py_END_ALLOW_THREADS

    for (int i = 0; i < FIRST; i++) {
        lu_qsbr_retire(blocks[i]);
    }
    for (int round = 0; round < 50; round++) {
        quiesce(ts);
        sched_yield();
    }
    CHECK(count_freed() == 0);          /* the reader has not been quiescent */
    CHECK(__atomic_load_n(&damaged, __ATOMIC_SEQ_CST) == 0);

    __atomic_store_n(&leave, 1, __ATOMIC_SEQ_CST);
    Py_BEGIN_ALLOW_THREADS
    while (!__atomic_load_n(&left, __ATOMIC_SEQ_CST)) {
        sched_yield();
    }
    Py_END_ALLOW_THREADS

    /* Every 128th retire advances the write sequence and asks for the queue
       to be processed at the next poll. */
    int retired = FIRST;
    for (int batch = 0; batch < 12 && count_freed() < FIRST; batch++) {
        for (int i = 0; i < 128 && retired < BLOCKS; i++, retired++) {
            lu_qsbr_retire(blocks[retired]);
        }
        quiesce(ts);
    }
    int first = 0;
    for (int i = 0; i < FIRST; i++) {
        first += freed[i];
    }
    CHECK(first == FIRST);
    CHECK(count_freed() <= retired);
    CHECK(__atomic_load_n(&double_frees, __ATOMIC_SEQ_CST) == 0);
    CHECK(__atomic_load_n(&damaged, __ATOMIC_SEQ_CST) == 0);

    PyMem_SetAllocator(PYMEM_DOMAIN_MEM, &previous);
    /* The reader sleeps detached; it is not waited for. */
    PyGILState_Release(gil);
    if (failures == 0) {
        puts("capi qsbr ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
