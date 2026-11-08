/* The optimizing tier as the interpreter sees it (DESIGN 6.2).

   Counters at JUMP_BACKWARD (one per site, in its cache word) and at a
   function's RESUME (one per code object) ask for a compile once hot. A
   compiled unit starts at one bytecode boundary, the loop header the
   back-edge jumps to or the instruction after RESUME, with the frame
   exactly as the interpreter left it there, and runs until the function
   returns, an exception no handler of the unit takes is raised, or a
   guard fails. A generator's unit starts after any of its RESUMEs (the
   frame's instruction picks) and also ends at a yield (LU_JIT_YIELD). The interpreter is
   the deopt landing: a unit hands back a frame (and any frames of inlined
   calls above it) at a bytecode boundary that has not executed. */
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
    LU_JIT_YIELD,       /* the frame's generator yielded *result (a new reference): the frame is
                           saved at its resume point, still current; the caller suspends it */
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

/* Counts the back-edge at `site` (a JUMP_BACKWARD; its cache word is the
   counter): whether to try lu_jit_enter at the jump's target. */
static inline int
lu_jit_backedge_hot(PyCodeObject *code, lu_codeunit *site)
{
    if (code->co_executors != NULL && !((uintptr_t)code->co_executors & 1)) {
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
   COMPARE_OP's, STORE_SUBSCR's and CALL's first): the left operand's (a
   call's first argument's) kinds in the low byte, the right's (the second
   argument's) in the high byte. The tier speculates on a site
   only when it has met nothing else. */
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

static inline void
lu_jit_observe(lu_codeunit *word, PyObject *left, PyObject *right)
{
    if (_PyRuntime.jit.mode == LU_JIT_OFF) {
        return;
    }
    uint16_t seen = __atomic_load_n(&word->cache, __ATOMIC_RELAXED);
    uint16_t now = seen | lu_jit_kind(left) | lu_jit_kind(right) << 8;
    if (now != seen) {
        __atomic_store_n(&word->cache, now, __ATOMIC_RELAXED);
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

/* POP_JUMP_IF_*'s cache word: the directions the interpreter has taken.
   In a callee too large to inline whole, a direction it never took while
   it took the other is not compiled. */
enum { LU_BRANCH_FELL = 1, LU_BRANCH_JUMPED = 2 };

static inline void
lu_jit_observe_branch(lu_codeunit *word, int jumped)
{
    uint16_t bit = jumped ? LU_BRANCH_JUMPED : LU_BRANCH_FELL;
    if (!(__atomic_load_n(&word->cache, __ATOMIC_RELAXED) & bit)) {
        __atomic_fetch_or(&word->cache, bit, __ATOMIC_RELAXED);
    }
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

/* Runs a generator's frame, just resumed for a C caller by lu_gen_resume
   (linked above the task's current frame, its sent value pushed), through
   the code's resume unit, with no activation of the eval loop until the
   unit leaves. 1 with *result what the generator yielded or returned (a
   return under LU_RETURN_FOR_ITER gives MISSING), NULL with an exception;
   0 with nothing done when there is no unit to enter. */
int lu_jit_resume(PyThreadState *ts, struct lu_frame *frame, PyObject **result);

/* Calls a generator function from compiled code: `frame`, pushed and bound
   (the task's topmost, not linked), runs RETURN_GENERATOR and the prologue
   before it as the interpreter does (a tool sees neither: PY_START comes
   at the first RESUME), and is popped. The new generator, coroutine or
   async generator, or NULL with an exception. */
PyObject *lu_jit_new_gen(PyThreadState *ts, struct lu_frame *frame);

/* -X lucifer_jit_stats: a generator was resumed. */
void lu_jit_note_resume(void);

/* A call of func with nargs positional arguments, then one value per name
   in kwnames (NULL: none), all borrowed, from C, through its code's direct
   entry (target.h) when it has a live one and the entry's checks pass: 1
   with *result its value (NULL with an exception), or 0 with nothing
   done. No exception may be pending. */
int lu_jit_call(PyThreadState *ts, PyFunctionObject *func, PyObject *const *args, Py_ssize_t nargs,
                PyObject *kwnames, PyObject **result);

/* A polymorphic inline cache (runtime/jit/jit.c): the shapes an attribute
   site the interpreter's one-shape cache gave up on has met in compiled
   code, each as the interpreter would have specialized it for that type
   (specialize.h). Compiled code tests the owner's type version against
   each (a version names one type in one state) and takes the generic path
   for anything else. Entries name descriptors under the inline-cache rule:
   immortal or deferred, forgotten by lu_specialize_forget before a
   collection frees them. */
#define LU_PIC_WAYS 4
struct lu_pic_entry {
    uint32_t version;
    uint16_t op;                /* LOAD_ATTR_INSTANCE_VALUE, _METHOD_* or _NONDESCRIPTOR_*; STORE_ATTR_INSTANCE_VALUE */
    uint16_t index;
    PyObject *descr;
};
struct lu_pic {
    PyCodeObject *code;         /* the site's, which frees the cache */
    int method;                 /* LOAD_ATTR's method form */
    int store;                  /* a STORE_ATTR site's */
    int n;                      /* entries published, release-stored after the entry */
    int tries;                  /* shapes looked at: a site whose owners have none stops looking */
    struct lu_pic_entry e[LU_PIC_WAYS];
};

/* The site's cache, made when `create`; NULL without one. */
struct lu_pic *lu_jit_pic(PyCodeObject *code, int pc, int create);
/* A generic load at the site met owner: its shape joins the cache, and the
   code recompiles to test for it, while there is room. */
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
enum { LU_CALLEE_BOUND = 1, LU_CALLEE_CODE = 2 };
struct lu_callee {
    PyObject *obj;              /* the function, or with LU_CALLEE_CODE its code */
    PyObject *globals;          /* LU_CALLEE_CODE: the functions' globals, compared but never read */
    int kind;
};
struct lu_callees {
    int n;                      /* entries published, release-stored after the entry */
    int mega;
    struct lu_callee e[LU_CALLEE_WAYS];
};

/* The site's table, or NULL. The site is the CALL's code unit. */
struct lu_callees *lu_jit_callees(PyCodeObject *code, int pc);
/* The interpreter's CALL met a callee whose tag (lu_jit_observe_callee) is
   not the one its cache words hold: its table learns it. */
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

/* A fact compiled code relies on between polls (lu_jit_invalidate). */
struct lu_fdep {
    int kind;
    uint32_t sub;
    uintptr_t obj;
    PyCodeObject *code;         /* the site whose guard it stands for */
    int pc;
};

/* Releases a code object's compiled units (code_dealloc). */
void lu_jit_code_free(PyCodeObject *code);

/* Prints the tier's counts to stderr (-X lucifer_jit_stats), at exit. */
void lu_jit_report(void);

#endif
