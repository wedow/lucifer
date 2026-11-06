/* The optimizing tier as the interpreter sees it (DESIGN 6.2).

   Counters at JUMP_BACKWARD (one per site, in its cache word) and at a
   function's RESUME (one per code object) ask for a compile once hot. A
   compiled unit starts at one bytecode boundary, the loop header the
   back-edge jumps to or the instruction after RESUME, with the frame
   exactly as the interpreter left it there, and runs until the function
   returns, an exception is raised, or a guard fails. The interpreter is
   the deopt landing: a unit hands back a frame (and any frames of inlined
   calls above it) at a bytecode boundary that has not executed. */
#ifndef LU_JIT_H
#define LU_JIT_H

#include "interp/frame.h"

enum lu_jit_mode { LU_JIT_OFF, LU_JIT_ON, LU_JIT_ALWAYS };

/* What lu_jit_enter did. */
enum lu_jit_result {
    LU_JIT_NONE,        /* nothing ran: interpret on from the frame as saved */
    LU_JIT_RETURN,      /* the frame's function returned *result (a new reference); the
                           frame is still pushed, its stack empty */
    LU_JIT_DEOPT,       /* frames were handed back at an unexecuted boundary: continue
                           with ts->current_frame, which may be an inlined callee's */
    LU_JIT_ERROR,       /* ts->current_frame raised: unwind from its instr */
};

/* Back-edges and calls before a function is compiled (-X lucifer_jit=on). */
#define LU_JIT_BACKEDGE_THRESHOLD 1000
#define LU_JIT_CALL_THRESHOLD 1000

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
    unsigned threshold = _PyRuntime.jit.mode == LU_JIT_ALWAYS ? 1 : LU_JIT_BACKEDGE_THRESHOLD;
    if (n + 1u >= threshold) {
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
    unsigned threshold = _PyRuntime.jit.mode == LU_JIT_ALWAYS ? 1 : LU_JIT_CALL_THRESHOLD;
    uintptr_t n = v >> 1;
    if (n + 1 >= threshold) {
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
        : t == &PyUnicode_Type ? LU_KIND_STR : LU_KIND_OTHER;
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
enum { LU_ITER_RANGE = 1, LU_ITER_LIST = 2, LU_ITER_OTHER = 4 };

static inline void
lu_jit_observe_iter(lu_codeunit *word, PyTypeObject *t)
{
    if (_PyRuntime.jit.mode == LU_JIT_OFF) {
        return;
    }
    uint16_t seen = __atomic_load_n(&word->cache, __ATOMIC_RELAXED);
    uint16_t now = seen | (t == &PyRangeIter_Type ? LU_ITER_RANGE : t == &PyListIter_Type ? LU_ITER_LIST
                           : LU_ITER_OTHER);
    if (now != seen) {
        __atomic_store_n(&word->cache, now, __ATOMIC_RELAXED);
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
    uint16_t op;                /* LOAD_ATTR_INSTANCE_VALUE, _METHOD_* or _NONDESCRIPTOR_* */
    uint16_t index;
    PyObject *descr;
};
struct lu_pic {
    PyCodeObject *code;         /* the site's, which frees the cache */
    int method;                 /* LOAD_ATTR's method form */
    int n;                      /* entries published, release-stored after the entry */
    int tries;                  /* shapes looked at: a site whose owners have none stops looking */
    struct lu_pic_entry e[LU_PIC_WAYS];
};

/* The site's cache, made when `create`; NULL without one. */
struct lu_pic *lu_jit_pic(PyCodeObject *code, int pc, int create);
/* A generic load at the site met owner: its shape joins the cache, and the
   code recompiles to test for it, while there is room. */
void lu_jit_pic_note(struct lu_pic *pic, PyObject *owner, PyObject *name);
/* lu_specialize_forget for the code's polymorphic caches. */
void lu_jit_pic_forget(PyCodeObject *code, int (*dead)(PyObject *));

/* Releases a code object's compiled units (code_dealloc). */
void lu_jit_code_free(PyCodeObject *code);

/* Prints the tier's counts to stderr (-X lucifer_jit_stats), at exit. */
void lu_jit_report(void);

#endif
