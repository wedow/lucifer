/* PyMutex and critical sections under contention: 8 threads, each with a
   thread state from PyGILState_Ensure, take one PyMutex, a two-object
   critical section in both argument orders, and a two-object section nested
   inside a one-object section on the same object, which must suspend the
   outer section to proceed and resume it after. Every count must be exact.
   Then a timed lock that times out inside a critical section must leave the
   mutex's queue before it waits to resume the section. */

#include "Python.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* Exported by cp314t; _thread.lock.acquire(timeout=...) calls it. */
PyAPI_FUNC(PyLockStatus) _PyMutex_LockTimed(PyMutex *m, PyTime_t timeout_ns,
                                            int flags);
enum { LOCK_DETACH = 1, MS = 1000000 };

enum { THREADS = 8, ROUNDS = 4000 };

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "mutex.c:%d: check failed: %s\n", __LINE__, #cond); \
    __atomic_add_fetch(&failures, 1, __ATOMIC_SEQ_CST); } } while (0)

static PyTypeObject Plain_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "plain",
    .tp_basicsize = sizeof(PyObject),
};

static PyMutex mutex;
static PyObject *a, *b;
static volatile long mutex_count, a_count, b_count, nested_count;
static pthread_barrier_t start;

/* A read-modify-write that loses updates unless the caller excludes others. */
static void
bump(volatile long *counter, int i)
{
    long v = *counter;
    if (i % 7 == 0) {
        sched_yield();
    }
    *counter = v + 1;
}

static void *
contend(void *arg)
{
    int t = (int)(intptr_t)arg;
    PyGILState_Ensure();
    CHECK(PyThreadState_GetUnchecked() != NULL);
    pthread_barrier_wait(&start);
    for (int i = 0; i < ROUNDS; i++) {
        PyMutex_Lock(&mutex);
        CHECK(PyMutex_IsLocked(&mutex));
        bump(&mutex_count, i);
        PyMutex_Unlock(&mutex);

        PyObject *x = (t + i) & 1 ? a : b, *y = (t + i) & 1 ? b : a;
        Py_BEGIN_CRITICAL_SECTION2(x, y);
        bump(&a_count, i);
        bump(&b_count, i);
        Py_END_CRITICAL_SECTION2();

        if (i % 16 == t % 16) {
            Py_BEGIN_CRITICAL_SECTION(a);
            bump(&a_count, i);
            Py_BEGIN_CRITICAL_SECTION2(b, a);
            bump(&nested_count, i);
            bump(&b_count, i);
            Py_END_CRITICAL_SECTION2();
            bump(&a_count, i);              /* the outer section holds a again */
            Py_END_CRITICAL_SECTION();
        }
    }
    return NULL;
}

static void
sleep_ms(int ms)
{
    struct timespec ts = { 0, ms * MS };
    nanosleep(&ts, NULL);
}

/* T waits 50 ms for `timed` inside a section on `a` and times out; U1 has
   taken `a` meanwhile, so T's resume waits for U1. U1 holds `a` until U2
   gets `timed`, which the holder releases at 100 ms. U2 must get it then:
   T's timed-out wait must not still be first in the queue. */
static PyMutex timed;
static int t_status, u2_got;

static void *
timed_waiter(void *arg)
{
    PyGILState_Ensure();
    Py_BEGIN_CRITICAL_SECTION(a);
    t_status = _PyMutex_LockTimed(&timed, 50 * MS, LOCK_DETACH);
    if (t_status == PY_LOCK_ACQUIRED) {
        PyMutex_Unlock(&timed);
    }
    Py_END_CRITICAL_SECTION();
    return NULL;
}

static void *
section_holder(void *arg)
{
    PyGILState_Ensure();
    sleep_ms(20);
    Py_BEGIN_CRITICAL_SECTION(a);
    for (int i = 0; i < 1000 && !__atomic_load_n(&u2_got, __ATOMIC_SEQ_CST); i++) {
        sleep_ms(1);
    }
    CHECK(__atomic_load_n(&u2_got, __ATOMIC_SEQ_CST));
    Py_END_CRITICAL_SECTION();
    return NULL;
}

static void *
next_waiter(void *arg)
{
    PyGILState_Ensure();
    sleep_ms(70);
    PyMutex_Lock(&timed);
    __atomic_store_n(&u2_got, 1, __ATOMIC_SEQ_CST);
    PyMutex_Unlock(&timed);
    return NULL;
}

static void
check_timeout_leaves_queue(void)
{
    PyMutex_Lock(&timed);
    pthread_t th[3];
    pthread_create(&th[0], NULL, timed_waiter, NULL);
    pthread_create(&th[1], NULL, section_holder, NULL);
    pthread_create(&th[2], NULL, next_waiter, NULL);
    sleep_ms(100);
    PyMutex_Unlock(&timed);
    for (int i = 0; i < 3; i++) {
        pthread_join(th[i], NULL);
    }
    CHECK(t_status == PY_LOCK_FAILURE);
    CHECK(!PyMutex_IsLocked(&timed) && !PyMutex_IsLocked(&a->ob_mutex));
}

static struct PyModuleDef mutex_module = {
    PyModuleDef_HEAD_INIT, .m_name = "mutex",
};

PyMODINIT_FUNC
PyInit_mutex(void)
{
    return PyModuleDef_Init(&mutex_module);
}

__attribute__((constructor)) static void
check(void)
{
    a = PyObject_Init(PyObject_Malloc(sizeof(PyObject)), &Plain_Type);
    b = PyObject_Init(PyObject_Malloc(sizeof(PyObject)), &Plain_Type);
    pthread_barrier_init(&start, NULL, THREADS);
    pthread_t th[THREADS];
    for (int t = 0; t < THREADS; t++) {
        pthread_create(&th[t], NULL, contend, (void *)(intptr_t)t);
    }
    for (int t = 0; t < THREADS; t++) {
        pthread_join(th[t], NULL);
    }
    long nested = 0;
    for (int t = 0; t < THREADS; t++) {
        for (int i = 0; i < ROUNDS; i++) {
            nested += i % 16 == t % 16;
        }
    }
    CHECK(mutex_count == THREADS * ROUNDS);
    CHECK(nested_count == nested);
    CHECK(a_count == THREADS * ROUNDS + 2 * nested);
    CHECK(b_count == THREADS * ROUNDS + nested);
    CHECK(!PyMutex_IsLocked(&mutex));
    CHECK(!PyMutex_IsLocked(&a->ob_mutex) && !PyMutex_IsLocked(&b->ob_mutex));
    check_timeout_leaves_queue();
    if (failures == 0) {
        puts("capi mutex ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
