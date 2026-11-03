/* Stop-the-world (_PyEval_StopTheWorld and _PyEval_StartTheWorld), requested
   by an attached thread while foreign threads alternate attached work and
   Py_BEGIN_ALLOW_THREADS, 1,000 times. While the world is stopped no worker
   is attached or runs attached work, and each is suspended; after it
   restarts they all make progress. Half of the workers poll while attached
   (so a stop reaches them as the please-stop reason); the others only stop
   when they detach. One more thread creates and deletes thread states
   (PyGILState_Ensure and Release) throughout, which a stop in progress must
   neither wait for nor lose. */

#include "Python.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define WORKERS 4
#define ROUNDS 1000
#define SUSPENDED 2             /* PyThreadState.state */

extern void _PyEval_StopTheWorld(PyInterpreterState *interp);
extern void _PyEval_StartTheWorld(PyInterpreterState *interp);
extern int _Py_HandlePending(PyThreadState *tstate);

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "stw.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

static int stopped;             /* set by main while the world is stopped */
static int done;
static int violations;          /* attached work seen while the world was stopped */

struct worker {
    PyThreadState *ts;
    int polls;                  /* polls while attached, not only when it detaches */
    unsigned long work;         /* attached phases finished */
};
static struct worker workers[WORKERS];
static unsigned long churned;
static pthread_barrier_t ready;

static void
attached_work(struct worker *w, PyThreadState *ts)
{
    for (int k = 0; k < (w->polls ? 50 : 5); k++) {
        if (w->polls) {
            _Py_HandlePending(ts);
        }
        if (__atomic_load_n(&stopped, __ATOMIC_SEQ_CST)) {
            __atomic_fetch_add(&violations, 1, __ATOMIC_SEQ_CST);
        }
    }
    __atomic_fetch_add(&w->work, 1, __ATOMIC_SEQ_CST);
}

static void *
worker(void *arg)
{
    struct worker *w = arg;
    PyGILState_STATE gil = PyGILState_Ensure();
    w->ts = PyThreadState_Get();
    pthread_barrier_wait(&ready);
    while (!__atomic_load_n(&done, __ATOMIC_SEQ_CST)) {
        attached_work(w, w->ts);
        Py_BEGIN_ALLOW_THREADS
        sched_yield();
        Py_END_ALLOW_THREADS
    }
    PyGILState_Release(gil);
    return NULL;
}

static void *
churn(void *arg)
{
    pthread_barrier_wait(&ready);
    struct worker w = { NULL, 0, 0 };
    while (!__atomic_load_n(&done, __ATOMIC_SEQ_CST)) {
        PyGILState_STATE gil = PyGILState_Ensure();
        attached_work(&w, PyThreadState_Get());
        __atomic_fetch_add(&churned, 1, __ATOMIC_SEQ_CST);      /* still attached */
        PyGILState_Release(gil);
    }
    return NULL;
}

static unsigned long
progress(void)
{
    unsigned long total = 0;
    for (int i = 0; i < WORKERS; i++) {
        total += __atomic_load_n(&workers[i].work, __ATOMIC_SEQ_CST);
    }
    return total + __atomic_load_n(&churned, __ATOMIC_SEQ_CST);
}

__attribute__((constructor)) static void
check(void)
{
    PyGILState_STATE gil = PyGILState_Ensure();
    PyInterpreterState *interp = PyInterpreterState_Get();
    pthread_t th[WORKERS + 1];
    pthread_barrier_init(&ready, NULL, WORKERS + 2);
    for (int i = 0; i < WORKERS; i++) {
        workers[i].polls = i % 2 == 0;
        pthread_create(&th[i], NULL, worker, &workers[i]);
    }
    pthread_create(&th[WORKERS], NULL, churn, NULL);

    /* Waiting for the others is outside the interpreter. */
    Py_BEGIN_ALLOW_THREADS
    pthread_barrier_wait(&ready);
    Py_END_ALLOW_THREADS

    for (int round = 0; round < ROUNDS; round++) {
        _PyEval_StopTheWorld(interp);
        __atomic_store_n(&stopped, 1, __ATOMIC_SEQ_CST);
        unsigned long before = progress();
        for (int i = 0; i < WORKERS; i++) {
            CHECK(__atomic_load_n(&workers[i].ts->state, __ATOMIC_SEQ_CST) == SUSPENDED);
        }
        struct timespec pause = { 0, 20000 };
        nanosleep(&pause, NULL);
        CHECK(progress() == before);
        __atomic_store_n(&stopped, 0, __ATOMIC_SEQ_CST);
        _PyEval_StartTheWorld(interp);

        /* Everything runs again. */
        for (int spins = 0; progress() == before && spins < 10000000; spins++) {
            sched_yield();
        }
        CHECK(progress() > before);
        if (failures != 0) {
            break;
        }
    }
    __atomic_store_n(&done, 1, __ATOMIC_SEQ_CST);
    Py_BEGIN_ALLOW_THREADS
    for (int i = 0; i <= WORKERS; i++) {
        pthread_join(th[i], NULL);
    }
    Py_END_ALLOW_THREADS
    CHECK(__atomic_load_n(&violations, __ATOMIC_SEQ_CST) == 0);
    for (int i = 0; i < WORKERS; i++) {
        CHECK(workers[i].work > 0);
    }
    PyGILState_Release(gil);
    if (failures == 0) {
        puts("capi stw ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
