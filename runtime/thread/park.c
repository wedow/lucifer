/* Park and unpark (DESIGN 5.5). A pending wait is linked in the wait table
   under its id's bucket lock, and only there can it complete: the unpark
   that finds it sets the result, unlinks it and wakes its carrier, all under
   the lock, so a second unpark or one after the wait has ended finds nothing.
   A deadline or signal ends the wait the same way, under the same lock,
   so exactly one of them wins. The waiter takes the lock once more before
   returning, which keeps its carrier alive until the unparker has woken it. */

#include "lucifer.h"

#include <errno.h>

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
    struct lu_wait_bucket *b = bucket_of(w->id);
    pthread_mutex_lock(&b->lock.mutex);
    w->next = b->head;
    b->head = w;
    pthread_mutex_unlock(&b->lock.mutex);
    return w->id;
}

LU_EXPORT int
lu_park(struct lu_wait *w, PyTime_t deadline, int detach)
{
    struct lu_carrier *c = w->carrier;
    w->deadline = deadline;
    PyThreadState *ts = detach ? lu_syscall_begin() : NULL;
    int ended = LU_PARK_PENDING;
    for (;;) {
        /* Read the futex word before the status: an unpark in between
           changes the word, and the sleep returns at once. */
        uint32_t seq = __atomic_load_n(&c->wake, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&w->status, __ATOMIC_SEQ_CST) != LU_PARK_PENDING) {
            break;
        }
        int err = lu_futex_wait(&c->wake, seq, deadline);
        if (err == ETIMEDOUT || err == EINTR) {
            ended = err == ETIMEDOUT ? LU_PARK_TIMEOUT : LU_PARK_INTR;
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
        __atomic_add_fetch(&w->carrier->wake, 1, __ATOMIC_SEQ_CST);
        lu_futex_wake(&w->carrier->wake);
    }
    pthread_mutex_unlock(&b->lock.mutex);
    return w != NULL;
}
