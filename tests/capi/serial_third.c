/* The switch out of the serial state, raced by a third task. The first task
   holds a critical section on a list, begun while it was the only task (so
   without its lock), and stays attached in C. Thread F then becomes the
   second task, which starts the switch; the switch waits for that section.
   Thread G becomes a third task meanwhile and takes the same list's section:
   as in free-threaded CPython it must wait until the first task's section
   ends, and then get it. */

#include "Python.h"

#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "serial_third.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static PyObject *shared;
static int g_in;

static double
now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void *
f_main(void *arg)
{
    PyGILState_Release(PyGILState_Ensure());
    return NULL;
}

static void *
g_main(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    Py_BEGIN_CRITICAL_SECTION(shared);
    __atomic_store_n(&g_in, 1, __ATOMIC_SEQ_CST);
    PyObject *s = PyUnicode_FromString("g");
    PyList_Append(shared, s);
    Py_DECREF(s);
    Py_END_CRITICAL_SECTION();
    PyGILState_Release(gil);
    return NULL;
}

__attribute__((constructor)) static void
check(void)
{
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    PyStatus status = Py_InitializeFromConfig(&config);
    CHECK(!PyStatus_Exception(status));
    shared = PyList_New(0);
    pthread_t f, g;
    Py_BEGIN_CRITICAL_SECTION(shared);
    pthread_create(&f, NULL, f_main, NULL);
    for (double end = now() + 0.2; now() < end;) {
    }
    pthread_create(&g, NULL, g_main, NULL);
    /* Attached in C, inside the section: G must not get in. */
    for (double end = now() + 1.0; now() < end && !__atomic_load_n(&g_in, __ATOMIC_SEQ_CST);) {
    }
    CHECK(!__atomic_load_n(&g_in, __ATOMIC_SEQ_CST));
    CHECK(PyList_GET_SIZE(shared) == 0);
    Py_END_CRITICAL_SECTION();
    Py_BEGIN_ALLOW_THREADS
    pthread_join(f, NULL);
    pthread_join(g, NULL);
    Py_END_ALLOW_THREADS
    CHECK(__atomic_load_n(&g_in, __ATOMIC_SEQ_CST));
    CHECK(PyList_GET_SIZE(shared) == 1);
    Py_DECREF(shared);
    if (failures == 0) {
        puts("capi serial_third ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
