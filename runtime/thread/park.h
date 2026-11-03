/* Park and unpark (DESIGN 5.5): the one wait every blocking primitive builds
   on. A waiter registers a wait record, publishes its id to whoever will
   complete it, then parks. lu_unpark(id, result) may come from any thread,
   before or after the park: the first unpark of a live id completes the wait
   and later ones are ignored, as are ids whose wait has ended.

   This header stands alone over Python.h so that tests/capi can include it. */
#ifndef LU_PARK_H
#define LU_PARK_H

#include "Python.h"

#include <stdint.h>
#include <time.h>

/* lu_park's results, numbered as Python/parking_lot.c's Py_PARK_*. */
enum {
    LU_PARK_OK = 0,             /* unparked: the record's result is set */
    LU_PARK_TIMEOUT = -2,       /* the deadline passed first */
    LU_PARK_INTR = -3,          /* a signal handler ran first */
    LU_PARK_PENDING = 1,        /* (the record's status while it waits) */
};

struct lu_task;
struct lu_carrier;

/* A wait record, owned by the waiter (usually on its stack) from
   lu_wait_begin until lu_park returns. */
struct lu_wait {
    struct lu_task *owner;      /* the waiting task, or NULL on a thread without one */
    struct lu_carrier *carrier; /* the carrier that sleeps for it */
    uint64_t id;                /* never reused */
    int status;                 /* LU_PARK_PENDING until it ends, then lu_park's result */
    uintptr_t result;           /* the unparker's, once status is LU_PARK_OK */
    PyTime_t deadline;          /* absolute CLOCK_MONOTONIC ns, or -1 for none */
    struct lu_wait *next;       /* in the wait table while pending */
};

/* Registers `w` for the calling thread and returns its id. From here an
   unpark of the id is kept until lu_park consumes it. */
uint64_t lu_wait_begin(struct lu_wait *w);

/* Waits until `w` is unparked or `deadline` (absolute CLOCK_MONOTONIC ns, -1
   for none) passes, then ends the wait: its id is stale from here on. A
   result that arrived before the call returns at once. With `detach` the
   task counts as detached while it sleeps (lu_syscall_begin), as it must
   unless the caller is CPython code that asked for _Py_LOCK_DONT_DETACH.

   The degenerate scheduler sleeps on the carrier's futex for every task.
   With migration (K1r) an unpinned task instead returns to its carrier's
   loop, and the unpark queues it on the carrier that last ran it. */
int lu_park(struct lu_wait *w, PyTime_t deadline, int detach);

/* Completes the wait `id` with `result` and wakes its carrier. Returns 1 if
   it did, or 0 if the id is stale or already completed. */
int lu_unpark(uint64_t id, uintptr_t result);

static inline PyTime_t
lu_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (PyTime_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

#endif
