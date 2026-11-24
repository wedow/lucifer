/* The optimizing tier as the interpreter sees it (DESIGN 6.2).

   Counters at JUMP_BACKWARD (one per site, in its cache word) and at a
   function's RESUME (one per code object) ask for a compile once hot. A
   compiled unit starts at one bytecode boundary, the loop header the
   back-edge jumps to or the instruction after RESUME, with the frame
   exactly as the interpreter left it there, and runs until the function
   returns, an exception no handler of the unit takes is raised, or a
   guard fails. A generator's units start after its RESUMEs and leave at
   its yields, which the interpreter runs. The interpreter is the deopt
   landing: a unit hands back a frame (and any frames of inlined calls
   above it) at a bytecode boundary that has not executed. */
#ifndef LU_JIT_H
#define LU_JIT_H

#include "interp/frame.h"
#include "interp/specialize.h"

enum lu_jit_mode { LU_JIT_OFF, LU_JIT_ON, LU_JIT_ALWAYS };

/* What lu_jit_enter did. */
enum lu_jit_result {
    LU_JIT_NONE,        /* nothing ran: interpret on from the frame as saved */
    LU_JIT_RETURN,      /* the frame's function returned *result (a new reference); the
                           frame is still pushed, its stack empty */
    LU_JIT_DEOPT,       /* frames were handed back at an unexecuted boundary: continue
                           with ts->current_frame, which may be an inlined callee's */
    LU_JIT_ERROR,       /* ts->current_frame raised: unwind from its instr */
    LU_JIT_YIELD,       /* the frame, a generator's, yields *result (a new reference): it is saved at its
                           resume point with its values, as the interpreter's YIELD_VALUE saves it, and is
                           still to be suspended */
    LU_JIT_RAISED,      /* the frame's function raised (the exception is set) with no handler: the
                           frame is still pushed, its slots released, as the interpreter's unwinding
                           leaves it before it pops the frame */
};

/* Back-edges (at one site) or calls before a code is compiled
   (-X lucifer_jit=on): LU_JIT_THRESHOLD, and LU_JIT_THRESHOLD_PER_UNIT
   more per code unit past the first LU_JIT_THRESHOLD_FREE, since a compile
   costs about the size of the code it compiles and has to be paid back by
   the runs it saves; the base pays for a function of ordinary size. */
#define LU_JIT_THRESHOLD 1000
#define LU_JIT_THRESHOLD_PER_UNIT 4
#define LU_JIT_THRESHOLD_FREE 256

static inline unsigned
lu_jit_threshold(PyCodeObject *code)
{
    if (_PyRuntime.jit.mode == LU_JIT_ALWAYS) {
        return 1;
    }
    size_t size = (size_t)Py_SIZE(code);
    size_t n = LU_JIT_THRESHOLD + (size > LU_JIT_THRESHOLD_FREE ? LU_JIT_THRESHOLD_PER_UNIT * (size - LU_JIT_THRESHOLD_FREE) : 0);
    return n < UINT16_MAX ? (unsigned)n : UINT16_MAX;      /* what a cache word counts */
}

/* The backend's name (runtime/jit/target.h), or NULL where there is none. */
const char *lu_jit_backend(void);

static inline int
lu_jit_on(void)
{
    return _PyRuntime.jit.mode != LU_JIT_OFF;
}

/* Whether the C stack is below the room it keeps for an activation of the
   interpreter: compiled code, which nests on the C stack at every call it
   makes through C, is not entered there, and the interpreter, which nests
   nothing, runs the calls. */
static inline int
lu_jit_stack_low(void)
{
    char here;
    return &here < lu_current_task()->carrier->jit_stack_floor;
}

/* Whether the code has JIT state (it is warm): co_executors holds its
   call count, tagged with bit 0, until then. */
static inline int
lu_jit_warm(PyCodeObject *code)
{
    uintptr_t v = (uintptr_t)__atomic_load_n(&code->co_executors, __ATOMIC_RELAXED);
    return v != 0 && !(v & 1);
}

/* Counts the back-edge at `site` (a JUMP_BACKWARD; its cache word is the
   counter): whether to try lu_jit_enter at the jump's target. */
static inline int
lu_jit_backedge_hot(PyCodeObject *code, lu_codeunit *site)
{
    if (lu_jit_warm(code)) {
        return 1;               /* compiled, or tried: lu_jit_enter knows */
    }
    lu_codeunit *counter = site + 1;
    uint16_t n = __atomic_load_n(&counter->cache, __ATOMIC_RELAXED);
    if (n + 1u >= lu_jit_threshold(code)) {
        return 1;
    }
    __atomic_store_n(&counter->cache, (uint16_t)(n + 1), __ATOMIC_RELAXED);
    return 0;
}

/* Counts a call of `code` at its RESUME. Until the code has JIT state,
   co_executors holds the count, tagged with bit 0. */
static inline int
lu_jit_call_hot(PyCodeObject *code)
{
    uintptr_t v = (uintptr_t)__atomic_load_n(&code->co_executors, __ATOMIC_RELAXED);
    if (v != 0 && !(v & 1)) {
        return 1;
    }
    uintptr_t n = v >> 1;
    if (n + 1 >= lu_jit_threshold(code)) {
        return 1;
    }
    /* A lost count under a race only delays the compile. */
    __atomic_compare_exchange_n((uintptr_t *)&code->co_executors, &v, ((n + 1) << 1) | 1, 0,
                                __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    return 0;
}

/* Type feedback: the kinds of operands an instruction has met, OR-ed into
   a cache word the interpreter does not otherwise use (BINARY_OP's second,
   COMPARE_OP's, STORE_SUBSCR's and UNPACK_SEQUENCE's first): the left
   operand's kinds in the low byte, the right's in the high byte. The tier
   speculates on a site only when it has met nothing else. */
enum {
    LU_KIND_INT = 1,            /* an exact int that fits 64 bits (but INT64_MIN) */
    LU_KIND_FLOAT = 2,
    LU_KIND_NONE = 4,
    LU_KIND_LIST = 8,
    LU_KIND_TUPLE = 16,
    LU_KIND_STR = 32,
    LU_KIND_BYTES = 64,
    LU_KIND_OTHER = 128,
};

int64_t lu_jit_unbox_wide(PyObject *o);

static inline unsigned
lu_jit_kind(PyObject *o)
{
    PyTypeObject *t = Py_TYPE(o);
    return t == &PyLong_Type ? (_PyLong_IsCompact((PyLongObject *)o) || lu_jit_unbox_wide(o) != INT64_MIN
                                ? LU_KIND_INT : LU_KIND_OTHER)
        : t == &PyFloat_Type ? LU_KIND_FLOAT : o == Py_None ? LU_KIND_NONE
        : t == &PyList_Type ? LU_KIND_LIST : t == &PyTuple_Type ? LU_KIND_TUPLE
        : t == &PyUnicode_Type ? LU_KIND_STR : t == &PyBytes_Type ? LU_KIND_BYTES : LU_KIND_OTHER;
}

/* Returns the operands' kinds as the word holds them (0 with the tier off). */
static inline unsigned
lu_jit_observe(lu_codeunit *word, PyObject *left, PyObject *right)
{
    if (_PyRuntime.jit.mode == LU_JIT_OFF) {
        return 0;
    }
    unsigned kinds = lu_jit_kind(left) | lu_jit_kind(right) << 8;
    uint16_t seen = __atomic_load_n(&word->cache, __ATOMIC_RELAXED);
    if ((seen | kinds) != seen) {
        __atomic_store_n(&word->cache, (uint16_t)(seen | kinds), __ATOMIC_RELAXED);
    }
    return kinds;
}

/* CALL's first cache word: whether the site ran, all the tier reads of it
   (lu_ev_feedback); the kinds of a call's arguments matter only to a warm
   code's builtin calls (lu_jit_observe_classes), which take them as they go. */
static inline void
lu_jit_observe_ran(lu_codeunit *word)
{
    if (__atomic_load_n(&word->cache, __ATOMIC_RELAXED) == 0) {
        __atomic_store_n(&word->cache, LU_KIND_OTHER, __ATOMIC_RELAXED);
    }
}

/* FOR_ITER's cache word: the iterators it has met. */
enum { LU_ITER_RANGE = 1, LU_ITER_LIST = 2, LU_ITER_OTHER = 4, LU_ITER_TUPLE = 8 };

static inline void
lu_jit_observe_iter(lu_codeunit *word, PyTypeObject *t)
{
    if (_PyRuntime.jit.mode == LU_JIT_OFF) {
        return;
    }
    uint16_t seen = __atomic_load_n(&word->cache, __ATOMIC_RELAXED);
    uint16_t now = seen | (t == &PyRangeIter_Type ? LU_ITER_RANGE : t == &PyListIter_Type ? LU_ITER_LIST
                           : t == &PyTupleIter_Type ? LU_ITER_TUPLE : LU_ITER_OTHER);
    if (now != seen) {
        __atomic_store_n(&word->cache, now, __ATOMIC_RELAXED);
    }
}

/* POP_JUMP_IF_*'s cache word: how often the interpreter fell through (low
   byte) and jumped (high byte) of late; both halve when one is full, so
   the word keeps their proportion. A direction it took under one time in
   LU_BRANCH_RARE is not compiled: a guard leaves for it. */
#define LU_BRANCH_RARE 64

static inline void
lu_jit_observe_branch(lu_codeunit *word, int jumped)
{
    if (_PyRuntime.jit.mode == LU_JIT_OFF) {
        return;
    }
    unsigned w = __atomic_load_n(&word->cache, __ATOMIC_RELAXED) + (jumped ? 0x100 : 1);
    if ((w & 0xff) == 0xff || (w >> 8) == 0xff) {
        w = (w >> 1) & 0x7f7f;
    }
    __atomic_store_n(&word->cache, (uint16_t)w, __ATOMIC_RELAXED);
}

/* Whether the interpreter went the way `jumped` at the branch rarely or never,
   and went the other. */
static inline int
lu_jit_branch_rare(lu_codeunit *word, int jumped)
{
    unsigned w = __atomic_load_n(&word->cache, __ATOMIC_RELAXED);
    unsigned fell = w & 0xff, jump = w >> 8;
    return jumped ? jump * LU_BRANCH_RARE < fell : fell * LU_BRANCH_RARE < jump;
}

/* Runs compiled code for `frame` from frame->instr, compiling it first if
   it is due. The frame is saved (instr, stackpointer) and is the task's
   current frame. */
enum lu_jit_result lu_jit_enter(PyThreadState *ts, struct lu_frame *frame, PyObject **result);

/* Where a call enters compiled code: the instruction after the RESUME
   that starts the code, past a prologue of MAKE_CELL and COPY_FREE_VARS,
   or -1 when the code starts otherwise. */
int lu_jit_call_entry(PyCodeObject *code);

/* Runs `frame`, pushed and bound for a call from C (lu_eval) with a frame
   running below, through the compiled unit at its entry after RESUME, with
   no activation of the eval loop until a unit leaves. A C-entry marker
   below the frame stands for the caller's C stack, as under lu_eval:
   walkers skip it, and an activation runs the frames a unit leaves until
   the frame returns into it. 1 with *result the frame's value (NULL with
   an exception), or 0 with nothing done when there is no unit to enter. */
int lu_jit_run(PyThreadState *ts, struct lu_frame *frame, PyObject **result);

/* A call C makes (a helper compiled code calls, function_vectorcall) of
   func by exactly its positional parameters, args[0 .. nargs), through
   the direct entry of its code's unit at the call entry, as compiled code
   makes it (target.h struct lu_jit_direct): 1 with *result the value, or
   NULL with an exception; 0 with nothing done when the code has no such
   entry or the entry declines. The arguments in `takes` (bits by index)
   give their references to the parameters. */
int lu_jit_direct(PyThreadState *ts, PyFunctionObject *func, PyObject *const *args, Py_ssize_t nargs,
                  uint64_t takes, PyObject **result);

/* Calls a generator function from compiled code: `frame`, pushed and bound
   (the task's topmost, not linked), runs RETURN_GENERATOR and the prologue
   before it as the interpreter does (a tool sees neither: PY_START comes
   at the first RESUME), and is popped. The new generator, coroutine or
   async generator, or NULL with an exception; LU_JIT_DECLINED with
   nothing done when the code does not start with RETURN_GENERATOR, which
   alone says that a call of it makes one (its flags do not). */
PyObject *lu_jit_new_gen(PyThreadState *ts, struct lu_frame *frame);

/* -X lucifer_jit_stats: a generator was resumed. */
void lu_jit_note_resume(void);

/* A polymorphic inline cache (runtime/jit/jit.c): the shapes an attribute
   site the interpreter's one-shape cache gave up on has met in compiled
   code, each as the interpreter would have specialized it for that type
   (specialize.h). Compiled code tests the owner's type version against
   each (a version names one type in one state) and takes the generic path
   for anything else. Entries name descriptors under the inline-cache rule:
   immortal or deferred, forgotten by lu_specialize_forget before a
   collection frees them. */
#define LU_PIC_WAYS 4
/* A pic entry's op for a store through an exact property whose setter is a
   Python function (descr the property): the interpreter has no such
   specialization, so the cache makes it itself (jit.c lu_jit_pic_note). */
#define LU_PIC_STORE_PROPERTY 0x1000
struct lu_pic_entry {
    uint32_t version;
    uint16_t op;                /* LOAD_ATTR_INSTANCE_VALUE, _WITH_HINT, _SLOT, _METHOD_*, _NONDESCRIPTOR_* or _PROPERTY;
                                   STORE_ATTR_INSTANCE_VALUE, _WITH_HINT, _SLOT or LU_PIC_STORE_PROPERTY */
    uint16_t index;
    PyObject *descr;
};
struct lu_pic {
    PyCodeObject *code;         /* the site's, which frees the cache */
    int method;                 /* LOAD_ATTR's method form */
    int store;                  /* a STORE_ATTR site's */
    int n;                      /* entries published, release-stored after the entry */
    int tries;                  /* shapes looked at: a site whose owners have none stops looking */
    unsigned passes;            /* generic loads by units built for fewer shapes than it has now */
    struct lu_pic_entry e[LU_PIC_WAYS];
};

/* Whether a cache of n shapes may gain one: it has room, and has not
   looked at so many owners with none to give that it stopped. */
static inline int
lu_jit_pic_learns(struct lu_pic *pic, int n)
{
    return n < LU_PIC_WAYS && __atomic_load_n(&pic->tries, __ATOMIC_RELAXED) < 4 * LU_PIC_WAYS;
}
/* The site's cache, made when `create`; NULL without one. */
struct lu_pic *lu_jit_pic(PyCodeObject *code, int pc, int create);
/* A generic load at the site met owner: its shape joins the cache, while
   there is room. */
void lu_jit_pic_note(struct lu_pic *pic, PyObject *owner, PyObject *name);
/* The interpreter's generic load or store at pc of code that has compiled units met
   owner: the site's cache (made now) notes its shape, so that a unit that
   inlines the code, whose generic load or store leaves, tests for it
   (luc-9ags, luc-n6yj). */
void lu_jit_pic_note_interpreted(PyCodeObject *code, int pc, PyObject *owner, PyObject *name);
/* lu_specialize_forget for the code's polymorphic caches. */
void lu_jit_pic_forget(PyCodeObject *code, int (*dead)(PyObject *));

/* The callees a CALL site has met (runtime/jit/jit.c): Python functions,
   each named by itself when a collection is the only thing that frees it
   (a top-level function or a method), else by its code and globals (a
   closure: compiled code calls any function of that code, guarded by
   both), and whether the call passes it a self (a bound method's, or the
   one a method-form load put beside it); and other callables a collection
   alone frees (a builtin, a class), by themselves. Up to LU_CALLEE_WAYS,
   then the site is megamorphic. Entries are read without the lock; a collection
   forgets those that name what it frees, with the world stopped. */
#define LU_CALLEE_WAYS 4
/* LU_CALLEE_CMETH: a builtin method bound to its self (f = lst.append),
   named by its kernel function, with the method def it binds (`globals`,
   compared only). */
enum { LU_CALLEE_BOUND = 1, LU_CALLEE_CODE = 2, LU_CALLEE_CMETH = 4 };
struct lu_callee {
    PyObject *obj;              /* the function, or with LU_CALLEE_CODE its code */
    PyObject *globals;          /* LU_CALLEE_CODE: the functions' globals, LU_CALLEE_CMETH: the method
                                   def, compared but never read */
    int kind;
    int ndefaults;              /* LU_CALLEE_CODE: the positional defaults of the first function met (-1: none) */
};
struct lu_callees {
    int n;                      /* entries published, release-stored after the entry */
    int mega;
    struct lu_callee e[LU_CALLEE_WAYS];
};

/* The site's table, or NULL. The site is the CALL's code unit. */
struct lu_callees *lu_jit_callees(PyCodeObject *code, int pc);
/* The interpreter's CALL met a callee whose tag (lu_jit_observe_callee) is
   not the one its cache words hold: its table learns it. A tag of 0: the
   instruction has no cache words (CALL_FUNCTION_EX), `cache` is the one
   after it, and the table is asked each time. */
void lu_jit_callee_note(PyCodeObject *code, lu_codeunit *cache, PyObject *callable, PyObject *self, uint32_t tag);
/* The tag a megamorphic site's cache words hold. */
#define LU_CALLEE_MEGA UINT32_MAX

/* CALL's callee feedback, in its second and third cache words: a tag of
   the last callee (a Python function's code version and whether a self
   was passed, or another callable's address bits), so that only a change
   reaches the table. A tag two callees share only hides the second. */
static inline void
lu_jit_observe_callee(PyCodeObject *code, lu_codeunit *cache, PyObject *callable, PyObject *self)
{
    if (_PyRuntime.jit.mode == LU_JIT_OFF) {
        return;
    }
    PyObject *fn = Py_IS_TYPE(callable, &PyMethod_Type) && self == NULL ? ((PyMethodObject *)callable)->im_func
        : callable;
    uint32_t tag;
    if (Py_IS_TYPE(fn, &PyFunction_Type)) {
        tag = ((PyCodeObject *)((PyFunctionObject *)fn)->func_code)->co_version << 1 | (fn != callable || self != NULL);
    }
    else if (fn == callable && self == NULL) {
        tag = (uint32_t)((uintptr_t)callable >> 4) << 1;
    }
    else {
        return;
    }
    uint32_t seen = lu_cache_load32(cache + 1);
    if (seen != tag && seen != LU_CALLEE_MEGA && tag > 1) {
        lu_jit_callee_note(code, cache, callable, self, tag);
    }
}

/* The operand classes an operator site has met where a kind (above) says
   only OTHER: BINARY_OP (its subscript too), COMPARE_OP and CONTAINS_OP
   (the container left). Pairs of exact types, up to LU_OPCLASS_WAYS, then
   the site is megamorphic. Classes are freed only by a collection (a
   class is in a cycle through its __mro__), which forgets them first, as
   callee tables do; static types are immortal. Read without the lock. */
#define LU_OPCLASS_WAYS 4
struct lu_opclasses {
    int n;                      /* entries published, release-stored after the entry */
    int mega;
    struct { PyTypeObject *left, *right; } e[LU_OPCLASS_WAYS];
    /* An iterator's step that no arm names (a megamorphic FOR_ITER, or a
       call of the kernel's next_item): the class its generic path meets
       most, by a majority vote of those steps (lu_jit_class_vote), joins
       the armed classes (each step taken in line, generic for the rest)
       once its votes reach `need`, which each class armed doubles. Up to
       LU_OPCLASS_WAYS; armed[] is published before narmed. */
    PyTypeObject *hot, *armed[LU_OPCLASS_WAYS];
    int narmed;
    uint32_t votes, need;
};

/* The site's table, or NULL. The site is the operator's code unit. */
struct lu_opclasses *lu_jit_opclasses(PyCodeObject *code, int pc);
void lu_jit_opclass_note(PyCodeObject *code, int pc, PyTypeObject *left, PyTypeObject *right);
/* The site's table, made empty if it had none: a build asked for its
   classes, which the interpreter notes from then on. */
struct lu_opclasses *lu_jit_want_classes(PyCodeObject *code, int pc);
/* The generic path of a step at the site met an iterator of class tp: its
   vote, and the units that depend on the table recompile when it arms a
   class. */
void lu_jit_class_vote(struct lu_opclasses *t, PyTypeObject *tp);

/* The interpreter's generic path of an operator at `instr` met left and
   right, of `kinds` (lu_jit_observe's): a warm code's site with a table
   learns their classes when either is of no kind. */
static inline void
lu_jit_observe_classes(PyCodeObject *code, lu_codeunit *instr, unsigned kinds, PyObject *left, PyObject *right)
{
    if (((kinds & 0xff) == LU_KIND_OTHER || kinds >> 8 == LU_KIND_OTHER) && lu_jit_warm(code)) {
        lu_jit_opclass_note(code, (int)(instr - lu_code_start(code)), Py_TYPE(left), Py_TYPE(right));
    }
}

/* A unary operator at `instr` met v: the site learns its class, an int's
   only once another class has made it polymorphic. */
static inline void
lu_jit_observe_unary(PyCodeObject *code, lu_codeunit *instr, PyObject *v)
{
    if (_PyRuntime.jit.mode == LU_JIT_OFF || !lu_jit_warm(code)) {
        return;
    }
    int pc = (int)(instr - lu_code_start(code));
    if (!PyLong_CheckExact(v) || lu_jit_opclasses(code, pc) != NULL) {
        lu_jit_opclass_note(code, pc, Py_TYPE(v), Py_TYPE(v));
    }
}

/* A fact compiled code relies on between polls (lu_jit_invalidate). */
struct lu_fdep {
    int kind;
    uint32_t sub;
    uintptr_t obj;
    PyCodeObject *code;         /* the site whose guard it stands for */
    int pc;
};

/* Releases a code object's compiled units and JIT state (code_dealloc). */
void lu_jit_code_free(PyCodeObject *code);
/* Releases its units alone (the collector's clear of a code in a cycle,
   which no activation can be in: a running frame holds its function).
   The state stays until dealloc: another garbage code's unit that inlined
   this one is in the dependency buckets until its own clear, and an
   invalidation may still mark this code's sites. */
void lu_jit_code_clear(PyCodeObject *code);
/* Visits what the code's units hold (their constants, among them the
   globals and builtins they were built for), for the collector. */
int lu_jit_code_traverse(PyCodeObject *code, visitproc visit, void *arg);

/* A collection frees dead units (runtime/jit/jit.c): with the world
   stopped, it takes those no activation is in off every list a thread
   reads, and frees them once the world runs again. */
struct lu_unit;
struct lu_unit *lu_jit_reclaim(PyInterpreterState *interp);
void lu_jit_reclaim_free(struct lu_unit *units);

/* Prints the tier's counts to stderr (-X lucifer_jit_stats), at exit. */
void lu_jit_report(void);

#endif
