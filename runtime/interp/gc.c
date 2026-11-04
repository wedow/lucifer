/* The collector's policy and its entry points (the rest of
   Python/gc_free_threading.c and Modules/gcmodule.c); the cycle collector
   itself is collector.c.

   Allocating a GC object counts it, and when the count passes the young
   threshold, and the process has grown or enough has been allocated, the GC
   reason is raised in the allocating carrier's poll word. The collection
   itself runs only from the poll handler, at a call or a back-edge, where
   every frame is published (FOUNDATIONS 2.5).

   The counts are as CPython's: each carrier buffers 512 allocations before
   adding them to the interpreter's count, and frees subtract the same way,
   so a loop that allocates and frees temporaries never collects. */

#include "prim/prim.h"
#include "interp/exceptions.h"
#include "interp/code.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define LOCAL_ALLOC_COUNT_THRESHOLD 512

/* get_process_mem_usage: resident plus swapped memory in KB, or -1. */
Py_ssize_t
lu_gc_mem_usage(void)
{
    FILE *fp = fopen("/proc/self/status", "r");
    if (fp == NULL) {
        return -1;
    }
    char line[256];
    long long rss = -1, swap = -1;
    while (fgets(line, sizeof line, fp) != NULL) {
        if (rss == -1 && strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%lld", &rss);
        }
        else if (swap == -1 && strncmp(line, "VmSwap:", 7) == 0) {
            sscanf(line + 7, "%lld", &swap);
        }
        if (rss != -1 && swap != -1) {
            break;
        }
    }
    fclose(fp);
    return rss != -1 && swap != -1 ? (Py_ssize_t)(rss + swap) : -1;
}

/* gc_should_collect_mem_usage: collect when the process has grown, or too
   many containers were deferred; otherwise defer these. */
static int
should_collect_mem_usage(struct lu_gc *gc)
{
    Py_ssize_t mem = lu_gc_mem_usage();
    if (mem < 0) {
        return 1;
    }
    if (__atomic_load_n(&gc->deferred_count, __ATOMIC_RELAXED) > (Py_ssize_t)gc->young_threshold * 40) {
        return 1;
    }
    Py_ssize_t last_mem = __atomic_load_n(&gc->last_mem, __ATOMIC_RELAXED);
    Py_ssize_t mem_threshold = last_mem / 10 > 128 ? last_mem / 10 : 128;
    if (mem - last_mem > mem_threshold) {
        return 1;
    }
    int young = __atomic_exchange_n(&gc->young_count, 0, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&gc->deferred_count, young, __ATOMIC_RELAXED);
    return 0;
}

static int
should_collect(struct lu_gc *gc)
{
    int count = __atomic_load_n(&gc->young_count, __ATOMIC_RELAXED);
    int threshold = gc->young_threshold;
    if (count <= threshold || threshold == 0 || !__atomic_load_n(&gc->enabled, __ATOMIC_RELAXED)) {
        return 0;
    }
    if (gc->old_threshold[0] == 0) {
        /* A few tests rely on immediate scheduling of the GC. */
        return 1;
    }
    if (count < gc->long_lived_total / 4) {
        /* Scale the threshold to the number of live objects. */
        return 0;
    }
    return should_collect_mem_usage(gc);
}

void
lu_gc_record_allocation(struct lu_entry *e)
{
    struct lu_gc *gc = &e->interp->gc;
    if (++e->gc_alloc_count >= LOCAL_ALLOC_COUNT_THRESHOLD) {
        __atomic_fetch_add(&gc->young_count, e->gc_alloc_count, __ATOMIC_RELAXED);
        e->gc_alloc_count = 0;
        if (should_collect(gc) && !__atomic_load_n(&gc->collecting, __ATOMIC_RELAXED)) {
            lu_poll_set(e->carrier, LU_POLL_GC_SCHEDULED);
        }
    }
}

void
lu_gc_record_deallocation(struct lu_entry *e)
{
    struct lu_gc *gc = &e->interp->gc;
    if (--e->gc_alloc_count <= -LOCAL_ALLOC_COUNT_THRESHOLD) {
        int count = __atomic_load_n(&gc->young_count, __ATOMIC_RELAXED);
        int new_count;
        do {
            if (count == 0) {
                break;
            }
            new_count = count + e->gc_alloc_count;
            if (new_count < 0) {
                new_count = 0;
            }
        } while (!__atomic_compare_exchange_n(&gc->young_count, &count, new_count, 0,
                                              __ATOMIC_RELAXED, __ATOMIC_RELAXED));
        e->gc_alloc_count = 0;
    }
}

/* invoke_gc_callback: gc.py's runner calls each of gc.callbacks. */
static void
invoke_callback(PyThreadState *ts, const char *phase, int generation, Py_ssize_t collected,
                Py_ssize_t uncollectable)
{
    PyObject *hook = ts->interp->gc.hook;
    if (hook == NULL) {
        return;
    }
    PyObject *args[4] = { PyUnicode_FromString(phase), PyLong_FromLong(generation),
                          PyLong_FromLong((long)collected), PyLong_FromLong((long)uncollectable) };
    PyObject *result = args[0] && args[1] && args[2] && args[3] ? lu_call(hook, args, 4, NULL) : NULL;
    for (int i = 0; i < 4; i++) {
        Py_XDECREF(args[i]);
    }
    if (result == NULL) {
        lu_err_unraisable_text("Exception ignored in garbage collection", hook);
    }
    Py_XDECREF(result);
}

static double
seconds_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* gc_collect_main. */
static Py_ssize_t
collect(PyThreadState *ts, int generation, int reason)
{
    struct lu_gc *gc = &ts->interp->gc;
    int expected = 0;
    if (!__atomic_compare_exchange_n(&gc->collecting, &expected, 1, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
        return 0;               /* one is in progress */
    }
    if (reason == LU_GC_REASON_HEAP && !should_collect(gc)) {
        __atomic_store_n(&gc->collecting, 0, __ATOMIC_SEQ_CST);
        return 0;
    }
    PyObject *pending = PyErr_GetRaisedException();
    int visible = reason != LU_GC_REASON_SHUTDOWN && reason != LU_GC_REASON_STRESS;
    if (visible) {
        invoke_callback(ts, "start", generation, 0, 0);
    }
    double started = 0;
    int stats = visible && (gc->debug & LU_GC_DEBUG_STATS);
    if (stats) {
        fprintf(stderr, "gc: collecting generation %d...\n", generation);
        started = seconds_now();
    }
    Py_ssize_t collected, uncollectable;
    lu_gc_collect_garbage(ts, generation, reason, &collected, &uncollectable);
    if (stats) {
        fprintf(stderr, "gc: done, %zd unreachable, %zd uncollectable, %.4fs elapsed\n",
                collected + uncollectable, uncollectable, seconds_now() - started);
    }
    if (visible) {
        gc->stats[generation][0] += 1;
        gc->stats[generation][1] += collected;
        gc->stats[generation][2] += uncollectable;
        invoke_callback(ts, "stop", generation, collected, uncollectable);
    }
    PyErr_SetRaisedException(pending);
    __atomic_store_n(&gc->collecting, 0, __ATOMIC_SEQ_CST);
    return collected + uncollectable;
}

/* _Py_RunGC: the GC reason was raised. */
void
lu_gc_run(PyThreadState *ts)
{
    if (__atomic_load_n(&ts->interp->gc.enabled, __ATOMIC_RELAXED)) {
        collect(ts, 0, LU_GC_REASON_HEAP);
    }
}

void
lu_gc_stress(PyThreadState *ts)
{
    if (__atomic_load_n(&ts->interp->gc.enabled, __ATOMIC_RELAXED)) {
        collect(ts, 2, LU_GC_REASON_STRESS);
    }
}

/* PyGC_Collect and its siblings. */
Py_ssize_t
PyGC_Collect(void)
{
    PyThreadState *ts = PyThreadState_Get();
    if (!__atomic_load_n(&ts->interp->gc.enabled, __ATOMIC_RELAXED)) {
        return 0;
    }
    return collect(ts, 2, LU_GC_REASON_MANUAL);
}

/* _PyGC_CollectNoFail: the last collection, before the interpreter ends. */
void
lu_gc_collect_shutdown(PyThreadState *ts)
{
    collect(ts, 2, LU_GC_REASON_SHUTDOWN);
}

int
PyGC_Enable(void)
{
    struct lu_gc *gc = &PyInterpreterState_Get()->gc;
    return __atomic_exchange_n(&gc->enabled, 1, __ATOMIC_SEQ_CST);
}

int
PyGC_Disable(void)
{
    struct lu_gc *gc = &PyInterpreterState_Get()->gc;
    return __atomic_exchange_n(&gc->enabled, 0, __ATOMIC_SEQ_CST);
}

int
PyGC_IsEnabled(void)
{
    return __atomic_load_n(&PyInterpreterState_Get()->gc.enabled, __ATOMIC_SEQ_CST);
}

/* gc.py's primitives. */

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__ + 5, (n)); \
        return NULL; } } while (0)

/* The settings and counts gc_get and gc_set address by index. */
static int *
gc_field(struct lu_gc *gc, long index)
{
    switch (index) {
    case 0: return &gc->enabled;
    case 1: return &gc->young_threshold;
    case 2: return &gc->old_threshold[0];
    case 3: return &gc->old_threshold[1];
    case 4: return &gc->young_count;
    case 5: return &gc->old_count[0];
    case 6: return &gc->old_count[1];
    case 7: return &gc->debug;
    default: return NULL;
    }
}

static int *
field_arg(PyObject *const *args, Py_ssize_t nargs, long *index)
{
    int *field = NULL;
    if (!PyLong_CheckExact(args[0]) || lu_long_as_long(args[0], index) < 0) {
        return NULL;
    }
    field = gc_field(&PyInterpreterState_Get()->gc, *index);
    if (field == NULL) {
        lu_err_format(PyExc_ValueError, "no such collector field");
    }
    return field;
}

/* gc_get(index): enabled, the three thresholds, then the three counts. */
static PyObject *
prim_gc_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long index;
    int *field = field_arg(args, nargs, &index);
    return field != NULL ? PyLong_FromLong(__atomic_load_n(field, __ATOMIC_RELAXED)) : NULL;
}

/* gc_set(index, value): one of enabled or the thresholds. */
static PyObject *
prim_gc_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    long index, value;
    int *field = field_arg(args, nargs, &index);
    if (field == NULL || !PyLong_CheckExact(args[1]) || lu_long_as_long(args[1], &value) < 0) {
        return NULL;
    }
    __atomic_store_n(field, (int)value, __ATOMIC_SEQ_CST);
    return Py_NewRef(Py_None);
}

/* gc_hook(runner, garbage): gc.py's runner(phase, generation, collected,
   uncollectable), and the list gc.garbage the collector appends to. */
static PyObject *
prim_gc_hook(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    struct lu_gc *gc = &PyInterpreterState_Get()->gc;
    PyObject *old_hook = gc->hook, *old_garbage = gc->garbage;
    gc->hook = Py_NewRef(args[0]);
    gc->garbage = Py_NewRef(args[1]);
    Py_XDECREF(old_hook);
    Py_XDECREF(old_garbage);
    return Py_NewRef(Py_None);
}

/* gc_collect(generation): gc.collect() with the collector's counters. */
static PyObject *
prim_gc_collect(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long generation;
    if (!PyLong_CheckExact(args[0]) || lu_long_as_long(args[0], &generation) < 0) {
        return NULL;
    }
    return PyLong_FromLong((long)collect(PyThreadState_Get(), (int)generation, LU_GC_REASON_MANUAL));
}

/* gc_get_objects(), gc_get_referrers(objs): lists. */
static PyObject *
prim_gc_get_objects(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    return lu_gc_get_objects(PyInterpreterState_Get());
}

static PyObject *
prim_gc_get_referrers(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyTuple_CheckExact(args[0]) ? lu_gc_get_referrers(PyInterpreterState_Get(), args[0]) : NULL;
}

static int
referents_visit(PyObject *obj, void *list)
{
    return lu_list_append(list, obj) < 0;
}

/* gc_get_referents(objs): what each GC object among them references. */
static PyObject *
prim_gc_get_referents(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *objs = args[0], *result = PyList_New(0);
    for (Py_ssize_t i = 0; result != NULL && PyTuple_CheckExact(objs) && i < PyTuple_GET_SIZE(objs); i++) {
        PyObject *obj = PyTuple_GET_ITEM(objs, i);
        traverseproc traverse = lu_is_gc(obj) ? Py_TYPE(obj)->tp_traverse : NULL;
        if (traverse != NULL && traverse(obj, referents_visit, result)) {
            Py_CLEAR(result);
        }
    }
    return result;
}

/* gc_freeze(mode): 1 freezes, 0 unfreezes, -1 only counts; the frozen count. */
static PyObject *
prim_gc_freeze(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long mode;
    if (!PyLong_CheckExact(args[0]) || lu_long_as_long(args[0], &mode) < 0) {
        return NULL;
    }
    return PyLong_FromLongLong(lu_gc_freeze(PyInterpreterState_Get(), (int)mode));
}

/* gc_is_tracked(obj), gc_is_finalized(obj) */
static PyObject *
prim_gc_is_tracked(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef((lu_is_gc(args[0]) && lu_gc_is_tracked(args[0])) ? Py_True : Py_False);
}

static PyObject *
prim_gc_is_finalized(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef((lu_is_gc(args[0]) && (args[0]->ob_gc_bits & LU_GC_FINALIZED)) ? Py_True : Py_False);
}

/* gc_stats(): collections, collected and uncollectable of each generation, flat. */
static PyObject *
prim_gc_stats(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    struct lu_gc *gc = &PyInterpreterState_Get()->gc;
    PyObject *t = PyTuple_New(9);
    for (int i = 0; t != NULL && i < 9; i++) {
        PyObject *n = PyLong_FromLongLong(gc->stats[i / 3][i % 3]);
        if (n == NULL) {
            Py_CLEAR(t);
            break;
        }
        PyTuple_SET_ITEM(t, i, n);
    }
    return t;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_gc_methods[] = {
    FAST(gc_get), FAST(gc_set), FAST(gc_hook), FAST(gc_collect), FAST(gc_get_objects),
    FAST(gc_get_referrers), FAST(gc_get_referents), FAST(gc_freeze), FAST(gc_is_tracked),
    FAST(gc_is_finalized), FAST(gc_stats),
    { NULL, NULL, 0, NULL },
};

/* The C API of GC objects (Python/gc_free_threading.c). */

static size_t
presize_of(PyTypeObject *tp)
{
    return (tp->tp_flags & Py_TPFLAGS_PREHEADER) ? 2 * sizeof(PyObject *) : 0;
}

void
PyObject_GC_Track(void *op)
{
    PyObject *o = op;
    if (lu_gc_is_tracked(o)) {
        Py_FatalError("object already tracked by the garbage collector");
    }
    o->ob_gc_bits |= LU_GC_TRACKED;
}

void
PyObject_GC_UnTrack(void *op)
{
    ((PyObject *)op)->ob_gc_bits &= ~LU_GC_TRACKED;
}

int
PyObject_GC_IsTracked(PyObject *op)
{
    return lu_is_gc(op) && lu_gc_is_tracked(op);
}

int
PyObject_GC_IsFinalized(PyObject *op)
{
    return lu_is_gc(op) && (op->ob_gc_bits & LU_GC_FINALIZED) != 0;
}

/* Uninitialized, untracked memory with the object header set. */
static PyObject *
gc_new(PyTypeObject *tp, size_t size, size_t extra)
{
    void *mem = lu_gc_alloc(tp, size + extra, presize_of(tp));
    return mem != NULL ? PyObject_Init(mem, tp) : PyErr_NoMemory();
}

PyObject *
_PyObject_GC_New(PyTypeObject *tp)
{
    return gc_new(tp, tp->tp_basicsize, 0);
}

PyVarObject *
_PyObject_GC_NewVar(PyTypeObject *tp, Py_ssize_t nitems)
{
    if (nitems < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }
    PyVarObject *op = (PyVarObject *)gc_new(tp, _PyObject_VAR_SIZE(tp, nitems), 0);
    if (op != NULL) {
        Py_SET_SIZE(op, nitems);
    }
    return op;
}

PyObject *
PyUnstable_Object_GC_NewWithExtraData(PyTypeObject *tp, size_t extra)
{
    PyObject *op = gc_new(tp, tp->tp_basicsize, extra);
    if (op != NULL) {
        memset((char *)op + tp->tp_basicsize, 0, extra);
    }
    return op;
}

PyVarObject *
_PyObject_GC_Resize(PyVarObject *op, Py_ssize_t nitems)
{
    PyTypeObject *tp = Py_TYPE(op);
    size_t size = _PyObject_VAR_SIZE(tp, nitems);
    /* A new block: realloc would not keep it in the GC heap. */
    void *mem = lu_gc_alloc(tp, size, presize_of(tp));
    if (mem == NULL) {
        return (PyVarObject *)PyErr_NoMemory();
    }
    size_t old = _PyObject_VAR_SIZE(tp, Py_SIZE(op));
    memcpy(mem, op, old < size ? old : size);
    ((PyObject *)mem)->ob_gc_bits &= ~LU_GC_TRACKED;
    PyObject_GC_Del(op);
    Py_SET_SIZE((PyVarObject *)mem, nitems);
    return mem;
}

void
PyObject_GC_Del(void *op)
{
    lu_object_free(op);
}
