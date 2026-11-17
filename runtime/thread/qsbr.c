/* Quiescent-state-based reclamation (Python/qsbr.c and the delayed frees of
   Objects/obmalloc.c, CPython 3.14).

   Storage that lock-free readers may still hold (a replaced list array, a
   dict's old keys table) is retired with a goal: the write sequence readers
   must reach. Every attached carrier reports the sequence it has seen at the
   interpreter's polls, and at compiled code's first poll after the sequence
   moves (an advance raises LU_POLL_QSBR on every attached carrier); a
   detached carrier is offline and holds nothing. The least
   sequence over the attached carriers is the read sequence; an item is freed
   once the read sequence has reached its goal. Each carrier keeps its own
   queue, in goal order, and processes it only at a poll with a task attached:
   a release can run any finalizer, which must not run in the middle of the
   change that retired the item, and a carrier with no task attached is not
   stopped by a stop-the-world, whose collection borrows ob_tid and empties
   the free lists. An exiting carrier leaves its queue to the next carrier
   that processes. A collection takes every queue, as CPython's does: what
   is queued dies by the end of gc.collect().

   Unlike CPython's array of per-thread states, the carriers form a list under
   the registry lock, which scans and advances take. */

#include "lucifer.h"

#define QSBR_OFFLINE 0
#define QSBR_INITIAL 1
#define QSBR_INCR 2
#define QSBR_LT(a, b) ((int64_t)((a) - (b)) < 0)
#define QSBR_LEQ(a, b) ((int64_t)((a) - (b)) <= 0)

/* Items retired per advance of the write sequence (QSBR_DEFERRED_LIMIT),
   which also asks the next poll to process the queue. A retire never
   processes it itself (CPython's does when a chunk fills): a release can run
   a finalizer, which must not run in the middle of the retiring change (a
   list resized under its lock). */
#define DEFERRED_LIMIT 127

void
lu_qsbr_register(struct lu_carrier *c)
{
    pthread_mutex_lock(&_PyRuntime.qsbr.lock.mutex);
    c->qsbr.next = _PyRuntime.qsbr.head;
    _PyRuntime.qsbr.head = c;
    pthread_mutex_unlock(&_PyRuntime.qsbr.lock.mutex);
}

void
lu_qsbr_attach(struct lu_carrier *c)
{
    uint64_t seq = __atomic_load_n(&_PyRuntime.qsbr.wr_seq, __ATOMIC_ACQUIRE);
    __atomic_store_n(&c->qsbr.seq, seq, __ATOMIC_SEQ_CST);
}

void
lu_qsbr_detach(struct lu_carrier *c)
{
    __atomic_store_n(&c->qsbr.seq, QSBR_OFFLINE, __ATOMIC_RELEASE);
}

/* Moves the write sequence on and asks every attached carrier to report
   it (LU_POLL_QSBR): compiled code reports only when its poll word asks. A
   carrier that attaches later starts from the new sequence. */
uint64_t
lu_qsbr_advance(void)
{
    uint64_t seq = __atomic_add_fetch(&_PyRuntime.qsbr.wr_seq, QSBR_INCR, __ATOMIC_SEQ_CST);
    pthread_mutex_lock(&_PyRuntime.qsbr.lock.mutex);
    for (struct lu_carrier *c = _PyRuntime.qsbr.head; c != NULL; c = c->qsbr.next) {
        if (__atomic_load_n(&c->qsbr.seq, __ATOMIC_ACQUIRE) != QSBR_OFFLINE) {
            lu_poll_set(c, LU_POLL_QSBR);
        }
    }
    pthread_mutex_unlock(&_PyRuntime.qsbr.lock.mutex);
    return seq;
}

uint64_t
lu_qsbr_next_goal(void)
{
    return __atomic_load_n(&_PyRuntime.qsbr.wr_seq, __ATOMIC_ACQUIRE) + QSBR_INCR;
}

#define advance lu_qsbr_advance
#define next_goal lu_qsbr_next_goal

/* The read sequence: the least sequence an attached carrier has seen, or the
   write sequence when none is attached. */
static uint64_t
scan(void)
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    pthread_mutex_lock(&_PyRuntime.qsbr.lock.mutex);
    uint64_t min = __atomic_load_n(&_PyRuntime.qsbr.wr_seq, __ATOMIC_ACQUIRE);
    for (struct lu_carrier *c = _PyRuntime.qsbr.head; c != NULL; c = c->qsbr.next) {
        uint64_t seq = __atomic_load_n(&c->qsbr.seq, __ATOMIC_ACQUIRE);
        if (seq != QSBR_OFFLINE && QSBR_LT(seq, min)) {
            min = seq;
        }
    }
    pthread_mutex_unlock(&_PyRuntime.qsbr.lock.mutex);
    uint64_t rd = __atomic_load_n(&_PyRuntime.qsbr.rd_seq, __ATOMIC_ACQUIRE);
    if (QSBR_LT(rd, min)) {
        /* A failed exchange means another carrier moved it at least as far. */
        __atomic_compare_exchange_n(&_PyRuntime.qsbr.rd_seq, &rd, min, 0,
                                    __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
        rd = min;
    }
    return rd;
}

int
lu_qsbr_goal_reached(uint64_t goal)
{
    uint64_t rd = __atomic_load_n(&_PyRuntime.qsbr.rd_seq, __ATOMIC_ACQUIRE);
    return QSBR_LEQ(goal, rd) || QSBR_LEQ(goal, scan());
}

#define goal_reached lu_qsbr_goal_reached

static void
push(struct lu_qsbr_queue *q, void *ptr, uint64_t goal)
{
    if (q->n == q->cap) {
        if (q->first > 0) {
            memmove(q->items, q->items + q->first, (q->n - q->first) * sizeof *q->items);
            q->n -= q->first;
            q->first = 0;
        }
        else {
            size_t cap = q->cap ? 2 * q->cap : 64;
            /* Runtime bookkeeping is libc's, not a hookable allocator domain. */
            struct lu_qsbr_item *items = realloc(q->items, cap * sizeof *items);
            if (items == NULL) {
                fputs("lucifer: out of memory in the QSBR queue\n", stderr);
                abort();
            }
            q->items = items;
            q->cap = cap;
        }
    }
    q->items[q->n++] = (struct lu_qsbr_item){ ptr, goal };
}

/* An item is memory to free or, tagged with the low bit, an object to
   release (_PyObject_XDecRefDelayed). */
static void
free_item(void *p)
{
    if ((uintptr_t)p & 1) {
        Py_DECREF((PyObject *)((uintptr_t)p - 1));
    }
    else {
        PyMem_Free(p);
    }
}

/* Frees the front of `q` whose goals are reached. Goals are in order, so the
   first one that is not ends it. A release can run a finalizer that queues
   or processes more, so each item leaves the queue before it is freed. */
static void
process_queue(struct lu_qsbr_queue *q)
{
    while (q->first < q->n && goal_reached(q->items[q->first].goal)) {
        free_item(q->items[q->first++].ptr);
    }
    if (q->first == q->n) {
        q->first = q->n = 0;
    }
}

/* Steals the orphaned queue, processes it, and gives back what remains. */
static void
process_orphans(void)
{
    struct lu_qsbr_queue taken;
    pthread_mutex_lock(&_PyRuntime.qsbr.lock.mutex);
    taken = _PyRuntime.qsbr.orphans;
    memset(&_PyRuntime.qsbr.orphans, 0, sizeof _PyRuntime.qsbr.orphans);
    pthread_mutex_unlock(&_PyRuntime.qsbr.lock.mutex);
    if (taken.n == 0) {
        free(taken.items);
        return;
    }
    process_queue(&taken);
    if (taken.n > taken.first) {
        pthread_mutex_lock(&_PyRuntime.qsbr.lock.mutex);
        for (size_t i = taken.first; i < taken.n; i++) {
            push(&_PyRuntime.qsbr.orphans, taken.items[i].ptr, taken.items[i].goal);
        }
        pthread_mutex_unlock(&_PyRuntime.qsbr.lock.mutex);
    }
    free(taken.items);
}

void
lu_qsbr_process(struct lu_carrier *c)
{
    c->qsbr.should_process = 0;
    process_queue(&c->qsbr.queue);
    process_orphans();
}

/* Empties `q`: frees its memory now and hands each object to `release`. */
static void
take_queue(struct lu_qsbr_queue *q, void (*release)(PyObject *, void *), void *arg)
{
    for (size_t i = q->first; i < q->n; i++) {
        void *p = q->items[i].ptr;
        if ((uintptr_t)p & 1) {
            release((PyObject *)((uintptr_t)p - 1), arg);
        }
        else {
            PyMem_Free(p);
        }
    }
    q->first = q->n = 0;
}

/* With the world stopped every other task is at a poll or detached, so no
   reader holds anything queued: every queue and the orphans are due. Each
   object's reference goes to `release`, which drops it without running a
   dealloc there (the collector merges it, as CPython's process_delayed_frees
   does), so a replaced dict nothing else holds dies in this collection. */
void
lu_qsbr_take_all(void (*release)(PyObject *, void *), void *arg)
{
    pthread_mutex_lock(&_PyRuntime.qsbr.lock.mutex);
    for (struct lu_carrier *c = _PyRuntime.qsbr.head; c != NULL; c = c->qsbr.next) {
        take_queue(&c->qsbr.queue, release, arg);
    }
    take_queue(&_PyRuntime.qsbr.orphans, release, arg);
    pthread_mutex_unlock(&_PyRuntime.qsbr.lock.mutex);
}

/* An exiting carrier, with no task attached: goes offline and leaves its items behind. */
void
lu_qsbr_unregister(struct lu_carrier *c)
{
    lu_qsbr_detach(c);
    pthread_mutex_lock(&_PyRuntime.qsbr.lock.mutex);
    struct lu_qsbr_queue *q = &c->qsbr.queue;
    for (size_t i = q->first; i < q->n; i++) {
        push(&_PyRuntime.qsbr.orphans, q->items[i].ptr, q->items[i].goal);
    }
    struct lu_carrier **p = &_PyRuntime.qsbr.head;
    while (*p != c) {
        p = &(*p)->qsbr.next;
    }
    *p = c->qsbr.next;
    pthread_mutex_unlock(&_PyRuntime.qsbr.lock.mutex);
    free(q->items);
    memset(q, 0, sizeof *q);
}

/* Frees `p` once no lock-free reader can still hold it. The caller has
   unlinked it; readers that began before may read it until they poll.
   While the world is stopped no reader runs. */
LU_EXPORT void
lu_qsbr_retire(void *p)
{
    if (p == NULL) {
        return;
    }
    struct lu_entry *e = lu_current_entry();
    if (lu_world_stopped(e->interp) || e->interp->tasks_closed) {
        free_item(p);
        return;
    }
    struct lu_carrier *c = e->carrier;
    uint64_t goal;
    if (++c->qsbr.deferred_count > DEFERRED_LIMIT) {
        c->qsbr.deferred_count = 0;
        c->qsbr.should_process = 1;
        lu_poll_set(c, LU_POLL_EXPLICIT_MERGE);     /* the poll handler processes it */
        goal = advance();
    }
    else {
        goal = next_goal();
    }
    push(&c->qsbr.queue, p, goal);
}

/* Releases `op` once no lock-free reader can still hold it (Objects/obmalloc.c). */
LU_EXPORT void
_PyObject_XDecRefDelayed(PyObject *op)
{
    if (op != NULL && !_Py_IsImmortal(op)) {
        lu_qsbr_retire((void *)((uintptr_t)op | 1));
    }
}
