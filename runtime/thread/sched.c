/* The scheduler (DESIGN 5.5 and 5.8). Without -X lucifer_handoff it is
   degenerate and this file does nothing. With it, a second carrier runs and
   the main task, no longer pinned for life, parks at the safepoints of its
   outermost activation and resumes on the other carrier.

   A park has two halves, split so that no other carrier runs the task while
   its activation is still unwinding:

   - In the eval loop, at the safepoint: the task registers a wait record in
     its own tail and completes it (lu_wait_begin, lu_unpark: an early result
     is kept), with `carrier` naming the other carrier. The loop saves its
     frame and returns LU_EVAL_PARKED.
   - In the carrier's loop, once the activation has returned: the task leaves
     the carrier (detached, unbound) and lu_park_commit queues it, since its
     wait is complete. A wait completed later queues it from lu_unpark.

   The carrier that dequeues a task binds it (lu_task_bind), attaches it, and
   enters the eval loop at its top frame (lu_resume). The task's first
   activation was entered from C on its home carrier, and that C stack waits
   in lu_sched_home for the result: when the outermost frame returns, on
   whichever carrier, the task goes home (a queued task with `done` set) and
   the home carrier returns the result to its C caller with the task bound
   to it again. */

#include "interp/frame.h"

#define TASK(ts) ((struct lu_task *)(ts))

static struct lu_carrier *
other_carrier(struct lu_carrier *c)
{
    return _PyRuntime.sched.carriers[_PyRuntime.sched.carriers[0] == c];
}

void
lu_sched_enqueue(struct lu_task *t, struct lu_carrier *c)
{
    pthread_mutex_lock(&_PyRuntime.sched.lock.mutex);
    t->runq_next = NULL;
    if (c->runq_tail != NULL) {
        c->runq_tail->runq_next = t;
    }
    else {
        c->runq_head = t;
    }
    c->runq_tail = t;
    pthread_mutex_unlock(&_PyRuntime.sched.lock.mutex);
    lu_carrier_wake(c);
}

static struct lu_task *
dequeue(struct lu_carrier *c)
{
    pthread_mutex_lock(&_PyRuntime.sched.lock.mutex);
    struct lu_task *t = c->runq_head;
    if (t != NULL) {
        c->runq_head = t->runq_next;
        if (c->runq_head == NULL) {
            c->runq_tail = NULL;
        }
    }
    pthread_mutex_unlock(&_PyRuntime.sched.lock.mutex);
    return t;
}

/* splitmix64 / xorshift64*: the seeded choices need only be a fixed
   function of the seed. */
static uint64_t
next_random(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static uint64_t
seed_state(uint64_t seed)
{
    uint64_t z = seed + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (z ^ (z >> 31)) | 1;
}

int
lu_sched_safepoint(struct lu_task *t)
{
    if (t->pin != 0 || t->pinned_for_life) {
        return 0;
    }
    struct lu_handoff *h = &_PyRuntime.handoff;
    if (h->seeded) {
        if (t->rng == 0) {
            t->rng = seed_state(h->seed);
        }
        if ((next_random(&t->rng) >> 32) & 1) {
            return 0;
        }
    }
    /* The migration-point invariants (DESIGN 5.4). The activation is the
       task's outermost, so no C frame is above it, and pin is zero, so none
       is below it but its caller's, which waits in lu_sched_home. A
       critical section is held only by C code. The carrier has just
       reported a quiescent state, and a Python instruction holds no lock-free
       reader's pointer across a safepoint, so no QSBR read interval is open. */
    if (t->ts.critical_section != 0) {
        Py_FatalError("lucifer: a task reached a migration point inside a critical section");
    }
    struct lu_wait *w = &t->wait;
    uint64_t id = lu_wait_begin(w);
    w->unpinned = 1;
    w->carrier = other_carrier(lu_carrier);
    lu_unpark(id, 0);
    return 1;
}

/* The task's activation has returned LU_EVAL_PARKED: it leaves the carrier
   and is queued. */
static void
commit(struct lu_task *t)
{
    lu_task_leave(t);
    lu_park_commit(&t->wait);
}

/* Runs the queued tasks of carrier `c`. `waiting` is the task whose C caller
   is blocked in this loop (NULL on the second carrier): the loop returns when
   that task finishes, or comes home finished. The second carrier's loop
   returns NULL once stopped. */
static PyObject *
run_loop(struct lu_carrier *c, struct lu_task *waiting)
{
    for (;;) {
        uint32_t seq = lu_carrier_wake_seq(c);
        struct lu_task *t = dequeue(c);
        if (t == NULL) {
            if (waiting == NULL && __atomic_load_n(&_PyRuntime.sched.stop, __ATOMIC_ACQUIRE)) {
                return NULL;
            }
            /* Nothing runs here, so nothing reads storage this carrier has
               retired: free what it can before sleeping. */
            lu_qsbr_process(c);
            lu_carrier_sleep(c, seq);
            continue;
        }
        lu_task_bind(t);
        lu_task_attach(&t->ts);
        if (t->done) {
            if (t != waiting) {
                Py_FatalError("lucifer: a finished task came home to a carrier that is not waiting for it");
            }
            PyObject *result = t->result;
            t->result = NULL;
            t->done = 0;
            return result;
        }
        PyObject *result = lu_resume(&t->ts);
        if (result == LU_EVAL_PARKED) {
            commit(t);
        }
        else if (c == t->home) {
            if (t != waiting) {
                Py_FatalError("lucifer: a task finished on a home carrier that is not waiting for it");
            }
            return result;
        }
        else {
            t->result = result;
            t->done = 1;
            lu_task_leave(t);
            lu_sched_enqueue(t, t->home);
        }
    }
}

PyObject *
lu_sched_home(PyThreadState *ts)
{
    struct lu_task *t = TASK(ts);
    struct lu_carrier *home = t->home;      /* the task may run elsewhere once committed */
    commit(t);
    return run_loop(home, t);
}

/* The second carrier's thread. */
static void *
second_carrier(void *arg)
{
    lu_current_entry();
    struct lu_carrier *c = lu_carrier;
    c->scheduled = 1;
    __atomic_store_n(&_PyRuntime.sched.carriers[1], c, __ATOMIC_RELEASE);
    lu_unpark(_PyRuntime.sched.ready_id, 0);
    run_loop(c, NULL);
    /* Retiring the owner record merges what other carriers queued to it,
       which can free objects, so it runs in a task. */
    c->scheduled = 0;
    PyGILState_Release(PyGILState_Ensure());
    return NULL;
}

void
lu_sched_start(void)
{
    struct lu_task *main = _PyRuntime.main_task;
    struct lu_wait ready;
    _PyRuntime.sched.ready_id = lu_wait_begin(&ready);
    _PyRuntime.sched.carriers[0] = lu_carrier;
    lu_carrier->scheduled = 1;
    if (pthread_create(&_PyRuntime.sched.second, NULL, second_carrier, NULL) != 0) {
        Py_FatalError("lucifer: cannot start the second carrier");
    }
    lu_park(&ready, -1, 1);
    main->pinned_for_life = 0;
    __atomic_store_n(&_PyRuntime.sched.active, 1, __ATOMIC_RELEASE);
}

void
lu_sched_stop(void)
{
    if (!_PyRuntime.sched.active) {
        return;
    }
    _PyRuntime.sched.active = 0;
    _PyRuntime.main_task->pinned_for_life = 1;
    __atomic_store_n(&_PyRuntime.sched.stop, 1, __ATOMIC_RELEASE);
    lu_carrier_wake(_PyRuntime.sched.carriers[1]);
    lu_join_thread(_PyRuntime.sched.second);
}
