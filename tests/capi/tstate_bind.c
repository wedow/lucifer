/* Thread-state binding through the public API: PyGILState_Ensure/Release from
   foreign threads, Save/Restore round trips, PyThreadState_New and Swap. A
   thread keeps one tstate for its whole native extent; a task is created for
   a foreign thread by its first Ensure and deleted by the matching Release. */

#include "Python.h"

#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

static int failures;
static pthread_mutex_t failures_lock = PTHREAD_MUTEX_INITIALIZER;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "tstate_bind.c:%d: check failed: %s\n", __LINE__, #cond); \
    pthread_mutex_lock(&failures_lock); failures++; pthread_mutex_unlock(&failures_lock); } } while (0)

enum { THREADS = 8, ROUNDS = 50 };

static PyThreadState *seen[THREADS];
static pthread_barrier_t barrier;

/* The thread owns `ts` and holds it attached. */
static void
check_bound_here(PyThreadState *ts)
{
    CHECK(ts != NULL);
    CHECK(PyThreadState_Get() == ts && PyThreadState_GetUnchecked() == ts);
    CHECK(PyGILState_GetThisThreadState() == ts);
    CHECK(PyGILState_Check());
    CHECK(ts->thread_id == (unsigned long)pthread_self());
    CHECK(ts->native_thread_id == (unsigned long)syscall(SYS_gettid));
    CHECK(ts->_status.bound && !ts->_status.unbound && ts->_status.bound_gilstate);
    CHECK(ts->state == 1);                      /* _Py_THREAD_ATTACHED */
    CHECK(PyThreadState_GetInterpreter(ts) == PyInterpreterState_Main());
}

static void
check_save_restore(PyThreadState *ts)
{
    PyObject *o = PyObject_Malloc(sizeof(PyObject));
    CHECK(o != NULL);
    PyThreadState *saved = PyEval_SaveThread();
    CHECK(saved == ts);
    CHECK(PyThreadState_GetUnchecked() == NULL);
    CHECK(PyGILState_GetThisThreadState() == ts);
    PyEval_RestoreThread(saved);
    check_bound_here(ts);
    PyObject_Free(o);

    Py_BEGIN_ALLOW_THREADS
    CHECK(PyThreadState_GetUnchecked() == NULL);
    Py_END_ALLOW_THREADS
    check_bound_here(ts);
}

static void *
foreign(void *arg)
{
    int slot = (int)(intptr_t)arg;
    CHECK(PyGILState_GetThisThreadState() == NULL && PyThreadState_GetUnchecked() == NULL);
    uint64_t first_id = 0;
    for (int round = 0; round < ROUNDS; round++) {
        PyGILState_STATE outer = PyGILState_Ensure();
        CHECK(outer == PyGILState_UNLOCKED);
        PyThreadState *ts = PyThreadState_Get();
        check_bound_here(ts);
        if (round == 0) {
            seen[slot] = ts;
            first_id = ts->id;
            pthread_barrier_wait(&barrier);     /* all live at once: distinct */
            for (int i = 0; i < THREADS; i++) {
                CHECK(i == slot || seen[i] != ts);
            }
            pthread_barrier_wait(&barrier);
        }
        CHECK(ts->id != 0 && (round == 0 || ts->id != first_id));

        PyGILState_STATE inner = PyGILState_Ensure();
        CHECK(inner == PyGILState_LOCKED && PyThreadState_Get() == ts);
        check_save_restore(ts);
        PyGILState_Release(inner);
        check_bound_here(ts);

        /* Ensure over a detached task reattaches it, and Release detaches it again. */
        PyThreadState *saved = PyEval_SaveThread();
        PyGILState_STATE again = PyGILState_Ensure();
        CHECK(again == PyGILState_UNLOCKED && PyThreadState_Get() == saved);
        PyGILState_Release(again);
        CHECK(PyThreadState_GetUnchecked() == NULL);
        PyEval_RestoreThread(saved);

        PyGILState_Release(outer);
        CHECK(PyThreadState_GetUnchecked() == NULL);
        CHECK(PyGILState_GetThisThreadState() == NULL);
    }
    return NULL;
}

/* A thread with no tstate makes one with PyThreadState_New, which also
   becomes its gilstate tstate, and swaps it in and out. */
static void *
new_and_swap(void *arg)
{
    PyThreadState *ts = PyThreadState_New(PyInterpreterState_Main());
    CHECK(ts != NULL && ts->_status.bound);
    CHECK(PyThreadState_GetUnchecked() == NULL);
    CHECK(PyThreadState_Swap(ts) == NULL);
    check_bound_here(ts);
    CHECK(PyThreadState_Swap(NULL) == ts);
    CHECK(PyThreadState_GetUnchecked() == NULL);
    PyEval_RestoreThread(ts);
    PyThreadState_Clear(ts);
    PyThreadState_DeleteCurrent();
    CHECK(PyThreadState_GetUnchecked() == NULL);
    return NULL;
}

static void
check_swap_on_main(void)
{
    PyThreadState *main_ts = PyThreadState_Get();
    PyThreadState *ts = PyThreadState_New(main_ts->interp);
    CHECK(ts != NULL && ts != main_ts && ts->interp == main_ts->interp);
    CHECK(ts->id != main_ts->id);
    CHECK(PyGILState_GetThisThreadState() == main_ts);
    CHECK(PyThreadState_Swap(ts) == main_ts);
    CHECK(PyThreadState_Get() == ts);
    CHECK(main_ts->state == 0 && ts->state == 1);
    CHECK(PyGILState_GetThisThreadState() == main_ts);
    CHECK(PyThreadState_Swap(main_ts) == ts);
    CHECK(PyThreadState_Get() == main_ts);
    PyThreadState_Clear(ts);
    PyThreadState_Delete(ts);
    check_bound_here(main_ts);
}

__attribute__((constructor)) static void
check(void)
{
    /* The constructor runs before the interpreter starts its main task, so
       this thread is foreign too. */
    PyGILState_STATE gil = PyGILState_Ensure();
    PyThreadState *main_ts = PyThreadState_Get();
    check_bound_here(main_ts);
    check_save_restore(main_ts);
    check_swap_on_main();

    pthread_t th[THREADS];
    pthread_barrier_init(&barrier, NULL, THREADS);
    for (int t = 0; t < THREADS; t++) {
        pthread_create(&th[t], NULL, foreign, (void *)(intptr_t)t);
    }
    for (int t = 0; t < THREADS; t++) {
        pthread_join(th[t], NULL);
    }
    pthread_create(&th[0], NULL, new_and_swap, NULL);
    pthread_join(th[0], NULL);
    check_bound_here(main_ts);
    PyGILState_Release(gil);
    CHECK(PyGILState_GetThisThreadState() == NULL);
    if (failures == 0) {
        puts("capi tstate_bind ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
