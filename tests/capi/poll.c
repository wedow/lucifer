/* The poll handler's reasons that other threads raise, through _Py_HandlePending
   (what the interpreter calls at a call or back-edge once the poll word has a
   reason): PyThreadState_SetAsyncExc aimed at one thread, with the thread
   attached or detached when it is set and cleared by NULL; pending calls
   added from any thread, the main-thread-only ones running only on the main
   thread, each call exactly once and a failing call leaving the rest for the
   next poll; and an object queued to its owner's refcount-merge queue being
   freed at the owner's next poll. The interpreter is started here so that
   exceptions can be raised. */

#include "Python.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

extern int _Py_HandlePending(PyThreadState *tstate);
extern int _PyEval_AddPendingCall(PyInterpreterState *interp, int (*func)(void *),
                                  void *arg, int flags);

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "poll.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static pthread_t main_thread;
static pthread_barrier_t barrier;
static int stop;

/* Polls until it raises (returns 1), or is told to stop (0). */
static int
poll_until_raised(PyThreadState *ts)
{
    while (!__atomic_load_n(&stop, __ATOMIC_SEQ_CST)) {
        if (_Py_HandlePending(ts) < 0) {
            return 1;
        }
        sched_yield();
    }
    return 0;
}

/* Async exceptions */

struct victim {
    unsigned long id;           /* set once the thread is running */
    int raised;                 /* KeyboardInterrupt came out of the poll */
    int detached_when_set;      /* wait detached for the exception to be set */
    int go;                     /* the exception has been set */
};

static void *
victim_thread(void *arg)
{
    struct victim *v = arg;
    PyGILState_STATE gil = PyGILState_Ensure();
    PyThreadState *ts = PyThreadState_Get();
    if (v->detached_when_set) {
        Py_BEGIN_ALLOW_THREADS
        __atomic_store_n(&v->id, ts->thread_id, __ATOMIC_SEQ_CST);
        while (!__atomic_load_n(&v->go, __ATOMIC_SEQ_CST)) {
            sched_yield();
        }
        Py_END_ALLOW_THREADS
    }
    else {
        __atomic_store_n(&v->id, ts->thread_id, __ATOMIC_SEQ_CST);
    }
    if (poll_until_raised(ts)) {
        __atomic_store_n(&v->raised, PyErr_ExceptionMatches(PyExc_KeyboardInterrupt),
                         __ATOMIC_SEQ_CST);
        PyErr_Clear();
    }
    PyGILState_Release(gil);
    return NULL;
}

static void
check_async_exc(int detached, int clear)
{
    struct victim v = { 0, 0, detached, 0 }, bystander = { 0, 0, 0, 0 };
    pthread_t a, b;
    __atomic_store_n(&stop, 0, __ATOMIC_SEQ_CST);
    pthread_create(&a, NULL, victim_thread, &v);
    /* A second thread polls throughout and must see nothing. */
    pthread_create(&b, NULL, victim_thread, &bystander);
    while (!__atomic_load_n(&v.id, __ATOMIC_SEQ_CST) || !__atomic_load_n(&bystander.id, __ATOMIC_SEQ_CST)) {
        sched_yield();
    }
    CHECK(PyThreadState_SetAsyncExc(v.id, PyExc_KeyboardInterrupt) == 1);
    if (clear) {
        CHECK(PyThreadState_SetAsyncExc(v.id, NULL) == 1);      /* replaces it */
    }
    __atomic_store_n(&v.go, 1, __ATOMIC_SEQ_CST);
    for (int spins = 0; spins < 200 && !__atomic_load_n(&v.raised, __ATOMIC_SEQ_CST); spins++) {
        usleep(1000);
    }
    CHECK(v.raised == !clear);
    __atomic_store_n(&stop, 1, __ATOMIC_SEQ_CST);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    CHECK(bystander.raised == 0);
}

/* Pending calls */

#define CALLS 200
static int ran[CALLS];
static pthread_t ran_on[CALLS];

static int
count_call(void *arg)
{
    int i = (int)(intptr_t)arg;
    ran_on[i] = pthread_self();
    __atomic_fetch_add(&ran[i], 1, __ATOMIC_SEQ_CST);
    return 0;
}

static int
failing_call(void *arg)
{
    PyErr_SetString(PyExc_RuntimeError, "pending call failed");
    return -1;
}

static int
total_ran(void)
{
    int n = 0;
    for (int i = 0; i < CALLS; i++) {
        n += __atomic_load_n(&ran[i], __ATOMIC_SEQ_CST);
    }
    return n;
}

static void *
caller_thread(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    PyThreadState *ts = PyThreadState_Get();
    CHECK(Py_AddPendingCall(count_call, (void *)(intptr_t)0) == 0);       /* for main */
    /* It does not run here, however often this thread polls. */
    for (int i = 0; i < 1000; i++) {
        CHECK(_Py_HandlePending(ts) == 0);
    }
    CHECK(__atomic_load_n(&ran[0], __ATOMIC_SEQ_CST) == 0);
    PyGILState_Release(gil);
    return NULL;
}

static void *
poller_thread(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    PyThreadState *ts = PyThreadState_Get();
    pthread_barrier_wait(&barrier);
    while (!__atomic_load_n(&stop, __ATOMIC_SEQ_CST)) {
        CHECK(_Py_HandlePending(ts) == 0);
        sched_yield();
    }
    PyGILState_Release(gil);
    return NULL;
}

static void
check_pending_calls(void)
{
    PyInterpreterState *interp = PyInterpreterState_Main();
    pthread_t t;
    pthread_create(&t, NULL, caller_thread, NULL);
    Py_BEGIN_ALLOW_THREADS
    pthread_join(t, NULL);
    Py_END_ALLOW_THREADS
    CHECK(ran[0] == 0);
    CHECK(Py_MakePendingCalls() == 0);
    CHECK(ran[0] == 1 && pthread_equal(ran_on[0], main_thread));
    CHECK(Py_MakePendingCalls() == 0 && ran[0] == 1);

    /* Calls for any thread: two other threads and this one race for them. */
    pthread_t pollers[2];
    __atomic_store_n(&stop, 0, __ATOMIC_SEQ_CST);
    pthread_barrier_init(&barrier, NULL, 3);
    for (int i = 0; i < 2; i++) {
        pthread_create(&pollers[i], NULL, poller_thread, NULL);
    }
    Py_BEGIN_ALLOW_THREADS
    pthread_barrier_wait(&barrier);
    Py_END_ALLOW_THREADS
    for (int i = 1; i < CALLS; i++) {
        CHECK(_PyEval_AddPendingCall(interp, count_call, (void *)(intptr_t)i, 0) == 0);
        if (i % 7 == 0) {
            CHECK(Py_MakePendingCalls() == 0);
        }
    }
    for (int spins = 0; spins < 5000 && total_ran() < CALLS; spins++) {
        usleep(1000);
    }
    __atomic_store_n(&stop, 1, __ATOMIC_SEQ_CST);
    Py_BEGIN_ALLOW_THREADS
    pthread_join(pollers[0], NULL);
    pthread_join(pollers[1], NULL);
    Py_END_ALLOW_THREADS
    pthread_barrier_destroy(&barrier);
    CHECK(total_ran() == CALLS);
    for (int i = 0; i < CALLS; i++) {
        CHECK(ran[i] == 1);
    }

    /* A call that fails raises, and the calls behind it wait for the next poll. */
    for (int i = 0; i < CALLS; i++) {
        ran[i] = 0;
    }
    Py_AddPendingCall(count_call, (void *)(intptr_t)1);
    Py_AddPendingCall(failing_call, NULL);
    Py_AddPendingCall(count_call, (void *)(intptr_t)2);
    CHECK(Py_MakePendingCalls() == -1);
    CHECK(PyErr_ExceptionMatches(PyExc_RuntimeError));
    PyErr_Clear();
    CHECK(ran[1] == 1 && ran[2] == 0);
    CHECK(Py_MakePendingCalls() == 0);
    CHECK(ran[1] == 1 && ran[2] == 1);
}

/* Refcount merge */

static int dealloc_runs;

static void
counting_dealloc(PyObject *op)
{
    __atomic_fetch_add(&dealloc_runs, 1, __ATOMIC_SEQ_CST);
    PyObject_Free(op);
}

static PyTypeObject Counting_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "counting",
    .tp_basicsize = sizeof(PyObject),
    .tp_dealloc = counting_dealloc,
};

static PyObject *handed;

/* Owns an object another thread will drop the last reference to, and frees
   it by polling, without releasing its thread state. */
static void *
owner_thread(void *arg)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    PyThreadState *ts = PyThreadState_Get();
    handed = PyObject_Init(PyObject_Malloc(sizeof(PyObject)), &Counting_Type);
    Py_INCREF(handed);                  /* main drops both: the merge frees it */
    pthread_barrier_wait(&barrier);
    pthread_barrier_wait(&barrier);
    for (int spins = 0; spins < 5000 && !__atomic_load_n(&dealloc_runs, __ATOMIC_SEQ_CST); spins++) {
        CHECK(_Py_HandlePending(ts) == 0);
        usleep(1000);
    }
    CHECK(__atomic_load_n(&dealloc_runs, __ATOMIC_SEQ_CST) == 1);
    CHECK(PyThreadState_Get() == ts);   /* it never left */
    PyGILState_Release(gil);
    return NULL;
}

static void
check_merge_at_poll(void)
{
    pthread_t t;
    pthread_barrier_init(&barrier, NULL, 2);
    pthread_create(&t, NULL, owner_thread, NULL);
    Py_BEGIN_ALLOW_THREADS
    pthread_barrier_wait(&barrier);
    Py_END_ALLOW_THREADS
    Py_DECREF(handed);                  /* queued to the owner */
    Py_DECREF(handed);
    Py_BEGIN_ALLOW_THREADS
    pthread_barrier_wait(&barrier);
    pthread_join(t, NULL);
    Py_END_ALLOW_THREADS
    pthread_barrier_destroy(&barrier);
}

__attribute__((constructor)) static void
check(void)
{
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    PyStatus status = Py_InitializeFromConfig(&config);
    CHECK(!PyStatus_Exception(status));
    main_thread = pthread_self();

    check_async_exc(0, 0);
    check_async_exc(1, 0);
    check_async_exc(1, 1);              /* cleared before it can poll */
    CHECK(PyThreadState_SetAsyncExc(0, PyExc_KeyboardInterrupt) == 0);
    check_pending_calls();
    check_merge_at_poll();

    if (failures == 0) {
        puts("capi poll ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
