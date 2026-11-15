/* The optimizing tier's policy (runtime/jit/jit.h): when to compile, the
   per-code state that holds compiled units, entering them, what exits
   teach the next compile, and the counts -X lucifer_jit_stats prints.

   Compiles run synchronously in the thread that found the code hot, one
   per code object at a time; another thread meanwhile interprets. A unit
   is published by a release store after it is complete. A unit may rely
   on what only a writer changes (a type's version, a dict's keys or one
   key's value, a function's code) between polls: a change another thread
   makes reaches it at its next poll, and the running thread's own at once
   (DESIGN 6.2 step 8, runtime/jit/deps.c). A unit also goes dead when one
   guard keeps failing, which marks its instruction not to be speculated
   on again (the donor's exit-driven recompile): each such death moves one
   site toward generic, once.

   An exit no recompile can remove (an instruction the tier does not
   compile, an exception the unit does not handle, a guard a unit built
   after its site's mark still keeps) costs an exit and, at the next call
   or back-edge, an entry. A unit without loops does a bounded amount of
   work per entry, and so does one whose entries leave at a kept guard;
   when such a unit leaves that way on most of its entries, it costs more
   than it saves, and it dies (is retired). An exception that leaves the
   root frame is no such exit: the call ends there.

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
   recompile. A compile that finds most of its sites never ran is put off
   (a first build that takes no call in line finds out):
   the interpreter runs that entry a few times first, once per entry. So
   is one that met an operator site without classes, which the
   interpreter notes only at sites a build asked about: code no build
   reads pays nothing for them.

   A dead unit is freed once nothing can be in it (reclaim, below): not
   when its code dies, which module code never does, nor at once, since
   activations of it may still run. */
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
/* Instructions a unit may have and still take a call in line at first
   (what the evaluator folds away costs it nothing), the entries
   after which a unit that left calls out is built again with twice that,
   and how often: past it a build costs more than its unit's code earns
   back (a regex engine inlined whole took minutes). */
#define INLINE_BUDGET 1000
#define HOT_ENTRIES 4096
#define MAX_GROWN 4
/* Entries a unit counts: one past this many is not retired, and the count
   is only read, so calls from many threads share its line. */
#define ENTRIES_SEEN (1u << 16)

struct lu_jit_code {
    int compiling;              /* a thread is compiling: others interpret */
    int disabled;               /* LUCIFER_JIT_ONLY does not list it */
    int level;                  /* the wait's doublings: retirements and failed compiles, less decay */
    struct lu_unit *units;      /* live ones and dead ones not yet freed */
    int ncode;
    uint8_t *nospec;            /* per code unit: NOSPEC, do not speculate; NOHOIST, keep its guards in place */
    uint16_t *fails;            /* per code unit: guard failures */
    uint32_t *wait;             /* per code unit: entries to leave to the interpreter */
    uint8_t *seen;              /* per code unit: WARMED */
    int noinline;               /* a side exit in an inlined copy keeps leaving: call it instead */
    unsigned long marked;       /* marks when a site of it (or noinline) was last marked */
    int call_pc;                /* where a call enters (call_entry), or -1 */
    struct lu_pic **pics;       /* per code unit: an attribute site's polymorphic cache */
    int grown;                  /* doublings of the inlining budget: a hot unit that left calls out asked */
    struct lu_callees **callees;        /* per code unit: a call site's callees */
    struct lu_opclasses **opclasses;    /* per code unit: an operator site's operand classes */
    struct lu_prof *profs;              /* a root's: what it learned of values, by context (target.h) */
    unsigned long learned;              /* how often one of them changed */
    PyMutex prof_lock;                  /* theirs */
};

struct lu_prof {
    struct lu_prof *next;
    uint64_t key;
    struct lu_jit_code *jc;     /* the root's */
    PyTypeObject *type;         /* the type the value had (a reference), or NULL before any */
    uint8_t poly;               /* it had another too, or the guard on it kept failing: nothing to rely on */
    uint16_t fails;
    uint32_t passes;            /* times met with nothing new while the root knew more than the unit */
};

/* Passes of a place, with more learned than its unit was built with,
   before the unit is built again: a build costs about what that many
   passes of unspecialized code do. */
#define REBUILD_PASSES 16

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
    unsigned long ir;                   /* instructions the compiles built */
    unsigned long failure_reasons[8];
    const char *failure_names[8];
    unsigned long c_paths[C_PATHS];
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

PyObject lu_jit_unbound = { .ob_flags = _Py_STATICALLY_ALLOCATED_FLAG, .ob_ref_local = _Py_IMMORTAL_REFCNT_LOCAL,
                             .ob_type = &PyBaseObject_Type };

struct lu_prof *
lu_jit_prof(PyCodeObject *root, uint64_t key)
{
    struct lu_jit_code *jc = state_for(root);
    PyMutex_Lock(&jc->prof_lock);
    struct lu_prof *p = jc->profs;
    while (p != NULL && p->key != key) {
        p = p->next;
    }
    if (p == NULL && (p = calloc(1, sizeof(struct lu_prof))) != NULL) {
        p->key = key;
        p->jc = jc;
        p->next = jc->profs;
        jc->profs = p;
    }
    PyMutex_Unlock(&jc->prof_lock);
    return p;
}

PyTypeObject *
lu_jit_prof_type(struct lu_prof *p, int *poly)
{
    PyMutex_Lock(&p->jc->prof_lock);
    PyTypeObject *t = (PyTypeObject *)Py_XNewRef((PyObject *)p->type);
    *poly = p->poly;
    PyMutex_Unlock(&p->jc->prof_lock);
    return t;
}

unsigned long
lu_jit_prof_learned(PyCodeObject *root)
{
    return __atomic_load_n(&state_for(root)->learned, __ATOMIC_RELAXED);
}

int
lu_jit_prof_note(struct lu_prof *p, PyObject *o, unsigned long learned)
{
    struct lu_jit_code *jc = p->jc;
    PyTypeObject *t = Py_TYPE(o);
    if (__atomic_load_n(&p->type, __ATOMIC_RELAXED) == t || __atomic_load_n(&p->poly, __ATOMIC_RELAXED)) {
        /* Nothing new here; once the root has learned anything since the
           unit was built, and the place has been passed often enough for
           another build to pay, the unit is built again. */
        return __atomic_load_n(&jc->learned, __ATOMIC_RELAXED) == learned
            || __atomic_add_fetch(&p->passes, 1, __ATOMIC_RELAXED) < REBUILD_PASSES;
    }
    PyMutex_Lock(&jc->prof_lock);
    if (p->type == NULL) {
        __atomic_store_n(&p->type, (PyTypeObject *)Py_NewRef((PyObject *)t), __ATOMIC_RELAXED);
        jc->learned++;
    }
    else if (p->type != t && !p->poly) {
        __atomic_store_n(&p->poly, 1, __ATOMIC_RELAXED);
        jc->learned++;
    }
    PyMutex_Unlock(&jc->prof_lock);
    return 1;
}

static void kill(struct lu_jit_code *jc, struct lu_unit *u);

/* An exit a learned type's site took: the unit is built again with what
   the root knows now, at once, when its sites reported; when the type
   keeps being another, nothing is relied on there and every unit of the
   root is built again. */
static void
prof_exit(struct lu_deopt *d)
{
    struct lu_prof *p = d->prof;
    struct lu_jit_code *jc = p->jc;
    if (d->tier) {
        kill(jc, d->unit);
        __atomic_store_n(&jc->wait[d->unit->entry_pc], 0, __ATOMIC_RELAXED);
        return;
    }
    PyMutex_Lock(&jc->prof_lock);
    int now = !p->poly && ++p->fails >= GUARD_FAILS;
    if (now) {
        p->poly = 1;
        jc->learned++;
    }
    PyMutex_Unlock(&jc->prof_lock);
    for (struct lu_unit *u = now ? __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE) : NULL; u != NULL; u = u->next) {
        kill(jc, u);
    }
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

/* An activation of a unit, linked from its task (freeing dead units, below). */
struct lu_jit_act {
    struct lu_unit *unit;
    struct lu_jit_act *outer;
};

/* The dead units not yet freed, the count of them, and of those freed. */
static struct { pthread_mutex_t lock; struct lu_unit *head; unsigned long n, freed; } dying = {
    .lock = PTHREAD_MUTEX_INITIALIZER };

static void
dying_unlink(struct lu_unit *u)
{
    *u->dying_pprev = u->dying_next;
    if (u->dying_next != NULL) {
        u->dying_next->dying_pprev = u->dying_pprev;
    }
    u->dying_pprev = NULL;
    dying.n--;
}

/* The unit just died: it waits on the dying list. */
static void
dying_add(struct lu_unit *u)
{
    pthread_mutex_lock(&dying.lock);
    u->dying_next = dying.head;
    u->dying_pprev = &dying.head;
    if (dying.head != NULL) {
        dying.head->dying_pprev = &u->dying_next;
    }
    dying.head = u;
    dying.n++;
    pthread_mutex_unlock(&dying.lock);
}

/* Frees a unit no thread can reach: its dependencies first, after which
   no writer kills it, so a unit that died unpublished (a compile a writer
   overtook) leaves the dying list here. */
static void
free_unit(struct lu_unit *u)
{
    lu_jit_deps_free(u);
    if (u->dying_pprev != NULL) {
        pthread_mutex_lock(&dying.lock);
        dying_unlink(u);
        pthread_mutex_unlock(&dying.lock);
    }
    lu_target_free(u);
    free(u->exits);
    for (int i = 0; i < u->nconsts; i++) {
        Py_XDECREF(u->consts[i]);
    }
    free(u->consts);
    free(u->stress_counter);
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

/* Freeing dead units.

   A unit is in use while an activation runs it or a thread reads its
   code's list of units. An activation is a call of the unit's code from
   lu_jit_enter or lu_jit_run, which links it from its task (jit_act) for
   exactly that call, innermost first. Whatever reads a unit's code or
   descriptions does so inside one: compiled code, its exits
   (lu_jit_deopt returns into the unit's epilogue), walkers that read a
   frame's published state (frame->jit_desc, which only an activation
   sets and which is cleared before it returns), and a generator's units,
   which leave at every yield, so a suspended generator is in none.

   A unit goes on the dying list when it dies. A collection, with the
   world stopped, marks every unit some task's activation is in, takes
   each dying unit none is in off its code's list and the dying list, and
   frees them once the world runs again. No thread can still reach one:

   - Every other task is stopped at a poll or detached, and nothing
     between taking a unit from the code's list (find) and linking the
     activation polls, detaches or takes a lock that may park: the marks
     see every activation that can run the unit.
   - Nothing that walks a code's list (find, the loops that kill its
     units) polls, detaches or parks either: no thread stands on the list
     when it changes, and after it none finds the unit.
   - A compile in progress (its code's compiling flag) may hold a unit it
     has not linked yet, which an invalidation may have killed: that
     code's dying units wait for the next collection.
   - A walker reading another task's published state (sys._current_frames,
     a frame object's line) is attached: the state was an activation's
     when it loaded jit_desc, and no stop frees that unit before the
     walker's next poll. faulthandler, which need not be attached, reads
     only its own task's.
   - The dependency buckets (deps.c) name the unit until the free unlinks
     its dependencies under their locks, so a writer that took the unit
     from a bucket is done with it.
   - Code dealloc takes its units off its list and the dying list under
     the dying lock before it frees them, so each unit is freed once; the
     free after the restart touches neither the code nor its state, which
     may be gone. A writer may still kill one of them while the dealloc
     waits on a bucket's lock to free its dependencies, putting it back on
     the dying list: a collection then finds it on no code's list and
     leaves it to the dealloc, whose free takes it off again.

   The dying lock is a pthread mutex: a holder never detaches, so none is
   stopped while it holds the lock. */

struct lu_unit *
lu_jit_reclaim(PyInterpreterState *interp)
{
    if (__atomic_load_n(&dying.head, __ATOMIC_RELAXED) == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        for (struct lu_jit_act *a = ((struct lu_task *)ts)->jit_act; a != NULL; a = a->outer) {
            a->unit->held = 1;
        }
    }
    pthread_mutex_lock(&dying.lock);
    struct lu_unit *freed = NULL;
    for (struct lu_unit *u = dying.head, *next; u != NULL; u = next) {
        next = u->dying_next;
        struct lu_jit_code *jc = state_of(u->code);
        if (u->held || jc->compiling) {
            continue;
        }
        struct lu_unit **link = &jc->units;
        while (*link != NULL && *link != u) {
            link = &(*link)->next;
        }
        if (*link == NULL) {
            continue;           /* its code is freeing it */
        }
        __atomic_store_n(link, u->next, __ATOMIC_RELEASE);
        dying_unlink(u);
        u->next = freed;
        freed = u;
    }
    pthread_mutex_unlock(&dying.lock);
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        for (struct lu_jit_act *a = ((struct lu_task *)ts)->jit_act; a != NULL; a = a->outer) {
            a->unit->held = 0;
        }
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    return freed;
}

void
lu_jit_reclaim_free(struct lu_unit *units)
{
    for (struct lu_unit *u = units, *next; u != NULL; u = next) {
        next = u->next;
        free_unit(u);
        count(&dying.freed, 1);
    }
}

static struct lu_unit *
compile(struct lu_jit_code *jc, struct lu_frame *frame, int pc, uint64_t ticket)
{
    struct lu_func f = { 0 };
    struct lu_unit *u = calloc(1, sizeof(struct lu_unit));
    u->code = frame->code;
    u->built = __atomic_load_n(&marks, __ATOMIC_ACQUIRE);
    f.code = frame->code;
    f.entry_pc = (int)(frame->instr - lu_code_start(frame->code));
    /* What the evaluator may inline, in code units: a unit that stayed hot
       with calls left out is built again with twice as much. */
    f.budget = INLINE_BUDGET << jc->grown;
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
    if (!(jc->seen[pc] & WARMED)) {
        /* Whether the interpreter has run the entry's own code, a build
           that takes no call in line can say: it reads every site of the
           entry's code and none of a callee's, at a fraction of the cost
           of the build that would be thrown away. */
        int budget = f.budget;
        f.budget = -1;
        int cold = lu_ssa_build(&f, frame) == 0 && f.cold > 0 && 2 * f.cold >= f.sites;
        f.budget = budget;
        if (cold) {
            stats.phase_seconds[4] += now() - t0;
            /* Most of it never ran: the interpreter runs the entry a few times
               to gather feedback, and the compile after that stands. */
            jc->seen[pc] |= WARMED;
            __atomic_store_n(&jc->wait[pc], WARMUP, __ATOMIC_RELAXED);
            lu_ir_free(&f);
            free_unit(u);
            return NULL;
        }
    }
    int ok = lu_ssa_build(&f, frame) == 0;
    double t1 = now();
    count(&stats.ir, (unsigned long)f.nins);
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
    u->refused = f.refused;
    u->consts = f.consts;
    u->nconsts = f.nconsts;
    f.consts = NULL;
    f.nconsts = 0;
    count(&stats.code_bytes, u->code_bytes);
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: compiled %s@%d: %zu bytes, %d calls inlined (%s:%d)\n",
                PyUnicode_AsUTF8(frame->code->co_qualname), pc, u->code_bytes, f.ninlined,
                PyUnicode_AsUTF8(frame->code->co_filename), frame->code->co_firstlineno);
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
    if (__atomic_exchange_n(&u->dead, 1, __ATOMIC_RELAXED)) {
        return;
    }
    dying_add(u);
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
   most of its entries is retired. So is one with loops that leaves at a
   guard it kept on a site already marked: no recompile removes that
   guard, and a loop that fails it each iteration pays an exit and an
   entry each iteration. */
static void
leave(struct lu_deopt *d, struct lu_unit *u, int kept)
{
    unsigned long n = bump(&u->leaves);
    unsigned long entries = __atomic_load_n(&u->entries, __ATOMIC_RELAXED);
    if ((kept || !u->loops) && n >= LEAVES && 2 * n >= entries && entries < ENTRIES_SEEN) {
        retire(state_for(lu_deopt_frame(d, 0)->code), u);
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
    struct lu_unit *u = find(jc, pc);
    if (u == NULL) {
        uint64_t ticket = lu_jit_deps_begin();     /* before anything the build reads */
        u = compile(jc, frame, pc, ticket);
        lu_jit_deps_end();
        if (u != NULL) {
            u->next = jc->units;
            __atomic_store_n(&jc->units, u, __ATOMIC_RELEASE);
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
    if (u->refused && __atomic_load_n(&u->entries, __ATOMIC_RELAXED) == HOT_ENTRIES && jc != NULL
        && jc->grown < MAX_GROWN) {
        /* It stayed hot with calls left out of line for want of budget:
           the next build of this code gets twice as much. */
        __atomic_store_n(&jc->grown, jc->grown + 1, __ATOMIC_RELAXED);
        kill(jc, u);
        return LU_JIT_NONE;
    }
    struct lu_jit_act act = { u, task->jit_act };
    task->jit_act = &act;
    bump_entries(u);
    int debug = _PyRuntime.jit.debug;
    if (debug) {
        fprintf(stderr, "jit: enter %s@%d\n", PyUnicode_AsUTF8(code->co_qualname), pc);
    }
    int r = u->fn(ts, frame, result);
    task->jit_act = act.outer;
    if (debug) {
        struct lu_frame *cf = (struct lu_frame *)ts->current_frame;
        int py = cf != NULL && cf->code != NULL;       /* else back in a C caller, or its shim frame */
        fprintf(stderr, "jit: %s@%d -> %d at %s@%d%s\n", PyUnicode_AsUTF8(code->co_qualname), pc, r,
                py ? PyUnicode_AsUTF8(cf->code->co_qualname) : "C", py ? (int)(cf->instr - lu_code_start(cf->code)) : -1,
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

PyObject *
lu_jit_new_gen(PyThreadState *ts, struct lu_frame *frame)
{
    PyCodeObject *code = frame->code;
    lu_codeunit *at = lu_code_start(code);
    while (at->op.code == MAKE_CELL || at->op.code == COPY_FREE_VARS) {
        at++;
    }
    if (at->op.code != RETURN_GENERATOR) {
        return LU_JIT_DECLINED;
    }
    PyObject *gen = NULL;
    if (prologue(frame) == 0) {
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
    struct lu_jit_act act = { u, task->jit_act };
    task->jit_act = &act;
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: call %s\n", PyUnicode_AsUTF8(code->co_qualname));
    }
    int pc = u->entry_pc;
    struct lu_frame entry = { .owner = LU_FRAME_C_ENTRY };
    entry.previous = (struct lu_frame *)ts->current_frame;
    frame->previous = &entry;
    ts->current_frame = (struct _PyInterpreterFrame *)frame;
    if (pc > 1 && prologue(frame) < 0) {
        task->jit_act = act.outer;
        *result = lu_eval_continue(ts, 1);
        return 1;
    }
    frame->instr = lu_code_start(code) + pc;
    frame->last = frame->instr - 1 - lu_opcode_caches[RESUME];
    lu_qsbr_quiescent(task->carrier);
    count(&stats.direct, 1);
    bump_entries(u);
    int r = u->fn(ts, frame, result);
    task->jit_act = act.outer;
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

/* Whether frame k of an error exit has a handler where it stands: the
   innermost at the raising instruction, each caller at its call. */
static int
handles(struct lu_deopt *d, int k)
{
    const struct lu_deopt_frame *fr = lu_deopt_frame(d, k);
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
    while (k >= 0 && !handles(d, k)) {
        k--;
    }
    return k;
}

/* An exit was taken: its counts; for a guard that keeps failing, no
   more speculation there and a recompile of the code it left; for a unit
   without loops that keeps leaving, retirement. An
   exception that leaves the root frame ends the call there: the
   interpreter only unwinds it, so it is not a leave. One that an inlined
   frame's handler keeps taking (compiled code takes only the root's and
   kernel frames', which make no traceback entry) is treated as a side
   exit there: the function is called instead, and its own unit takes it. */
void
lu_jit_note_exit(struct lu_deopt *d)
{
    count(&stats.exits[d->reason], 1);
    if (d->reason == EXIT_YIELD) {
        return;                 /* the end of a generator unit's run, no leave */
    }
    count(&stats.taken[d->opcode & 0xff], 1);
    if (_PyRuntime.jit.debug) {
        const struct lu_deopt_frame *in = lu_deopt_frame(d, d->nframes - 1);
        fprintf(stderr, "jit: exit %s %s@%d op %d depth %d\n", reasons[d->reason],
                PyUnicode_AsUTF8(in->code->co_qualname), in->pc, d->opcode, d->nframes);
    }
    if (d->prof != NULL) {
        prof_exit(d);
        return;
    }
    struct lu_unit *u = d->unit;
    int handled = d->reason == EXIT_ERROR ? handling_frame(d) : d->nframes - 1;
    if (handled < 0) {
        return;
    }
    if (d->reason == EXIT_ERROR || (d->reason == EXIT_SIDE && d->nframes == 1)) {
        leave(d, u, 0);
        if (d->reason != EXIT_ERROR || handled == 0) {
            return;
        }
    }
    if (d->reason != EXIT_GUARD && d->reason != EXIT_ERROR && !(d->reason == EXIT_SIDE && d->nframes > 1)) {
        return;
    }
    /* The site the exit counts against: the instruction it left at, or
       for a guard that speculates for a loop header, the header. */
    const struct lu_deopt_frame *inner = lu_deopt_frame(d, handled);
    PyCodeObject *site = inner->code;
    int site_pc = inner->pc;
    if (d->reason == EXIT_GUARD && d->blame_code != NULL) {
        site = d->blame_code;
        site_pc = d->blame_pc;
    }
    struct lu_jit_code *jc = state_for(site);
    if (site_pc < 0 || site_pc >= jc->ncode) {
        return;
    }
    /* Past the threshold too: another root that inlined the same copy before
       the flag was set leaves here as well, and is recompiled when it does. */
    uint16_t n = __atomic_add_fetch(&jc->fails[site_pc], 1, __ATOMIC_RELAXED);
    if (n >= GUARD_FAILS) {
        int again;
        if (d->reason != EXIT_GUARD) {
            /* The inlined copy keeps leaving here: it is called instead from
               now on (and if that call leaves its caller's copy, the caller next). */
            again = __atomic_exchange_n(&jc->noinline, 1, __ATOMIC_RELAXED);
        }
        else {
            again = __atomic_fetch_or(&jc->nospec[site_pc], NOSPEC, __ATOMIC_RELAXED) & NOSPEC;
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
            leave(d, u, 1);
            return;
        }
        PyCodeObject *code = lu_deopt_frame(d, 0)->code;
        say_recompile(code, d->reason != EXIT_GUARD ? "inlined copy leaves" : "guard fails", site, site_pc);
        struct lu_jit_code *root = state_for(code);
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
    if (!lu_jit_pic_learns(pic, n)) {
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
        && op != STORE_ATTR_INSTANCE_VALUE && op != LOAD_ATTR_PROPERTY) {
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
    if (added && _PyRuntime.jit.debug) {
        /* A unit that tests for fewer shapes is built again when its generic load next runs (eval_attr.c). */
        fprintf(stderr, "jit: pic grows %s for %s\n", PyUnicode_AsUTF8(pic->code->co_qualname), tp->tp_name);
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
       what every function of its code shares; a builtin method bound to
       its self by its kernel function; any other callable not. */
    int named = _Py_IsImmortal(fn) || lu_has_deferred_refcount(fn);
    PyObject *bself = NULL;
    PyObject *kfn = !named && self == NULL && PyCFunction_Check(fn) ? lu_kernel_function_of(fn, &bself) : NULL;
    int cmeth = kfn != NULL && bself != NULL && !PyModule_Check(bself) && PyFunction_Check(kfn)
        && (_Py_IsImmortal(kfn) || lu_has_deferred_refcount(kfn));
    if (!named && !cmeth && !PyFunction_Check(fn)) {
        lu_cache_store32(cache + 1, tag);
        return;
    }
    struct lu_callee want = cmeth
        ? (struct lu_callee){ kfn, (PyObject *)((PyCFunctionObject *)fn)->m_ml, LU_CALLEE_CMETH }
        : (struct lu_callee){ named ? fn : f->func_code, named ? NULL : f->func_globals,
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

/* Operator class tables (jit.h), kept as callee tables are. A site has
   one only once a build asked for it; the first pair it learns recompiles
   the units built without any, later ones nothing: a unit that meets one
   leaves at its arm's guard, and one that keeps leaving is retired and
   compiled again with the pair. */

struct lu_opclasses *
lu_jit_opclasses(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    struct lu_opclasses **t = jc != NULL ? __atomic_load_n(&jc->opclasses, __ATOMIC_ACQUIRE) : NULL;
    return t != NULL && pc >= 0 && pc < jc->ncode ? __atomic_load_n(&t[pc], __ATOMIC_ACQUIRE) : NULL;
}

struct lu_opclasses *
lu_jit_want_classes(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_cold(code);
    PyMutex_Lock(&_PyRuntime.pic_lock);
    if (jc->opclasses == NULL) {
        __atomic_store_n(&jc->opclasses, calloc((size_t)jc->ncode, sizeof(struct lu_opclasses *)), __ATOMIC_RELEASE);
    }
    struct lu_opclasses *t = jc->opclasses[pc];
    if (t == NULL) {
        t = calloc(1, sizeof(struct lu_opclasses));
        __atomic_store_n(&jc->opclasses[pc], t, __ATOMIC_RELEASE);
    }
    PyMutex_Unlock(&_PyRuntime.pic_lock);
    return t;
}

void
lu_jit_opclass_note(PyCodeObject *code, int pc, PyTypeObject *left, PyTypeObject *right)
{
    struct lu_opclasses *t = lu_jit_opclasses(code, pc);
    if (t == NULL || __atomic_load_n(&t->mega, __ATOMIC_RELAXED)) {
        return;
    }
    int n = __atomic_load_n(&t->n, __ATOMIC_ACQUIRE);
    for (int i = 0; i < n; i++) {
        if (t->e[i].left == left && t->e[i].right == right) {
            return;
        }
    }
    PyMutex_Lock(&_PyRuntime.pic_lock);
    int first = t->n == 0, known = 0;
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
    }
    else {
        first = 0;
    }
    PyMutex_Unlock(&_PyRuntime.pic_lock);
    if (first) {
        lu_jit_invalidate(LU_DEP_CALLEES, (uintptr_t)t, 0);
    }
}

/* The votes for the first class armed at a site: the steps a loop that
   meets it all along makes before it runs in line. */
#define LU_HOT_VOTES 256

void
lu_jit_class_vote(struct lu_opclasses *t, PyTypeObject *tp)
{
    /* Races lose votes or let a wrong class lead: an armed class is a
       speculation the generic path backs, and only the lock arms one. */
    PyTypeObject *hot = __atomic_load_n(&t->hot, __ATOMIC_RELAXED);
    uint32_t votes = __atomic_load_n(&t->votes, __ATOMIC_RELAXED);
    if (hot != tp) {
        if (votes > 0) {
            __atomic_store_n(&t->votes, votes - 1, __ATOMIC_RELAXED);
        }
        else {
            __atomic_store_n(&t->hot, tp, __ATOMIC_RELAXED);
            __atomic_store_n(&t->votes, 1, __ATOMIC_RELAXED);
        }
        return;
    }
    uint32_t need = __atomic_load_n(&t->need, __ATOMIC_RELAXED);
    if (votes + 1 < (need != 0 ? need : LU_HOT_VOTES)) {
        __atomic_store_n(&t->votes, votes + 1, __ATOMIC_RELAXED);
        return;
    }
    int armed = 0;
    PyMutex_Lock(&_PyRuntime.pic_lock);
    int known = 0;
    for (int i = 0; i < t->narmed; i++) {
        known |= t->armed[i] == tp;
    }
    if (t->hot == tp && !known && t->narmed < LU_OPCLASS_WAYS) {
        t->armed[t->narmed] = tp;
        __atomic_store_n(&t->narmed, t->narmed + 1, __ATOMIC_RELEASE);
        t->need = (t->need != 0 ? t->need : LU_HOT_VOTES) * 2;
        armed = 1;
    }
    t->hot = NULL;
    t->votes = 0;
    PyMutex_Unlock(&_PyRuntime.pic_lock);
    if (armed) {
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: step arms %s\n", tp->tp_name);
        }
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
            if (t->hot != NULL && dead((PyObject *)t->hot)) {
                t->hot = NULL;
                t->votes = 0;
            }
            int a = 0;
            for (int i = 0; i < t->narmed; i++) {
                if (!dead((PyObject *)t->armed[i])) {
                    t->armed[a++] = t->armed[i];
                }
            }
            t->narmed = a;
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
    pthread_mutex_lock(&dying.lock);
    struct lu_unit *units = jc->units;
    jc->units = NULL;
    for (struct lu_unit *u = units; u != NULL; u = u->next) {
        if (u->dying_pprev != NULL) {
            dying_unlink(u);
        }
    }
    pthread_mutex_unlock(&dying.lock);
    for (struct lu_unit *u = units, *next; u != NULL; u = next) {
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
    for (struct lu_prof *p = jc->profs, *next; p != NULL; p = next) {
        next = p->next;
        Py_XDECREF(p->type);
        free(p);
    }
    free(jc->nospec);
    free(jc->fails);
    free(jc->wait);
    free(jc->seen);
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
            "builds put off %.1f ms, ir instructions %lu\n", stats.phase_seconds[0] * 1e3, stats.phase_seconds[1] * 1e3,
            stats.phase_seconds[2] * 1e3, stats.phase_seconds[3] * 1e3, stats.phase_seconds[4] * 1e3, stats.ir);
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
    fprintf(stderr, "jit: units freed %lu, dead held %lu\n", dying.freed, dying.n);
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
    fprintf(stderr, "\n");
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
