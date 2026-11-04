/* os.fork (FOUNDATIONS 2.9, Python/pystate.c PyOS_BeforeFork and PyOS_AfterFork_Child).

   The forking task stops the world, so that every other task is suspended or detached, then takes
   every lock of the registry (runtime.c lu_lock_register). The child inherits them all taken by
   its one thread and starts them afresh; the parent releases them.

   Only the forking thread exists in the child. Every other task and carrier is dead: its tasks
   leave the interpreter's list (their memory is leaked, since frame objects may point into it),
   and each carrier is retired as if its thread had exited (runtime/thread/carrier.c): its
   biased-refcount queues are merged and its heaps abandoned. The forking task becomes the main
   task, and the child runs without a second scheduler carrier.

   Under -X lucifer_handoff the main task may be on the second carrier, and then the thread that
   called the program (the home carrier's) is gone in the child: the surviving thread finishes the
   program, through lu_main_finished. Both scheduler carriers do their frees under the scheduler
   lock, so the fork never finds one in the middle of a free. */

#define _GNU_SOURCE
#include "thread/lock.h"

#include <errno.h>
#include <sched.h>
#include <unistd.h>

/* Every registered lock, or none: a thread detached by the stop may hold one while it takes
   another, in an order the registry does not follow. */
static void
lock_registry(void)
{
    for (;;) {
        struct lu_lock *l;
        if (pthread_mutex_trylock(&_PyRuntime.locks.mutex) == 0) {
            for (l = _PyRuntime.locks.head; l != NULL; l = l->next) {
                if (pthread_mutex_trylock(&l->mutex) != 0) {
                    break;
                }
            }
            if (l == NULL) {
                return;
            }
            for (struct lu_lock *m = _PyRuntime.locks.head; m != l; m = m->next) {
                pthread_mutex_unlock(&m->mutex);
            }
            pthread_mutex_unlock(&_PyRuntime.locks.mutex);
        }
        sched_yield();
    }
}

static void
unlock_registry(void)
{
    for (struct lu_lock *l = _PyRuntime.locks.head; l != NULL; l = l->next) {
        pthread_mutex_unlock(&l->mutex);
    }
    pthread_mutex_unlock(&_PyRuntime.locks.mutex);
}

/* The registry's locks, started afresh in the child. */
static void
reinit_registry(void)
{
    for (struct lu_lock *l = _PyRuntime.locks.head; l != NULL; l = l->next) {
        pthread_mutex_init(&l->mutex, NULL);
    }
    pthread_mutex_init(&_PyRuntime.locks.mutex, NULL);
}

/* The carrier's thread is gone. */
static void
retire_dead_carrier(struct lu_carrier *c)
{
    for (struct lu_entry *e = c->entries; e != NULL; e = e->next) {
        if (e->brc.tid != 0) {
            lu_brc_retire(e);
        }
    }
    pthread_mutex_lock(&_PyRuntime.heaps_lock.mutex);
    lu_qsbr_unregister(c);
    for (struct lu_entry *e = c->entries; e != NULL; e = e->next) {
        lu_mimalloc_abandon(e);
    }
    pthread_mutex_unlock(&_PyRuntime.heaps_lock.mutex);
    while (c->entries != NULL) {
        struct lu_entry *e = c->entries;
        c->entries = e->next;
        free(e);
    }
    free(c);
}

static void
child_after_fork(struct _is *interp, struct lu_task *self)
{
    reinit_registry();
    /* The waits of the threads that are gone. */
    for (int i = 0; i < LU_WAIT_BUCKETS; i++) {
        _PyRuntime.waits.buckets[i].head = NULL;
    }
    for (int i = 0; i < LU_LOT_BUCKETS; i++) {
        struct lu_lot_bucket *b = &_PyRuntime.parking_lot[i];
        b->head = b->tail = NULL;
        b->num_waiters = 0;
    }
    /* Their tasks. */
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        if ((struct lu_task *)ts != self) {
            ((struct lu_task *)ts)->carrier = NULL;
        }
    }
    interp->tasks = &self->ts;
    self->ts.prev = self->ts.next = NULL;
    /* Their carriers: retiring one unlinks it from the registry. */
    for (;;) {
        struct lu_carrier *c = _PyRuntime.qsbr.head;
        while (c == lu_carrier && c != NULL) {
            c = c->qsbr.next;
        }
        if (c == NULL) {
            break;
        }
        retire_dead_carrier(c);
    }
    /* This thread is the process's main thread and its task the main task. The main task forked on
       the second scheduler carrier when its home carrier's thread, which holds the C frames that
       called the program, is gone: this thread finishes the program. */
    if (_PyRuntime.sched.active && self == _PyRuntime.main_task && lu_carrier != self->home) {
        self->home = lu_carrier;
        _PyRuntime.sched.orphaned = 1;
    }
    lu_carrier->native_id = (unsigned long)gettid();
    self->ts.native_thread_id = lu_carrier->native_id;
    lu_carrier->scheduled = 0;
    lu_carrier->runq_head = lu_carrier->runq_tail = NULL;
    _PyRuntime.main_thread = pthread_self();
    _PyRuntime.main_task = self;
    _PyRuntime.sched.active = 0;
    _PyRuntime.sched.carriers[0] = _PyRuntime.sched.carriers[1] = NULL;
    self->pinned_for_life = 1;
}

long
lu_fork(void)
{
    struct _is *interp = &_PyRuntime.main_interp;
    struct lu_task *self = lu_current_task();
    lu_stw_stop(interp);
    lock_registry();
    pid_t pid = fork();
    int saved_errno = errno;
    if (pid == 0) {
        child_after_fork(interp, self);
    }
    else {
        unlock_registry();
    }
    lu_stw_start(interp);
    errno = saved_errno;
    return pid;
}
