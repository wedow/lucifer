/* The collector's trigger (the policy half of Python/gc_free_threading.c).

   Allocating a GC object counts it, and when the count passes the young
   threshold, and the process has grown or enough has been allocated, the GC
   reason is raised in the allocating carrier's poll word. The collection
   itself runs only from the poll handler, at a call or a back-edge, where
   every frame is published (FOUNDATIONS 2.5). Until the cycle collector
   arrives (M0.2) a collection runs gc.callbacks, stops the world, and finds
   nothing.

   The counts are as CPython's: each carrier buffers 512 allocations before
   adding them to the interpreter's count, and frees subtract the same way,
   so a loop that allocates and frees temporaries never collects. */

#include "prim/prim.h"
#include "interp/exceptions.h"
#include "interp/code.h"

#include <stdio.h>
#include <string.h>

#define LOCAL_ALLOC_COUNT_THRESHOLD 512
enum { REASON_HEAP, REASON_MANUAL };

/* get_process_mem_usage: resident plus swapped memory in KB, or -1. */
static Py_ssize_t
process_mem_usage(void)
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
    Py_ssize_t mem = process_mem_usage();
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
    if (reason == REASON_HEAP && !should_collect(gc)) {
        __atomic_store_n(&gc->collecting, 0, __ATOMIC_SEQ_CST);
        return 0;
    }
    invoke_callback(ts, "start", generation, 0, 0);

    /* gc_collect_internal: the counters, under a stopped world. */
    lu_stw_stop(ts->interp);
    if (generation + 1 < 3) {
        gc->old_count[generation] += 1;
    }
    __atomic_store_n(&gc->young_count, 0, __ATOMIC_RELAXED);
    gc->deferred_count = 0;
    for (int i = 1; i <= generation; i++) {
        gc->old_count[i - 1] = 0;
    }
    gc->last_mem = process_mem_usage();
    lu_stw_start(ts->interp);
    lu_code_release_deferred(ts->interp);

    invoke_callback(ts, "stop", generation, 0, 0);
    __atomic_store_n(&gc->collecting, 0, __ATOMIC_SEQ_CST);
    return 0;
}

/* _Py_RunGC: the GC reason was raised. */
void
lu_gc_run(PyThreadState *ts)
{
    if (__atomic_load_n(&ts->interp->gc.enabled, __ATOMIC_RELAXED)) {
        collect(ts, 0, REASON_HEAP);
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
    PyObject *exc = PyErr_GetRaisedException();
    Py_ssize_t n = collect(ts, 2, REASON_MANUAL);
    PyErr_SetRaisedException(exc);
    return n;
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

/* gc_hook(runner): gc.py's runner(phase, generation, collected, uncollectable). */
static PyObject *
prim_gc_hook(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    struct lu_gc *gc = &PyInterpreterState_Get()->gc;
    PyObject *old = gc->hook;
    gc->hook = Py_NewRef(args[0]);
    Py_XDECREF(old);
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
    return PyLong_FromLong((long)collect(PyThreadState_Get(), (int)generation, REASON_MANUAL));
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_gc_methods[] = {
    FAST(gc_get), FAST(gc_set), FAST(gc_hook), FAST(gc_collect),
    { NULL, NULL, 0, NULL },
};
