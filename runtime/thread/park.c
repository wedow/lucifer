/* Park and unpark (DESIGN 5.5). A pending wait is linked in the wait table
   under its id's bucket lock, and only there can it complete: the unpark
   that finds it sets the result, unlinks it and wakes its carrier, all under
   the lock, so a second unpark or one after the wait has ended finds nothing.
   A deadline or signal ends the wait the same way, under the same lock,
   so exactly one of them wins. The waiter takes the lock once more before
   returning, which keeps its carrier alive until the unparker has woken it. */

#include "lucifer.h"

#include <errno.h>
#include <sched.h>

static struct lu_wait_bucket *
bucket_of(uint64_t id)
{
    return &_PyRuntime.waits.buckets[id % LU_WAIT_BUCKETS];
}

/* Unlinks a pending wait; the caller holds its bucket lock. */
static void
unlink_wait(struct lu_wait_bucket *b, struct lu_wait *w)
{
    struct lu_wait **p = &b->head;
    while (*p != w) {
        p = &(*p)->next;
    }
    *p = w->next;
}

LU_EXPORT uint64_t
lu_wait_begin(struct lu_wait *w)
{
    lu_current_entry();
    w->owner = lu_current_task();
    w->carrier = lu_carrier;
    w->id = __atomic_add_fetch(&_PyRuntime.waits.last_id, 1, __ATOMIC_RELAXED);
    w->status = LU_PARK_PENDING;
    w->result = 0;
    w->deadline = -1;
    w->unpinned = w->parked = w->signals = 0;
    w->registered = 1;
    struct lu_wait_bucket *b = bucket_of(w->id);
    pthread_mutex_lock(&b->lock.mutex);
    w->next = b->head;
    b->head = w;
    pthread_mutex_unlock(&b->lock.mutex);
    return w->id;
}

/* The carrier thread's sleep on its wake word, which returns at once if the
   word is no longer `seq`. `sleeping` lets a waker skip the futex call when
   the thread is not asleep (a handoff finds it spinning): the sleeper sets
   it, then looks at the word; the waker changes the word, then looks at it.
   Both are sequentially consistent, so one of them sees the other's store. */
static int
carrier_sleep(struct lu_carrier *c, uint32_t seq, PyTime_t deadline)
{
    int err = 0;
    __atomic_store_n(&c->sleeping, 1, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&c->wake, __ATOMIC_SEQ_CST) == seq) {
        err = lu_futex_wait(&c->wake, seq, deadline);
    }
    __atomic_store_n(&c->sleeping, 0, __ATOMIC_SEQ_CST);
    return err;
}

void
lu_wait_ready(struct lu_wait *w, struct lu_carrier *carrier)
{
    *w = (struct lu_wait){
        .owner = lu_current_task(), .carrier = carrier,
        .id = __atomic_add_fetch(&_PyRuntime.waits.last_id, 1, __ATOMIC_RELAXED),
        .status = LU_PARK_OK, .deadline = -1, .unpinned = 1,
    };
}

LU_EXPORT int
lu_park(struct lu_wait *w, PyTime_t deadline, int detach)
{
    struct lu_carrier *c = w->carrier;
    w->deadline = deadline;
    PyThreadState *ts = detach ? lu_syscall_begin() : NULL;
    int ended = LU_PARK_PENDING;
    for (;;) {
        /* Read the futex word before the status and the signals reason: an
           unpark or a signal in between changes the word, and the sleep
           returns at once. */
        uint32_t seq = __atomic_load_n(&c->wake, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&w->status, __ATOMIC_SEQ_CST) != LU_PARK_PENDING) {
            break;
        }
        /* A Python handler to run (the signals reason, which only the main
           task's carrier ever has, and whose C handler wakes that carrier)
           ends a wait whose caller runs it. It is looked for before every
           sleep, the first included: a signal that came before the wait, as
           one does while a builtin runs, spent its wake already. The caller
           clears the reason as it runs the handler. Other waits sleep on. */
        if (w->signals && (__atomic_load_n(&c->poll, __ATOMIC_SEQ_CST) & LU_POLL_SIGNALS_PENDING)) {
            ended = LU_PARK_INTR;
            break;
        }
        if (carrier_sleep(c, seq, deadline) == ETIMEDOUT) {
            ended = LU_PARK_TIMEOUT;
            break;
        }
    }
    struct lu_wait_bucket *b = bucket_of(w->id);
    pthread_mutex_lock(&b->lock.mutex);
    if (w->status == LU_PARK_PENDING) {
        w->status = ended;
        unlink_wait(b, w);
    }
    int status = w->status;
    pthread_mutex_unlock(&b->lock.mutex);
    lu_syscall_end(ts);
    return status;
}

LU_EXPORT int
lu_unpark(uint64_t id, uintptr_t result)
{
    struct lu_wait_bucket *b = bucket_of(id);
    pthread_mutex_lock(&b->lock.mutex);
    struct lu_wait *w = b->head;
    while (w != NULL && w->id != id) {
        w = w->next;
    }
    if (w != NULL) {
        unlink_wait(b, w);
        w->result = result;
        __atomic_store_n(&w->status, LU_PARK_OK, __ATOMIC_SEQ_CST);
        if (w->parked) {
            lu_sched_enqueue(w->owner, w->carrier);
        }
        else if (!w->unpinned) {
            lu_carrier_wake(w->carrier);
        }
    }
    pthread_mutex_unlock(&b->lock.mutex);
    return w != NULL;
}

/* The task has left its carrier. Under the bucket lock, the same one that
   completes a wait: either the completion came first, and the task is queued
   here, or it comes later and finds `parked` set. */
void
lu_park_commit(struct lu_wait *w)
{
    if (!w->registered) {
        w->parked = 1;
        lu_sched_enqueue(w->owner, w->carrier);
        return;
    }
    struct lu_wait_bucket *b = bucket_of(w->id);
    pthread_mutex_lock(&b->lock.mutex);
    w->parked = 1;
    if (w->status != LU_PARK_PENDING) {
        lu_sched_enqueue(w->owner, w->carrier);
    }
    pthread_mutex_unlock(&b->lock.mutex);
}

uint32_t
lu_carrier_wake_seq(struct lu_carrier *c)
{
    return __atomic_load_n(&c->wake, __ATOMIC_SEQ_CST);
}

void
lu_carrier_wake(struct lu_carrier *c)
{
    __atomic_add_fetch(&c->wake, 1, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&c->sleeping, __ATOMIC_SEQ_CST)) {
        lu_futex_wake(&c->wake);
    }
}

/* The wake word changes with every queued task, so a sleep that began after
   `seq` was read returns at once if one was queued since. A handoff wakes the
   other carrier within microseconds, so it spins first: on the processor's
   pause hint, then yielding, then the futex. */
void
lu_carrier_sleep(struct lu_carrier *c, uint32_t seq)
{
    for (int i = 0; i < 4000; i++) {
        if (__atomic_load_n(&c->wake, __ATOMIC_ACQUIRE) != seq) {
            return;
        }
#if defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        __asm__ volatile("yield");
#endif
    }
    for (int i = 0; i < 50; i++) {
        if (__atomic_load_n(&c->wake, __ATOMIC_ACQUIRE) != seq) {
            return;
        }
        sched_yield();
    }
    carrier_sleep(c, seq, -1);
}
