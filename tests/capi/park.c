/* lu_park and lu_unpark (DESIGN 5.5), called from a preloaded extension's
   constructor and its threads: an unpark from another thread, an early
   result kept, duplicate and stale ids ignored, never-reused ids, deadlines,
   and an unpark racing the deadline completing exactly one way. The wait API
   is Lucifer's own, so its header comes from runtime/. */

#include "Python.h"
#include "../../runtime/thread/park.h"

#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "park.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

#define MS 1000000

static void
sleep_ns(PyTime_t ns)
{
    struct timespec ts = { ns / 1000000000, ns % 1000000000 };
    nanosleep(&ts, NULL);
}

struct unparker {
    uint64_t id;
    PyTime_t delay;
    uintptr_t result;
    int delivered;
};

static void *
unpark_later(void *arg)
{
    struct unparker *u = arg;
    sleep_ns(u->delay);
    u->delivered = lu_unpark(u->id, u->result);
    return NULL;
}

static void
check_unpark_from_another_thread(void)
{
    struct lu_wait w;
    struct unparker u = { lu_wait_begin(&w), 20 * MS, 42, 0 };
    pthread_t th;
    pthread_create(&th, NULL, unpark_later, &u);
    PyTime_t start = lu_monotonic_ns();
    CHECK(lu_park(&w, -1, 1) == LU_PARK_OK);
    CHECK(lu_monotonic_ns() - start >= 15 * MS);
    pthread_join(th, NULL);
    CHECK(u.delivered == 1);
    CHECK(w.result == 42);
}

static void
check_early_and_duplicate(void)
{
    struct lu_wait w;
    uint64_t id = lu_wait_begin(&w);
    CHECK(lu_unpark(id, 7) == 1);
    CHECK(lu_unpark(id, 8) == 0);                       /* duplicate */
    PyTime_t start = lu_monotonic_ns();
    CHECK(lu_park(&w, start + 1000 * MS, 1) == LU_PARK_OK);
    CHECK(lu_monotonic_ns() - start < 500 * MS);        /* kept, no sleep */
    CHECK(w.result == 7);
    CHECK(lu_unpark(id, 9) == 0);                       /* ended */
}

static void
check_deadlines_and_stale_ids(void)
{
    struct lu_wait w;
    uint64_t old = lu_wait_begin(&w);
    CHECK(lu_park(&w, 0, 1) == LU_PARK_TIMEOUT);        /* already passed */
    CHECK(lu_unpark(old, 1) == 0);

    uint64_t id = lu_wait_begin(&w);
    CHECK(id > old);
    CHECK(lu_unpark(old, 1) == 0);                      /* stale: not this wait */
    CHECK(lu_unpark(id + 1000, 1) == 0);                /* never issued */
    PyTime_t start = lu_monotonic_ns();
    CHECK(lu_park(&w, start + 30 * MS, 1) == LU_PARK_TIMEOUT);
    CHECK(lu_monotonic_ns() - start >= 30 * MS);
    CHECK(lu_unpark(id, 1) == 0);

    /* A stale unpark from another thread does not end a later wait. */
    struct unparker u = { id, 5 * MS, 1, -1 };
    uint64_t next = lu_wait_begin(&w);
    CHECK(next > id);
    pthread_t th;
    pthread_create(&th, NULL, unpark_later, &u);
    CHECK(lu_park(&w, lu_monotonic_ns() + 40 * MS, 1) == LU_PARK_TIMEOUT);
    pthread_join(th, NULL);
    CHECK(u.delivered == 0);
}

/* An unpark aimed at the deadline: either it delivered and the park saw the
   result, or it was ignored and the park timed out. */
static void
check_unpark_races_deadline(void)
{
    int ok = 0, timeouts = 0;
    for (int i = 0; i < 300; i++) {
        struct lu_wait w;
        struct unparker u = { lu_wait_begin(&w), 100000, (uintptr_t)i + 1, -1 };
        pthread_t th;
        pthread_create(&th, NULL, unpark_later, &u);
        int status = lu_park(&w, lu_monotonic_ns() + 100000, 1);
        pthread_join(th, NULL);
        if (status == LU_PARK_OK) {
            CHECK(u.delivered == 1 && w.result == (uintptr_t)i + 1);
            ok++;
        }
        else {
            CHECK(status == LU_PARK_TIMEOUT && u.delivered == 0);
            timeouts++;
        }
    }
    CHECK(ok + timeouts == 300);
}

static struct PyModuleDef park_module = {
    PyModuleDef_HEAD_INIT, .m_name = "park",
};

PyMODINIT_FUNC
PyInit_park(void)
{
    return PyModuleDef_Init(&park_module);
}

__attribute__((constructor)) static void
check(void)
{
    check_unpark_from_another_thread();
    check_early_and_duplicate();
    check_deadlines_and_stale_ids();
    check_unpark_races_deadline();
    if (failures == 0) {
        puts("capi park ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
