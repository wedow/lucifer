/* The parking lot (Python/parking_lot.c of CPython 3.14): waiters hashed by
   address, each sleeping on lu_park instead of a semaphore. A waiter that
   times out after an unparker has dequeued it counts as unparked, as in
   CPython; the unparker's lu_unpark then finds a stale id, so the waiter
   need not wait for it. */

#include "thread/lock.h"

#include <stdbool.h>

struct lu_lot_entry {
    void *park_arg;
    uintptr_t addr;
    struct lu_wait wait;
    struct lu_lot_entry *prev, *next;
    bool is_unparking;
};

static struct lu_lot_bucket *
bucket_of(const void *addr)
{
    return &_PyRuntime.parking_lot[(uintptr_t)addr % LU_LOT_BUCKETS];
}

static void
enqueue(struct lu_lot_bucket *bucket, struct lu_lot_entry *wait)
{
    wait->prev = bucket->tail;
    wait->next = NULL;
    if (bucket->tail != NULL) {
        bucket->tail->next = wait;
    }
    else {
        bucket->head = wait;
    }
    bucket->tail = wait;
    ++bucket->num_waiters;
}

static void
remove_entry(struct lu_lot_bucket *bucket, struct lu_lot_entry *wait)
{
    *(wait->prev != NULL ? &wait->prev->next : &bucket->head) = wait->next;
    *(wait->next != NULL ? &wait->next->prev : &bucket->tail) = wait->prev;
    --bucket->num_waiters;
}

/* The first waiter on `address`, removed and marked as being unparked. */
static struct lu_lot_entry *
dequeue(struct lu_lot_bucket *bucket, const void *address)
{
    for (struct lu_lot_entry *wait = bucket->head; wait != NULL; wait = wait->next) {
        if (wait->addr == (uintptr_t)address) {
            remove_entry(bucket, wait);
            wait->is_unparking = true;
            return wait;
        }
    }
    return NULL;
}

/* Checks that `*addr == *expected` (only works for 1, 2, 4, or 8 bytes). */
static int
atomic_memcmp(const void *addr, const void *expected, size_t addr_size)
{
    switch (addr_size) {
    case 1: return _Py_atomic_load_uint8(addr) == *(const uint8_t *)expected;
    case 2: return _Py_atomic_load_uint16(addr) == *(const uint16_t *)expected;
    case 4: return _Py_atomic_load_uint32(addr) == *(const uint32_t *)expected;
    case 8: return _Py_atomic_load_uint64(addr) == *(const uint64_t *)expected;
    default: Py_UNREACHABLE();
    }
}

LU_EXPORT int
_PyParkingLot_Park(const void *addr, const void *expected, size_t size,
                   PyTime_t timeout_ns, void *park_arg, int flags)
{
    struct lu_lot_entry wait = {
        .park_arg = park_arg,
        .addr = (uintptr_t)addr,
        .is_unparking = false,
    };
    struct lu_lot_bucket *bucket = bucket_of(addr);

    pthread_mutex_lock(&bucket->lock.mutex);
    if (!atomic_memcmp(addr, expected, size)) {
        pthread_mutex_unlock(&bucket->lock.mutex);
        return Py_PARK_AGAIN;
    }
    lu_wait_begin(&wait.wait);
    wait.wait.signals = (flags & (_PY_LOCK_HANDLE_SIGNALS | _PY_LOCK_FAIL_IF_INTERRUPTED)) != 0;
    enqueue(bucket, &wait);
    pthread_mutex_unlock(&bucket->lock.mutex);

    // Detached until the entry has left the bucket: reattaching may wait to
    // resume a critical section, and a wait still queued meanwhile would
    // take a wakeup or a handoff that belongs to the next waiter.
    PyThreadState *tstate = (flags & _PY_LOCK_DETACH) ? lu_syscall_begin() : NULL;
    PyTime_t deadline = timeout_ns < 0 ? -1 : lu_monotonic_ns() + timeout_ns;
    int res = lu_park(&wait.wait, deadline, 0);
    if (res != Py_PARK_OK) {
        // timeout or interrupt
        pthread_mutex_lock(&bucket->lock.mutex);
        if (wait.is_unparking) {
            // Another thread has dequeued us and run its callback: we are
            // unparked, and its lu_unpark will find our id stale.
            res = Py_PARK_OK;
        }
        else {
            remove_entry(bucket, &wait);
        }
        pthread_mutex_unlock(&bucket->lock.mutex);
    }
    lu_syscall_end(tstate);
    return res;
}

LU_EXPORT void
_PyParkingLot_Unpark(const void *addr, _Py_unpark_fn_t *fn, void *arg)
{
    struct lu_lot_bucket *bucket = bucket_of(addr);

    // Find the first waiter that is waiting on `addr`
    pthread_mutex_lock(&bucket->lock.mutex);
    struct lu_lot_entry *waiter = dequeue(bucket, addr);
    uint64_t id = 0;
    if (waiter) {
        int has_more_waiters = (bucket->num_waiters > 0);
        fn(arg, waiter->park_arg, has_more_waiters);
        id = waiter->wait.id;
    }
    else {
        fn(arg, NULL, 0);
    }
    pthread_mutex_unlock(&bucket->lock.mutex);

    if (waiter) {
        // Wake up the waiter outside of the bucket lock, by id: the waiter
        // may already have timed out and returned.
        lu_unpark(id, 0);
    }
}

LU_EXPORT void
_PyParkingLot_UnparkAll(const void *addr)
{
    struct lu_lot_bucket *bucket = bucket_of(addr);

    // Wakes under the bucket lock: a dequeued waiter that times out
    // returns without waiting for its wake, so its id must be read here.
    pthread_mutex_lock(&bucket->lock.mutex);
    struct lu_lot_entry *next;
    for (struct lu_lot_entry *wait = bucket->head; wait != NULL; wait = next) {
        next = wait->next;
        if (wait->addr == (uintptr_t)addr) {
            remove_entry(bucket, wait);
            wait->is_unparking = true;
            lu_unpark(wait->wait.id, 0);
        }
    }
    pthread_mutex_unlock(&bucket->lock.mutex);
}
