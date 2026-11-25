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
#include "interp/instrument.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* nospec bits. */
#define NOSPEC 1
#define NOHOIST 2
#define CATCH 4                 /* at a kernel handler: what a call raises under it is taken in the unit */
#define RAISES 8                /* at an instruction: what it raises leaves the unit, which returns it */
#define NOBORROW 16             /* at an OSR entry: its entry values are counted (opt.c borrow_items) */
/* What a rebuild is worth. A unit is built again to give up a guard that
   keeps failing, to take in what its root learned since its build, or to
   take calls in line; each is made once what the unit loses meanwhile
   (exits at the guard, passes that call the kernel where a learned type
   would fold the call, calls made out of line) has cost what the new build
   will: code that runs on then pays at most twice what the best choice in
   hindsight would, and code that stops soon pays little. Both sides are
   estimated by the ledger, below. A guard that fails rarely in a large
   unit keeps its speculation. */
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
   (what the evaluator folds away costs it nothing). A unit that left calls
   out is built again with twice that once the calls it made out of line
   have cost what that larger build will (hot_exit). */
#define INLINE_BUDGET 1000
#define MAX_GROWN 4                 /* doublings of the inlining budget: at most 16,000 */
/* The calls per entry of a unit that show a site in a loop it runs. */
#define LOOP_CALLS 64

struct lu_jit_code {
    struct lu_jit_direct direct;        /* first: compiled callers read it (target.h) */
    int compiling;              /* a thread is compiling: others interpret */
    int disabled;               /* LUCIFER_JIT_ONLY does not list it */
    int level;                  /* the wait's doublings: retirements and failed compiles, less decay */
    struct lu_unit *units;      /* live ones and dead ones not yet freed */
    int ncode;
    uint8_t *nospec;            /* per code unit: NOSPEC, do not speculate; NOHOIST, keep its guards in place */
    uint64_t *fails;            /* per code unit: what its guards' exits lost (exit_cost) */
    uint32_t *wait;             /* per code unit: entries to leave to the interpreter */
    uint8_t *seen;              /* per code unit: WARMED */
    int noinline;               /* a side exit in an inlined copy keeps leaving: call it instead */
    struct lu_ev_scan *scan;    /* the evaluator's scan of its blocks (eval_flow.c), once made */
    unsigned long marked;       /* marks when a site of it (or noinline) was last marked */
    int call_pc;                /* where a call enters (call_entry), or -1 */
    struct lu_pic **pics;       /* per code unit: an attribute site's polymorphic cache */
    uint8_t *grown;             /* per code unit: doublings of the inlining budget of the unit entered there, which
                                   its own calls left out bought (hot_exit); another entry starts at the base */
    struct lu_callees **callees;        /* per code unit: a call site's callees */
    int *keyed, nkeyed;                 /* the sites whose tables hold globals (LU_CALLEE_CODE entries) */
    struct lu_prof *profs;              /* a root's: what it learned of values, by context (target.h) */
    struct lu_prof **held;              /* those that met a mortal GC object, which the collector reads */
    int nheld;
    unsigned long learned;              /* how often one of them changed */
    int built_ins;                      /* instructions its last build made: about what the next costs */
    PyMutex prof_lock;                  /* theirs, and hot's */
    PyCodeObject *code;                 /* LUCIFER_JIT_DEBUG: whose (a name only) */
    char *cause;                        /* LUCIFER_JIT_DEBUG: why its last unit died, for its next build */
    uint64_t print;                     /* LUCIFER_JIT_DEBUG: its last build's fingerprint */
    uint64_t *hot;                      /* a root's: call sites its units called out of line at often */
    int nhot;
    struct lu_jit_code *next, **pprev;  /* the codes with JIT state (below) */
};

/* The codes whose JIT state holds references, for the collector
   (lu_jit_traverse, lu_jit_collect): a code joins before its first unit,
   profile or code-keyed callee, and leaves first in its dealloc. The lock is
   a pthread mutex no holder detaches under, so a stopped world finds it
   free. */
static struct { pthread_mutex_t lock; struct lu_jit_code *head; } states = { .lock = PTHREAD_MUTEX_INITIALIZER };

static void
enroll(struct lu_jit_code *jc)
{
    if (__atomic_load_n(&jc->pprev, __ATOMIC_ACQUIRE) != NULL) {
        return;
    }
    pthread_mutex_lock(&states.lock);
    if (jc->pprev == NULL) {
        jc->next = states.head;
        if (states.head != NULL) {
            __atomic_store_n(&states.head->pprev, &jc->next, __ATOMIC_RELEASE);
        }
        states.head = jc;
        __atomic_store_n(&jc->pprev, &states.head, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&states.lock);
}


struct lu_prof {
    struct lu_prof *next;
    uint64_t key;
    struct lu_jit_code *jc;     /* the root's */
    PyCodeObject *code;         /* LUCIFER_JIT_DEBUG: the site it was made for (a key only) */
    int pc;
    PyObject *met[LU_OPCLASS_WAYS];     /* the types the value had, in the order met (references); of a value
                                           profile, the values themselves */
    int n;                      /* how many: release-stored after the type */
    uint8_t value_kind;         /* lu_prof_value_kind, or zero for types of operands */
    uint8_t poly;               /* it had more than those: nothing to rely on */
    uint8_t dropped;            /* not relied on: a guard on its one type failed as a site's does (kills_cost) */
    uint8_t held;               /* on its root's held list */
    uint64_t fails;             /* what that guard's exits lost (exit_cost) */
    unsigned long at;           /* its root's learned count when it last learned: a unit built before then is
                                   stale here */
    unsigned long unshared;     /* the LU_PROF_KINDS flags some type it met lacks */
    uint64_t pass;              /* what a pass of its instruction loses in a unit built without what it learned
                                   (lu_jit_prof_built) */
};

static struct {
    unsigned long compiles, failures, entries, returns, units_dead, direct, compiled, invalidated, waited, retired,
        generic;
    unsigned long resumes, resumes_compiled, raised;
    unsigned long left_stale, ended_in_c;   /* lu_jit_left: a stale unit's activations, frames C ended */
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

static void event(const char *fmt, ...);
static const char *qualname(PyCodeObject *code);

/* The ledger: what builds, exits and calls cost, in machine instructions
   of this binary, estimated from counts so that every decision is the same
   run to run (tools/jitcost measures them). A build of n IR instructions
   costs about BUILD_INS n + BUILD_INS2 n^2: each exit of a unit carries the
   frames of every call in line around it, so a larger unit costs more per
   instruction (within a factor of two per size from 16 to 8,000
   instructions, on 3,000 builds of seven programs and on tools/jitcost's).
   An exit costs DEOPT_INS, the deopt path and the entry back, and what the
   interpreter does in the unit's stead until then: the bytecodes the build
   evaluated after the exit's place in the code of each frame the exit
   rebuilds (the rest of the loop's pass or of the call; all of it for a
   guard at the unit's entry; the calls the interpreter makes enter their
   own units), at BYTECODE_INS each (the interpreter's, less compiled
   code's few).
   A call made out of line costs CALL_COST (tools/jitcost: the same small
   function called out of line and in line, its argument and result boxed
   for the call), and what the callee's unit loses per entry (out_cost); a
   pass through a place built without what it learned since, what a
   generic path of the place's kind loses (below), or for another
   instruction a call for each helper call the build made for it
   (lu_jit_prof_built). A pass through a generic path
   (a helper that stands where a build for the operands' classes would have
   had a guard and an in-line path: an iterator's step, an attribute's load
   or store, an operator, a call of an unknown callable) costs what the
   helper path takes beyond that in-line form, by kind (lu_jit_generic). */
#define BUILD_INS 6500
#define BUILD_INS2 5
#define DEOPT_INS 3800
#define BYTECODE_INS 100
#define CALL_COST 420
#define ITER_INS 1000
#define ATTR_INS 550
#define OP_INS 1900
#define ANYCALL_INS 500

/* The instructions a build of n IR instructions takes. */
static double
build_cost(int n)
{
    return (double)n * (BUILD_INS + BUILD_INS2 * (double)n);
}

/* What exit d loses. */
static uint64_t
exit_cost(const struct lu_deopt *d)
{
    return DEOPT_INS + BYTECODE_INS * (uint64_t)d->rest;
}

/* What unit u lost, for unit_losses: counted while its entries are, until
   it covers u's own build (enough), past which no decision needs more.
   Relaxed: a lost update under a race only delays a decision, and a
   saturated count is not written, so threads do not share its line. */
static void
lose(struct lu_unit *u, uint64_t cost)
{
    uint64_t lost = __atomic_load_n(&u->lost, __ATOMIC_RELAXED);
    unsigned long entries = __atomic_load_n(&u->entries, __ATOMIC_RELAXED);
    if (lost >= u->enough || entries >= ENTRIES_SEEN) {
        if (!__atomic_load_n(&u->settled, __ATOMIC_RELAXED) && !_PyRuntime.jit.stats) {
            __atomic_store_n(&u->settled, 1, __ATOMIC_RELAXED);     /* with the counts on, generic passes are counted */
        }
        return;
    }
    __atomic_store_n(&u->lost, lost + cost, __ATOMIC_RELAXED);
    if (lost + cost >= u->enough) {
        __atomic_store_n(&u->lost_entries, entries > 0 ? entries : 1, __ATOMIC_RELAXED);
    }
}

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
static const char reasons[EXIT_REASONS][8] = { "guard", "stress", "poll", "side", "error", "frame", "yield", "hot" };

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

int
lu_jit_direct_nargs(PyCodeObject *code)
{
    if (lu_jit_call_entry(code) <= 0 || !(code->co_flags & CO_OPTIMIZED) || code->co_kwonlyargcount != 0
        || (code->co_flags & (CO_VARARGS | CO_VARKEYWORDS | LU_CO_GENERATORS))) {
        return -1;
    }
    return code->co_argcount;
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
    jc->code = code;
    jc->nospec = calloc((size_t)jc->ncode + 1, 1);
    jc->fails = calloc((size_t)jc->ncode + 1, sizeof(uint64_t));
    jc->wait = calloc((size_t)jc->ncode + 1, sizeof(uint32_t));
    jc->seen = calloc((size_t)jc->ncode + 1, 1);
    jc->grown = calloc((size_t)jc->ncode + 1, 1);
    jc->call_pc = lu_jit_call_entry(code);
    jc->direct.nargs = lu_jit_direct_nargs(code);
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
    free(jc->grown);
    free(jc);
    return (struct lu_jit_code *)seen;
}

int
lu_jit_noborrow(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && pc >= 0 && pc < jc->ncode && (__atomic_load_n(&jc->nospec[pc], __ATOMIC_RELAXED) & NOBORROW);
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

int
lu_jit_raises_out(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && pc >= 0 && pc < jc->ncode && (__atomic_load_n(&jc->nospec[pc], __ATOMIC_RELAXED) & RAISES);
}

int
lu_jit_catches(PyCodeObject *code, int pc)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && pc >= 0 && pc < jc->ncode && (__atomic_load_n(&jc->nospec[pc], __ATOMIC_RELAXED) & CATCH);
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
lu_jit_prof(PyCodeObject *root, uint64_t key, PyCodeObject *code, int pc)
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
        p->code = code;
        p->pc = pc;
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
    PyTypeObject *t = p->n > 0 ? (PyTypeObject *)Py_NewRef(p->met[0]) : NULL;
    *poly = p->poly || p->n > 1 || p->dropped;
    PyMutex_Unlock(&p->jc->prof_lock);
    return t;
}

PyObject *
lu_jit_prof_value(struct lu_prof *p, enum lu_prof_value_kind kind, int *poly)
{
    PyMutex_Lock(&p->jc->prof_lock);
    __atomic_store_n(&p->value_kind, kind, __ATOMIC_RELAXED);
    PyObject *k = p->n > 0 ? Py_NewRef(p->met[0]) : NULL;
    *poly = p->poly || p->n > 1 || p->dropped;
    PyMutex_Unlock(&p->jc->prof_lock);
    return k;
}

/* A type profile whose one type was dropped knows nothing of what failed
   its guard: it learns the next type it meets, and its kind is then what
   both share. */
int
lu_jit_prof_learns(struct lu_prof *p)
{
    PyMutex_Lock(&p->jc->prof_lock);
    int learns = p->dropped && p->n == 1 && !p->poly && !p->value_kind;
    PyMutex_Unlock(&p->jc->prof_lock);
    return learns;
}

unsigned long
lu_jit_prof_kinds(struct lu_prof *p)
{
    PyMutex_Lock(&p->jc->prof_lock);
    unsigned long k = (p->n > 1 || p->poly || (p->n == 1 && !p->dropped)) && !p->value_kind
        ? LU_PROF_KINDS & ~p->unshared : 0;
    PyMutex_Unlock(&p->jc->prof_lock);
    return k;
}

int
lu_jit_prof_types(struct lu_prof *p, PyTypeObject **types, int *relied)
{
    PyMutex_Lock(&p->jc->prof_lock);
    int n = p->poly ? -1 : p->n;
    for (int i = 0; i < n; i++) {
        types[i] = (PyTypeObject *)Py_NewRef(p->met[i]);
    }
    *relied = n == 1 && !p->dropped;
    PyMutex_Unlock(&p->jc->prof_lock);
    return n;
}

unsigned long
lu_jit_prof_learned(PyCodeObject *root)
{
    return __atomic_load_n(&state_for(root)->learned, __ATOMIC_RELAXED);
}

/* A learned rebuild is made only once its root's places are quiet: what
   unit u paid towards it since a place last learned covers half its
   predicted cost. A rebuild made while the places are still learning is
   wasted with certainty when the next class arrives, and is made again;
   waiting costs at most half a build more than not waiting. A place past
   its polymorphic limit learns nothing more, and so does not count. */
static int
quiet(struct lu_unit *u, uint64_t paid, uint64_t cost)
{
    return __atomic_add_fetch(&u->quiet, paid, __ATOMIC_RELAXED) >= cost / 2;
}

/* A place or attribute cache of root jc's learned: its units' places are
   not quiet. */
static void
learning(struct lu_jit_code *jc)
{
    for (struct lu_unit *u = jc != NULL ? __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE) : NULL; u != NULL; u = u->next) {
        __atomic_store_n(&u->quiet, 0, __ATOMIC_RELAXED);
    }
}

/* A pass of unit u that lost `cost` for want of what its root learned
   since its build: 0 once such passes have cost what u's build did and
   the places are quiet, and u is to be built again. */
static int
stale_pass(struct lu_unit *u, uint64_t cost)
{
    int still = quiet(u, cost, u->enough) == 0;
    return __atomic_add_fetch(&u->unlearned, cost, __ATOMIC_RELAXED) < u->enough || still;
}

/* A pass of unit u through place p, built without what p learned since:
   the learned rebuild is predicted at u's own build (enough), never less:
   measured rebuilds are 0.95-1.9 times the unit they replace. */
int
lu_jit_stale_pass(struct lu_prof *p, struct lu_unit *u)
{
    uint64_t pass = __atomic_load_n(&p->pass, __ATOMIC_RELAXED);
    return stale_pass(u, pass != 0 ? pass : CALL_COST);
}

void
lu_jit_prof_built(struct lu_prof *p, int base, int calls)
{
    uint64_t pass;
    switch (base) {
    case FOR_ITER:
        pass = ITER_INS;
        break;
    case LOAD_ATTR:
    case STORE_ATTR:
        pass = ATTR_INS;
        break;
    case BINARY_OP:
    case COMPARE_OP:
        pass = OP_INS;
        break;
    case CALL:
    case CALL_KW:
        pass = ANYCALL_INS;
        break;
    default:
        pass = (uint64_t)CALL_COST * (uint64_t)(calls > 0 ? calls : 1);
    }
    __atomic_store_n(&p->pass, pass, __ATOMIC_RELAXED);
}

int
lu_jit_prof_note(struct lu_prof *p, PyObject *o, unsigned long learned, struct lu_unit *u)
{
    struct lu_jit_code *jc = p->jc;
    int kind = __atomic_load_n(&p->value_kind, __ATOMIC_RELAXED);
    PyObject *t = kind ? o : (PyObject *)Py_TYPE(o);
    int met = __atomic_load_n(&p->poly, __ATOMIC_RELAXED);
    for (int i = 0, n = __atomic_load_n(&p->n, __ATOMIC_ACQUIRE); i < n && !met; i++) {
        met = p->met[i] == t;
    }
    if (met) {
        /* Nothing new here. Once the place has learned anything since the
           unit was built, its passes are of code built without it, and the
           unit is built again when they have cost about what the build
           will: code that runs only a few times more is not built again
           for it. */
        return __atomic_load_n(&p->at, __ATOMIC_RELAXED) <= learned || lu_jit_stale_pass(p, u);
    }
    PyMutex_Lock(&jc->prof_lock);
    int known = p->poly;
    for (int i = 0; i < p->n && !known; i++) {
        known = p->met[i] == t;
    }
    if (!known && !kind) {
        p->unshared |= LU_PROF_KINDS & ~Py_TYPE(o)->tp_flags;
    }
    if (!known && p->n < LU_OPCLASS_WAYS
        && (!kind || (kind == LU_PROF_CLASS ? PyType_Check(o) : _Py_IsImmortal(o)))) {
        if (!p->held && lu_is_gc(t) && !_Py_IsImmortal(t)) {
            struct lu_prof **held = realloc(jc->held, sizeof(*held) * (size_t)(jc->nheld + 1));
            if (held != NULL) {
                held[jc->nheld++] = p;
                jc->held = held;
                p->held = 1;
                enroll(jc);
            }
        }
        p->met[p->n] = Py_NewRef(t);
        __atomic_store_n(&p->n, p->n + 1, __ATOMIC_RELEASE);
        __atomic_store_n(&p->at, ++jc->learned, __ATOMIC_RELAXED);
    }
    else if (!known) {
        __atomic_store_n(&p->poly, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&p->at, ++jc->learned, __ATOMIC_RELAXED);
    }
    PyMutex_Unlock(&jc->prof_lock);
    if (!known) {
        learning(jc);
    }
    if (!known && _PyRuntime.jit.events) {
        event("learn %s %s at %s@%d: %s%s", qualname(jc->code), jc->print ? "built" : "unbuilt", qualname(p->code), p->pc,
              kind ? Py_TYPE(o)->tp_name : ((PyTypeObject *)t)->tp_name, p->poly ? " (poly)" : "");
    }
    return 1;
}

static int kill(struct lu_jit_code *jc, struct lu_unit *u, const char *why, PyCodeObject *at, int pc, double paid,
                double cost);

/* What a purchase that kills every live unit of root costs: the builds it
   causes, each at least the unit it replaces (enough), so never below any
   one of them. With none live it shapes root's next build, about its last. */
static double
kills_cost(struct lu_jit_code *root)
{
    double sum = 0;
    for (struct lu_unit *u = __atomic_load_n(&root->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
        sum += __atomic_load_n(&u->dead, __ATOMIC_RELAXED) ? 0 : (double)u->enough;
    }
    return sum > 0 ? sum : build_cost(__atomic_load_n(&root->built_ins, __ATOMIC_RELAXED));
}

/* d's unit is built again, at once, with what its root jc learned since. */
static void
relearn(struct lu_jit_code *jc, struct lu_deopt *d)
{
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: recompile %s: learned since its build\n", PyUnicode_AsUTF8(d->unit->code->co_qualname));
    }
    const struct lu_deopt_frame *in = lu_deopt_frame(d, d->nframes - 1);
    kill(jc, d->unit, "learned-since-build", in->code, in->pc,
         (double)__atomic_load_n(&d->unit->unlearned, __ATOMIC_RELAXED),
         (double)d->unit->enough);
    __atomic_store_n(&jc->wait[d->unit->entry_pc], 0, __ATOMIC_RELAXED);
}

/* An exit a learned type's site took: the unit is built again with what
   the root knows now, at once, when its sites reported; when the type
   keeps being another, its one type is not relied on there (the site
   learns the others: lu_jit_prof_learns, lu_ev_kernel_arms) and every
   unit of the root is built again; when a kind its types shared keeps
   failing, the kind is not relied on either. */
static void
prof_exit(struct lu_deopt *d)
{
    struct lu_prof *p = d->prof;
    struct lu_jit_code *jc = p->jc;
    if (d->tier) {
        relearn(jc, d);
        return;
    }
    PyMutex_Lock(&jc->prof_lock);
    int now = 0;
    const char *what = "type";
    uint64_t cost = exit_cost(d);
    /* Once its type and kind are both given up, the place has nothing left to learn: no price. */
    int learns = (!p->poly && !p->dropped) || p->unshared != LU_PROF_KINDS;
    double paid = (double)(p->fails += cost), price = learns ? kills_cost(jc) : 0;
    if (quiet(d->unit, cost, (uint64_t)price) && learns && paid >= price) {
        if (!p->poly && !p->dropped) {
            p->dropped = now = 1;
        }
        else if (p->unshared != LU_PROF_KINDS) {
            p->unshared = LU_PROF_KINDS;
            now = 1;
            what = "kind";
        }
        p->fails = 0;
        jc->learned += now;
    }
    PyMutex_Unlock(&jc->prof_lock);
    if (now && _PyRuntime.jit.debug) {
        fprintf(stderr, "jit: recompile %s: learned %s fails\n", PyUnicode_AsUTF8(d->unit->code->co_qualname), what);
    }
    const struct lu_deopt_frame *in = lu_deopt_frame(d, d->nframes - 1);
    for (struct lu_unit *u = now ? __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE) : NULL; u != NULL; u = u->next) {
        kill(jc, u, what[0] == 't' ? "learned-type-fails" : "learned-kind-fails", in->code, in->pc, paid, price);
    }
}

int
lu_jit_noinline(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL && __atomic_load_n(&jc->noinline, __ATOMIC_RELAXED);
}

struct lu_ev_scan *
lu_jit_scan(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    return jc != NULL ? __atomic_load_n(&jc->scan, __ATOMIC_ACQUIRE) : NULL;
}

struct lu_ev_scan *
lu_jit_keep_scan(PyCodeObject *code, struct lu_ev_scan *scan)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc == NULL) {
        return NULL;
    }
    struct lu_ev_scan *kept = NULL;
    if (__atomic_compare_exchange_n(&jc->scan, &kept, scan, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return scan;
    }
    free(scan);
    return kept;
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

/* LUCIFER_JIT_DEBUG: a unit built or killed, at the wall-clock time (to
   line up with a harness's own timestamps). */
static void
event(const char *fmt, ...)
{
    struct timespec t, mono;
    clock_gettime(CLOCK_REALTIME, &t);
    static int clocks;
    if (!clocks) {
        /* Both clocks once, for profilers that stamp samples with the monotonic one. */
        clock_gettime(CLOCK_MONOTONIC, &mono);
        fprintf(stderr, "jit-ev-clocks %ld.%06ld %ld.%06ld\n", (long)t.tv_sec, t.tv_nsec / 1000, (long)mono.tv_sec,
                mono.tv_nsec / 1000);
        clocks = 1;
    }
    char line[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "jit-ev %ld.%06ld %s\n", (long)t.tv_sec, t.tv_nsec / 1000, line);
}

static const char *
qualname(PyCodeObject *code)
{
    return code != NULL ? PyUnicode_AsUTF8(code->co_qualname) : "-";
}

/* LUCIFER_JIT_DEBUG: counts by what happened where, printed at exit (exits
   by site, entries the interpreter kept). */
struct tally {
    struct tally *next;
    const char *kind;
    const void *k[3];
    int kpc[3];
    char *what;
    unsigned long n;
};
static struct {
    PyMutex lock;
    struct tally *head[1024];
} tallies;

static void
tally(const char *kind, const void *root, int rpc, PyCodeObject *code, int pc, const void *extra, int epc)
{
    uint64_t h = ((uint64_t)(uintptr_t)root * 31 + (uint64_t)rpc) * 0x9e3779b97f4a7c15u;
    h = (h ^ ((uint64_t)(uintptr_t)code + (uint64_t)pc * 7 + (uint64_t)(uintptr_t)kind)) * 0xff51afd7ed558ccdu;
    h = (h ^ ((uint64_t)(uintptr_t)extra + (uint64_t)epc)) * 0x9e3779b97f4a7c15u;
    struct tally **head = &tallies.head[h >> 54];
    PyMutex_Lock(&tallies.lock);
    struct tally *t = *head;
    while (t != NULL && (t->k[0] != root || t->kpc[0] != rpc || t->k[1] != code || t->kpc[1] != pc
                         || t->k[2] != extra || t->kpc[2] != epc || t->kind != kind)) {
        t = t->next;
    }
    if (t == NULL) {
        char what[768];
        snprintf(what, sizeof what, "%s %s@%d %s@%d %d", kind, root != NULL ? qualname((PyCodeObject *)root) : "-",
                 rpc, qualname(code), pc, epc);
        t = calloc(1, sizeof *t);
        *t = (struct tally){ *head, kind, { root, code, extra }, { rpc, pc, epc }, strdup(what), 0 };
        *head = t;
    }
    t->n++;
    PyMutex_Unlock(&tallies.lock);
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

/* Whether a dict change killed a unit since the last release (lu_jit_release). */
static int release_due;

/* The dead units not yet freed, the count of them, and of those freed;
   and what profiles forgot in a collection, released with its units. */
static struct {
    pthread_mutex_t lock;
    struct lu_unit *head;
    unsigned long n, freed;
    PyObject **forgot;
    int nforgot, capforgot;
} dying = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* A reference a collection took from a profile or a callee table, with the
   world stopped: released once it runs again (lu_jit_reclaim_free). */
static int
forgot_add(PyObject *o)
{
    pthread_mutex_lock(&dying.lock);
    if (dying.nforgot == dying.capforgot) {
        int cap = dying.capforgot ? 2 * dying.capforgot : 16;
        PyObject **v = realloc(dying.forgot, sizeof(PyObject *) * (size_t)cap);
        if (v == NULL) {
            pthread_mutex_unlock(&dying.lock);
            return 0;           /* out of memory: the holder keeps its reference */
        }
        dying.forgot = v;
        dying.capforgot = cap;
    }
    dying.forgot[dying.nforgot++] = o;
    pthread_mutex_unlock(&dying.lock);
    return 1;
}

/* A collection left the site's table k entries. */
static void
forget_callees(PyCodeObject *code, int pc, struct lu_callees *t, int k)
{
    t->n = k;
    if (lu_base_opcode(code, pc) != CALL_FUNCTION_EX) {
        lu_cache_store32(lu_code_start(code) + pc + 2, 0);     /* the next call relearns its callee */
    }
}

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
    for (int i = 0; i < u->nmine; i++) {
        Py_DECREF(u->mine[i]);
    }
    free(u->mine);
    free(u->outs);
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
    memset(u, 0xA5, sizeof *u);         /* a reader after the free (its stale word, its fn) finds garbage */
    free(u);
}

/* Freeing dead units.

   A unit is in use while an activation runs it or a thread reads its
   code's list of units. An activation is a run of the unit's code, which
   links it from its task (jit_act), innermost first, in its own machine
   frame: the entries link it after their gate, the epilogue unlinks it.
   Whatever reads a unit's code or descriptions does so inside one, or in
   the windows below: compiled code, its exits
   (lu_jit_deopt returns into the unit's epilogue), walkers that read a
   frame's published state (frame->jit_desc, which only an activation
   sets and which is cleared before it returns), and a generator's units,
   which leave at every yield, so a suspended generator is in none.

   A unit goes on the dying list when it dies. A collection, with the
   world stopped, marks every unit some task's activation is in, takes
   each dying unit none is in off its code's list and the dying list, and
   frees them once the world runs again. No thread can still reach one:

   - Every other task is stopped at a poll or detached, and nothing
     between taking a unit from the code's list (find), or its direct
     entry from the code's state, and the unit's link of the activation
     polls, detaches, runs Python or takes a lock that may park: the marks
     see every activation that can run the unit. A gate that declines
     leaves the unit inside that window. So does the epilogue, from its
     unlink to the return, or to the jump (not a call: the stack then holds
     no address in the unit) to lu_jit_left, whose `left` may stop the
     world and free it.
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

/* Marks (on) or unmarks the units some task's activation is in. */
static void
hold(PyInterpreterState *interp, int on)
{
    for (PyThreadState *ts = interp->tasks; ts != NULL; ts = ts->next) {
        for (struct lu_jit_act *a = ((struct lu_task *)ts)->jit_act; a != NULL; a = a->outer) {
            a->unit->held = on ? a->unit->held + 1 : 0;
        }
    }
}

/* What a collection does to the code's units, profiles and callee tables
   (jit.h): with keeping, those that cannot let go keep what they hold;
   then the others let go of the garbage. The keeping pass runs over every
   code first, so that no unit dies for an object another one then keeps
   alive. What cannot be let go for want of memory is kept the same way. */
static void
weaken(struct lu_jit_code *jc, int keeping, int (*unreachable)(PyObject *), void (*keep)(PyObject *))
{
    int compiling = __atomic_load_n(&jc->compiling, __ATOMIC_RELAXED);
    for (int j = 0; keeping == compiling && j < jc->nheld; j++) {
        struct lu_prof *p = jc->held[j];
        int k = 0;
        for (int i = 0; i < p->n; i++) {
            if (keeping || !unreachable(p->met[i]) || !forgot_add(p->met[i])) {
                keep(p->met[i]);
                p->met[k++] = p->met[i];
            }
        }
        __atomic_store_n(&p->n, k, __ATOMIC_RELEASE);
    }
    for (int j = 0; !keeping && j < jc->nkeyed; j++) {
        int pc = jc->keyed[j];
        struct lu_callees *t = jc->callees[pc];
        int k = 0;
        for (int i = 0; t != NULL && i < t->n; i++) {
            PyObject *g = t->e[i].kind & LU_CALLEE_CODE ? t->e[i].globals : NULL;
            if (g == NULL || !unreachable(g) || !forgot_add(g)) {
                if (g != NULL) {
                    keep(g);
                }
                t->e[k++] = t->e[i];
            }
        }
        if (t != NULL && k < t->n) {
            forget_callees(jc->code, pc, t, k);
        }
    }
    for (struct lu_unit *u = jc->units; u != NULL; u = u->next) {
        int keeps = compiling || u->held;
        if (keeping != keeps) {
            continue;
        }
        /* The objects the build made itself die with the unit: it lives
           while what it learned does, and then keeps them too. */
        int dies = 0;
        for (int i = 0; i < u->ngc && !dies; i++) {
            PyObject *o = u->consts[i];
            int made = 0;
            for (int k = 0; k < u->nmine && !made; k++) {
                made = u->mine[k] == o;
            }
            if (!made && unreachable(o)) {
                if (keeps) {
                    keep(o);
                }
                else {
                    kill(jc, u, "collected", u->code, u->entry_pc, 0, 0);
                    dies = 1;
                }
            }
        }
        for (int k = 0; k < u->nmine && !dies; k++) {
            keep(u->mine[k]);
        }
    }
}

struct lu_unit *
lu_jit_collect(PyInterpreterState *interp, int (*unreachable)(PyObject *), void (*keep)(PyObject *))
{
    if (unreachable == NULL && __atomic_load_n(&dying.head, __ATOMIC_RELAXED) == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    hold(interp, 1);
    if (unreachable != NULL) {
        pthread_mutex_lock(&states.lock);
        for (int keeping = 1; keeping >= 0; keeping--) {
            for (struct lu_jit_code *jc = states.head; jc != NULL; jc = jc->next) {
                weaken(jc, keeping, unreachable, keep);
            }
        }
        pthread_mutex_unlock(&states.lock);
    }
    pthread_mutex_lock(&dying.lock);
    struct lu_unit *freed = NULL;
    for (struct lu_unit *u = dying.head, *next; u != NULL; u = next) {
        next = u->dying_next;
        struct lu_jit_code *jc = state_of(u->code);
        __atomic_store_n(&u->holds, u->held, __ATOMIC_RELAXED);
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
    hold(interp, 0);
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    return freed;
}

struct lu_unit *
lu_jit_reclaim(PyInterpreterState *interp)
{
    return lu_jit_collect(interp, NULL, NULL);
}

void
lu_jit_reclaim_free(struct lu_unit *units)
{
    pthread_mutex_lock(&dying.lock);
    PyObject **forgot = dying.forgot;
    int n = dying.nforgot;
    dying.forgot = NULL;
    dying.nforgot = dying.capforgot = 0;
    pthread_mutex_unlock(&dying.lock);
    for (int i = 0; i < n; i++) {
        Py_DECREF(forgot[i]);
    }
    free(forgot);
    for (struct lu_unit *u = units, *next; u != NULL; u = next) {
        next = u->next;
        free_unit(u);
        count(&dying.freed, 1);
    }
}

/* A collection's reclaim, now, when a dict change killed a unit since the
   last (lucifer.h). */
void
lu_jit_release(void)
{
    PyThreadState *ts = PyThreadState_GetUnchecked();
    if (!__atomic_load_n(&release_due, __ATOMIC_RELAXED) || ts == NULL || lu_world_stopped(ts->interp)
        || !__atomic_exchange_n(&release_due, 0, __ATOMIC_ACQUIRE)) {
        return;
    }
    lu_stw_stop(ts->interp);
    struct lu_unit *dead = lu_jit_reclaim(ts->interp);
    lu_stw_start(ts->interp);
    lu_jit_reclaim_free(dead);
}

/* An activation of u ended. A stale unit goes once no activation is in
   it: the last of those the release counted stops the world again. */
static void
left(struct lu_unit *u)
{
    if (__atomic_load_n(&u->stale, __ATOMIC_RELAXED) && __atomic_sub_fetch(&u->holds, 1, __ATOMIC_ACQ_REL) == 0) {
        __atomic_store_n(&release_due, 1, __ATOMIC_RELAXED);
        lu_jit_release();
    }
}

uint64_t
lu_jit_site_key(PyCodeObject *code, int pc, PyCodeObject *outer, int outer_pc)
{
    uint64_t k = ((uint64_t)(uintptr_t)code + (uint64_t)pc) * 0x9e3779b97f4a7c15u;
    k = (k ^ ((uint64_t)(uintptr_t)outer + (uint64_t)outer_pc)) * 0xff51afd7ed558ccdu;
    return k | 1;               /* 0 is an empty entry */
}

int
lu_jit_hot_site(PyCodeObject *root, uint64_t key)
{
    struct lu_jit_code *jc = state_of(root);
    int hot = 0;
    if (jc != NULL && __atomic_load_n(&jc->nhot, __ATOMIC_RELAXED) > 0) {
        PyMutex_Lock(&jc->prof_lock);
        for (int i = 0; i < jc->nhot && !hot; i++) {
            hot = jc->hot[i] == key;
        }
        PyMutex_Unlock(&jc->prof_lock);
    }
    return hot;
}

/* The sites the build left calls out of line at, for the unit to count:
   each leaves first after as many calls as the build made instructions,
   then after as many again as it made before (hot_exit). */
static void
keep_outs(struct lu_unit *u, const struct lu_func *f)
{
    if (f->nouts == 0) {
        return;
    }
    u->nouts = f->nouts;
    u->outs = malloc(sizeof(struct lu_out) * (size_t)f->nouts);
    for (int i = 0; i < f->nouts; i++) {
        u->outs[i] = (struct lu_out){ f->outs[i], -(int64_t)f->nins, f->nins, 0, f->out_callees[i] };
    }
}

static struct lu_unit *
compile(struct lu_jit_code *jc, struct lu_frame *frame, int pc, uint64_t ticket)
{
    struct lu_func f = { 0 };
    struct lu_unit *u = calloc(1, sizeof(struct lu_unit));
    enroll(jc);
    u->code = frame->code;
    u->globals = frame->globals;
    u->builtins = frame->builtins;
    u->built = __atomic_load_n(&marks, __ATOMIC_ACQUIRE);
    u->version = __atomic_load_n(&frame->code->_co_instrumentation_version, __ATOMIC_ACQUIRE);
    f.code = frame->code;
    f.unit = u;
    f.entry_pc = (int)(frame->instr - lu_code_start(frame->code));
    /* What the evaluator may inline, in code units: a unit that stayed hot
       with calls left out is built again with twice as much. */
    /* The ceiling is master's (MAX_GROWN), kept until the owner decides whether growth may be unbounded. */
    f.budget = INLINE_BUDGET << jc->grown[f.entry_pc];
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
            if (_PyRuntime.jit.events) {
                event("putoff %s@%d ms %.3f", qualname(frame->code), pc, (now() - t0) * 1e3);
            }
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
    if (ok) {
        keep_outs(u, &f);
        u->enough = (uint64_t)build_cost(f.nins);
        u->nins = f.nins;
        __atomic_store_n(&jc->built_ins, f.nins, __ATOMIC_RELAXED);
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
    if (_PyRuntime.jit.events) {
        /* What tells an identical rebuild: the IR as built, before the optimizer. */
        uint64_t print = (uint64_t)f.nins;
        for (int i = 0; ok && i < f.nins; i++) {
            const struct lu_ins *in = &f.ins[i];
            uint64_t w[5] = { (uint64_t)in->op | (uint64_t)in->rep << 8 | (uint64_t)in->flags << 16,
                              (uint64_t)in->a | (uint64_t)in->b << 32, (uint64_t)in->c, (uint64_t)in->imm,
                              (uint64_t)in->imm2 };
            for (int k = 0; k < 5; k++) {
                print = (print ^ w[k]) * 0x100000001b3u;
            }
        }
        event("build %s@%d %s ins %d bytes %zu ms %.3f grown %d level %d%s: %s", qualname(frame->code), pc,
              ok ? "ok" : "failed", f.nins, u->code_bytes, dt * 1e3, jc->grown[f.entry_pc], jc->level,
              ok && print == jc->print ? " same" : "", jc->cause != NULL ? jc->cause : "first");
        jc->print = ok ? print : jc->print;
    }
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
        if (_PyRuntime.jit.events) {
            event("overtaken %s@%d", qualname(frame->code), pc);
        }
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
    for (int i = 0; i < u->nconsts; i++) {
        PyObject *o = u->consts[i];
        if (o != NULL && lu_is_gc(o) && !_Py_IsImmortal(o)) {
            u->consts[i] = u->consts[u->ngc];
            u->consts[u->ngc++] = o;
        }
    }
    u->mine = f.mine;
    u->nmine = f.nmine;
    f.consts = NULL;
    f.nconsts = 0;
    f.mine = NULL;
    f.nmine = 0;
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
   holds starts the code over. 1 for the one call that killed it. */
static int
kill(struct lu_jit_code *jc, struct lu_unit *u, const char *why, PyCodeObject *at, int pc, double paid, double cost)
{
    if (__atomic_exchange_n(&u->dead, 1, __ATOMIC_SEQ_CST)) {
        return 0;
    }
    if (_PyRuntime.jit.events) {
        char cause[512], ledger[64] = "";
        snprintf(cause, sizeof cause, "%s %s@%d", why, qualname(at), pc);
        if (cost > 0) {
            snprintf(ledger, sizeof ledger, " paid %.0f cost %.0f", paid, cost);
        }
        event("kill %s@%d entries %lu leaves %lu level %d ins %d%s: %s", qualname(u->code), u->entry_pc, u->entries,
              u->leaves, jc->level, u->nins, ledger, cause);
        free(jc->cause);
        jc->cause = strdup(cause);
    }
    void *entry = u->direct;
    if (entry != NULL) {
        /* Callers stop entering it; one that read the entry before finds it
           dead. One not published yet is taken back by its compile. */
        __atomic_compare_exchange_n(&jc->direct.entry, &entry, NULL, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
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
    return 1;
}

/* A writer took back what the unit relies on (runtime/jit/deps.c), or a
   guard moved out of its loop failed: it dies, and calls stop entering it
   directly. stale: a dict changed, so it may hold a value the dict lets
   go, and the next release frees it (lu_jit_release). */
void
lu_jit_unit_kill(struct lu_unit *u, int stale, const char *why, PyCodeObject *at, int pc)
{
    if (stale && !__atomic_exchange_n(&u->stale, 1, __ATOMIC_RELAXED)) {
        __atomic_store_n(&release_due, 1, __ATOMIC_RELEASE);
    }
    if (!__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
        count(&stats.invalidated, 1);
    }
    kill(state_of(u->code), u, why, at, pc, 0, 0);
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
    kill(jc, u, "retired", NULL, jc->level, 0, 0);
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

/* The live unit at pc built for frame's globals and builtins. */
static struct lu_unit *
find(struct lu_jit_code *jc, int pc, struct lu_frame *frame)
{
    for (struct lu_unit *u = __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
        if (u->entry_pc == pc && u->globals == frame->globals && u->builtins == frame->builtins
            && !__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
            return u;
        }
    }
    return NULL;
}

/* Whether frame's function may have units of its own. The code's warmth
   (its call and back-edge counts, its waits) is the first function's to
   have units; a function of the same code with other globals or builtins
   (another namespace that runs it) compiles once it is hot itself, by its
   own count of entries against the code's threshold, so one that runs
   once stays in the interpreter. One that had a unit (on the list, live
   or dead) was hot: the code's waits apply to it as to the first. */
static int
own_hot(struct lu_jit_code *jc, struct lu_frame *frame)
{
    int other = 0, own = 0;
    for (struct lu_unit *u = __atomic_load_n(&jc->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
        int mine = u->globals == frame->globals && u->builtins == frame->builtins;
        own |= mine;
        other |= !mine && !__atomic_load_n(&u->dead, __ATOMIC_RELAXED);
    }
    if (own || !other || !PyFunction_Check(frame->func)) {
        return 1;
    }
    PyFunctionObject *f = (PyFunctionObject *)frame->func;
    return __atomic_add_fetch(&f->lu_jit_calls, 1, __ATOMIC_RELAXED) >= lu_jit_threshold(frame->code);
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
   unit, after those that hold for the unit's (its gate holds them while
   it lives: the version moves whenever they change). */
static struct lu_unit *
compile_at(PyThreadState *ts, struct lu_frame *frame, int pc)
{
    PyCodeObject *code = frame->code;
    _PyFrameEvalFunction hook = ts->interp->eval_frame;
    if ((hook != NULL && hook != lu_perf_eval) || lu_jit_stack_low()) {
        return NULL;
    }
    if (!ts->interp->kernel_booted || !(code->co_flags & CO_OPTIMIZED)) {
        if (_PyRuntime.jit.events) {
            tally(ts->interp->kernel_booted ? "refused-unoptimized" : "refused-boot", code, pc, NULL, 0, NULL, 0);
        }
        return NULL;
    }
    struct lu_jit_code *jc = state_for(code);
    const char *only = _PyRuntime.jit.only;
    if (only != NULL && !listed(only, PyUnicode_AsUTF8(code->co_qualname))) {
        __atomic_store_n(&jc->disabled, 1, __ATOMIC_RELAXED);      /* asked once */
        return NULL;
    }
    if (!own_hot(jc, frame)) {
        return NULL;
    }
    int idle = 0;
    if (__atomic_load_n(&jc->disabled, __ATOMIC_RELAXED) || jc->wait[pc] != 0
        || !__atomic_compare_exchange_n(&jc->compiling, &idle, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return NULL;
    }
    struct lu_unit *u = find(jc, pc, frame);
    if (u == NULL) {
        uint64_t ticket = lu_jit_deps_begin();     /* before anything the build reads */
        u = compile(jc, frame, pc, ticket);
        lu_jit_deps_end();
        if (u != NULL) {
            u->next = jc->units;
            __atomic_store_n(&jc->units, u, __ATOMIC_RELEASE);
            if (u->direct != NULL) {
                /* A writer that killed the unit since its dependencies went
                   in found no entry of it to take back (kill): this does,
                   before a collection may free it. Both sides store, then
                   read the other's word, in one total order. */
                void *entry = u->direct;
                __atomic_store_n(&jc->direct.entry, entry, __ATOMIC_SEQ_CST);
                if (__atomic_load_n(&u->dead, __ATOMIC_SEQ_CST)) {
                    __atomic_compare_exchange_n(&jc->direct.entry, &entry, NULL, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
                }
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

/* The code was instrumented for tools other than those the unit was built
   under (the poll word's version): the unit runs what it took in line as it
   was then, and its gate never passes. It dies; the next build is current. */
static int
tools_moved(struct lu_jit_code *jc, struct lu_unit *u, uintptr_t version)
{
    if (u->version == version) {
        return 0;
    }
    kill(jc, u, "tools", u->code, u->entry_pc, 0, 0);
    return 1;
}

static uintptr_t
poll_word(PyThreadState *ts)
{
    return __atomic_load_n(&((struct lu_task *)ts)->carrier->poll, __ATOMIC_RELAXED);
}

/* The unit to enter frame by at pc: the live one built for its globals
   and builtins, else (compile) one built now. None while the frame's
   locals are a mapping or a tool's callback runs on the thread, nor (an
   interpreter's entry) while the code's entry waits: tested before the
   find, which costs more on the entries that wait, though a unit another
   function of the code has live then waits too. */
static struct lu_unit *
unit_for(PyThreadState *ts, struct lu_frame *frame, struct lu_jit_code *jc, int pc, int compile)
{
    if (frame->locals != NULL || ts->tracing || lu_code_watched(frame->code)) {
        return NULL;
    }
    if (compile && jc != NULL) {
        uint32_t wait = __atomic_load_n(&jc->wait[pc], __ATOMIC_RELAXED);
        if (wait != 0 || __atomic_load_n(&jc->disabled, __ATOMIC_RELAXED)) {
            if (wait != 0) {
                __atomic_store_n(&jc->wait[pc], wait - 1, __ATOMIC_RELAXED);
                count(&stats.waited, 1);
                if (_PyRuntime.jit.events) {
                    tally(jc->print != 0 ? "waited-after-unit" : "waited-cold", frame->code, pc, NULL, 0, NULL, 0);
                }
            }
            return NULL;
        }
    }
    struct lu_unit *u = jc != NULL ? find(jc, pc, frame) : NULL;
    return u != NULL || !compile ? u : compile_at(ts, frame, pc);
}

/* Runs u on frame. Nothing between the find and the unit's link of its
   activation polls ("Freeing dead units"). The unit's gate declines
   (LU_JIT_NONE) for an event pending, a low C stack, death, or tools other
   than those it was built under: then it dies, still in that window, as
   it linked nothing. */
/* The task as a frame's push left it, for checking a unit's frame end:
   current frame, depth and datastack top. */
struct as_before {
    struct _PyInterpreterFrame *current;
    int remaining, kernel_room;
    PyObject **top;
};

static struct as_before
as_before(PyThreadState *ts, struct _PyInterpreterFrame *current, PyCodeObject *code, PyObject **top)
{
    struct as_before b = { current, ts->py_recursion_remaining, ((struct lu_task *)ts)->kernel_room, top };
    if (code != NULL) {
        b.remaining += !lu_frame_is_kernel(code);
        b.kernel_room += lu_frame_is_kernel(code) ? code->co_framesize : 0;
    }
    return b;
}

/* A unit returned or raised: its frame ended as the protocol says. */
static void
check_ended(PyThreadState *ts, int r, struct as_before b)
{
    if (r != LU_JIT_RETURN && r != LU_JIT_RAISED) {
        return;
    }
    if (ts->current_frame != b.current || ts->py_recursion_remaining != b.remaining
        || ((struct lu_task *)ts)->kernel_room != b.kernel_room || (b.top != NULL && ts->datastack_top != b.top)) {
        Py_FatalError("jit: a unit's frame did not end as before its push");
    }
}

static int
enter(PyThreadState *ts, struct lu_unit *u, struct lu_frame *frame, PyObject **result)
{
    int first = (PyObject **)frame == &ts->datastack_chunk->data[0] || frame->owner == LU_FRAME_GENERATOR;
    struct as_before b = as_before(ts, (struct _PyInterpreterFrame *)frame->previous, frame->code,
                                   first ? NULL : (PyObject **)frame);
    int r = u->fn(ts, frame, result);
    if (r == LU_JIT_NONE) {
        tools_moved(state_of(frame->code), u, poll_word(ts) & ~LU_POLL_EVENTS_MASK);
    }
    check_ended(ts, r, b);
    return r;
}

enum lu_jit_result
lu_jit_enter(PyThreadState *ts, struct lu_frame *frame, PyObject **result)
{
    PyCodeObject *code = frame->code;
    int pc = (int)(frame->instr - lu_code_start(code));
    struct lu_unit *u = unit_for(ts, frame, state_of(code), pc, 1);
    if (u == NULL) {
        return LU_JIT_NONE;
    }
    int r = enter(ts, u, frame, result);
    if (_PyRuntime.jit.debug) {         /* after: nothing between the find and the entry allocates */
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
    if (r != LU_JIT_NONE) {
        count(&stats.entries, 1);
    }
    if (r == LU_JIT_RETURN) {
        count(&stats.returns, 1);
    }
    return (enum lu_jit_result)r;
}

/* MAKE_CELL and COPY_FREE_VARS as the interpreter runs them, up to the
   RESUME, instrumented or not (or a generator's RETURN_GENERATOR): 0, or -1 with an exception
   and frame->instr after the instruction that raised. */
static int
prologue(struct lu_frame *frame)
{
    PyCodeObject *code = frame->code;
    for (lu_codeunit *at = lu_code_start(code);
         at->op.code != RESUME && at->op.code != INSTRUMENTED_RESUME && at->op.code != RETURN_GENERATOR; at++) {
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
    /* A generator's frame, resumed, goes on in the unit entered after
       the RESUME it was saved at (a yield's, not a delegation's, which
       the interpreter's loop runs), if there is one: with no turn of the
       interpreter. One not started yet stands at the POP_TOP that drops
       the value sent, before RESUME 0: it goes on in its call's unit, the
       value dropped here. */
    int gen = frame->owner == LU_FRAME_GENERATOR;
    uint8_t ret = frame->ret;           /* read before the unit ends the frame */
    lu_codeunit *resume = frame->instr;
    int fresh = gen && resume->op.code == POP_TOP && resume[1].op.code == RESUME && resume[1].op.arg == 0;
    resume += fresh;
    if (gen && !(resume->op.code == RESUME && (fresh || resume->op.arg != 0)
                 && (resume->op.arg & RESUME_OPARG_LOCATION_MASK) < RESUME_AFTER_YIELD_FROM)) {
        return 0;
    }
    int at = gen ? (int)(resume - lu_code_start(code)) + 1 + lu_opcode_caches[RESUME] : jc != NULL ? jc->call_pc : 0;
    struct lu_unit *u = at > 0 ? unit_for(ts, frame, jc, at, 0) : NULL;
    if (u == NULL) {
        return 0;
    }
    if (fresh) {
        Py_DECREF(*--frame->stackpointer);
    }
    struct lu_frame entry = { .owner = LU_FRAME_C_ENTRY };
    entry.previous = (struct lu_frame *)ts->current_frame;
    frame->previous = &entry;
    ts->current_frame = (struct _PyInterpreterFrame *)frame;
    if (!gen && at > 1 && prologue(frame) < 0) {
        *result = lu_eval_continue(ts, 1);
        return 1;
    }
    if (fresh || (!gen && at > 1)) {
        u = find(jc, at, frame);        /* a finalizer or a collection since may have taken it */
    }
    frame->instr = lu_code_start(code) + at;
    frame->last = frame->instr - 1 - lu_opcode_caches[RESUME];
    int r = u != NULL ? enter(ts, u, frame, result) : LU_JIT_NONE;
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: call %s -> %d\n", PyUnicode_AsUTF8(code->co_qualname), r);
    }
    if (r == LU_JIT_NONE) {
        /* Declined, the frame made ready: the interpreter runs it from its RESUME. */
        frame->instr = frame->last;
        r = LU_JIT_DEOPT;
    }
    else {
        count(gen ? &stats.resumes_compiled : &stats.direct, 1);
    }
    if (r == LU_JIT_RETURN && gen) {
        *result = lu_gen_returned(ret, *result);
    }
    *result = lu_jit_finish(ts, frame, r, *result, &entry);
    return 1;
}

int
lu_jit_direct(PyThreadState *ts, PyFunctionObject *func, PyObject *const *args, Py_ssize_t nargs, uint64_t takes,
              PyObject **out)
{
    PyCodeObject *code = (PyCodeObject *)func->func_code;
    struct lu_jit_code *jc = state_of(code);
    int (*entry)(PyThreadState *, PyFunctionObject *, PyObject *const *, PyObject **, uint64_t) =
        jc != NULL && jc->direct.nargs == nargs ? __atomic_load_n(&jc->direct.entry, __ATOMIC_ACQUIRE) : NULL;
    /* An exception pending, which lu_eval would set aside, or a tool's
       callback running: the interpreter's call. */
    if (entry == NULL || ts->current_exception != NULL || ts->tracing) {
        return 0;
    }
    takes &= nargs < 64 ? ((uint64_t)1 << nargs) - 1 : ~(uint64_t)0;
    PyObject **top = ts->datastack_top;
    struct as_before b = as_before(ts, ts->current_frame, NULL,
                                   top != NULL && top != &ts->datastack_chunk->data[0] ? top : NULL);
    PyObject *result;
    int r = entry(ts, func, args, &result, takes);
    if (r == 0) {
        return 0;
    }
    check_ended(ts, r, b);
    /* DEOPT and ERROR name the unit's frame in the result word. */
    result = lu_jit_finish(ts, (struct lu_frame *)result, r, result, NULL);
    /* The parameters released what the arguments gave them: they have it again. */
    for (; takes != 0; takes &= takes - 1) {
        Py_INCREF(args[__builtin_ctzll(takes)]);
    }
    *out = result;
    return 1;
}

int
lu_jit_left(struct lu_unit *u, int r, PyThreadState *ts, struct lu_frame *f)
{
    count(&stats.left_stale, __atomic_load_n(&u->stale, __ATOMIC_RELAXED) != 0);
    left(u);                    /* first: ending the frame may free the unit with its code */
    count(&stats.ended_in_c, f != NULL);
    if (f != NULL && f->owner == LU_FRAME_GENERATOR) {
        lu_gen_finish(ts, f);
    }
    else if (f != NULL) {
        lu_frame_end(ts, f);
    }
    return r;
}

int
lu_jit_prologue(struct lu_frame *frame)
{
    return prologue(frame);
}

unsigned long *
lu_jit_direct_count(void)
{
    return _PyRuntime.jit.stats ? &stats.compiled : NULL;
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

/* The handler (a code unit of frame k's code) that takes an error exit's
   exception in the unit's build when told to (eval_exc.c): frame k's, when
   every frame from the innermost to k is the kernel's, or k is the root,
   whose traceback entry the unit makes, and none a generator's; else -1. */
static int
kernel_handler(struct lu_deopt *d, int k)
{
    for (int i = k; i < d->nframes; i++) {
        PyCodeObject *co = lu_deopt_frame(d, i)->code;
        if ((!(co->co_flags & LU_CO_KERNEL) && !(i == 0 && k == 0)) || (co->co_flags & LU_CO_GENERATORS)) {
            return -1;
        }
    }
    const struct lu_deopt_frame *fr = lu_deopt_frame(d, k);
    lu_codeunit *start = lu_code_start(fr->code);
    int at = k == d->nframes - 1 ? fr->pc : (int)(lu_instr_before(fr->code, start + fr->pc) - start);
    int level, handler, lasti;
    return lu_exception_handler(fr->code, at, &level, &handler, &lasti) && !lasti ? handler : -1;
}

/* Whether every frame of an exit is kernel code, the root's too, and none
   a generator's: none makes a traceback entry. */
static int
all_kernel(struct lu_deopt *d)
{
    for (int i = 0; i < d->nframes; i++) {
        PyCodeObject *co = lu_deopt_frame(d, i)->code;
        if (!(co->co_flags & LU_CO_KERNEL) || (co->co_flags & LU_CO_GENERATORS)) {
            return 0;
        }
    }
    return 1;
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
/* Calls made at site i of the unit's out-of-line calls. Read under
   another root's lock while the owner re-arms them, so a torn read is
   possible: never below 0. */
static int64_t
out_calls(const struct lu_out *o)
{
    int64_t n = o->before + o->armed + __atomic_load_n(&o->left, __ATOMIC_RELAXED);
    return n > 0 ? n : 0;
}

/* What a unit has lost per entry: what its exits and generic paths lost
   (lost, over the entries that made it: past `enough` it stops, and
   lost_entries says after how many) and its calls made out of line. Read
   only when a caller decides: the loss is valued for that decision, not
   added to the caller's ledger. Past ENTRIES_SEEN, which the count stops
   at, the value first made then stands. */
static double
unit_losses(struct lu_unit *u)
{
    unsigned long entries = __atomic_load_n(&u->entries, __ATOMIC_RELAXED);
    uint64_t kept = __atomic_load_n(&u->per_entry, __ATOMIC_RELAXED);
    if (entries == 0 || (entries >= ENTRIES_SEEN && kept != 0)) {
        return (double)kept;
    }
    unsigned long upto = __atomic_load_n(&u->lost_entries, __ATOMIC_RELAXED);
    double lost = (double)__atomic_load_n(&u->lost, __ATOMIC_RELAXED);
    double per = lost / (double)(upto != 0 ? upto : entries);
    for (int i = 0; i < u->nouts; i++) {
        per += CALL_COST * (double)out_calls(&u->outs[i]) / (double)entries;
    }
    /* out_calls is never below 0, so no negative value reaches the cast. A
       torn read of a site while u's root re-arms it can still count up to
       about 2-3 times its calls; when that is the first nonzero value made
       once entries reach ENTRIES_SEEN, it stands in per_entry. */
    __atomic_store_n(&u->per_entry, (uint64_t)per, __ATOMIC_RELAXED);
    return per;
}

/* What a call made out of line at o loses: its own cost, and what the
   callee's unit then loses per entry for running uncustomized (its exits,
   generic paths, its own calls out of line). */
static double
out_cost(const struct lu_out *o)
{
    struct lu_jit_code *cc = state_of(o->callee);
    double callee = 0;
    for (struct lu_unit *v = cc != NULL ? __atomic_load_n(&cc->units, __ATOMIC_ACQUIRE) : NULL; v != NULL; v = v->next) {
        if (!__atomic_load_n(&v->dead, __ATOMIC_RELAXED) && v->entry_pc == cc->call_pc) {
            callee = unit_losses(v);
            break;
        }
    }
    return CALL_COST + callee;
}

/* A site the unit left out of line for want of budget was called as often
   as it was armed for, and the unit left there: a look at its sites, each
   valued at its calls times out_cost. When one site's calls have cost what
   the unit's next build will, the sites that cost a quarter of that, and
   are called LOOP_CALLS times per entry of the unit (calls in a loop it
   runs), are hot for that build, and it dies. One made a few times per
   entry is a caller's to make: taken in line, its code is copied into each
   unit that calls it, as a recursive descent's would be into every other.
   Else, when its calls out of line, all sites together, have cost what a
   build with twice the budget will, the unit is built again with it. Else
   each site that left is armed for as many calls again as it made: looks
   are few next to the calls. A site is taken in line by how often it is
   called, not by where the build came to it. */
static void
hot_exit(struct lu_deopt *d)
{
    struct lu_unit *u = d->unit;
    if (__atomic_load_n(&u->dead, __ATOMIC_RELAXED)) {
        return;
    }
    /* A unit entered where a generator resumes after a yield runs one turn
       of the generator's loop per entry: a call made on most turns is in it. */
    lu_codeunit *resume = lu_code_start(u->code) + u->entry_pc - 1 - lu_opcode_caches[RESUME];
    int turns = u->entry_pc > 1 + lu_opcode_caches[RESUME] && resume->op.code == RESUME
        && (resume->op.arg & RESUME_OPARG_LOCATION_MASK) == RESUME_AFTER_YIELD;
    unsigned long entries = __atomic_load_n(&u->entries, __ATOMIC_RELAXED);
    unsigned long loop = turns ? entries / 2 : LOOP_CALLS * entries;
    struct lu_jit_code *jc = state_of(u->code);
    double worth = (double)u->enough, grow = build_cost(2 * u->nins);     /* the builds they cause: u's */
    PyMutex_Lock(&jc->prof_lock);
    double most = 0, total = 0, *cost = malloc(sizeof(double) * (size_t)u->nouts);
    for (int i = 0; i < u->nouts; i++) {
        cost[i] = (double)out_calls(&u->outs[i]) * out_cost(&u->outs[i]);
        total += cost[i];
        most = cost[i] > most ? cost[i] : most;
    }
    int was = jc->nhot;
    for (int i = 0; i < u->nouts && most >= worth; i++) {
        int64_t calls = out_calls(&u->outs[i]);
        int known = 0;
        for (int k = 0; k < jc->nhot && !known; k++) {
            known = jc->hot[k] == u->outs[i].key;
        }
        if (!known && cost[i] >= worth / 4 && (unsigned long)calls >= loop) {
            jc->hot = realloc(jc->hot, sizeof(uint64_t) * (size_t)(jc->nhot + 1));
            jc->hot[jc->nhot] = u->outs[i].key;
            __atomic_store_n(&jc->nhot, jc->nhot + 1, __ATOMIC_RELAXED);
        }
    }
    const char *why = jc->nhot > was ? "hot-site"
        : u->refused && jc->grown[u->entry_pc] < MAX_GROWN && total >= grow ? "grown" : NULL;
    free(cost);
    for (int i = 0; why == NULL && i < u->nouts; i++) {
        struct lu_out *o = &u->outs[i];
        if (__atomic_load_n(&o->left, __ATOMIC_RELAXED) >= 0) {
            o->before = out_calls(o);
            o->armed = o->before;
            __atomic_store_n(&o->left, -o->armed, __ATOMIC_RELAXED);
        }
    }
    PyMutex_Unlock(&jc->prof_lock);
    if (why == NULL) {
        return;
    }
    const struct lu_deopt_frame *in = lu_deopt_frame(d, d->nframes - 1);
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: %s@%d: %s at %s@%d, %d hot now\n", PyUnicode_AsUTF8(u->code->co_qualname),
                u->entry_pc, why, PyUnicode_AsUTF8(in->code->co_qualname), in->pc, jc->nhot);
    }
    /* Only the exit that kills the unit grows its entry, and never past MAX_GROWN. */
    uint8_t grown = __atomic_load_n(&jc->grown[u->entry_pc], __ATOMIC_RELAXED);
    if (kill(jc, u, why, in->code, in->pc, why[0] == 'g' ? total : most, why[0] == 'g' ? grow : worth)
        && why[0] == 'g' && grown < MAX_GROWN) {
        __atomic_store_n(&jc->grown[u->entry_pc], grown + 1, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&jc->wait[u->entry_pc], 0, __ATOMIC_RELAXED);    /* not for its faults: built again at once */
}

void
lu_jit_generic(struct lu_unit *u, enum lu_generic kind)
{
    static const uint16_t costs[LU_GENERIC_KINDS] = { ITER_INS, ATTR_INS, OP_INS, ANYCALL_INS };
    count(&stats.generic, 1);
    lose(u, costs[kind]);
}

/* The site a guard's exit counts against: the instruction it left at, or
   for a guard that speculates for a loop header, the header; NULL when it
   is outside its code. */
static struct lu_jit_code *
guard_site(struct lu_deopt *d, int *pc)
{
    const struct lu_deopt_frame *in = lu_deopt_frame(d, d->nframes - 1);
    struct lu_jit_code *jc = state_for(d->blame_code != NULL ? d->blame_code : in->code);
    *pc = d->blame_code != NULL ? d->blame_pc : in->pc;
    return *pc >= 0 && *pc < jc->ncode ? jc : NULL;
}

/* Whether guard exit d's site (pc of jc) was marked after d's unit was built. */
static int
marked_since(struct lu_jit_code *jc, int pc, struct lu_deopt *d)
{
    return (__atomic_load_n(&jc->nospec[pc], __ATOMIC_RELAXED) & (d->borrow ? NOBORROW : NOSPEC))
        && d->unit->built < __atomic_load_n(&jc->marked, __ATOMIC_RELAXED);
}

void
lu_jit_note_exit(struct lu_deopt *d)
{
    count(&stats.exits[d->reason], 1);
    if (d->reason == EXIT_YIELD) {
        return;                 /* the end of a generator unit's run, no leave */
    }
    if (d->reason == EXIT_HOT) {
        hot_exit(d);
        return;
    }
    if (d->reason != EXIT_POLL) {
        lose(d->unit, exit_cost(d));    /* a poll's exit is the runtime's, when it asks: no build removes it */
    }
    count(&stats.taken[d->opcode & 0xff], 1);
    if (_PyRuntime.jit.events) {
        static const char kinds[EXIT_REASONS][12] = { "exit-guard", "exit-stress", "exit-poll", "exit-side",
                                                      "exit-error", "exit-frame", "exit-yield", "exit-hot" };
        const struct lu_deopt_frame *in = lu_deopt_frame(d, d->nframes - 1);
        tally(kinds[d->reason], d->unit->code, d->unit->entry_pc, in->code, in->pc, NULL, d->opcode);
    }
    if (_PyRuntime.jit.debug) {
        const struct lu_deopt_frame *in = lu_deopt_frame(d, d->nframes - 1);
        const struct lu_deopt_frame *root = lu_deopt_frame(d, 0);
        PyObject *exc = d->reason == EXIT_ERROR ? lu_err_occurred() : NULL;
        fprintf(stderr, "jit: exit %s %s@%d op %d depth %d root %s%s%s\n", reasons[d->reason],
                PyUnicode_AsUTF8(in->code->co_qualname), in->pc, d->opcode, d->nframes,
                PyUnicode_AsUTF8(root->code->co_qualname), exc != NULL ? " raising " : "",
                exc != NULL ? ((PyTypeObject *)exc)->tp_name : "");
    }
    if (d->prof != NULL) {
        prof_exit(d);
        return;
    }
    /* A guard's exit while its unit is stale: a pass of the unit went
       through a place that has met a class its build did not know (and had
       room to take it: a place past its polymorphic limit teaches nothing
       more), which charged the unit's stale ledger (unlearned). Such exits
       are mostly that pass's own doing (a guard after a place's generic
       path, on a value the path gives in another form), so they are losses
       of the stale build, paid towards the rebuild that takes what was
       learned, not towards a mark that makes the guard's instruction
       generic. The rebuild has what was learned: a guard of its that fails
       has failed on what its places know, and its exits buy the mark. Each
       diverted rebuild needs a place to have learned since the unit's
       build, so there are at most as many as its root's places learn. */
    /* The diverted exit pays its site's mark ledger too: if the rebuild
       with what was learned keeps the guard, which then fails, the mark is
       bought on what the guard already lost, not paid for again from
       nothing. The mark is still decided only by an exit of a unit that is
       not stale, below. An exit at a site marked since the unit's build is
       not diverted: the mark is bought, and the unit dies at once. */
    if (d->reason == EXIT_GUARD && __atomic_load_n(&d->unit->unlearned, __ATOMIC_RELAXED) != 0) {
        int pc;
        struct lu_jit_code *sc = guard_site(d, &pc);
        if (sc == NULL || !marked_since(sc, pc, d)) {
            if (sc != NULL) {
                __atomic_add_fetch(&sc->fails[pc], exit_cost(d), __ATOMIC_RELAXED);
            }
            if (!stale_pass(d->unit, exit_cost(d)) && !__atomic_load_n(&d->unit->dead, __ATOMIC_RELAXED)) {
                relearn(state_of(d->unit->code), d);
            }
            return;
        }
    }
    struct lu_unit *u = d->unit;
    int handled = d->reason == EXIT_ERROR ? handling_frame(d) : d->nframes - 1;
    /* An exception that leaves a kernel root through kernel frames only is
       the root's return once it keeps doing so: counted at the raising site. */
    int out = handled < 0 && d->reason == EXIT_ERROR && d->catchable && all_kernel(d);
    if (handled < 0 && !out) {
        return;
    }
    if (out) {
        handled = d->nframes - 1;
    }
    /* Only an exit a rebuild can take in the unit teaches it to. */
    int caught = d->reason == EXIT_ERROR && d->catchable && !out ? kernel_handler(d, handled) : -1;
    if (!out && (d->reason == EXIT_ERROR || (d->reason == EXIT_SIDE && d->nframes == 1))) {
        leave(d, u, 0);
        if (d->reason != EXIT_ERROR || (handled == 0 && caught < 0)) {
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
    uint64_t n = __atomic_add_fetch(&jc->fails[site_pc], exit_cost(d), __ATOMIC_RELAXED);
    PyCodeObject *code = lu_deopt_frame(d, 0)->code;
    struct lu_jit_code *root = state_for(code);
    /* A guard's site marked since its unit's build was bought (again, below): its kills carry no price. */
    int marked = d->reason == EXIT_GUARD && marked_since(jc, site_pc, d);
    double price = marked ? 0 : kills_cost(root);
    if (marked || n >= price) {
        int again;
        if (caught >= 0) {
            /* A kernel handler keeps taking what a call raises: the unit is
               built to take it there itself. */
            struct lu_jit_code *hc = state_for(inner->code);
            again = __atomic_fetch_or(&hc->nospec[caught], CATCH, __ATOMIC_RELAXED) & CATCH;
        }
        else if (out) {
            /* What it raises keeps leaving the unit: the unit returns it. */
            again = __atomic_fetch_or(&jc->nospec[site_pc], RAISES, __ATOMIC_RELAXED) & RAISES;
        }
        else if (d->reason != EXIT_GUARD) {
            /* The inlined copy keeps leaving here: it is called instead from
               now on (and if that call leaves its caller's copy, the caller next). */
            again = __atomic_exchange_n(&jc->noinline, 1, __ATOMIC_RELAXED);
        }
        else if (d->borrow) {
            /* An entry value keeps not being its holder's item: the loop counts it. */
            again = __atomic_fetch_or(&jc->nospec[site_pc], NOBORROW, __ATOMIC_RELAXED) & NOBORROW;
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
            if (!out && d->reason != EXIT_ERROR) {
                leave(d, u, 1);         /* an error exit counted its leave above */
            }
            return;
        }
        say_recompile(code, d->reason != EXIT_GUARD ? "inlined copy leaves" : "guard fails", site, site_pc);
        static const char *const whys[2][5] = {
            { "mark-catch", "mark-raises", "mark-noinline", "mark-noborrow", "mark-nospec" },
            { "old-unit-catch", "old-unit-raises", "old-unit-noinline", "old-unit-noborrow", "old-unit-nospec" } };
        const char *why = whys[again != 0][caught >= 0 ? 0 : out ? 1 : d->reason != EXIT_GUARD ? 2 : d->borrow ? 3 : 4];
        for (struct lu_unit *u = __atomic_load_n(&root->units, __ATOMIC_ACQUIRE); u != NULL; u = u->next) {
            /* An old unit's death buys nothing: the mark was bought. */
            kill(root, u, why, site, site_pc, again ? 0 : (double)n, again ? 0 : price);
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
lu_jit_pic_note(struct lu_pic *pic, PyObject *owner, PyObject *name, struct lu_unit *u)
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
    PyObject *prop = NULL;
    uint32_t tag = pic->store && op == STORE_ATTR && lu_type_assign_version(tp)
        ? __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) : 0;     /* before what it vouches for */
    if (tag != 0 && lu_attr_way_ours(tp, LU_ATTR_STORE) == LU_ATTR_GENERIC) {
        /* A store through a property: its setter, under the class's version,
           where the class stores with object's algorithm (its word, as the
           specializer asks of its stores); a type as owner (a metaclass
           property) sets attributes its own way and keeps the generic store. */
        PyObject *d = _PyType_Lookup(tp, name);
        PyObject *fset = d != NULL && Py_IS_TYPE(d, &PyProperty_Type) ? lu_property_fset(d) : NULL;
        if (fset != NULL && PyFunction_Check(fset)) {
            prop = d;
            op = LU_PIC_STORE_PROPERTY;
            lu_cache_store32(site + 1 + LA_VERSION, tag);      /* a change since takes the tag back */
        }
    }
    uint32_t wrapped = 0;
    int managed;
    PyObject *w = !pic->store && op == LOAD_ATTR ? lu_specialize_instance_method(tp, name, &wrapped, &managed) : NULL;
    if (w != NULL && wrapped != 0) {
        /* A staticmethod or classmethod of the class, named by its version
           alone (the wrapper is no deferred object a cache could hold). */
        op = LU_PIC_LOAD_WRAPPED;
        lu_cache_store32(site + 1 + LA_VERSION, wrapped);
    }
    Py_XDECREF(w);
    if (op != LU_PIC_STORE_PROPERTY && op != LU_PIC_LOAD_WRAPPED && op != LOAD_ATTR_INSTANCE_VALUE && op != LOAD_ATTR_METHOD_NO_DICT && op != LOAD_ATTR_METHOD_WITH_VALUES
        && op != LOAD_ATTR_NONDESCRIPTOR_NO_DICT && op != LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES
        && op != LOAD_ATTR_WITH_HINT && op != LOAD_ATTR_SLOT && op != LOAD_ATTR_PROPERTY
        && op != STORE_ATTR_INSTANCE_VALUE && op != STORE_ATTR_WITH_HINT && op != STORE_ATTR_SLOT) {
        return;
    }
    struct lu_pic_entry e = { lu_cache_load32(site + 1 + LA_VERSION), (uint16_t)op, lu_cache_load(site + 1 + LA_INDEX),
                              prop != NULL ? prop : pic->store || op == LU_PIC_LOAD_WRAPPED ? NULL
                              : lu_cache_load_ptr(site + 1 + LA_DESCR) };
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
        learning(state_of(u != NULL ? u->code : pic->code));
    }
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
        lu_jit_pic_note(pic, owner, name, NULL);
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
    /* What only a collection frees can be named, except a builtin function,
       which the compiler never speculates on (speculate_callee); any other
       function by what every function of its code shares; a builtin method
       bound to its self by its kernel function; any other callable not. */
    int named = !PyCFunction_Check(fn) && (_Py_IsImmortal(fn) || lu_has_deferred_refcount(fn));
    PyObject *bself = NULL;
    PyObject *kfn = !named && self == NULL && PyCFunction_Check(fn) ? lu_kernel_function_of(fn, &bself) : NULL;
    int cmeth = kfn != NULL && bself != NULL && !PyModule_Check(bself) && PyFunction_Check(kfn)
        && (_Py_IsImmortal(kfn) || lu_has_deferred_refcount(kfn));
    if (!named && !cmeth && !PyFunction_Check(fn)) {
        if (tag) {
            lu_cache_store32(cache + 1, tag);
        }
        return;
    }
    struct lu_callee want = cmeth
        ? (struct lu_callee){ kfn, (PyObject *)((PyCFunctionObject *)fn)->m_ml, LU_CALLEE_CMETH, -1 }
        : (struct lu_callee){ named ? fn : f->func_code, named ? NULL : f->func_globals,
                              (fn != callable || self != NULL ? LU_CALLEE_BOUND : 0) | (named ? 0 : LU_CALLEE_CODE),
                              !named && f->func_defaults != NULL ? (int)PyTuple_GET_SIZE(f->func_defaults) : -1 };
    struct lu_jit_code *jc = state_cold(code);
    struct lu_callees *t = lu_jit_callees(code, pc);
    int n = t != NULL ? __atomic_load_n(&t->n, __ATOMIC_ACQUIRE) : 0;
    if (tag == 0 && t != NULL && __atomic_load_n(&t->mega, __ATOMIC_ACQUIRE)) {
        return;                 /* nothing to learn, and no cache words to say so */
    }
    for (int i = 0; i < n; i++) {
        if (t->e[i].obj == want.obj && t->e[i].kind == want.kind && t->e[i].globals == want.globals) {
            if (tag) {
                lu_cache_store32(cache + 1, tag);
            }
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
            if (want.kind & LU_CALLEE_CODE) {
                Py_INCREF(want.globals);        /* weak to the collector (lu_jit_traverse) */
                int listed = 0;
                for (int i = 0; i < jc->nkeyed && !listed; i++) {
                    listed = jc->keyed[i] == pc;
                }
                int *keyed = listed ? jc->keyed : realloc(jc->keyed, sizeof(int) * (size_t)(jc->nkeyed + 1));
                if (keyed == NULL) {
                    PyMutex_Unlock(&_PyRuntime.pic_lock);
                    Py_DECREF(want.globals);
                    return;     /* out of memory: not learned */
                }
                if (!listed) {
                    keyed[jc->nkeyed] = pc;
                    jc->keyed = keyed;
                    jc->nkeyed++;
                }
                enroll(jc);
            }
            t->e[t->n] = want;
            __atomic_store_n(&t->n, t->n + 1, __ATOMIC_RELEASE);
        }
        else {
            __atomic_store_n(&t->mega, 1, __ATOMIC_RELEASE);
        }
        added = 1;
    }
    if (tag) {
        lu_cache_store32(cache + 1, t->mega ? LU_CALLEE_MEGA : tag);
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
            else if (t->e[i].kind & LU_CALLEE_CODE) {
                forgot_add(t->e[i].globals);     /* its code is dead: out of memory, the globals leak */
            }
        }
        if (t != NULL && k < t->n) {
            forget_callees(code, pc, t, k);
        }
    }
}

void
lu_jit_traverse(visitproc visit, void *arg)
{
    pthread_mutex_lock(&states.lock);
    for (struct lu_jit_code *jc = states.head; jc != NULL; jc = jc->next) {
        for (struct lu_unit *u = jc->units; u != NULL; u = u->next) {
            for (int i = 0; i < u->ngc; i++) {
                visit(u->consts[i], arg);
            }
            for (int i = 0; i < u->nmine; i++) {
                visit(u->mine[i], arg);
            }
        }
        for (int j = 0; j < jc->nheld; j++) {
            for (int i = 0; i < jc->held[j]->n; i++) {
                visit(jc->held[j]->met[i], arg);
            }
        }
        for (int j = 0; j < jc->nkeyed; j++) {
            struct lu_callees *t = jc->callees[jc->keyed[j]];
            for (int i = 0; i < t->n; i++) {
                if (t->e[i].kind & LU_CALLEE_CODE) {
                    visit(t->e[i].globals, arg);
                }
            }
        }
    }
    pthread_mutex_unlock(&states.lock);
}

/* Takes the code's units off its list and the dying list under the dying
   lock, so each is freed once, and frees them: their dependencies, then
   their constants. */
static void
free_units(struct lu_jit_code *jc)
{
    __atomic_store_n(&jc->direct.entry, NULL, __ATOMIC_SEQ_CST);
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
}

void
lu_jit_code_clear(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc != NULL) {
        free_units(jc);
    }
}

void
lu_jit_code_free(PyCodeObject *code)
{
    struct lu_jit_code *jc = state_of(code);
    if (jc == NULL) {
        return;
    }
    pthread_mutex_lock(&states.lock);
    if (jc->pprev != NULL) {
        *jc->pprev = jc->next;
        if (jc->next != NULL) {
            __atomic_store_n(&jc->next->pprev, jc->pprev, __ATOMIC_RELEASE);
        }
    }
    pthread_mutex_unlock(&states.lock);
    free_units(jc);
    for (int i = 0; jc->pics != NULL && i < jc->ncode; i++) {
        free(jc->pics[i]);
    }
    free(jc->pics);
    for (int i = 0; jc->callees != NULL && i < jc->ncode; i++) {
        for (int k = 0; jc->callees[i] != NULL && k < jc->callees[i]->n; k++) {
            if (jc->callees[i]->e[k].kind & LU_CALLEE_CODE) {
                Py_DECREF(jc->callees[i]->e[k].globals);
            }
        }
        free(jc->callees[i]);
    }
    free(jc->callees);
    free(jc->keyed);
    free(jc->held);
    free(jc->scan);
    free(jc->hot);
    for (struct lu_prof *p = jc->profs, *next; p != NULL; p = next) {
        next = p->next;
        for (int i = 0; i < p->n; i++) {
            Py_DECREF(p->met[i]);
        }
        free(p);
    }
    free(jc->nospec);
    free(jc->fails);
    free(jc->wait);
    free(jc->seen);
    free(jc->grown);
    free(jc->cause);
    free(jc);
    code->co_executors = NULL;
}

void
lu_jit_report(void)
{
    for (int i = 0; _PyRuntime.jit.events && i < 1024; i++) {
        for (struct tally *t = tallies.head[i]; t != NULL; t = t->next) {
            fprintf(stderr, "jit-tally %lu %s\n", t->n, t->what);
        }
    }
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
    fprintf(stderr, "jit: entries waited %lu, units retired %lu, generic passes %lu\n", stats.waited, stats.retired,
            stats.generic);
    fprintf(stderr, "jit: units freed %lu, dead held %lu, stale leaves %lu, frames ended in C %lu\n", dying.freed,
            dying.n, stats.left_stale, stats.ended_in_c);
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
