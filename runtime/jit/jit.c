/* The optimizing tier's policy (runtime/jit/jit.h): when to compile, the
   per-code state that holds compiled units, entering them, what exits
   teach the next compile, and the counts -X lucifer_jit_stats prints.

   Compiles run synchronously in the thread that found the code hot, one
   per code object at a time; another thread meanwhile interprets. A unit
   is published by a release store after it is complete, and never freed
   while its code lives. A unit may rely on what only a writer changes (a
   type's version, a dict's keys or one key's value, a function's code)
   between polls: a change another thread makes reaches it at its next
   poll, and the running thread's own at once (DESIGN 6.2 step 8,
   runtime/jit/deps.c). A unit also goes dead when one guard keeps failing,
   which marks its instruction not to be speculated on again (the donor's
   exit-driven recompile): each such death moves one site toward generic,
   once.

   An exit no recompile can remove (an instruction the tier does not
   compile, an exception the unit does not handle) costs an exit and, at
   the next call or back-edge, an entry. A unit without loops does a
   bounded amount of work per entry; when such a unit leaves that way on
   most of its entries, it costs more than it saves, and it dies (is
   retired). An exception that leaves the root frame is no such exit: the
   call ends there.

   Nothing is given up for good. Whenever a unit dies, or a compile fails,
   its entry waits in the interpreter before it compiles again: WARMUP
   entries plus one per code unit (a compile costs about the size of what
   it compiles). A death that moves the code toward generic (a site
   marked, a cache grown, a fact taken back) can happen only so often per
   site; a death that does not (a unit retired for leaving, a failed
   compile) doubles the code's waits from then on (its level), and a unit
   that served longer than its wait decays the level back. A unit that
   keeps leaving is compiled ever more rarely, so its exits and compiles
   cost a shrinking share of the code's interpreted work; a phase change
   gets a fresh compile; a code that settled recompiles soon.

   Code the interpreter has not run (on the first call under
   -X lucifer_jit=always, or a path that turned hot later) has no feedback
   to speculate on, and generic paths never fail a guard to ask for a
   recompile. A compile that finds most of its sites never ran is put off:
   the interpreter runs that entry a few times first, once per entry. */
#include "jit/jit.h"
#include "jit/target.h"
#include "interp/specialize.h"
#include "interp/perf.h"
#include "interp/exceptions.h"
#include "interp/gen.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* nospec bits. */
#define NOSPEC 1
#define NOHOIST 2
/* A guard that fails this often makes its instruction generic. */
#define GUARD_FAILS 50
/* Exits a unit without loops takes, on at least half its entries, before
   it is retired. */
#define LEAVES 50
/* Interpreted entries before a compile put off for feedback, and the wait
   after a death at level 0. */
#define WARMUP 50
/* Doublings of the wait an entry's 32 bits hold: past it, the wait stays. */
#define MAX_LEVEL 25
/* seen bits. */
#define WARMED 1                /* the entry's compile was put off for feedback once */
/* Entries a unit counts: one past this many is not retired, and the count
   is only read, so calls from many threads share its line. */
#define ENTRIES_SEEN (1u << 16)

struct lu_jit_code {
    struct lu_jit_direct direct;        /* first: compiled callers read it (target.h) */
    int compiling;              /* a thread is compiling: others interpret */
    int disabled;               /* LUCIFER_JIT_ONLY does not list it */
    int level;                  /* the wait's doublings: retirements and failed compiles, less decay */
    struct lu_unit *units;
    struct lu_unit *retired;    /* units off the list, freed with the code */
    int ncode;
    uint8_t *nospec;            /* per code unit: NOSPEC, do not speculate; NOHOIST, keep its guards in place */
    uint16_t *fails;            /* per code unit: guard failures */
    uint32_t *wait;             /* per code unit: entries to leave to the interpreter */
    uint8_t *seen;              /* per code unit: WARMED */
    int noinline;               /* a side exit in an inlined copy keeps leaving: call it instead */
    unsigned long marked;       /* marks when a site of it (or noinline) was last marked */
    int call_pc;                /* where a call enters (call_entry), or -1 */
    struct lu_pic **pics;       /* per code unit: an attribute site's polymorphic cache */
    /* A generator's code: per code unit, 1 where a resumed frame goes on
       (after each RESUME), all of them entries of one unit, found at the
       first of them (resume_key; -1 for other code). */
    uint8_t *resume;
    int resume_key;
    lu_codeunit *fresh;         /* where a generator that never ran stands (the POP_TOP before the first RESUME) */
    struct lu_callees **callees;        /* per code unit: a call site's callees */
    struct lu_opclasses **opclasses;    /* per code unit: an operator site's operand classes */
    struct ctxmark *ctx;                /* a root's marks on kernel code inlined in it, by context */
    int nctx, capctx;
    PyMutex ctx_lock;                   /* the marks' */
};

/* A root's mark on kernel code by context (target.h): its failures and bits. */
struct ctxmark {
    uint64_t key;
    uint16_t fails;
    uint8_t bits;
};

static struct {
    unsigned long compiles, failures, entries, returns, units_dead, direct, compiled, invalidated, waited, retired;
    unsigned long resumes, resumes_compiled, raised;
    unsigned long exits[EXIT_REASONS];
    unsigned long inlined;
    unsigned long bailouts[256];        /* side exits planted, per base opcode */
    unsigned long taken[256];           /* exits taken, per base opcode at the exit */
    unsigned long code_bytes;
    double compile_seconds;
    double phase_seconds[5];            /* build, optimize, regalloc, emit; builds put off for feedback */
    unsigned long failure_reasons[8];
    const char *failure_names[8];
    unsigned long c_paths[C_PATHS];
    unsigned long why_not[WHY_NOTS];
} stats;

/* Sites marked generic (nospec, noinline) so far: a unit built after a
   mark knows it. */
static unsigned long marks;

/* -X lucifer_jit_stats: every unit with counts, and the counts of units freed. */
static struct {
    PyMutex lock;
    struct lu_unit *units;
    unsigned long freed[CNT_COUNT];
} census;

/* -X lucifer_jit_stats: allocation sites (lu_jit_alloc_site), never freed. */
struct alloc_site {
    struct alloc_site *next;
    PyCodeObject *code;         /* the key only: the code may be gone by the report */
    int pc, kind;
    char *where;
    unsigned long n;
};
static struct {
    PyMutex lock;
    struct alloc_site *sites;
} allocs;

/* Exit reasons by name, for the counts and LUCIFER_JIT_DEBUG. */
static const char reasons[EXIT_REASONS][8] = { "guard", "stress", "poll", "side", "error", "frame", "yield" };

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

static void
bump_entries(struct lu_unit *u)
{
    count(&u->entered, 1);
    if (__atomic_load_n(&u->entries, __ATOMIC_RELAXED) < ENTRIES_SEEN) {
        bump(&u->entries);
    }
}

unsigned long *
lu_jit_alloc_site(PyCodeObject *code, int pc, int kind)
{
    PyMutex_Lock(&allocs.lock);
    struct alloc_site *s = allocs.sites;
    while (s != NULL && (s->code != code || s->pc != pc || s->kind != kind)) {
        s = s->next;
    }
    if (s == NULL) {
        char where[512];
        PyObject *file = code->co_filename;
        snprintf(where, sizeof where, "%s (%s:%d)", PyUnicode_AsUTF8(code->co_qualname),
                 file != NULL && PyUnicode_Check(file) ? PyUnicode_AsUTF8(file) : "?",
                 PyCode_Addr2Line(code, pc * (int)sizeof(lu_codeunit)));
        s = calloc(1, sizeof *s);
        *s = (struct alloc_site){ allocs.sites, code, pc, kind, strdup(where), 0 };
        allocs.sites = s;
    }
    PyMutex_Unlock(&allocs.lock);
    return &s->n;
}

/* The busiest allocation sites, merged by place and kind. */
static void
report_allocs(void)
{
    static const char names[ALLOC_KINDS][12] = {
#define LU_ALLOC_NAME(name) #name,
        LU_ALLOC_KINDS(LU_ALLOC_NAME)
#undef LU_ALLOC_NAME
    };
    unsigned long total[ALLOC_KINDS] = { 0 };
    int n = 0;
    for (struct alloc_site *s = allocs.sites; s != NULL; s = s->next) {
        total[s->kind] += s->n;
        n++;
    }
    fprintf(stderr, "jit: allocations by kind:");
    for (int k = 0; k < ALLOC_KINDS; k++) {
        fprintf(stderr, "%s %s %lu", k ? "," : "", names[k], total[k]);
    }
    fprintf(stderr, "\n");
    struct alloc_site **v = malloc(sizeof(struct alloc_site *) * (size_t)(n ? n : 1));
    n = 0;
    for (struct alloc_site *s = allocs.sites; s != NULL; s = s->next) {
        int k = 0;
        while (k < n && (v[k]->kind != s->kind || strcmp(v[k]->where, s->where) != 0)) {
            k++;
        }
        if (k < n) {
            v[k]->n += s->n;    /* the same place in another unit (or a code made again) */
            s->n = 0;
        }
        else if (s->n > 0) {
            v[n++] = s;
        }
    }
    for (int shown = 0; shown < 40 && n > 0; shown++) {
        int best = 0;
        for (int k = 1; k < n; k++) {
            best = v[k]->n > v[best]->n ? k : best;
        }
        fprintf(stderr, "jit: allocated %lu %s at %s\n", v[best]->n, names[v[best]->kind], v[best]->where);
        v[best] = v[--n];
    }
    free(v);
}

void
lu_jit_c_path_count(enum lu_c_path k)
{
    count(&stats.c_paths[k], 1);
}

/* -X lucifer_jit_stats: the callees behind the why-not counts, by kind and
   identity (a code, a type, a method definition, a descriptor), named when
   first seen: the report lists the most called. */
#define WHY_NOT_CALLEES 8192
static struct {
    PyMutex lock;
    struct { int kind; void *key; char *name; unsigned long n; } e[WHY_NOT_CALLEES];
} why_not_callees;

static void
why_not_callee(enum lu_why_not k, PyObject *callable)
{
    void *key = Py_TYPE(callable);
    char buf[200];
    const char *name = NULL;
    if (Py_IS_TYPE(callable, &PyMethod_Type)) {
        callable = PyMethod_GET_FUNCTION(callable);
        key = Py_TYPE(callable);
    }
    if (PyFunction_Check(callable)) {
        key = ((PyFunctionObject *)callable)->func_code;
    }
    else if (PyType_Check(callable) || PyCFunction_Check(callable)) {
        key = PyCFunction_Check(callable) ? (void *)((PyCFunctionObject *)callable)->m_ml : callable;
    }
    else if (Py_IS_TYPE(callable, &PyMethodDescr_Type) || Py_IS_TYPE(callable, &PyWrapperDescr_Type)) {
        key = callable;
    }
    uintptr_t h = ((uintptr_t)key >> 4) * 31 + (uintptr_t)k;
    PyMutex_Lock(&why_not_callees.lock);
    for (int i = 0; i < WHY_NOT_CALLEES; i++) {
        int at = (int)((h + (uintptr_t)i) % WHY_NOT_CALLEES);
        if (why_not_callees.e[at].key == key && why_not_callees.e[at].kind == (int)k) {
            why_not_callees.e[at].n++;
            break;
        }
        if (why_not_callees.e[at].key == NULL) {
            if (PyType_Check(callable)) {
                snprintf(buf, sizeof buf, "class %s", ((PyTypeObject *)callable)->tp_name);
            }
            else if (Py_IS_TYPE(callable, &PyMethodDescr_Type) || Py_IS_TYPE(callable, &PyWrapperDescr_Type)) {
                PyDescrObject *d = (PyDescrObject *)callable;
                snprintf(buf, sizeof buf, "%s %s.%s", Py_TYPE(callable)->tp_name, d->d_type->tp_name,
                         d->d_name != NULL ? PyUnicode_AsUTF8(d->d_name) : "?");
            }
            else if (PyFunction_Check(callable) || PyCFunction_Check(callable)) {
                name = lu_function_str(callable, buf, sizeof buf);
                if (name != buf) {
                    snprintf(buf, sizeof buf, "%s", name);
                }
            }
            else {
                snprintf(buf, sizeof buf, "<%s>", Py_TYPE(callable)->tp_name);
            }
            why_not_callees.e[at].key = key;
            why_not_callees.e[at].kind = (int)k;
            why_not_callees.e[at].name = strdup(buf);
            why_not_callees.e[at].n = 1;
            break;
        }
    }
    PyMutex_Unlock(&why_not_callees.lock);
}

static void
report_why_not_callees(void)
{
    static const char names[WHY_NOTS][16] = {
#define LU_WHY_NOT_NAME(name) #name,
        LU_WHY_NOT(LU_WHY_NOT_NAME)
#undef LU_WHY_NOT_NAME
    };
    for (int shown = 0; shown < 40; shown++) {
        int best = -1;
        for (int i = 0; i < WHY_NOT_CALLEES; i++) {
            if (why_not_callees.e[i].n > 0 && (best < 0 || why_not_callees.e[i].n > why_not_callees.e[best].n)) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        fprintf(stderr, "jit: whynot %s %lu %s\n", names[why_not_callees.e[best].kind], why_not_callees.e[best].n,
                why_not_callees.e[best].name);
        why_not_callees.e[best].n = 0;
    }
}

void
lu_jit_why_not_count(enum lu_why_not k, PyObject *callable)
{
    count(&stats.why_not[k], 1);
    why_not_callee(k, callable);
}

static struct lu_jit_code *state_of(PyCodeObject *code);

void
lu_jit_why_not(PyObject *callable, Py_ssize_t nargs)
{
    if (Py_IS_TYPE(callable, &PyMethod_Type)) {
        callable = PyMethod_GET_FUNCTION(callable);
        nargs++;
    }
    enum lu_why_not k;
    if (!PyFunction_Check(callable)) {
        k = PyType_Check(callable) ? WHY_NOT_type
            : PyCFunction_Check(callable) || Py_IS_TYPE(callable, &PyMethodDescr_Type) ? WHY_NOT_builtin
            : WHY_NOT_object;
    }
    else {
        PyCodeObject *code = (PyCodeObject *)((PyFunctionObject *)callable)->func_code;
        struct lu_jit_code *jc = state_of(code);
        int pc = jc != NULL ? jc->call_pc : -1;
        k = code->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR) ? WHY_NOT_generator
            : jc == NULL ? WHY_NOT_cold
            : pc <= 0 || jc->direct.nargs < 0 ? WHY_NOT_prologue
            : nargs > jc->direct.nargs ? WHY_NOT_toomany
            : __atomic_load_n(&jc->disabled, __ATOMIC_RELAXED) ? WHY_NOT_excluded
            : __atomic_load_n(&jc->wait[pc], __ATOMIC_RELAXED) != 0 ? WHY_NOT_waiting
            : __atomic_load_n(&jc->direct.entry, __ATOMIC_RELAXED) == NULL ? WHY_NOT_nounit
            : WHY_NOT_entry;
    }
    count(&stats.why_not[k], 1);
    why_not_callee(k, callable);
}

unsigned long *
lu_jit_direct_count(void)
{
    return _PyRuntime.jit.stats ? &stats.compiled : NULL;
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
    jc->wait = calloc((size_t)jc->ncode + 1, sizeof(uint32_t));
    jc->seen = calloc((size_t)jc->ncode + 1, 1);
    jc->call_pc = lu_jit_call_entry(code);
    jc->resume_key = -1;
    if (code->co_flags & LU_CO_GENERATORS) {
        jc->resume = calloc((size_t)jc->ncode + 1, 1);
        for (int pc = 0; pc < jc->ncode; pc = lu_jit_next_pc(code, pc)) {
            if (lu_base_opcode(code, pc) == RETURN_GENERATOR) {
                jc->fresh = lu_code_start(code) + lu_jit_next_pc(code, pc);
            }
            if (lu_base_opcode(code, pc) == RESUME) {
                int at = lu_jit_next_pc(code, pc);
                jc->resume[at] = 1;
                jc->resume_key = jc->resume_key < 0 ? at : jc->resume_key;
            }
        }
    }
    /* Compiled code and C call the call entry's unit directly. */
    jc->direct.nargs = jc->call_pc <= 0 || code->co_argcount >= 100 ? -1
        : code->co_flags & CO_VARARGS ? LU_DIRECT_ANY : code->co_argcount;
    jc->direct.call_pc = jc->call_pc;
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
    free(jc->seen);
    free(jc->resume);
    free(jc);
    return (struct lu_jit_code *)seen;
}

int
lu_jit_nospec(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && pc >= 0 && pc < jc->ncode && (__atomic_load_n(&jc->nospec[pc], __ATOMIC_RELAXED) & NOSPEC);
}

int
lu_jit_nohoist(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && pc >= 0 && pc < jc->ncode && (__atomic_load_n(&jc->nospec[pc], __ATOMIC_RELAXED) & NOHOIST);
}

void
lu_jit_nohoist_set(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_for(code);
    if (pc >= 0 && pc < jc->ncode) {
        __atomic_fetch_or(&jc->nospec[pc], NOHOIST, __ATOMIC_RELAXED);
    }
}

/* The root's mark for key, made when `create` (under its ctx_lock), or NULL. */
static struct ctxmark *
ctx_entry(struct lu_jit_code *jc, uint64_t key, int create)
{
    for (int i = 0; i < jc->nctx; i++) {
        if (jc->ctx[i].key == key) {
            return &jc->ctx[i];
        }
    }
    if (!create) {
        return NULL;
    }
    if (jc->nctx == jc->capctx) {
        jc->capctx = jc->capctx ? jc->capctx * 2 : 8;
        jc->ctx = realloc(jc->ctx, sizeof(struct ctxmark) * (size_t)jc->capctx);
    }
    jc->ctx[jc->nctx] = (struct ctxmark){ .key = key };
    return &jc->ctx[jc->nctx++];
}

int
lu_jit_ctx_marked(PyCodeObject *root, uint64_t key, int bit)
{
    struct lu_jit_code *jc = state_of(root);
    if (jc == NULL) {
        return 0;
    }
    PyMutex_Lock(&jc->ctx_lock);
    struct ctxmark *m = ctx_entry(jc, key, 0);
    int set = m != NULL && (m->bits & bit);
    PyMutex_Unlock(&jc->ctx_lock);
    return set;
}

/* The context of the exit's frame k (frames 1..k kernel code inlined in
   the root), and with `pc` 0 that of the call that inlined it. */
static uint64_t
exit_context(struct lu_deopt *d, int k, int pc)
{
    uint64_t h = 0;
    for (int i = 0; i < k; i++) {
        h = lu_jit_ctx_mix(h, d->frames[i].code, d->frames[i].pc);
    }
    return lu_jit_ctx_mix(h, d->frames[k].code, pc ? d->frames[k].pc : -1);
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
            lu_jit_dv_free(d->frames[k].vals, d->frames[k].nlocals + d->frames[k].nstack);
            free(d->frames[k].vals);
        }
        free(d->frames);
        free(d->release);
        free(d);
    }
    free(u->deopts);
    free(u->deopt_table);
    for (int i = 0; i < u->nconsts; i++) {
        Py_XDECREF(u->consts[i]);
    }
    free(u->consts);
    free(u->stress_counter);
    lu_jit_deps_free(u);
    if (u->counts != NULL) {
        /* Its counts stay in the report's totals. */
        PyMutex_Lock(&census.lock);
        for (struct lu_unit **link = &census.units; *link != NULL; link = &(*link)->stats_next) {
            if (*link == u) {
                *link = u->stats_next;
                break;
            }
        }
        for (int k = 0; k < CNT_COUNT; k++) {
            census.freed[k] += u->counts[k];
        }
        PyMutex_Unlock(&census.lock);
        free(u->counts);
        free(u->stats_name);
    }
    free(u);
}

static struct lu_unit *
compile(struct lu_jit_code *jc, struct lu_frame *frame, int pc, uint64_t ticket)
{
    struct lu_func f = { 0 };
    struct lu_unit *u = calloc(1, sizeof(struct lu_unit));
    u->code = frame->code;
    u->built = __atomic_load_n(&marks, __ATOMIC_ACQUIRE);
    f.code = frame->code;
    /* A generator's resume unit (keyed at its first resume point) is
       compiled where this frame stands, one of its entries. */
    f.entry_pc = (int)(frame->instr - lu_code_start(frame->code));
    f.resume = pc == jc->resume_key;
    f.direct = pc == jc->call_pc && jc->direct.nargs >= 0;
    f.stress = _PyRuntime.stress.deopt != 0;
    if (f.stress) {
        u->stress_counter = malloc(sizeof(long));
        *u->stress_counter = (long)(_PyRuntime.stress.seed % _PyRuntime.stress.deopt) + 1;
    }
    double t0 = now();
    int dump = _PyRuntime.jit.dump;
    if (dump) {
        fprintf(stderr, "jit: compiling %s@%d%s\n", PyUnicode_AsUTF8(frame->code->co_qualname), pc,
                f.resume ? " (resume)" : "");
    }
    int ok = lu_ssa_build(&f, frame) == 0;
    double t1 = now();
    if (ok && f.cold > 0 && 2 * f.cold >= f.sites && !(jc->seen[pc] & WARMED)) {
        stats.phase_seconds[4] += t1 - t0;
        /* Most of it never ran: the interpreter runs the entry a few times
           to gather feedback, and the compile after that stands. */
        jc->seen[pc] |= WARMED;
        __atomic_store_n(&jc->wait[pc], WARMUP, __ATOMIC_RELAXED);
        lu_ir_free(&f);
        free_unit(u);
        return NULL;
    }
    ok = ok && lu_ssa_optimize(&f) == 0;
    double t2 = now(), t3 = t2;
    if (_PyRuntime.jit.stats) {
        u->counts = calloc(CNT_COUNT, sizeof(unsigned long));
    }
    if (ok && lu_regalloc(&f) == 0) {
        t3 = now();
        if (dump) {
            lu_ir_dump(&f, stderr);
        }
        ok = lu_target_emit(&f, u) == 0;
    }
    else {
        ok = 0;
    }
    double t4 = now(), dt = t4 - t0;
    stats.phase_seconds[0] += t1 - t0;
    stats.phase_seconds[1] += t2 - t1;
    stats.phase_seconds[2] += t3 - t2;
    stats.phase_seconds[3] += t4 - t3;
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
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: failed %s@%d: %s\n", PyUnicode_AsUTF8(frame->code->co_qualname), pc,
                    f.fail != NULL ? f.fail : "compile failed");
        }
        lu_ir_free(&f);
        free_unit(u);
        return NULL;
    }
    if (lu_jit_deps_publish(u, f.deps, f.ndeps, ticket) < 0) {
        /* A fact it relies on changed while it compiled: the next entry compiles again. */
        note_failure("a dependency changed during the compile");
        __atomic_store_n(&jc->wait[pc], 1, __ATOMIC_RELAXED);
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
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: compiled %s@%d: %zu bytes, %d calls inlined\n", PyUnicode_AsUTF8(frame->code->co_qualname),
                pc, u->code_bytes, f.ninlined);
    }
    if (u->counts != NULL) {
        char name[256];
        snprintf(name, sizeof name, "%s@%d", PyUnicode_AsUTF8(frame->code->co_qualname), pc);
        u->stats_name = strdup(name);
        PyMutex_Lock(&census.lock);
        u->stats_next = census.units;
        census.units = u;
        PyMutex_Unlock(&census.lock);
    }
    lu_ir_free(&f);
    lu_perf_unit(u->mem, u->code_bytes, frame->code, pc);
    return u;
}

/* LUCIFER_JIT_DEBUG: why a code's units die. */
static void
say_recompile(PyCodeObject *root, const char *why, PyCodeObject *at, int pc)
{
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: recompile %s: %s at %s@%d\n", PyUnicode_AsUTF8(root->co_qualname), why,
                PyUnicode_AsUTF8(at->co_qualname), pc);
    }
}

/* The entry waits (WARMUP + the code's size) << level interpreted
   entries before it compiles again: a compile costs about the size of
   what it compiles. Races between threads only shift when. */
static void
wait_at(struct lu_jit_code *jc, int pc)
{
    int level = __atomic_load_n(&jc->level, __ATOMIC_RELAXED);
    __atomic_store_n(&jc->wait[pc], (uint32_t)(WARMUP + jc->ncode) << level, __ATOMIC_RELAXED);
}

/* The unit is not entered again, and its entry waits before the next
   compile. A unit that served at least that wait's entries, leaving on
   fewer than half, decays the code's level first: the wait never exceeds
   what the last good unit served, and one that served past what its count
   holds starts the code over. */
static void
kill(struct lu_jit_code *jc, struct lu_unit *u)
{
    void *entry = u->direct;
    if (entry != NULL) {
        __atomic_compare_exchange_n(&jc->direct.entry, &entry, NULL, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    }
    if (__atomic_exchange_n(&u->dead, 1, __ATOMIC_RELAXED)) {
        return;
    }
    count(&stats.units_dead, 1);
    unsigned long served = __atomic_load_n(&u->entries, __ATOMIC_RELAXED);
    if (2 * __atomic_load_n(&u->leaves, __ATOMIC_RELAXED) < served) {
        int level = served >= ENTRIES_SEEN ? 0 : __atomic_load_n(&jc->level, __ATOMIC_RELAXED);
        while (level > 0 && served >= (unsigned long)(WARMUP + jc->ncode) << level) {
            level--;
        }
        __atomic_store_n(&jc->level, level, __ATOMIC_RELAXED);
    }
    wait_at(jc, u->entry_pc);
}

/* A writer took back what the unit relies on (runtime/jit/deps.c), or a
   guard moved out of its loop failed: it dies, and calls stop entering it
   directly. */
void
lu_jit_unit_kill(struct lu_unit *u)
{
    if (!__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
        count(&stats.invalidated, 1);
    }
    kill(state_of(u->code), u);
}

/* A death no recompile was asked to fix (a unit that keeps leaving, a
   compile that failed): the code's waits double from now on. */
static void
escalate(struct lu_jit_code *jc)
{
    int level = __atomic_load_n(&jc->level, __ATOMIC_RELAXED);
    if (level < MAX_LEVEL) {
        __atomic_store_n(&jc->level, level + 1, __ATOMIC_RELAXED);
    }
}

/* A unit that keeps leaving dies; its entry waits, ever longer if the
   next one leaves too. Activations still running a unit that died (a
   recursion's callers) leave as well: the unit is retired once. */
static void
retire(struct lu_jit_code *jc, struct lu_unit *u)
{
    if (__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
        return;
    }
    if (_PyRuntime.jit.debug || _PyRuntime.jit.stats) {
        fprintf(stderr, "jit: retire %s@%d after %lu entries, %lu leaves, level %d\n",
                PyUnicode_AsUTF8(u->code->co_qualname), u->entry_pc, u->entries, u->leaves, jc->level);
    }
    count(&stats.retired, 1);
    escalate(jc);
    kill(jc, u);
}

/* An exit no recompile can remove: a unit without loops that leaves on
   most of its entries is retired. */
static void
leave(struct lu_deopt *d, struct lu_unit *u)
{
    unsigned long n = bump(&u->leaves);
    unsigned long entries = __atomic_load_n(&u->entries, __ATOMIC_RELAXED);
    if (!u->loops && n >= LEAVES && 2 * n >= entries && entries < ENTRIES_SEEN) {
        retire(state_for(d->frames[0].code), u);
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
    if (!ts->interp->kernel_booted || !(code->co_flags & CO_OPTIMIZED)) {
        return NULL;
    }
    struct lu_jit_code *jc = state_for(code);
    const char *only = _PyRuntime.jit.only;
    if (only != NULL && !listed(only, PyUnicode_AsUTF8(code->co_qualname))) {
        __atomic_store_n(&jc->disabled, 1, __ATOMIC_RELAXED);      /* asked once */
        return NULL;
    }
    int idle = 0;
    if (__atomic_load_n(&jc->disabled, __ATOMIC_RELAXED) || jc->wait[pc] != 0
        || !__atomic_compare_exchange_n(&jc->compiling, &idle, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return NULL;
    }
    sweep(jc);
    struct lu_unit *u = find(jc, pc);
    if (u == NULL) {
        uint64_t ticket = lu_jit_deps_begin();     /* before anything the build reads */
        u = compile(jc, frame, pc, ticket);
        lu_jit_deps_end();
        if (u != NULL) {
            u->next = jc->units;
            __atomic_store_n(&jc->units, u, __ATOMIC_RELEASE);
            if (u->direct != NULL) {
                __atomic_store_n(&jc->direct.entry, u->direct, __ATOMIC_RELEASE);
            }
        }
        else if (jc->wait[pc] == 0) {
            /* It failed: tried again later, like a unit that died. (A compile
               put off for feedback, or one a writer overtook, has its wait.) */
            escalate(jc);
            wait_at(jc, pc);
        }
    }
    __atomic_store_n(&jc->compiling, 0, __ATOMIC_RELEASE);
    return u;
}

const char *
lu_jit_backend(void)
{
    return lu_target.name;
}

enum lu_jit_result
lu_jit_enter(PyThreadState *ts, struct lu_frame *frame, PyObject **result)
{
    PyCodeObject *code = frame->code;
    struct lu_task *task = (struct lu_task *)ts;
    /* What can change while the code has units: the frame, tools, a hook. */
    if (frame->locals != NULL || code->_co_monitoring != NULL
        || ts->tracing || (ts->interp->eval_frame != NULL && ts->interp->eval_frame != lu_perf_eval)
        || code->_co_instrumentation_version
           != (__atomic_load_n(&task->carrier->poll, __ATOMIC_RELAXED) & ~LU_POLL_EVENTS_MASK)) {
        return LU_JIT_NONE;
    }
    int pc = (int)(frame->instr - lu_code_start(code));
    struct lu_jit_code *jc = state_of(code);
    struct lu_unit *u = NULL;
    if ((code->co_flags & LU_CO_GENERATORS) && jc == NULL) {
        jc = state_for(code);
    }
    int resumed = jc != NULL && jc->resume != NULL && jc->resume[pc];
    if (resumed) {
        pc = jc->resume_key;    /* every resume point enters the one unit */
    }
    if (jc != NULL) {
        uint32_t wait = __atomic_load_n(&jc->wait[pc], __ATOMIC_RELAXED);
        if (wait != 0 || __atomic_load_n(&jc->disabled, __ATOMIC_RELAXED)) {
            if (wait != 0) {
                __atomic_store_n(&jc->wait[pc], wait - 1, __ATOMIC_RELAXED);
                count(&stats.waited, 1);
            }
            return LU_JIT_NONE;
        }
        u = find(jc, pc);
    }
    if (u == NULL && (u = compile_at(ts, frame, pc)) == NULL) {
        return LU_JIT_NONE;
    }
    count(&stats.entries, 1);
    count(&stats.resumes_compiled, (unsigned long)resumed);
    bump_entries(u);
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

/* MAKE_CELL and COPY_FREE_VARS as the interpreter runs them, up to the
   RESUME (or a generator's RETURN_GENERATOR): 0, or -1 with an exception
   and frame->instr after the instruction that raised. */
static int
prologue(struct lu_frame *frame)
{
    PyCodeObject *code = frame->code;
    for (lu_codeunit *at = lu_code_start(code); at->op.code != RESUME && at->op.code != RETURN_GENERATOR; at++) {
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

/* The parameter keyword names: its index among those that take keywords
   (from the positional-only ones on), or -1. Exact strs only, compared
   by identity first, as the names are interned; no Python runs. */
static int
keyword_index(PyCodeObject *code, PyObject *name)
{
    int total = code->co_argcount + code->co_kwonlyargcount;
    PyObject **names = &PyTuple_GET_ITEM(code->co_localsplusnames, 0);
    for (int j = code->co_posonlyargcount; j < total; j++) {
        if (names[j] == name) {
            return j;
        }
    }
    for (int j = code->co_posonlyargcount; j < total; j++) {
        if (lu_str_eq(names[j], name)) {
            return j;
        }
    }
    return -1;
}

struct lu_frame *
lu_jit_bind(PyThreadState *ts, PyFunctionObject *func, PyObject *const *args, int given, PyObject *kwnames,
            int nborrowed)
{
    PyCodeObject *code = (PyCodeObject *)func->func_code;
    PyObject **base = ts->datastack_top;
    if (base == NULL || ts->datastack_limit - base < code->co_framesize) {
        return NULL;            /* the entry's caller pops it to here: a new chunk would not be */
    }
    ts->datastack_top = base + code->co_framesize;
    struct lu_frame *f = (struct lu_frame *)base;
    *f = (struct lu_frame){ .code = code, .func = (PyObject *)func, .globals = func->func_globals,
                            .builtins = func->func_builtins, .ret = LU_RETURN_PUSH, .jit_lent = 1,
                            .previous = (struct lu_frame *)ts->current_frame };
    f->instr = lu_code_start(code) + ((struct lu_jit_direct *)code->co_executors)->call_pc;
    f->last = f->instr - 1 - lu_opcode_caches[RESUME];
    f->stackpointer = f->localsplus;    /* compiled code's from here */
    PyObject **locals = f->localsplus;
    for (int i = 0; i < code->co_nlocalsplus; i++) {
        locals[i] = NULL;
    }
    int argc = code->co_argcount, total = argc + code->co_kwonlyargcount;
    int star = (code->co_flags & CO_VARARGS) != 0, nkw = kwnames != NULL ? (int)PyTuple_GET_SIZE(kwnames) : 0;
    PyObject *kwdict = NULL;
    int n = given < argc ? given : argc;
    for (int i = 0; i < n; i++) {
        locals[i] = i < nborrowed ? args[i] : Py_NewRef(args[i]);
    }
    if (given > argc && !star) {
        goto decline;
    }
    for (int k = 0; k < nkw; k++) {
        PyObject *name = PyTuple_GET_ITEM(kwnames, k), *value = args[given + k];
        int j = PyUnicode_CheckExact(name) ? keyword_index(code, name) : -2;
        if (j >= 0 && locals[j] == NULL) {
            locals[j] = j < nborrowed ? value : Py_NewRef(value);
            continue;
        }
        if (j != -1 || !(code->co_flags & CO_VARKEYWORDS)) {
            goto decline;       /* not a str, given twice, or a name it does not take */
        }
        if ((kwdict == NULL && (kwdict = PyDict_New()) == NULL) || lu_ns_insert(kwdict, name, value) < 0) {
            goto decline;
        }
    }
    /* What the call leaves out: defaults, never borrowed. */
    PyObject *defaults = func->func_defaults, *kwdefaults = func->func_kwdefaults;
    int first = argc - (defaults != NULL ? (int)PyTuple_GET_SIZE(defaults) : 0);
    for (int i = 0; i < argc; i++) {
        if (locals[i] == NULL) {
            if (i < first || i < nborrowed) {
                goto decline;
            }
            locals[i] = Py_NewRef(PyTuple_GET_ITEM(defaults, i - first));
        }
    }
    for (int j = argc; j < total; j++) {
        if (locals[j] == NULL) {
            PyObject *d = kwdefaults != NULL && PyDict_CheckExact(kwdefaults)
                ? lu_ns_get(kwdefaults, PyTuple_GET_ITEM(code->co_localsplusnames, j)) : NULL;
            if (d == NULL) {
                goto decline;
            }
            locals[j] = d;
        }
    }
    if (star) {
        PyObject *rest = given > argc ? PyTuple_New(given - argc)
            : Py_NewRef((PyObject *)&_PyRuntime.singletons.empty_tuple);
        if (rest == NULL) {
            goto decline;
        }
        for (int i = argc; i < given; i++) {
            PyTuple_SET_ITEM(rest, i - argc, Py_NewRef(args[i]));
        }
        locals[total] = rest;
    }
    if (code->co_flags & CO_VARKEYWORDS) {
        if (kwdict == NULL && (kwdict = PyDict_New()) == NULL) {
            goto decline;
        }
        locals[total + star] = kwdict;
        kwdict = NULL;
    }
    if (prologue(f) < 0) {
        goto decline;
    }
    return f;
decline:
    /* The binding would raise, or memory ran out: the slow way does it again. */
    PyErr_Clear();
    Py_XDECREF(kwdict);
    for (int i = 0; i < code->co_nlocalsplus; i++) {
        if (i >= nborrowed) {
            Py_XDECREF(locals[i]);
        }
    }
    ts->datastack_top = base;
    return NULL;
}

PyObject *
lu_jit_new_gen(PyThreadState *ts, struct lu_frame *frame)
{
    PyCodeObject *code = frame->code;
    lu_codeunit *at = lu_code_start(code);
    while (at->op.code == MAKE_CELL || at->op.code == COPY_FREE_VARS) {
        at++;
    }
    PyObject *gen = NULL;
    if (at->op.code != RETURN_GENERATOR) {
        PyErr_SetString(PyExc_SystemError, "a generator's code that does not start with RETURN_GENERATOR");
    }
    else if (prologue(frame) == 0) {
        frame->last = at;
        frame->instr = at + 1 + lu_opcode_caches[RETURN_GENERATOR];
        frame->stackpointer = frame->localsplus + code->co_nlocalsplus;
        gen = lu_gen_new(ts, frame);
    }
    lu_frame_pop(ts, frame);
    return gen;
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
    /* A unit is live only while its entry does not wait (kill marks it dead). */
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
    bump_entries(u);
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

void
lu_jit_note_resume(void)
{
    count(&stats.resumes, 1);
}

int
lu_jit_resume(PyThreadState *ts, struct lu_frame *frame, PyObject **result)
{
    PyCodeObject *code = frame->code;
    struct lu_jit_code *jc = state_of(code);
    struct lu_task *task = (struct lu_task *)ts;
    /* lu_jit_run's conditions, and no hook: a PEP 523 hook sees resumes. */
    uintptr_t poll = __atomic_load_n(&task->carrier->poll, __ATOMIC_RELAXED);
    if (jc == NULL || jc->resume_key < 0 || frame->locals != NULL || (poll & LU_POLL_EVENTS_MASK)
        || __atomic_load_n(&jc->disabled, __ATOMIC_RELAXED) || code->_co_monitoring != NULL
        || ts->tracing || code->_co_instrumentation_version != (poll & ~LU_POLL_EVENTS_MASK)
        || ts->interp->eval_frame != NULL) {
        return 0;
    }
    uint32_t wait = __atomic_load_n(&jc->wait[jc->resume_key], __ATOMIC_RELAXED);
    if (wait != 0) {
        /* A dead resume unit's entry counts its wait down here too. */
        __atomic_store_n(&jc->wait[jc->resume_key], wait - 1, __ATOMIC_RELAXED);
        count(&stats.waited, 1);
        return 0;
    }
    /* What the interpreter runs before the unit's entry: a frame that
       never ran pops the None it was sent, then RESUME (the instruction
       before each resume point, whose caches it has). */
    lu_codeunit *start = lu_code_start(code);
    int fresh = frame->instr == jc->fresh;
    int pc = fresh ? jc->resume_key - 1 - lu_opcode_caches[RESUME] : (int)(frame->instr - start);
    int at = pc + 1 + lu_opcode_caches[RESUME];
    /* Inside `yield from` or `await` the delegation runs in the eval loop
       (ceval.c RESUME). */
    struct lu_unit *u = at < jc->ncode && jc->resume[at] && (start[pc].op.arg & 3) < 2
        ? find(jc, jc->resume_key) : NULL;
    if (u == NULL) {
        return 0;
    }
    if (fresh) {
        Py_DECREF(*--frame->stackpointer);
    }
    frame->instr = start + at;
    frame->last = start + pc;
    struct lu_frame entry = { .owner = LU_FRAME_C_ENTRY };
    entry.previous = (struct lu_frame *)ts->current_frame;
    frame->previous = &entry;
    ts->current_frame = (struct _PyInterpreterFrame *)frame;
    count(&stats.entries, 1);
    count(&stats.resumes_compiled, 1);
    bump_entries(u);
    int r = u->fn(ts, frame, result);
    if (r == LU_JIT_RETURN) {
        uint8_t ret = frame->ret;
        lu_gen_finish(ts, frame);
        ts->current_frame = (struct _PyInterpreterFrame *)entry.previous;
        if (ret == LU_RETURN_FOR_ITER) {
            Py_DECREF(*result);
            *result = LU_MISSING;
        }
        return 1;
    }
    if (r == LU_JIT_YIELD) {
        lu_gen_suspend(ts, frame, frame->last->op.arg);
        ts->current_frame = (struct _PyInterpreterFrame *)entry.previous;
        return 1;
    }
    lu_codeunit *y = frame->instr;
    if (r == LU_JIT_DEOPT && (struct lu_frame *)ts->current_frame == frame && y->op.code == YIELD_VALUE) {
        /* Left at a yield (EXIT_YIELD): YIELD_VALUE, as the interpreter runs it. */
        *result = *--frame->stackpointer;
        frame->last = y;
        frame->instr = y + 1 + lu_opcode_caches[YIELD_VALUE];
        lu_gen_suspend(ts, frame, y->op.arg);
        ts->current_frame = (struct _PyInterpreterFrame *)entry.previous;
        return 1;
    }
    *result = lu_eval_continue(ts, r == LU_JIT_ERROR);
    return 1;
}

/* Whether frame k of an error exit has a handler where it stands: the
   innermost at the raising instruction, each caller at its call. */
int
lu_jit_handles(struct lu_deopt *d, int k)
{
    struct lu_deopt_frame *fr = &d->frames[k];
    lu_codeunit *start = lu_code_start(fr->code);
    int at = k == d->nframes - 1 ? fr->pc : (int)(lu_instr_before(fr->code, start + fr->pc) - start);
    int level, handler, lasti;
    return lu_exception_handler(fr->code, at, &level, &handler, &lasti);
}

/* The frame of an error exit whose handler takes its exception, as the
   interpreter unwinds it, or -1 when the exception leaves the root frame. */
static int
handling_frame(struct lu_deopt *d)
{
    int k = d->nframes - 1;
    while (k >= 0 && !lu_jit_handles(d, k)) {
        k--;
    }
    return k;
}

/* An error exit that leaves as its direct call's result (deopt.c). */
void
lu_jit_note_raised(struct lu_deopt *d)
{
    count(&stats.raised, 1);
    if (_PyRuntime.jit.debug) {
        struct lu_deopt_frame *in = &d->frames[d->nframes - 1];
        fprintf(stderr, "jit: raised %s@%d op %d depth %d\n", PyUnicode_AsUTF8(in->code->co_qualname), in->pc,
                d->opcode, d->nframes);
    }
}

/* An exit was taken: its counts; for a guard that keeps failing, no
   more speculation there and a recompile of the code it left; for a unit
   without loops that keeps leaving, retirement. An
   exception that leaves the root frame ends the call there: the
   interpreter only unwinds it, so it is not a leave. One that an inlined
   frame's handler keeps taking (compiled code takes only the root's and
   kernel frames', which make no traceback entry) is treated as a side
   exit there: the function is called instead, and its own unit takes it. */
/* lu_jit_note_exit for kernel code inlined in the root (frame k): the
   failures count, and the mark goes, in the root's marks by context, so
   that the kernel function compiles differently only where its copy
   failed. */
static void
note_in_context(struct lu_deopt *d, struct lu_unit *u, int k)
{
    int bit = d->reason == EXIT_GUARD ? LU_CTX_NOSPEC : LU_CTX_NOINLINE;
    uint64_t key = exit_context(d, k, bit == LU_CTX_NOSPEC);
    struct lu_jit_code *root = state_for(d->frames[0].code);
    PyMutex_Lock(&root->ctx_lock);
    struct ctxmark *m = ctx_entry(root, key, 1);
    int n = m->fails < UINT16_MAX ? ++m->fails : m->fails;
    int again = (m->bits & bit) != 0;
    if (n >= GUARD_FAILS) {
        m->bits |= (uint8_t)bit;
    }
    PyMutex_Unlock(&root->ctx_lock);
    if (n < GUARD_FAILS) {
        return;
    }
    if (!again) {
        __atomic_store_n(&root->marked, __atomic_add_fetch(&marks, 1, __ATOMIC_RELEASE), __ATOMIC_RELAXED);
    }
    else if (__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
        return;
    }
    else if (u->built >= __atomic_load_n(&root->marked, __ATOMIC_RELAXED)) {
        leave(d, u);
        return;
    }
    say_recompile(d->frames[0].code, bit == LU_CTX_NOINLINE ? "inlined copy leaves here" : "guard fails here",
                  d->frames[k].code, d->frames[k].pc);
    for (struct lu_unit *r = __atomic_load_n(&root->units, __ATOMIC_ACQUIRE); r != NULL; r = r->next) {
        kill(root, r);
    }
}

void
lu_jit_note_exit(struct lu_deopt *d)
{
    count(&stats.exits[d->reason], 1);
    if (d->reason == EXIT_YIELD) {
        return;                 /* the end of a generator unit's run, no leave */
    }
    count(&stats.taken[d->opcode & 0xff], 1);
    if (_PyRuntime.jit.debug) {
        struct lu_deopt_frame *in = &d->frames[d->nframes - 1];
        fprintf(stderr, "jit: exit %s %s@%d op %d depth %d\n", reasons[d->reason],
                PyUnicode_AsUTF8(in->code->co_qualname), in->pc, d->opcode, d->nframes);
    }
    struct lu_unit *u = d->unit;
    if (d->reason == EXIT_GUARD && d->blame_code != NULL) {
        /* A guard moved out of a loop failed: its site keeps it in place,
           and the code recompiles. */
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: hoisted guard failed, from %s@%d\n", PyUnicode_AsUTF8(d->blame_code->co_qualname),
                    d->blame_pc);
        }
        lu_jit_nohoist_set(d->blame_code, d->blame_pc);
        say_recompile(d->frames[0].code, "hoisted guard", d->blame_code, d->blame_pc);
        struct lu_jit_code *root = state_for(d->frames[0].code);
        for (struct lu_unit *r = __atomic_load_n(&root->units, __ATOMIC_ACQUIRE); r != NULL; r = r->next) {
            lu_jit_unit_kill(r);
        }
        return;
    }
    int handled = d->reason == EXIT_ERROR ? handling_frame(d) : d->nframes - 1;
    if (handled < 0) {
        return;
    }
    if (d->reason == EXIT_ERROR || (d->reason == EXIT_SIDE && d->nframes == 1)) {
        leave(d, u);
        if (d->reason != EXIT_ERROR || handled == 0) {
            return;
        }
    }
    if (d->reason != EXIT_GUARD && d->reason != EXIT_ERROR && !(d->reason == EXIT_SIDE && d->nframes > 1)) {
        return;
    }
    struct lu_deopt_frame *inner = &d->frames[handled];
    if (handled > 0 && (inner->code->co_flags & LU_CO_KERNEL)) {
        note_in_context(d, u, handled);
        return;
    }
    struct lu_jit_code *jc = state_for(inner->code);
    if (inner->pc < 0 || inner->pc >= jc->ncode) {
        return;
    }
    /* Past the threshold too: another root that inlined the same copy before
       the flag was set leaves here as well, and is recompiled when it does. */
    uint16_t n = __atomic_add_fetch(&jc->fails[inner->pc], 1, __ATOMIC_RELAXED);
    if (n >= GUARD_FAILS) {
        int again;
        if (d->reason != EXIT_GUARD) {
            /* The inlined copy keeps leaving here: it is called instead from
               now on (and if that call leaves its caller's copy, the caller next). */
            again = __atomic_exchange_n(&jc->noinline, 1, __ATOMIC_RELAXED);
        }
        else {
            again = __atomic_fetch_or(&jc->nospec[inner->pc], NOSPEC, __ATOMIC_RELAXED) & NOSPEC;
        }
        /* Every unit of the root code dies and recompiles after its wait.
           A dead unit still running (a recursion's older activations) that
           fails at a site already marked teaches nothing: the units compiled
           since know the mark. Nor does a live one built after the mark: its
           build kept that check (an entry of a polymorphic cache reads in
           line what an instance may lack), so no recompile removes the exit,
           which counts as a leave. One built before it (a root that inlined
           the copy earlier) recompiles. */
        if (!again) {
            __atomic_store_n(&jc->marked, __atomic_add_fetch(&marks, 1, __ATOMIC_RELEASE), __ATOMIC_RELAXED);
        }
        else if (__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
            return;
        }
        else if (u->built >= __atomic_load_n(&jc->marked, __ATOMIC_RELAXED)) {
            leave(d, u);
            return;
        }
        say_recompile(d->frames[0].code, d->reason != EXIT_GUARD ? "inlined copy leaves" : "guard fails", inner->code,
                      inner->pc);
        struct lu_jit_code *root = state_for(d->frames[0].code);
        for (struct lu_unit *u = __atomic_load_n(&root->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
            kill(root, u);
        }
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
            pic->store = lu_base_opcode(code, pc) == STORE_ATTR;
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
    site[0].op.code = pic->store ? STORE_ATTR : LOAD_ATTR;
    site[0].op.arg = (uint8_t)pic->method;
    if (pic->store) {
        lu_specialize_store_attr(owner, site, name);
    }
    else {
        lu_specialize_load_attr(owner, site, name);
    }
    int op = site[0].op.code;
    if (op != LOAD_ATTR_INSTANCE_VALUE && op != LOAD_ATTR_METHOD_NO_DICT && op != LOAD_ATTR_METHOD_WITH_VALUES
        && op != LOAD_ATTR_NONDESCRIPTOR_NO_DICT && op != LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES
        && op != STORE_ATTR_INSTANCE_VALUE) {
        return;
    }
    struct lu_pic_entry e = { lu_cache_load32(site + 1 + LA_VERSION), (uint16_t)op, lu_cache_load(site + 1 + LA_INDEX),
                              pic->store ? NULL : lu_cache_load_ptr(site + 1 + LA_DESCR) };
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
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: pic grows %s for %s\n", PyUnicode_AsUTF8(pic->code->co_qualname), tp->tp_name);
        }
        struct lu_jit_code *jc = state_of(pic->code);
        for (struct lu_unit *u = __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
            kill(jc, u);
        }
    }
}

void
lu_jit_pic_note_interpreted(PyCodeObject *code, int pc, PyObject *owner, PyObject *name)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc == NULL || __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE) == NULL) {
        return;
    }
    struct lu_pic *pic = lu_jit_pic(code, pc, 1);
    if (pic != NULL) {
        lu_jit_pic_note(pic, owner, name);
    }
}

/* Callee tables (jit.h). Writers (the interpreter's calls, on any thread)
   take _PyRuntime.pic_lock; the translator reads published entries without
   it, and depends on the table (LU_DEP_CALLEES), so a callee added later
   recompiles the units that test for the others. */

struct lu_callees *
lu_jit_callees(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    struct lu_callees **t = jc != NULL ? __atomic_load_n(&jc->callees, __ATOMIC_ACQUIRE) : NULL;
    return t != NULL && pc >= 0 && pc < jc->ncode ? __atomic_load_n(&t[pc], __ATOMIC_ACQUIRE) : NULL;
}

/* The code's state, made before the code is hot if it has none: its
   counts go on as entries the interpreter keeps (wait), so that it still
   warms up before it compiles. */
static struct lu_jit_code *
state_cold(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc != NULL) {
        return jc;
    }
    uintptr_t v = (uintptr_t)__atomic_load_n(&code->co_executors, __ATOMIC_RELAXED);
    int mode = _PyRuntime.jit.mode == LU_JIT_ALWAYS;
    unsigned calls = v & 1 ? (unsigned)(v >> 1) : 0, threshold = lu_jit_threshold(code);
    jc = state_for(code);
    for (int i = 0; i < jc->ncode && !mode; i++) {
        __atomic_store_n(&jc->wait[i], threshold, __ATOMIC_RELAXED);
    }
    if (jc->call_pc >= 0 && !mode) {
        unsigned left = calls < threshold ? threshold - calls : 0;
        __atomic_store_n(&jc->wait[jc->call_pc], left, __ATOMIC_RELAXED);
    }
    return jc;
}

void
lu_jit_callee_note(PyCodeObject *code, lu_codeunit *cache, PyObject *callable, PyObject *self, uint32_t tag)
{
    int pc = (int)(cache - 1 - lu_code_start(code));
    PyObject *fn = Py_IS_TYPE(callable, &PyMethod_Type) && self == NULL ? ((PyMethodObject *)callable)->im_func
        : callable;
    PyFunctionObject *f = (PyFunctionObject *)fn;
    /* What only a collection frees can be named; any other function by
       what every function of its code shares; any other callable not. */
    int named = _Py_IsImmortal(fn) || lu_has_deferred_refcount(fn);
    if (!named && !PyFunction_Check(fn)) {
        lu_cache_store32(cache + 1, tag);
        return;
    }
    struct lu_callee want = { named ? fn : f->func_code, named ? NULL : f->func_globals,
                              (fn != callable || self != NULL ? LU_CALLEE_BOUND : 0) | (named ? 0 : LU_CALLEE_CODE) };
    struct lu_jit_code *jc = state_cold(code);
    struct lu_callees *t = lu_jit_callees(code, pc);
    int n = t != NULL ? __atomic_load_n(&t->n, __ATOMIC_ACQUIRE) : 0;
    for (int i = 0; i < n; i++) {
        if (t->e[i].obj == want.obj && t->e[i].kind == want.kind && t->e[i].globals == want.globals) {
            lu_cache_store32(cache + 1, tag);
            return;
        }
    }
    int added = 0;
    PyMutex_Lock(&_PyRuntime.pic_lock);
    if (jc->callees == NULL) {
        __atomic_store_n(&jc->callees, calloc((size_t)jc->ncode, sizeof(struct lu_callees *)), __ATOMIC_RELEASE);
    }
    t = jc->callees[pc];
    if (t == NULL) {
        t = calloc(1, sizeof(struct lu_callees));
        __atomic_store_n(&jc->callees[pc], t, __ATOMIC_RELEASE);
    }
    int known = 0;
    for (int i = 0; i < t->n; i++) {
        known |= t->e[i].obj == want.obj && t->e[i].kind == want.kind && t->e[i].globals == want.globals;
    }
    if (!known && !t->mega) {
        if (t->n < LU_CALLEE_WAYS) {
            t->e[t->n] = want;
            __atomic_store_n(&t->n, t->n + 1, __ATOMIC_RELEASE);
        }
        else {
            __atomic_store_n(&t->mega, 1, __ATOMIC_RELEASE);
        }
        added = 1;
    }
    lu_cache_store32(cache + 1, t->mega ? LU_CALLEE_MEGA : tag);
    PyMutex_Unlock(&_PyRuntime.pic_lock);
    if (added) {
        lu_jit_invalidate(LU_DEP_CALLEES, (uintptr_t)t, 0);
    }
}

/* Operator class tables (jit.h), kept as callee tables are. */

struct lu_opclasses *
lu_jit_opclasses(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    struct lu_opclasses **t = jc != NULL ? __atomic_load_n(&jc->opclasses, __ATOMIC_ACQUIRE) : NULL;
    return t != NULL && pc >= 0 && pc < jc->ncode ? __atomic_load_n(&t[pc], __ATOMIC_ACQUIRE) : NULL;
}

void
lu_jit_opclass_note(PyCodeObject *code, int pc, PyTypeObject *left, PyTypeObject *right)
{
    struct lu_opclasses *t = lu_jit_opclasses(code, pc);
    int n = t != NULL ? __atomic_load_n(&t->n, __ATOMIC_ACQUIRE) : 0;
    if (t != NULL && __atomic_load_n(&t->mega, __ATOMIC_RELAXED)) {
        return;
    }
    for (int i = 0; i < n; i++) {
        if (t->e[i].left == left && t->e[i].right == right) {
            return;
        }
    }
    struct lu_jit_code *jc = state_cold(code);
    int added = 0;
    PyMutex_Lock(&_PyRuntime.pic_lock);
    if (jc->opclasses == NULL) {
        __atomic_store_n(&jc->opclasses, calloc((size_t)jc->ncode, sizeof(struct lu_opclasses *)), __ATOMIC_RELEASE);
    }
    t = jc->opclasses[pc];
    if (t == NULL) {
        t = calloc(1, sizeof(struct lu_opclasses));
        __atomic_store_n(&jc->opclasses[pc], t, __ATOMIC_RELEASE);
    }
    int known = 0;
    for (int i = 0; i < t->n; i++) {
        known |= t->e[i].left == left && t->e[i].right == right;
    }
    if (!known && !t->mega) {
        if (t->n < LU_OPCLASS_WAYS) {
            t->e[t->n].left = left;
            t->e[t->n].right = right;
            __atomic_store_n(&t->n, t->n + 1, __ATOMIC_RELEASE);
        }
        else {
            __atomic_store_n(&t->mega, 1, __ATOMIC_RELEASE);
        }
        added = 1;
    }
    PyMutex_Unlock(&_PyRuntime.pic_lock);
    if (added) {
        lu_jit_invalidate(LU_DEP_CALLEES, (uintptr_t)t, 0);
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
    for (int pc = 0; jc != NULL && jc->callees != NULL && pc < jc->ncode; pc++) {
        struct lu_callees *t = jc->callees[pc];
        int k = 0;
        for (int i = 0; t != NULL && i < t->n; i++) {
            if (!dead(t->e[i].obj)) {
                t->e[k++] = t->e[i];
            }
        }
        if (t != NULL && k < t->n) {
            t->n = k;
            lu_cache_store32(lu_code_start(code) + pc + 2, 0);     /* the next call relearns its callee */
        }
    }
    for (int pc = 0; jc != NULL && jc->opclasses != NULL && pc < jc->ncode; pc++) {
        struct lu_opclasses *t = jc->opclasses[pc];
        int k = 0;
        for (int i = 0; t != NULL && i < t->n; i++) {
            if (!dead((PyObject *)t->e[i].left) && !dead((PyObject *)t->e[i].right)) {
                t->e[k++] = t->e[i];
            }
        }
        if (t != NULL) {
            t->n = k;
        }
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
    for (int i = 0; jc->callees != NULL && i < jc->ncode; i++) {
        free(jc->callees[i]);
    }
    free(jc->callees);
    for (int i = 0; jc->opclasses != NULL && i < jc->ncode; i++) {
        free(jc->opclasses[i]);
    }
    free(jc->opclasses);
    free(jc->ctx);
    free(jc->nospec);
    free(jc->fails);
    free(jc->wait);
    free(jc->seen);
    free(jc->resume);
    free(jc);
    code->co_executors = NULL;
}

void
lu_jit_report(void)
{
    if (!_PyRuntime.jit.stats) {
        return;
    }
    fprintf(stderr, "jit: backend %s, mode %s\n", lu_target.name != NULL ? lu_target.name : "none",
            _PyRuntime.jit.mode == LU_JIT_ALWAYS ? "always" : _PyRuntime.jit.mode == LU_JIT_ON ? "on" : "off");
    fprintf(stderr, "jit: compiles %lu, failed %lu, units recompiled %lu, inlined calls %lu, "
            "compile time %.1f ms, code %lu bytes\n", stats.compiles, stats.failures, stats.units_dead,
            stats.inlined, stats.compile_seconds * 1e3, stats.code_bytes);
    fprintf(stderr, "jit: compile phases: build %.1f ms, optimize %.1f ms, regalloc %.1f ms, emit %.1f ms, "
            "builds put off %.1f ms\n", stats.phase_seconds[0] * 1e3, stats.phase_seconds[1] * 1e3,
            stats.phase_seconds[2] * 1e3, stats.phase_seconds[3] * 1e3, stats.phase_seconds[4] * 1e3);
    fprintf(stderr, "jit: units invalidated %lu\n", stats.invalidated);
    fprintf(stderr, "jit: generator resumes interpreted %lu, compiled %lu\n",
            stats.resumes > stats.resumes_compiled ? stats.resumes - stats.resumes_compiled : 0, stats.resumes_compiled);
    for (int i = 0; i < 8 && stats.failure_names[i] != NULL; i++) {
        fprintf(stderr, "jit:   failed: %s: %lu\n", stats.failure_names[i], stats.failure_reasons[i]);
    }
    fprintf(stderr, "jit: entries %lu, returns %lu, direct calls %lu, compiled calls %lu, exits", stats.entries,
            stats.returns, stats.direct, stats.compiled);
    for (int i = 0; i < EXIT_REASONS; i++) {
        fprintf(stderr, " %s %lu", reasons[i], stats.exits[i]);
    }
    fprintf(stderr, ", raised %lu\n", stats.raised);
    fprintf(stderr, "jit: entries waited %lu, units retired %lu\n", stats.waited, stats.retired);
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
    static const char c_path_names[C_PATHS][16] = {
#define LU_C_PATH_NAME(name) #name,
        LU_C_PATHS(LU_C_PATH_NAME)
#undef LU_C_PATH_NAME
    };
    static const char why_not_names[WHY_NOTS][16] = {
#define LU_WHY_NOT_NAME(name) #name,
        LU_WHY_NOT(LU_WHY_NOT_NAME)
#undef LU_WHY_NOT_NAME
    };
    fprintf(stderr, "\njit: whynot");
    for (int k = 0; k < WHY_NOTS; k++) {
        fprintf(stderr, "%s %s %lu", k ? "," : "", why_not_names[k], stats.why_not[k]);
    }
    fprintf(stderr, "\n");
    report_why_not_callees();
    fprintf(stderr, "jit: C paths");
    for (int k = 0; k < C_PATHS; k++) {
        fprintf(stderr, "%s %s %lu", k ? "," : "", c_path_names[k], stats.c_paths[k]);
    }
    fprintf(stderr, "\n");
    /* The census: what compiled code executed, in all and per unit. */
    unsigned long total[CNT_COUNT];
    PyMutex_Lock(&census.lock);
    memcpy(total, census.freed, sizeof total);
    for (struct lu_unit *u = census.units; u != NULL; u = u->stats_next) {
        unsigned long guards = 0, all = 0;
        for (int k = 0; k < CNT_COUNT; k++) {
            total[k] += u->counts[k];
            guards += LU_COUNT_IS_GUARD(k) ? u->counts[k] : 0;
            all += u->counts[k];
        }
        if (all >= 1000) {
            fprintf(stderr, "jit: unit %s entries %lu guards %lu:", u->stats_name, u->entered, guards);
            for (int k = 0; k < CNT_COUNT; k++) {
                if (u->counts[k]) {
                    fprintf(stderr, " %s %lu", lu_count_names[k], u->counts[k]);
                }
            }
            fprintf(stderr, "\n");
        }
    }
    PyMutex_Unlock(&census.lock);
    fprintf(stderr, "jit: executed:");
    for (int k = 0; k < CNT_COUNT; k++) {
        fprintf(stderr, " %s %lu", lu_count_names[k], total[k]);
    }
    fprintf(stderr, "\n");
    report_allocs();
}
