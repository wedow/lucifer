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
   not to be speculated on again (the donor's exit-driven recompile).

   An exit no recompile can remove (an instruction the tier does not
   compile, an exception) costs an exit and, at the next call or back-edge,
   an entry. A unit without loops does a bounded amount of work per entry;
   when such a unit leaves that way on most of its entries, it costs more
   than it saves, and its entry stays in the interpreter.

   Code the interpreter has not run (on the first call under
   -X lucifer_jit=always, or a path that turned hot later) has no feedback
   to speculate on, and generic paths never fail a guard to ask for a
   recompile. A compile that finds most of its sites never ran is put off:
   the interpreter runs that entry a few times first, once per entry. */
#include "jit/jit.h"
#include "jit/target.h"
#include "interp/specialize.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* A guard that fails this often makes its instruction generic. */
#define GUARD_FAILS 50
/* Exits a unit without loops takes, on at least half its entries, before
   its entry stays in the interpreter. */
#define LEAVES 50
/* Interpreted entries before a compile put off for feedback. */
#define WARMUP 50
/* An entry's wait that never runs out. */
#define FOREVER UINT16_MAX
/* Recompiles of one code object before the tier leaves it alone. */
#define MAX_COMPILES 12
#define MAX_ENTRIES 16

struct lu_jit_code {
    int compiling;              /* a thread is compiling: others interpret */
    int disabled;
    int compiles;
    struct lu_unit *units;
    struct lu_unit *retired;    /* units off the list, freed with the code */
    int nfailed;
    int failed[MAX_ENTRIES];    /* entry pcs that did not compile */
    int ncode;
    uint8_t *nospec;            /* per code unit: do not speculate */
    uint16_t *fails;            /* per code unit: guard failures */
    uint16_t *wait;             /* per code unit: entries to leave to the interpreter, or FOREVER */
    uint8_t *warmed;            /* per code unit: an entry whose compile was put off for feedback once */
    int noinline;               /* a side exit in an inlined copy keeps leaving: call it instead */
    int call_pc;                /* where a call enters (call_entry), or -1 */
    struct lu_pic **pics;       /* per code unit: an attribute site's polymorphic cache */
};

static struct {
    unsigned long compiles, failures, entries, returns, units_dead, direct;
    unsigned long exits[EXIT_REASONS];
    unsigned long inlined;
    unsigned long bailouts[256];        /* side exits planted, per base opcode */
    unsigned long taken[256];           /* exits taken, per base opcode at the exit */
    unsigned long code_bytes;
    double compile_seconds;
    unsigned long failure_reasons[8];
    const char *failure_names[8];
    PyMutex perf_map;           /* one writer of /tmp/perf-PID.map at a time */
} stats;

/* A count for -X lucifer_jit_stats: free when the counts are off. */
static void
count(unsigned long *c, unsigned long n)
{
    if (_PyRuntime.jit.stats) {
        __atomic_fetch_add(c, n, __ATOMIC_RELAXED);
    }
}

/* A per-unit count: a lost update under a race only delays a decision. */
static unsigned long
bump(unsigned long *c)
{
    unsigned long n = __atomic_load_n(c, __ATOMIC_RELAXED) + 1;
    __atomic_store_n(c, n, __ATOMIC_RELAXED);
    return n;
}

static struct lu_jit_code *
state_of(PyCodeObject *code)
{
    uintptr_t v = (uintptr_t)__atomic_load_n(&code->co_executors, __ATOMIC_ACQUIRE);
    return v != 0 && !(v & 1) ? (struct lu_jit_code *)v : NULL;
}

int lu_jit_next_pc(PyCodeObject *code, int pc);

int
lu_jit_call_entry(PyCodeObject *code)
{
    for (int pc = 0; pc < (int)Py_SIZE(code); pc++) {
        int op = lu_base_opcode(code, pc);
        if (op == RESUME) {
            return lu_code_start(code)[pc].op.arg == 0 ? lu_jit_next_pc(code, pc) : -1;
        }
        if (op != MAKE_CELL && op != COPY_FREE_VARS) {
            return -1;
        }
    }
    return -1;
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
    jc->wait = calloc((size_t)jc->ncode + 1, sizeof(uint16_t));
    jc->warmed = calloc((size_t)jc->ncode + 1, 1);
    jc->call_pc = lu_jit_call_entry(code);
    uintptr_t seen = (uintptr_t)__atomic_load_n(&code->co_executors, __ATOMIC_RELAXED);
    while (seen == 0 || (seen & 1)) {
        if (__atomic_compare_exchange_n((uintptr_t *)&code->co_executors, &seen, (uintptr_t)jc, 0,
                                        __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
            return jc;
        }
    }
    free(jc->nospec);
    free(jc->fails);
    free(jc->wait);
    free(jc->warmed);
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
lu_jit_noinline(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && __atomic_load_n(&jc->noinline, __ATOMIC_RELAXED);
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
    PyMutex_Lock(&stats.perf_map);
    char path[64];
    snprintf(path, sizeof path, "/tmp/perf-%d.map", (int)getpid());
    FILE *out = fopen(path, "a");
    if (out != NULL) {
        fprintf(out, "%lx %zx py::%s:%s@%d\n", (unsigned long)(uintptr_t)u->mem, u->code_bytes,
                PyUnicode_AsUTF8(code->co_qualname), PyUnicode_AsUTF8(code->co_filename), u->entry_pc);
        fclose(out);
    }
    PyMutex_Unlock(&stats.perf_map);
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
    int dump = _PyRuntime.jit.dump;
    if (dump) {
        fprintf(stderr, "jit: compiling %s@%d\n", PyUnicode_AsUTF8(frame->code->co_qualname), pc);
    }
    int ok = lu_ssa_build(&f, frame) == 0;
    if (ok && f.cold > 0 && 2 * f.cold >= f.sites && !jc->warmed[pc]) {
        /* Most of it never ran: the interpreter runs the entry a few times
           to gather feedback, and the compile after that stands. */
        jc->warmed[pc] = 1;
        __atomic_store_n(&jc->wait[pc], WARMUP, __ATOMIC_RELAXED);
        lu_ir_free(&f);
        free_unit(u);
        return NULL;
    }
    ok = ok && lu_ssa_optimize(&f) == 0;
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
    count(&stats.compiles, 1);
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
    u->loops = f.loops > 0;
    u->consts = f.consts;
    u->nconsts = f.nconsts;
    f.consts = NULL;
    f.nconsts = 0;
    count(&stats.code_bytes, u->code_bytes);
    lu_ir_free(&f);
    perf_map(frame->code, u);
    return u;
}

/* The unit is not entered again, and its entry stays in the interpreter. */
static void
retire(struct lu_jit_code *jc, struct lu_unit *u)
{
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: retire %s@%d after %lu entries, %lu leaves\n",
                PyUnicode_AsUTF8(u->deopts != NULL && u->ndeopts > 0 ? u->deopts[0]->frames[0].code->co_qualname
                                                                     : Py_None), u->entry_pc, u->entries, u->leaves);
    }
    __atomic_store_n(&jc->wait[u->entry_pc], FOREVER, __ATOMIC_RELAXED);
    if (!__atomic_exchange_n(&u->dead, 1, __ATOMIC_RELAXED)) {
        count(&stats.units_dead, 1);
    }
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

/* LUCIFER_JIT_ONLY: a comma-separated list of qualnames. */
static int
listed(const char *list, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = list; *p != '\0';) {
        const char *end = strchr(p, ',');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, name, n) == 0) {
            return 1;
        }
        p += len + (end != NULL);
    }
    return 0;
}

/* Units of the code that were retired are moved off the list find walks
   (a reader standing on one walks on into the retired list, which holds
   nothing to find), and freed with the code. The compiling lock is held. */
static void
sweep(struct lu_jit_code *jc)
{
    struct lu_unit **link = &jc->units;
    for (struct lu_unit *u = *link; u != NULL; u = *link) {
        if (__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
            __atomic_store_n(link, u->next, __ATOMIC_RELEASE);
            __atomic_store_n(&u->next, jc->retired, __ATOMIC_RELEASE);
            jc->retired = u;
        }
        else {
            link = &u->next;
        }
    }
}

/* The unit to enter at pc, compiled now if this thread may: the checks
   that hold for the code's whole life are made here, before its first
   unit. */
static struct lu_unit *
compile_at(PyThreadState *ts, struct lu_frame *frame, int pc)
{
    PyCodeObject *code = frame->code;
    if (lu_target.name == NULL || !ts->interp->kernel_booted || !(code->co_flags & CO_OPTIMIZED)
        || (code->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_ITERABLE_COROUTINE))) {
        return NULL;
    }
    struct lu_jit_code *jc = state_for(code);
    const char *only = _PyRuntime.jit.only;
    if (only != NULL && !listed(only, PyUnicode_AsUTF8(code->co_qualname))) {
        __atomic_store_n(&jc->disabled, 1, __ATOMIC_RELAXED);      /* asked once */
        return NULL;
    }
    int idle = 0;
    if (__atomic_load_n(&jc->disabled, __ATOMIC_RELAXED) || jc->wait[pc] != 0 || failed_at(jc, pc)
        || !__atomic_compare_exchange_n(&jc->compiling, &idle, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return NULL;
    }
    sweep(jc);
    struct lu_unit *u = find(jc, pc);
    if (u == NULL && jc->compiles < MAX_COMPILES) {
        jc->compiles++;
        u = compile(jc, frame, pc);
        if (u != NULL) {
            u->next = jc->units;
            __atomic_store_n(&jc->units, u, __ATOMIC_RELEASE);
        }
        else if (jc->nfailed < MAX_ENTRIES && jc->wait[pc] == 0) {     /* not put off for warm-up */
            jc->failed[jc->nfailed++] = pc;
        }
    }
    if (jc->compiles >= MAX_COMPILES && u == NULL) {
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: disabled %s\n", PyUnicode_AsUTF8(code->co_qualname));
        }
        __atomic_store_n(&jc->disabled, 1, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&jc->compiling, 0, __ATOMIC_RELEASE);
    return u;
}

enum lu_jit_result
lu_jit_enter(PyThreadState *ts, struct lu_frame *frame, PyObject **result)
{
    PyCodeObject *code = frame->code;
    struct lu_task *task = (struct lu_task *)ts;
    /* What can change while the code has units: the frame, tools, a hook. */
    if (frame->locals != NULL || frame->owner == LU_FRAME_GENERATOR || code->_co_monitoring != NULL
        || ts->tracing || ts->interp->eval_frame != NULL
        || code->_co_instrumentation_version
           != (__atomic_load_n(&task->carrier->poll, __ATOMIC_RELAXED) & ~LU_POLL_EVENTS_MASK)) {
        return LU_JIT_NONE;
    }
    int pc = (int)(frame->instr - lu_code_start(code));
    struct lu_jit_code *jc = state_of(code);
    struct lu_unit *u = NULL;
    if (jc != NULL) {
        uint16_t wait = __atomic_load_n(&jc->wait[pc], __ATOMIC_RELAXED);
        if (wait != 0 || __atomic_load_n(&jc->disabled, __ATOMIC_RELAXED)) {
            if (wait != 0 && wait != FOREVER) {
                __atomic_store_n(&jc->wait[pc], wait - 1, __ATOMIC_RELAXED);
            }
            return LU_JIT_NONE;
        }
        u = find(jc, pc);
    }
    if (u == NULL && (u = compile_at(ts, frame, pc)) == NULL) {
        return LU_JIT_NONE;
    }
    count(&stats.entries, 1);
    bump(&u->entries);
    int debug = _PyRuntime.jit.debug;
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

/* MAKE_CELL and COPY_FREE_VARS as the interpreter runs them: 0, or -1
   with an exception and frame->instr after the instruction that raised. */
static int
prologue(struct lu_frame *frame)
{
    PyCodeObject *code = frame->code;
    for (lu_codeunit *at = lu_code_start(code); at->op.code != RESUME; at++) {
        if (at->op.code == COPY_FREE_VARS) {
            PyObject **closure = &PyTuple_GET_ITEM(((PyFunctionObject *)frame->func)->func_closure, 0);
            PyObject **free = frame->localsplus + code->co_nlocalsplus - at->op.arg;
            for (int i = 0; i < at->op.arg; i++) {
                free[i] = Py_NewRef(closure[i]);
            }
            continue;
        }
        PyObject **local = &frame->localsplus[at->op.arg];
        PyObject *cell = PyCell_New(*local);
        Py_XSETREF(*local, cell);
        if (cell == NULL) {
            frame->instr = frame->last = at + 1;
            return -1;
        }
    }
    return 0;
}

int
lu_jit_run(PyThreadState *ts, struct lu_frame *frame, PyObject **result)
{
    PyCodeObject *code = frame->code;
    struct lu_jit_code *jc = state_of(code);
    struct lu_task *task = (struct lu_task *)ts;
    /* lu_jit_enter's conditions, and RESUME's: no poll pending, the code
       instrumented for the tools as they are now. */
    uintptr_t poll = __atomic_load_n(&task->carrier->poll, __ATOMIC_RELAXED);
    if (jc == NULL || jc->call_pc <= 0 || frame->locals != NULL || frame->owner == LU_FRAME_GENERATOR
        || (poll & LU_POLL_EVENTS_MASK)
        || __atomic_load_n(&jc->disabled, __ATOMIC_RELAXED) || code->_co_monitoring != NULL
        || ts->tracing || code->_co_instrumentation_version != (poll & ~LU_POLL_EVENTS_MASK)) {
        return 0;
    }
    /* A unit is live only while its entry is not left to the interpreter
       (retire marks it dead). */
    struct lu_unit *u = find(jc, jc->call_pc);
    if (u == NULL) {
        return 0;
    }
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: call %s\n", PyUnicode_AsUTF8(code->co_qualname));
    }
    int pc = u->entry_pc;
    struct lu_frame entry = { .owner = LU_FRAME_C_ENTRY };
    entry.previous = (struct lu_frame *)ts->current_frame;
    frame->previous = &entry;
    ts->current_frame = (struct _PyInterpreterFrame *)frame;
    if (pc > 1 && prologue(frame) < 0) {
        *result = lu_eval_continue(ts, 1);
        return 1;
    }
    frame->instr = lu_code_start(code) + pc;
    frame->last = frame->instr - 1 - lu_opcode_caches[RESUME];
    lu_qsbr_quiescent(task->carrier);
    count(&stats.direct, 1);
    bump(&u->entries);
    int r = u->fn(ts, frame, result);
    if (r == LU_JIT_RETURN) {
        lu_frame_pop(ts, frame);
        ts->current_frame = (struct _PyInterpreterFrame *)entry.previous;
    }
    else {
        *result = lu_eval_continue(ts, r == LU_JIT_ERROR);
    }
    return 1;
}

/* An exit was taken: its counts; for a guard that keeps failing, no
   more speculation there and a recompile of the code it left; for a unit
   without loops that keeps leaving, the interpreter at its entry. */
void
lu_jit_note_exit(struct lu_deopt *d)
{
    count(&stats.exits[d->reason], 1);
    count(&stats.taken[d->opcode & 0xff], 1);
    struct lu_unit *u = d->unit;
    if (d->reason == EXIT_ERROR || (d->reason == EXIT_SIDE && d->nframes == 1)) {
        unsigned long n = bump(&u->leaves);
        if (!u->loops && n >= LEAVES && 2 * n >= __atomic_load_n(&u->entries, __ATOMIC_RELAXED)) {
            retire(state_for(d->frames[0].code), u);
        }
        return;
    }
    if (d->reason != EXIT_GUARD && !(d->reason == EXIT_SIDE && d->nframes > 1)) {
        return;
    }
    struct lu_deopt_frame *inner = &d->frames[d->nframes - 1];
    struct lu_jit_code *jc = state_for(inner->code);
    if (inner->pc < 0 || inner->pc >= jc->ncode) {
        return;
    }
    /* Past the threshold too: another root that inlined the same copy before
       the flag was set leaves here as well, and is recompiled when it does. */
    uint16_t n = __atomic_add_fetch(&jc->fails[inner->pc], 1, __ATOMIC_RELAXED);
    if (n >= GUARD_FAILS) {
        if (d->reason == EXIT_SIDE) {
            /* The inlined copy keeps leaving here: it is called instead from
               now on (and if that call leaves its caller's copy, the caller next). */
            __atomic_store_n(&jc->noinline, 1, __ATOMIC_RELAXED);
        }
        else {
            __atomic_store_n(&jc->nospec[inner->pc], 1, __ATOMIC_RELAXED);
        }
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

/* Polymorphic caches. Writers (compiled code's generic loads, on any
   thread) take _PyRuntime.pic_lock; the translator reads published
   entries without it; a collection forgets entries with the world stopped. */

struct lu_pic *
lu_jit_pic(PyCodeObject *code, int pc, int create)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc == NULL || pc < 0 || pc >= jc->ncode) {
        return NULL;
    }
    struct lu_pic **pics = __atomic_load_n(&jc->pics, __ATOMIC_ACQUIRE);
    if (pics == NULL || pics[pc] == NULL) {
        if (!create) {
            return NULL;
        }
        PyMutex_Lock(&_PyRuntime.pic_lock);
        if (jc->pics == NULL) {
            __atomic_store_n(&jc->pics, calloc((size_t)jc->ncode, sizeof(struct lu_pic *)), __ATOMIC_RELEASE);
        }
        if (jc->pics[pc] == NULL) {
            struct lu_pic *pic = calloc(1, sizeof(struct lu_pic));
            pic->code = code;
            pic->method = lu_code_start(code)[pc].op.arg & 1;
            __atomic_store_n(&jc->pics[pc], pic, __ATOMIC_RELEASE);
        }
        PyMutex_Unlock(&_PyRuntime.pic_lock);
        pics = jc->pics;
    }
    return pics[pc];
}

void
lu_jit_pic_note(struct lu_pic *pic, PyObject *owner, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(owner);
    int n = __atomic_load_n(&pic->n, __ATOMIC_ACQUIRE);
    for (int i = 0; i < n; i++) {
        if (pic->e[i].version == tp->tp_version_tag) {
            return;
        }
    }
    if (n == LU_PIC_WAYS || __atomic_load_n(&pic->tries, __ATOMIC_RELAXED) >= 4 * LU_PIC_WAYS) {
        return;                 /* megamorphic, or shapes no entry can hold: the generic path it is */
    }
    __atomic_fetch_add(&pic->tries, 1, __ATOMIC_RELAXED);
    /* What the interpreter's specializer makes of this owner, on a site
       of its own. */
    lu_codeunit site[1 + 9 + 1] = { 0 };
    site[0].op.code = LOAD_ATTR;
    site[0].op.arg = (uint8_t)pic->method;
    lu_specialize_load_attr(owner, site, name);
    int op = site[0].op.code;
    if (op != LOAD_ATTR_INSTANCE_VALUE && op != LOAD_ATTR_METHOD_NO_DICT && op != LOAD_ATTR_METHOD_WITH_VALUES
        && op != LOAD_ATTR_NONDESCRIPTOR_NO_DICT && op != LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES) {
        return;
    }
    struct lu_pic_entry e = { lu_cache_load32(site + 1 + LA_VERSION), (uint16_t)op,
                              lu_cache_load(site + 1 + LA_INDEX), lu_cache_load_ptr(site + 1 + LA_DESCR) };
    int added = 0;
    PyMutex_Lock(&_PyRuntime.pic_lock);
    n = pic->n;
    int known = 0;
    for (int i = 0; i < n; i++) {
        known |= pic->e[i].version == e.version;
    }
    if (n < LU_PIC_WAYS && e.version != 0 && !known) {
        pic->e[n] = e;
        __atomic_store_n(&pic->n, n + 1, __ATOMIC_RELEASE);
        added = 1;
    }
    PyMutex_Unlock(&_PyRuntime.pic_lock);
    if (added) {
        /* The code's units recompile at their next entry, testing for it. */
        struct lu_jit_code *jc = state_of(pic->code);
        for (struct lu_unit *u = __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
            if (!__atomic_exchange_n(&u->dead, 1, __ATOMIC_RELAXED)) {
                count(&stats.units_dead, 1);
            }
        }
        jc->nfailed = 0;
    }
}

void
lu_jit_pic_forget(PyCodeObject *code, int (*dead)(PyObject *))
{
    struct lu_jit_code *jc = state_of(code);
    for (int pc = 0; jc != NULL && jc->pics != NULL && pc < jc->ncode; pc++) {
        struct lu_pic *pic = jc->pics[pc];
        if (pic == NULL) {
            continue;
        }
        int k = 0;
        for (int i = 0; i < pic->n; i++) {
            if (pic->e[i].descr == NULL || !dead(pic->e[i].descr)) {
                pic->e[k++] = pic->e[i];
            }
        }
        pic->n = k;
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
    for (struct lu_unit *u = jc->retired, *next; u != NULL; u = next) {
        next = u->next;
        free_unit(u);
    }
    for (int i = 0; jc->pics != NULL && i < jc->ncode; i++) {
        free(jc->pics[i]);
    }
    free(jc->pics);
    free(jc->nospec);
    free(jc->fails);
    free(jc->wait);
    free(jc->warmed);
    free(jc);
    code->co_executors = NULL;
}

void
lu_jit_report(void)
{
    if (!_PyRuntime.jit.stats) {
        return;
    }
    static const char reasons[EXIT_REASONS][8] = { "guard", "stress", "poll", "side", "error" };
    fprintf(stderr, "jit: backend %s, mode %s\n", lu_target.name != NULL ? lu_target.name : "none",
            _PyRuntime.jit.mode == LU_JIT_ALWAYS ? "always" : _PyRuntime.jit.mode == LU_JIT_ON ? "on" : "off");
    fprintf(stderr, "jit: compiles %lu, failed %lu, units recompiled %lu, inlined calls %lu, "
            "compile time %.1f ms, code %lu bytes\n", stats.compiles, stats.failures, stats.units_dead,
            stats.inlined, stats.compile_seconds * 1e3, stats.code_bytes);
    for (int i = 0; i < 8 && stats.failure_names[i] != NULL; i++) {
        fprintf(stderr, "jit:   failed: %s: %lu\n", stats.failure_names[i], stats.failure_reasons[i]);
    }
    fprintf(stderr, "jit: entries %lu, returns %lu, direct calls %lu, exits", stats.entries, stats.returns,
            stats.direct);
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
