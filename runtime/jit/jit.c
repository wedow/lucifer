/* The optimizing tier's policy (runtime/jit/jit.h): when to compile, the
   per-code state that holds compiled units, entering them, what exits
   teach the next compile, and the counts -X lucifer_jit_stats prints.

   Compiles run synchronously in the thread that found the code hot, one
   per code object at a time; another thread meanwhile interprets. A unit
   is published by a release store after it is complete, and never freed
   while its code lives. Guards are checked by the code itself each time
   they run, so a change another thread makes (a type, a global, a
   function's code) sends running code out at its next guard; units only
   go dead when one guard keeps failing, which also marks its instruction
   not to be speculated on again (the donor's exit-driven recompile). */
#include "jit/jit.h"
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* A guard that fails this often makes its instruction generic. */
#define GUARD_FAILS 50
/* Recompiles of one code object before the tier leaves it alone. */
#define MAX_COMPILES 12
#define MAX_ENTRIES 16

struct lu_jit_code {
    int compiling;              /* a thread is compiling: others interpret */
    int disabled;
    int compiles;
    struct lu_unit *units;
    int nfailed;
    int failed[MAX_ENTRIES];    /* entry pcs that did not compile */
    int ncode;
    uint8_t *nospec;            /* per code unit: do not speculate */
    uint16_t *fails;            /* per code unit: guard failures */
};

static struct {
    unsigned long compiles, failures, entries, returns, units_dead;
    unsigned long exits[EXIT_REASONS];
    unsigned long inlined;
    unsigned long bailouts[256];        /* side exits planted, per base opcode */
    unsigned long taken[256];           /* exits taken, per base opcode at the exit */
    unsigned long code_bytes;
    double compile_seconds;
    const char *last_failure;
    unsigned long failure_reasons[8];
    const char *failure_names[8];
} stats;

static void
count(unsigned long *c, unsigned long n)
{
    __atomic_fetch_add(c, n, __ATOMIC_RELAXED);
}

static struct lu_jit_code *
state_of(PyCodeObject *code)
{
    uintptr_t v = (uintptr_t)__atomic_load_n(&code->co_executors, __ATOMIC_ACQUIRE);
    return v != 0 && !(v & 1) ? (struct lu_jit_code *)v : NULL;
}

static struct lu_jit_code *
state_for(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc != NULL) {
        return jc;
    }
    jc = calloc(1, sizeof(struct lu_jit_code));
    jc->ncode = (int)Py_SIZE(code);
    jc->nospec = calloc((size_t)jc->ncode + 1, 1);
    jc->fails = calloc((size_t)jc->ncode + 1, sizeof(uint16_t));
    uintptr_t seen = (uintptr_t)__atomic_load_n(&code->co_executors, __ATOMIC_RELAXED);
    while (seen == 0 || (seen & 1)) {
        if (__atomic_compare_exchange_n((uintptr_t *)&code->co_executors, &seen, (uintptr_t)jc, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            return jc;
        }
    }
    free(jc->nospec);
    free(jc->fails);
    free(jc);
    return (struct lu_jit_code *)seen;
}

int
lu_jit_nospec(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && pc >= 0 && pc < jc->ncode && __atomic_load_n(&jc->nospec[pc], __ATOMIC_RELAXED);
}

int
lu_jit_next_pc(PyCodeObject *code, int pc)
{
    while (lu_base_opcode(code, pc) == EXTENDED_ARG) {
        pc++;
    }
    return pc + 1 + lu_opcode_caches[lu_base_opcode(code, pc)];
}

static double
now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void
note_failure(const char *why)
{
    count(&stats.failures, 1);
    for (int i = 0; i < 8; i++) {
        const char *n = __atomic_load_n(&stats.failure_names[i], __ATOMIC_RELAXED);
        if (n == NULL) {
            __atomic_compare_exchange_n(&stats.failure_names[i], &n, why, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
            n = stats.failure_names[i];
        }
        if (n == why || strcmp(n, why) == 0) {
            count(&stats.failure_reasons[i], 1);
            return;
        }
    }
}

static void
free_unit(struct lu_unit *u)
{
    lu_target_free(u);
    for (int i = 0; i < u->ndeopts; i++) {
        struct lu_deopt *d = u->deopts[i];
        for (int k = 0; k < d->nframes; k++) {
            free(d->frames[k].vals);
        }
        free(d->frames);
        free(d);
    }
    free(u->deopts);
    free(u->deopt_table);
    for (int i = 0; i < u->nconsts; i++) {
        Py_XDECREF(u->consts[i]);
    }
    free(u->consts);
    free(u->stress_counter);
    free(u);
}

/* -X perf: each unit as a py:: symbol in /tmp/perf-PID.map (FOUNDATIONS 2.12). */
static void
perf_map(PyCodeObject *code, struct lu_unit *u)
{
    PyThreadState *ts = PyThreadState_Get();
    if (ts->interp->config.perf_profiling <= 0) {
        return;
    }
    static PyMutex lock;
    PyMutex_Lock(&lock);
    char path[64];
    snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)getpid());
    FILE *out = fopen(path, "a");
    if (out != NULL) {
        fprintf(out, "%lx %zx py::%s:%s@%d\n", (unsigned long)(uintptr_t)u->mem, u->code_bytes,
                PyUnicode_AsUTF8(code->co_qualname), PyUnicode_AsUTF8(code->co_filename), u->entry_pc);
        fclose(out);
    }
    PyMutex_Unlock(&lock);
}

static struct lu_unit *
compile(struct lu_jit_code *jc, struct lu_frame *frame, int pc)
{
    struct lu_func f = { 0 };
    struct lu_unit *u = calloc(1, sizeof(struct lu_unit));
    f.code = frame->code;
    f.entry_pc = pc;
    f.stress = _PyRuntime.stress.deopt != 0;
    if (f.stress) {
        u->stress_counter = malloc(sizeof(long));
        *u->stress_counter = (long)(_PyRuntime.stress.seed % _PyRuntime.stress.deopt) + 1;
    }
    double t0 = now();
    int ok = lu_ssa_build(&f, frame) == 0 && lu_ssa_optimize(&f) == 0;
    static int dump = -1;
    if (dump < 0) {
        dump = getenv("LUCIFER_JIT_DUMP") != NULL;
    }
    if (ok && lu_regalloc(&f) == 0) {
        if (dump) {
            lu_ir_dump(&f, stderr);
        }
        ok = lu_target_emit(&f, u) == 0;
    }
    else {
        ok = 0;
    }
    double dt = now() - t0;
    __atomic_fetch_add(&stats.compiles, 1, __ATOMIC_RELAXED);
    stats.compile_seconds += dt;
    for (int i = 0; i < 256; i++) {
        if (f.bailouts[i]) {
            count(&stats.bailouts[i], (unsigned long)f.bailouts[i]);
        }
    }
    count(&stats.inlined, (unsigned long)f.ninlined);
    if (!ok) {
        note_failure(f.fail != NULL ? f.fail : "compile failed");
        lu_ir_free(&f);
        free_unit(u);
        return NULL;
    }
    u->entry_pc = pc;
    u->consts = f.consts;
    u->nconsts = f.nconsts;
    f.consts = NULL;
    f.nconsts = 0;
    count(&stats.code_bytes, u->code_bytes);
    lu_ir_free(&f);
    perf_map(frame->code, u);
    return u;
}

static struct lu_unit *
find(struct lu_jit_code *jc, int pc)
{
    for (struct lu_unit *u = __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
        if (u->entry_pc == pc && !__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
            return u;
        }
    }
    return NULL;
}

static int
failed_at(struct lu_jit_code *jc, int pc)
{
    for (int i = 0; i < jc->nfailed; i++) {
        if (jc->failed[i] == pc) {
            return 1;
        }
    }
    return 0;
}

enum lu_jit_result
lu_jit_enter(PyThreadState *ts, struct lu_frame *frame, PyObject **result)
{
    PyCodeObject *code = frame->code;
    struct lu_task *task = (struct lu_task *)ts;
    if (lu_target.name == NULL || !ts->interp->kernel_booted || frame->locals != NULL || frame->owner == LU_FRAME_GENERATOR
        || !(code->co_flags & CO_OPTIMIZED)
        || (code->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_ITERABLE_COROUTINE))
        || code->_co_monitoring != NULL || ts->tracing || ts->interp->eval_frame != NULL
        || code->_co_instrumentation_version
           != (__atomic_load_n(&task->carrier->poll, __ATOMIC_RELAXED) & ~LU_POLL_EVENTS_MASK)) {
        return LU_JIT_NONE;
    }
    static const char *only = (const char *)1;
    if (only == (const char *)1) {
        only = getenv("LUCIFER_JIT_ONLY");     /* debugging: compile only matching qualnames */
    }
    if (only != NULL && strstr(PyUnicode_AsUTF8(code->co_qualname), only) == NULL) {
        return LU_JIT_NONE;
    }
    struct lu_jit_code *jc = state_for(code);
    if (__atomic_load_n(&jc->disabled, __ATOMIC_RELAXED)) {
        return LU_JIT_NONE;
    }
    int pc = (int)(frame->instr - lu_code_start(code));
    struct lu_unit *u = find(jc, pc);
    if (u == NULL) {
        int idle = 0;
        if (failed_at(jc, pc) || !__atomic_compare_exchange_n(&jc->compiling, &idle, 1, 0,
                                                              __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return LU_JIT_NONE;
        }
        u = find(jc, pc);
        if (u == NULL && jc->compiles < MAX_COMPILES) {
            jc->compiles++;
            u = compile(jc, frame, pc);
            if (u != NULL) {
                u->next = jc->units;
                __atomic_store_n(&jc->units, u, __ATOMIC_RELEASE);
            }
            else if (jc->nfailed < MAX_ENTRIES) {
                jc->failed[jc->nfailed++] = pc;
            }
        }
        if (jc->compiles >= MAX_COMPILES && u == NULL) {
            __atomic_store_n(&jc->disabled, 1, __ATOMIC_RELAXED);
        }
        __atomic_store_n(&jc->compiling, 0, __ATOMIC_RELEASE);
        if (u == NULL) {
            return LU_JIT_NONE;
        }
    }
    count(&stats.entries, 1);
    __atomic_fetch_add(&u->entries, 1, __ATOMIC_RELAXED);
    static int debug = -1;
    if (debug < 0) {
        debug = getenv("LUCIFER_JIT_DEBUG") != NULL;
    }
    if (debug) {
        fprintf(stderr, "jit: enter %s@%d\n", PyUnicode_AsUTF8(code->co_qualname), pc);
    }
    int r = u->fn(ts, frame, result);
    if (debug) {
        struct lu_frame *cf = (struct lu_frame *)ts->current_frame;
        fprintf(stderr, "jit: %s@%d -> %d at %s@%d%s\n", PyUnicode_AsUTF8(code->co_qualname), pc, r,
                PyUnicode_AsUTF8(cf->code->co_qualname), (int)(cf->instr - lu_code_start(cf->code)),
                r == LU_JIT_ERROR && !PyErr_Occurred() ? " NO EXCEPTION" : "");
        if (r == LU_JIT_ERROR && PyErr_Occurred()) {
            PyObject *e = PyErr_GetRaisedException();
            fprintf(stderr, "jit:   %s\n", Py_TYPE(e)->tp_name);
            PyErr_SetRaisedException(e);
        }
    }
    if (r == LU_JIT_RETURN) {
        count(&stats.returns, 1);
    }
    return (enum lu_jit_result)r;
}

/* An exit was taken: its counts, and for a guard that keeps failing, no
   more speculation there and a recompile of the code it left. */
void
lu_jit_note_exit(struct lu_deopt *d)
{
    __atomic_fetch_add(&d->count, 1, __ATOMIC_RELAXED);
    count(&stats.exits[d->reason], 1);
    count(&stats.taken[d->opcode & 0xff], 1);
    if (d->reason != EXIT_GUARD) {
        return;
    }
    struct lu_deopt_frame *inner = &d->frames[d->nframes - 1];
    struct lu_jit_code *jc = state_for(inner->code);
    if (inner->pc < 0 || inner->pc >= jc->ncode) {
        return;
    }
    uint16_t n = __atomic_add_fetch(&jc->fails[inner->pc], 1, __ATOMIC_RELAXED);
    if (n == GUARD_FAILS) {
        __atomic_store_n(&jc->nospec[inner->pc], 1, __ATOMIC_RELAXED);
        /* Every unit of the root code is recompiled at its next entry. */
        struct lu_jit_code *root = state_for(d->frames[0].code);
        for (struct lu_unit *u = __atomic_load_n(&root->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
            if (!__atomic_exchange_n(&u->dead, 1, __ATOMIC_RELAXED)) {
                count(&stats.units_dead, 1);
            }
        }
        root->nfailed = 0;
    }
}

void
lu_jit_code_free(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc == NULL) {
        return;
    }
    for (struct lu_unit *u = jc->units, *next; u != NULL; u = next) {
        next = u->next;
        free_unit(u);
    }
    free(jc->nospec);
    free(jc->fails);
    free(jc);
    code->co_executors = NULL;
}

void
lu_jit_report(void)
{
    if (!_PyRuntime.jit.stats) {
        return;
    }
    static const char *const reasons[EXIT_REASONS] = { "guard", "stress", "poll", "side", "error" };
    fprintf(stderr, "jit: backend %s, mode %s\n", lu_target.name != NULL ? lu_target.name : "none",
            _PyRuntime.jit.mode == LU_JIT_ALWAYS ? "always" : _PyRuntime.jit.mode == LU_JIT_ON ? "on" : "off");
    fprintf(stderr, "jit: compiles %lu, failed %lu, units recompiled %lu, inlined calls %lu, "
            "compile time %.1f ms, code %lu bytes\n", stats.compiles, stats.failures, stats.units_dead,
            stats.inlined, stats.compile_seconds * 1e3, stats.code_bytes);
    for (int i = 0; i < 8 && stats.failure_names[i] != NULL; i++) {
        fprintf(stderr, "jit:   failed: %s: %lu\n", stats.failure_names[i], stats.failure_reasons[i]);
    }
    fprintf(stderr, "jit: entries %lu, returns %lu, exits", stats.entries, stats.returns);
    for (int i = 0; i < EXIT_REASONS; i++) {
        fprintf(stderr, " %s %lu", reasons[i], stats.exits[i]);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "jit: side exits compiled (bailouts) by opcode:");
    for (int i = 0; i < 256; i++) {
        if (stats.bailouts[i]) {
            fprintf(stderr, " %d:%lu", i, stats.bailouts[i]);
        }
    }
    fprintf(stderr, "\njit: exits taken by opcode:");
    for (int i = 0; i < 256; i++) {
        if (stats.taken[i]) {
            fprintf(stderr, " %d:%lu", i, stats.taken[i]);
        }
    }
    fprintf(stderr, "\n");
}
