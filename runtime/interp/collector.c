/* The cycle collector: trial deletion over the GC heaps, a port of the
   collection half of Python/gc_free_threading.c (DESIGN 4.3).

   Extensions hold references the runtime cannot see, so reachability is
   computed from reference counts, not from roots: an object whose count is
   larger than the references to it from other tracked objects is held from
   outside and keeps everything it reaches alive. The rest is garbage.

   Invariants it relies on:

   1. Everything runs on the collecting task, which is attached, with the
      world stopped inside the two windows below (lu_stw_stop). Every other
      task is suspended at a poll or detached in a syscall bracket, and
      neither touches a refcount or a heap, so the walks read heaps and
      counts without atomics. Finalizers, weakref callbacks and tp_clear run
      outside the windows, with the world restarted.
   2. A frame's references are counted. A reference in a running or parked
      task's frame is therefore an outside reference, and the stacks need not
      be scanned. A frame that stops counting must add its references to
      gc_refs after update_refs, as gc_visit_thread_stacks does: a frame a
      direct call pushed (jit_lent) borrows its code, which f.__code__ =
      ... may leave no other holder of (borrowed_code_refs).
   3. A dying object is untracked before any code of its dealloc runs
      (_Py_Dealloc), so a collection from inside a __del__ or a weakref
      callback never sees an object with a zero count and live fields.
   4. ob_tid is the owning carrier's id; for a collection's duration it holds
      gc_refs, and afterwards for garbage the link of a worklist. Neither
      is a carrier id, so nothing mistakes the object for its own: the
      refcount functions take their shared path. gc_restore_tid gives the
      owner back from the segment of the block, as the allocator knows it.
   5. Heaps are visited by walking every registered carrier's GC heaps and
      the abandoned segments; a retiring carrier moves its heaps from the
      one to the other under heaps_lock, which a window holds.
   6. Objects other carriers own are handled alike: their counts are merged
      first (the biased queues), so each object's count is its true count.
      Garbage is merged and held by the collector's worklist reference, so
      finalizers running on any carrier use the shared path on it.
   7. Inline caches name deferred objects without owning them, and only a
      collection frees those: when the garbage holds a deferred object, every
      code object's caches forget the garbage before the world restarts
      (lu_specialize_forget), so a cache never names freed memory. A
      shutdown collection ends deferral for all, so the caches forget
      everything they name. */

#include "interp/exceptions.h"
#include "interp/specialize.h"
#include "prim/prim.h"
#include "lucifer.h"

#include <stdio.h>
#include <time.h>

/* Objects linked through ob_tid. */
struct worklist {
    uintptr_t head;
};

struct visitor_args {
    size_t offset;              /* of the PyObject from the start of the block */
};

struct collection_state {
    struct visitor_args base;   /* first: the heap visitors' argument */
    struct _is *interp;
    struct lu_gc *gc;
    int reason;
    int debug;                          /* gc.set_debug flags; none for a stress collection */
    Py_ssize_t collected;
    Py_ssize_t uncollectable;
    Py_ssize_t long_lived_total;
    int forget;                         /* caches may name the garbage (invariant 7) */
    struct worklist unreachable;        /* garbage, each holding a reference */
    struct worklist legacy_finalizers;  /* garbage with tp_del: uncollectable */
    struct worklist wrcb_to_call;       /* weakrefs whose callbacks run */
    struct worklist objs_to_decref;     /* dead objects to release once restarted */
};

#define WORKSTACK_FOR_EACH(stack, op) \
    for ((op) = (PyObject *)(stack)->head; (op) != NULL; (op) = (PyObject *)(op)->ob_tid)

static void
worklist_push(struct worklist *worklist, PyObject *op)
{
    op->ob_tid = worklist->head;
    worklist->head = (uintptr_t)op;
}

static PyObject *
worklist_pop(struct worklist *worklist)
{
    PyObject *op = (PyObject *)worklist->head;
    if (op != NULL) {
        worklist->head = op->ob_tid;
        __atomic_store_n(&op->ob_tid, 0, __ATOMIC_RELAXED);
    }
    return op;
}

/* Removes from the list every object for which keep is false, preserving
   neither order nor identity beyond the survivors: the callers' lists are
   sets. `drop` receives each removed object, with ob_tid cleared. */
static void
worklist_filter(struct worklist *worklist, int (*keep)(PyObject *),
                void (*drop)(PyObject *, struct collection_state *), struct collection_state *state)
{
    uintptr_t *link = &worklist->head;
    while (*link != 0) {
        PyObject *op = (PyObject *)*link;
        if (keep(op)) {
            link = &op->ob_tid;
        }
        else {
            *link = op->ob_tid;
            op->ob_tid = 0;
            drop(op, state);
        }
    }
}

#define gc_refs(op) ((Py_ssize_t)(op)->ob_tid)

static inline int
gc_is_unreachable(PyObject *op)
{
    return (op->ob_gc_bits & LU_GC_UNREACHABLE) != 0;
}

static inline int
gc_is_alive(PyObject *op)
{
    return (op->ob_gc_bits & LU_GC_ALIVE) != 0;
}

static inline int
gc_is_frozen(PyObject *op)
{
    return (op->ob_gc_bits & LU_GC_FROZEN) != 0;
}

static inline int
is_merged(PyObject *op)
{
    return (op->ob_ref_shared & _Py_REF_SHARED_FLAG_MASK) == _Py_REF_MERGED;
}

/* ob_tid becomes gc_refs, starting at zero, when the object is first reached. */
static void
gc_maybe_init_refs(PyObject *op)
{
    if (!gc_is_unreachable(op)) {
        op->ob_gc_bits |= LU_GC_UNREACHABLE;
        op->ob_tid = 0;
    }
}

/* Gives the object up to the shared count, adding `extra` references. */
static Py_ssize_t
merge_refcount(PyObject *op, Py_ssize_t extra)
{
    Py_ssize_t refcount = Py_REFCNT(op) + extra;
    op->ob_tid = 0;
    op->ob_ref_local = 0;
    op->ob_ref_shared = _Py_REF_SHARED(refcount, _Py_REF_MERGED);
    return refcount;
}

static void
disable_deferred_refcounting(PyObject *op)
{
    if (lu_has_deferred_refcount(op)) {
        op->ob_gc_bits &= ~LU_GC_DEFERRED;
        op->ob_ref_shared -= _Py_REF_SHARED(LU_REF_DEFERRED, 0);
        merge_refcount(op, 0);
    }
}

/* Restores the owner in ob_tid: the carrier whose segment the block is in,
   or none for an abandoned segment, whose objects are merged. */
static void
gc_restore_tid(PyObject *op)
{
    if (is_merged(op)) {
        op->ob_tid = 0;
    }
    else {
        op->ob_tid = __atomic_load_n(&_mi_ptr_segment(op)->thread_id, __ATOMIC_RELAXED);
        if (op->ob_tid == 0) {
            merge_refcount(op, 0);
        }
    }
}

static void
gc_restore_refs(PyObject *op)
{
    if (gc_is_unreachable(op)) {
        gc_restore_tid(op);
        op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
    }
    else {
        op->ob_gc_bits &= ~LU_GC_ALIVE;
    }
}

/* ---- Walking the heaps ---- */

/* A type that is GC but traverses nothing is sound: what it references stays
   counted as held from outside until it dies, so only cycles through it are
   not found. */
static int
traverse(PyObject *op, visitproc visit, void *arg)
{
    traverseproc tp_traverse = Py_TYPE(op)->tp_traverse;
    return tp_traverse != NULL ? tp_traverse(op, visit, arg) : 0;
}

/* The tracked, unfrozen object in a block, or NULL. */
static PyObject *
op_from_block(void *block, void *arg, int include_frozen)
{
    if (block == NULL) {
        return NULL;
    }
    PyObject *op = (PyObject *)((char *)block + ((struct visitor_args *)arg)->offset);
    if (!lu_gc_is_tracked(op) || (!include_frozen && gc_is_frozen(op))) {
        return NULL;
    }
    return op;
}

/* Visits every GC object of the interpreter: the GC heaps of each carrier
   registered in it, and the segments carriers left behind. The caller holds
   the stopped world and heaps_lock; the visitor allocates and frees no
   mimalloc memory. */
static void
visit_heaps(struct _is *interp, mi_block_visit_fun *visitor, struct visitor_args *args)
{
    size_t plain = 0, pre = 2 * sizeof(PyObject *);
    for (struct lu_carrier *c = __atomic_load_n(&_PyRuntime.qsbr.head, __ATOMIC_ACQUIRE); c != NULL;
         c = c->qsbr.next) {
        for (struct lu_entry *e = c->entries; e != NULL; e = e->next) {
            if (e->interp != interp) {
                continue;
            }
            args->offset = plain;
            mi_heap_visit_blocks(&e->mimalloc.heaps[LU_HEAP_GC], true, visitor, args);
            args->offset = pre;
            mi_heap_visit_blocks(&e->mimalloc.heaps[LU_HEAP_GC_PRE], true, visitor, args);
        }
    }
    args->offset = plain;
    mi_abandoned_visit_blocks(interp->subproc, LU_HEAP_GC, true, visitor, args);
    args->offset = pre;
    mi_abandoned_visit_blocks(interp->subproc, LU_HEAP_GC_PRE, true, visitor, args);
}

static void
stop_the_world(struct _is *interp)
{
    lu_stw_stop(interp);
    pthread_mutex_lock(&_PyRuntime.heaps_lock.mutex);
}

static void
start_the_world(struct _is *interp)
{
    pthread_mutex_unlock(&_PyRuntime.heaps_lock.mutex);
    lu_stw_start(interp);
}

/* ---- Deducing the unreachable set ---- */

/* An object another thread queued to its owner is merged now: the queue's
   reference is dropped, and if that was the last, a dead object is released
   after the restart (a tracked one is found as garbage with no references). */
static void
merge_queued_object(PyObject *op, void *arg)
{
    struct collection_state *state = arg;
    if (merge_refcount(op, -1) == 0 && (!lu_gc_is_tracked(op) || gc_is_frozen(op))) {
        op->ob_ref_shared += 1 << _Py_REF_SHARED_SHIFT;
        worklist_push(&state->objs_to_decref, op);
    }
}

/* Subtracts an incoming reference from the computed gc_refs. */
static int
visit_decref(PyObject *op, void *arg)
{
    if (lu_gc_is_tracked(op) && !_Py_IsImmortal(op) && !gc_is_frozen(op) && !gc_is_alive(op)) {
        gc_maybe_init_refs(op);
        op->ob_tid -= 1;
    }
    return 0;
}

/* _PyObject_GC_MAY_BE_TRACKED. */
static int
may_be_tracked(PyObject *op)
{
    return lu_is_gc(op) && (!PyTuple_CheckExact(op) || lu_gc_is_tracked(op));
}

/* Untracks objects that can never be in a cycle: a tuple of objects that
   can't be tracked. */
static int
gc_maybe_untrack(PyObject *op)
{
    if (!PyTuple_CheckExact(op)) {
        return 0;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(op); i++) {
        PyObject *item = PyTuple_GET_ITEM(op, i);
        if (item == NULL || may_be_tracked(item)) {
            return 0;
        }
    }
    op->ob_gc_bits &= ~LU_GC_TRACKED;
    return 1;
}

/* Computes gc_refs, the references from outside the tracked objects, in
   ob_tid. */
static bool
update_refs(const mi_heap_t *heap, const mi_heap_area_t *area, void *block, size_t block_size,
            void *args)
{
    PyObject *op = op_from_block(block, args, 0);
    if (op == NULL || gc_is_alive(op)) {
        return true;
    }
    if (_Py_IsImmortal(op)) {
        op->ob_tid = 0;
        op->ob_gc_bits &= ~(LU_GC_TRACKED | LU_GC_UNREACHABLE);
        return true;
    }
    Py_ssize_t refcount = Py_REFCNT(op);
    if (lu_has_deferred_refcount(op)) {
        refcount -= LU_REF_DEFERRED;
    }
    if (refcount > 0 && !lu_has_deferred_refcount(op) && gc_maybe_untrack(op)) {
        gc_restore_refs(op);
        return true;
    }
    gc_maybe_init_refs(op);
    op->ob_tid += refcount;
    traverse(op, visit_decref, NULL);
    return true;
}

static int
visit_clear_unreachable(PyObject *op, void *stack)
{
    if (gc_is_unreachable(op)) {
        op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
        lu_stack_push(stack, op);
    }
    return 0;
}

/* Clears the unreachable bit of everything reachable from op. */
static void
mark_reachable(PyObject *op)
{
    struct lu_stack stack = { NULL, 0, 0 };
    do {
        traverse(op, visit_clear_unreachable, &stack);
        op = stack.n > 0 ? stack.items[--stack.n] : NULL;
    } while (op != NULL);
    free(stack.items);
}

/* Objects held from outside, and what they reach, are reachable. */
static bool
mark_heap_visitor(const mi_heap_t *heap, const mi_heap_area_t *area, void *block, size_t block_size,
                  void *args)
{
    PyObject *op = op_from_block(block, args, 0);
    if (op == NULL || gc_is_alive(op) || !gc_is_unreachable(op)) {
        return true;
    }
    if (gc_refs(op) != 0) {
        op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
        mark_reachable(op);
    }
    return true;
}

static int
has_legacy_finalizer(PyObject *op)
{
    return Py_TYPE(op)->tp_del != NULL;
}

/* Collects the remaining unreachable objects, merged and held by a
   reference of the worklist; restores ob_tid of the rest. */
static bool
scan_heap_visitor(const mi_heap_t *heap, const mi_heap_area_t *area, void *block, size_t block_size,
                  void *args)
{
    if (block == NULL) {
        return true;
    }
    struct collection_state *state = (struct collection_state *)args;
    PyObject *op = (PyObject *)((char *)block + state->base.offset);
    /* The cost of a collection is the number of blocks, tracked or not. */
    state->long_lived_total++;
    if (!lu_gc_is_tracked(op) || gc_is_frozen(op)) {
        return true;
    }
    if (gc_is_unreachable(op)) {
        /* Collected as soon as it is finalized. */
        state->forget |= lu_has_deferred_refcount(op);
        disable_deferred_refcounting(op);
        merge_refcount(op, 1);
        if (has_legacy_finalizer(op)) {
            op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
            worklist_push(&state->legacy_finalizers, op);
        }
        else {
            worklist_push(&state->unreachable, op);
        }
        state->long_lived_total--;
        return true;
    }
    if (state->reason == LU_GC_REASON_SHUTDOWN) {
        disable_deferred_refcounting(op);
    }
    gc_restore_tid(op);
    op->ob_gc_bits &= ~LU_GC_ALIVE;
    return true;
}

static int
still_unreachable(PyObject *op)
{
    return gc_is_unreachable(op);
}

static void
to_legacy_finalizers(PyObject *op, struct collection_state *state)
{
    worklist_push(&state->legacy_finalizers, op);
}

/* Everything reachable from an object with a legacy finalizer is
   uncollectable too. */
static void
move_legacy_finalizer_reachable(struct collection_state *state)
{
    PyObject *op;
    WORKSTACK_FOR_EACH(&state->legacy_finalizers, op) {
        mark_reachable(op);
    }
    worklist_filter(&state->unreachable, still_unreachable, to_legacy_finalizers, state);
}

static int
not_immortal(PyObject *op)
{
    return !_Py_IsImmortal(op);
}

/* Invariant 7: code objects' caches forget the garbage, frozen code too. */
static bool
forget_heap_visitor(const mi_heap_t *heap, const mi_heap_area_t *area, void *block, size_t block_size,
                    void *args)
{
    PyObject *op = op_from_block(block, args, 1);
    if (op != NULL && PyCode_Check(op)) {
        int shutdown = ((struct collection_state *)args)->reason == LU_GC_REASON_SHUTDOWN;
        lu_specialize_forget((PyCodeObject *)op, shutdown ? not_immortal : gc_is_unreachable);
    }
    return true;
}

/* Invariant 2 for the code a direct call's frame borrows: an outside
   reference, in every task's chain. */
static void
borrowed_code_refs(struct _is *interp)
{
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        for (struct lu_frame *f = (struct lu_frame *)ts->current_frame; f != NULL; f = f->previous) {
            PyObject *code = (PyObject *)f->code;
            if (f->jit_lent && lu_gc_is_tracked(code) && !_Py_IsImmortal(code) && !gc_is_frozen(code)
                && !gc_is_alive(code)) {
                gc_maybe_init_refs(code);
                code->ob_tid += 1;
            }
        }
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
}

static void
deduce_unreachable_heap(struct collection_state *state)
{
    struct _is *interp = state->interp;
    visit_heaps(interp, update_refs, &state->base);
    borrowed_code_refs(interp);
    visit_heaps(interp, mark_heap_visitor, &state->base);
    visit_heaps(interp, scan_heap_visitor, &state->base);
    if (state->legacy_finalizers.head) {
        move_legacy_finalizer_reachable(state);
    }
    if (state->forget || state->reason == LU_GC_REASON_SHUTDOWN) {
        visit_heaps(interp, forget_heap_visitor, &state->base);
    }
}

/* ---- Weakrefs, finalizers, resurrection ---- */

static void
debug_cycle(const char *msg, PyObject *op)
{
    fprintf(stderr, "gc: %s <%s %p>\n", msg, Py_TYPE(op)->tp_name, (void *)op);
}

/* Clears every weakref to an unreachable object. With enqueue_callbacks, the
   weakrefs whose callbacks must run are queued, not called. */
static void
clear_weakrefs(struct collection_state *state, int enqueue_callbacks)
{
    PyObject *op;
    WORKSTACK_FOR_EACH(&state->unreachable, op) {
        if (PyWeakref_Check(op)) {
            /* Unreachable weakrefs are cleared so that their callbacks never
               run from a tp_clear later (bpo-38006). */
            _PyWeakref_ClearRef((PyWeakReference *)op);
        }
        PyWeakReference **list = lu_weakref_list(op);
        if (list == NULL) {
            continue;
        }
        for (PyWeakReference *wr = *list; wr != NULL; wr = *list) {
            _PyWeakref_ClearRef(wr);        /* also changes *list */
            if (!enqueue_callbacks) {
                continue;
            }
            /* No callback for a weakref that is itself unreachable, or dead
               already: its free waits in a deep dealloc chain's queue. */
            if (wr->wr_callback == NULL || gc_is_unreachable((PyObject *)wr)
                || Py_REFCNT((PyObject *)wr) == 0) {
                continue;
            }
            merge_refcount((PyObject *)wr, 1);
            worklist_push(&state->wrcb_to_call, (PyObject *)wr);
        }
    }
}

static void
call_weakref_callbacks(struct collection_state *state)
{
    PyObject *op;
    while ((op = worklist_pop(&state->wrcb_to_call)) != NULL) {
        PyObject *callback = ((PyWeakReference *)op)->wr_callback;
        PyObject *result = lu_call(callback, &op, 1, NULL);
        if (result == NULL) {
            lu_err_unraisable("calling weakref callback", callback);
        }
        Py_XDECREF(result);
        Py_DECREF(op);
    }
}

/* Runs the first-time finalizers of the garbage. A finalizer may drop the
   last outside reference of other garbage, which stays alive on the list. */
static void
finalize_garbage(struct collection_state *state)
{
    PyObject *op;
    WORKSTACK_FOR_EACH(&state->unreachable, op) {
        destructor finalize = Py_TYPE(op)->tp_finalize;
        if (!(op->ob_gc_bits & LU_GC_FINALIZED) && finalize != NULL) {
            op->ob_gc_bits |= LU_GC_FINALIZED;
            finalize(op);
            if (PyErr_Occurred()) {
                lu_err_unraisable_text("Exception ignored in garbage collection", op);
            }
        }
    }
}

static int
visit_decref_unreachable(PyObject *op, void *data)
{
    if (gc_is_unreachable(op) && lu_gc_is_tracked(op)) {
        op->ob_ref_local -= 1;
    }
    return 0;
}

static int
unreachable_and_tracked(PyObject *op)
{
    return lu_gc_is_tracked(op);
}

static void
untracked_by_finalizer(PyObject *op, struct collection_state *state)
{
    op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
    worklist_push(&state->objs_to_decref, op);
}

static int
not_reachable_again(PyObject *op)
{
    return gc_is_unreachable(op);
}

static void
resurrected(PyObject *op, struct collection_state *state)
{
    merge_refcount(op, -1);     /* the worklist's reference */
    state->long_lived_total++;
}

/* Objects a finalizer made reachable again, and what they reach, come off the
   list. The garbage's own references are subtracted from their counts in
   ob_ref_local (ob_tid holds the list); what remains is held from outside. */
static void
handle_resurrected_objects(struct collection_state *state)
{
    PyObject *op;
    worklist_filter(&state->unreachable, unreachable_and_tracked, untracked_by_finalizer, state);
    WORKSTACK_FOR_EACH(&state->unreachable, op) {
        Py_ssize_t refcount = op->ob_ref_shared >> _Py_REF_SHARED_SHIFT;
        if (refcount > INT32_MAX) {
            /* More than ob_ref_local can hold: it stays. */
            op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
            lu_set_immortal(op);
            continue;
        }
        op->ob_ref_local += (uint32_t)refcount;
        op->ob_ref_local -= 1;          /* the worklist's reference */
        traverse(op, visit_decref_unreachable, NULL);
    }
    int any_resurrected = 0;
    WORKSTACK_FOR_EACH(&state->unreachable, op) {
        int32_t refs = (int32_t)op->ob_ref_local;
        op->ob_ref_local = 0;
        if (gc_is_unreachable(op) && refs > 0) {
            any_resurrected = 1;
            op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
            mark_reachable(op);
        }
    }
    if (any_resurrected) {
        worklist_filter(&state->unreachable, not_reachable_again, resurrected, state);
    }
}

/* Breaks the cycles by clearing the containers. */
static void
delete_garbage(struct collection_state *state)
{
    struct lu_gc *gc = state->gc;
    PyObject *op;
    while ((op = worklist_pop(&state->objs_to_decref)) != NULL) {
        Py_DECREF(op);
    }
    while ((op = worklist_pop(&state->unreachable)) != NULL) {
        op->ob_gc_bits &= ~LU_GC_UNREACHABLE;
        if (!lu_gc_is_tracked(op)) {
            Py_DECREF(op);              /* untracked by another tp_clear */
            continue;
        }
        state->collected++;
        if (state->debug & LU_GC_DEBUG_SAVEALL) {
            if (gc->garbage == NULL || lu_list_append(gc->garbage, op) < 0) {
                PyErr_Clear();
            }
        }
        else if (Py_TYPE(op)->tp_clear != NULL) {
            Py_TYPE(op)->tp_clear(op);
            if (PyErr_Occurred()) {
                lu_err_unraisable_text("Exception ignored in tp_clear", op);
            }
        }
        Py_DECREF(op);
    }
}

static void
handle_legacy_finalizers(struct collection_state *state)
{
    struct lu_gc *gc = state->gc;
    PyObject *op;
    while ((op = worklist_pop(&state->legacy_finalizers)) != NULL) {
        state->uncollectable++;
        if (state->debug & LU_GC_DEBUG_UNCOLLECTABLE) {
            debug_cycle("uncollectable", op);
        }
        if (((state->debug & LU_GC_DEBUG_SAVEALL) || has_legacy_finalizer(op)) && gc->garbage != NULL
            && lu_list_append(gc->garbage, op) < 0) {
            PyErr_Clear();
        }
        Py_DECREF(op);
    }
}

/* ---- A collection ---- */

static void
collect_internal(struct collection_state *state, int generation)
{
    struct _is *interp = state->interp;
    struct lu_gc *gc = state->gc;
    PyObject *op;

    stop_the_world(interp);
    if (state->reason != LU_GC_REASON_STRESS) {
        if (generation + 1 < 3) {
            gc->old_count[generation] += 1;
        }
        __atomic_store_n(&gc->young_count, 0, __ATOMIC_RELAXED);
        gc->deferred_count = 0;
        for (int i = 1; i <= generation; i++) {
            gc->old_count[i - 1] = 0;
        }
    }
    lu_brc_take_queued(interp, merge_queued_object, state);
    deduce_unreachable_heap(state);
    if (state->debug & LU_GC_DEBUG_COLLECTABLE) {
        WORKSTACK_FOR_EACH(&state->unreachable, op) {
            debug_cycle("collectable", op);
        }
    }
    clear_weakrefs(state, 1);
    start_the_world(interp);

    /* Callbacks and finalizers run with the world going, so that a lock one
       of them takes is not one a stopped task holds. */
    while ((op = worklist_pop(&state->objs_to_decref)) != NULL) {
        Py_DECREF(op);
    }
    call_weakref_callbacks(state);
    finalize_garbage(state);

    stop_the_world(interp);
    handle_resurrected_objects(state);
    /* No Python code may reach the garbage now: a finalizer outside the set
       that runs during delete_garbage must find no weakref to it. */
    clear_weakrefs(state, 0);
    if (state->reason != LU_GC_REASON_STRESS) {
        gc->long_lived_total = state->long_lived_total;
    }
    start_the_world(interp);

    delete_garbage(state);
    if (state->reason != LU_GC_REASON_STRESS) {
        gc->last_mem = lu_gc_mem_usage();
    }
    handle_legacy_finalizers(state);
}

void
lu_gc_collect_garbage(PyThreadState *ts, int generation, int reason, Py_ssize_t *collected,
                      Py_ssize_t *uncollectable)
{
    struct collection_state state = { .interp = ts->interp, .gc = &ts->interp->gc, .reason = reason,
                                      .debug = reason == LU_GC_REASON_STRESS ? 0 : ts->interp->gc.debug };
    collect_internal(&state, generation);
    *collected = state.collected;
    *uncollectable = state.uncollectable;
}

/* ---- Looking at the heap ---- */

struct object_list {
    struct visitor_args base;
    struct lu_stack objects;
    PyObject *objs;             /* get_referrers: the objects asked about */
    int (*select)(PyObject *, struct object_list *);
};

static int
select_all(PyObject *op, struct object_list *list)
{
    return 1;
}

static int
referrers_visit(PyObject *obj, void *arg)
{
    PyObject *objs = arg;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(objs); i++) {
        if (PyTuple_GET_ITEM(objs, i) == obj) {
            return 1;
        }
    }
    return 0;
}

static int
select_referrers(PyObject *op, struct object_list *list)
{
    return op != list->objs && traverse(op, referrers_visit, list->objs);
}

/* The objects the collector would see, held by a reference of the list. */
static bool
collect_objects(const mi_heap_t *heap, const mi_heap_area_t *area, void *block, size_t block_size,
                void *args)
{
    PyObject *op = op_from_block(block, args, 0);
    struct object_list *list = args;
    if (op != NULL && !gc_is_unreachable(op) && list->select(op, list)) {
        Py_INCREF(op);
        lu_stack_push(&list->objects, op);
    }
    return true;
}

/* A list of the selected objects. The list is built after the world restarts:
   allocating while the heaps are walked could reclaim a segment under it. */
static PyObject *
objects_list(struct _is *interp, PyObject *objs, int (*select)(PyObject *, struct object_list *))
{
    struct object_list list = { .objs = objs, .select = select };
    stop_the_world(interp);
    visit_heaps(interp, collect_objects, &list.base);
    start_the_world(interp);
    PyObject *result = PyList_New(list.objects.n);
    for (size_t i = 0; i < list.objects.n; i++) {
        if (result != NULL) {
            PyList_SET_ITEM(result, i, list.objects.items[i]);
        }
        else {
            Py_DECREF(list.objects.items[i]);
        }
    }
    free(list.objects.items);
    return result;
}

PyObject *
lu_gc_get_objects(struct _is *interp)
{
    return objects_list(interp, NULL, select_all);
}

PyObject *
lu_gc_get_referrers(struct _is *interp, PyObject *objs)
{
    return objects_list(interp, objs, select_referrers);
}

struct freeze_args {
    struct visitor_args base;
    Py_ssize_t count;
    int freeze;                 /* 1 freeze, 0 unfreeze, -1 count */
};

static bool
freeze_visitor(const mi_heap_t *heap, const mi_heap_area_t *area, void *block, size_t block_size,
               void *args)
{
    struct freeze_args *a = args;
    PyObject *op = op_from_block(block, args, 1);
    if (op == NULL) {
        return true;
    }
    if (a->freeze > 0 && !gc_is_unreachable(op)) {
        op->ob_gc_bits |= LU_GC_FROZEN;
    }
    else if (a->freeze == 0) {
        op->ob_gc_bits &= ~LU_GC_FROZEN;
    }
    else if (a->freeze < 0 && gc_is_frozen(op)) {
        a->count++;
    }
    return true;
}

/* gc.freeze, gc.unfreeze and gc.get_freeze_count; returns the frozen count. */
Py_ssize_t
lu_gc_freeze(struct _is *interp, int mode)
{
    struct freeze_args args = { .freeze = mode };
    stop_the_world(interp);
    if (mode >= 0) {
        interp->gc.freeze_active = mode;
    }
    visit_heaps(interp, freeze_visitor, &args.base);
    start_the_world(interp);
    return args.count;
}

struct visit_args {
    struct visitor_args base;
    int (*callback)(PyObject *, void *);
    void *arg;
    int more;
};

static bool
visit_each(const mi_heap_t *heap, const mi_heap_area_t *area, void *block, size_t block_size,
           void *args)
{
    struct visit_args *a = args;
    PyObject *op = op_from_block(block, args, 1);  /* frozen objects are visitors' too */
    if (op != NULL && a->more) {
        a->more = a->callback(op, a->arg);
    }
    return a->more;
}

/* PyUnstable_GC_VisitObjects. */
void
PyUnstable_GC_VisitObjects(int (*callback)(PyObject *, void *), void *arg)
{
    struct _is *interp = PyInterpreterState_Get();
    struct visit_args args = { .callback = callback, .arg = arg, .more = 1 };
    stop_the_world(interp);
    visit_heaps(interp, visit_each, &args.base);
    start_the_world(interp);
}
