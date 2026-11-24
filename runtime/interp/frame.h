/* The frame record (DESIGN 5.2, FOUNDATIONS 2.2): one private struct for
   every Python activation, on the task's chunked datastack. Nothing about it
   lives on the C stack except a C-entry marker, so a task's state is its
   frames. PyFrameObject is created lazily on observation (K1s). */
#ifndef LU_FRAME_H
#define LU_FRAME_H

#include "lucifer.h"
#include "opcode_ids.h"
#include "interp/code.h"

typedef union {
    uint16_t cache;
    struct {
        uint8_t code;
        uint8_t arg;
    } op;
} lu_codeunit;

enum lu_frame_owner {
    LU_FRAME_INTERPRETER,       /* a task's datastack frame */
    LU_FRAME_GENERATOR,         /* embedded in a generator or coroutine */
    LU_FRAME_JIT,               /* compiled code owns its locals and stack: lu_frame_slot reads them */
    LU_FRAME_KERNEL_HIDDEN,     /* a kernel builtin: skipped by every user-visible walker */
    LU_FRAME_C_ENTRY,           /* where C entered the eval loop; the task is pinned below it */
    LU_FRAME_FRAME_OBJECT,      /* the copy a frame object took when its frame stopped executing */
};

/* What the caller's loop does with the value the frame returns: an attribute
   protocol the interpreter runs as a kernel call pushes nothing (STORE_ATTR)
   or a NULL self after the value (LOAD_ATTR's method form). */
enum lu_frame_return {
    LU_RETURN_PUSH,
    LU_RETURN_METHOD,           /* push the value, then NULL */
    LU_RETURN_DISCARD,
    LU_RETURN_PAIR,             /* the value is a 2-tuple: push both items */
    LU_RETURN_UNPACK,           /* the value is a tuple: push its items, last first */
    LU_RETURN_FOR_ITER,         /* the next item, or MISSING: the loop's exit */
    LU_RETURN_NEXT,             /* the next item, or MISSING pushed (next_item's value) */
    LU_RETURN_SEND,             /* a generator resumed by SEND: a yield pushes the value, a
                                   return pushes it and leaves the yield-from loop */
    LU_RETURN_SENT,             /* the kernel's SEND: (value, returned), as SEND's two outcomes */
    LU_RETURN_INIT,             /* an __init__ called for a new instance already on the stack: the
                                   value must be None and is dropped */
    LU_RETURN_NEW,              /* a class's __new__, below which the stack holds the class and the
                                   call's arguments: type.__call__ finishes the instance */
};

struct lu_deopt;
struct lu_forced;

struct lu_frame {
    PyCodeObject *code;         /* strong */
    struct lu_frame *previous;  /* single-store link, so a signal-time walker sees a chain */
    PyObject *func;             /* strong */
    PyObject *globals;          /* borrowed from func */
    PyObject *builtins;         /* borrowed from func */
    PyObject *locals;           /* strong; NULL for optimized code */
    PyFrameObject *frame_obj;   /* lazy; owns nothing and reads through this record */
    lu_codeunit *instr;         /* the next instruction; valid at safepoints and C calls */
    lu_codeunit *last;          /* the start of the instruction before instr, as the loop saw it */
    PyObject **stackpointer;    /* the value stack's top, valid with instr */
    uint8_t owner;              /* enum lu_frame_owner */
    uint8_t ret;                /* enum lu_frame_return */
    uint8_t home;               /* LU_FRAME_JIT: the owner it had when compiled code took it, and has again
                                   when compiled code leaves it */
    /* LU_FRAME_JIT: where its values are while it calls something during
       which a walker may see it (runtime/jit/exit.c), NULL otherwise; the
       spill words; the registers a slow path saved, when jit_desc says so;
       and what walkers made real of it, NULL until one does. */
    struct lu_deopt *jit_desc;
    const uint64_t *jit_spills;
    const uint64_t *jit_regs;
    struct lu_forced *jit_forced;
    PyObject *localsplus[];     /* fast locals, cells and free variables, then the stack */
};

#define LU_FRAME_SPECIALS (offsetof(struct lu_frame, localsplus) / sizeof(PyObject *))

static inline lu_codeunit *
lu_code_start(PyCodeObject *code)
{
    return (lu_codeunit *)code->co_code_adaptive;
}

/* CACHE entries after each opcode, specialized forms included
   (_PyOpcode_Caches; runtime/interp/specialize.c). Cache words hold any
   16 bits, so an instruction's start is found from the front. */
extern const uint8_t lu_opcode_caches[256];

/* The cache words of the instruction at `at`: INSTRUMENTED_LINE and
   INSTRUMENTED_INSTRUCTION stand for another opcode (runtime/interp/instrument.c). */
int lu_instrumented_caches(PyCodeObject *code, lu_codeunit *at);

static inline int
lu_caches_at(PyCodeObject *code, lu_codeunit *at)
{
    int op = at->op.code;
    return op == INSTRUMENTED_LINE || op == INSTRUMENTED_INSTRUCTION
        ? lu_instrumented_caches(code, at) : lu_opcode_caches[op];
}

/* The code's instruction starts: one bit per code unit, set at each
   instruction's first unit, in the 8-aligned words after the bytecode. The
   layout never changes, so lu_code_new writes the map once, before the code
   is shared, and readers need no synchronization (runtime/interp/code.c). */
static inline uint64_t *
lu_code_starts(PyCodeObject *code)
{
    return (uint64_t *)(((uintptr_t)(lu_code_start(code) + Py_SIZE(code)) + 7) & ~(uintptr_t)7);
}

/* Set once an attribute site of the code has cached a descriptor, which the
   cache does not own: the word after the instruction starts. A collection
   makes only such code forget its garbage (lu_specialize_forget); most code
   never runs, or never caches one. */
static inline uint32_t *
lu_code_names_descrs(PyCodeObject *code)
{
    return (uint32_t *)(lu_code_starts(code) + (Py_SIZE(code) + 63) / 64);
}

/* The instruction that ends just before `after`, caches skipped: the last
   start below it. Unit 0 is a start, so the backward word walk ends. */
static inline lu_codeunit *
lu_instr_before(PyCodeObject *code, lu_codeunit *after)
{
    lu_codeunit *start = lu_code_start(code);
    if (after <= start) {
        return start;
    }
    size_t i = (size_t)(after - 1 - start);
    const uint64_t *starts = lu_code_starts(code);
    const uint64_t *w = starts + i / 64;
    uint64_t bits = *w & (~(uint64_t)0 >> (63 - i % 64));
    while (bits == 0) {
        bits = *--w;
    }
    return start + (w - starts) * 64 + 63 - __builtin_clzll(bits);
}

/* A frame compiled code runs: the instr of the state it published at its
   call (runtime/jit/exit.c). */
lu_codeunit *lu_jit_instr(struct lu_frame *f);

/* The code unit of the instruction the frame last started: the one before
   instr, caches skipped. */
static inline Py_ssize_t
lu_frame_lasti(struct lu_frame *f)
{
    lu_codeunit *instr = __atomic_load_n(&f->owner, __ATOMIC_ACQUIRE) == LU_FRAME_JIT ? lu_jit_instr(f) : f->instr;
    return lu_instr_before(f->code, instr) - lu_code_start(f->code);
}

/* The owner of an executing frame of `code` that the interpreter runs. */
static inline uint8_t
lu_frame_owner_for(PyCodeObject *code)
{
    return (code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN)) ? LU_FRAME_KERNEL_HIDDEN : LU_FRAME_INTERPRETER;
}

/* Code whose frames live in a generator, coroutine or async generator. */
#define LU_CO_GENERATORS (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR)

/* Whether f is a generator's frame, whoever runs it. */
static inline int
lu_frame_in_gen(struct lu_frame *f)
{
    uint8_t owner = __atomic_load_n(&f->owner, __ATOMIC_ACQUIRE);
    return owner == LU_FRAME_GENERATOR || (owner == LU_FRAME_JIT && f->home == LU_FRAME_GENERATOR);
}

/* What local i's slot holds (a cell for a cell variable) as a new reference,
   or NULL when unbound, for f on this task's chain or with every other task
   stopped. A frame compiled code runs (LU_FRAME_JIT) is read through the
   state it published at its call. */
PyObject *lu_frame_slot(struct lu_frame *f, int i);

/* The walker (FOUNDATIONS 2.2): calls visit(f, arg) on `from` and then each
   frame outward until it returns nonzero, and returns that frame, or NULL.
   Every owner appears, entry markers and kernel-hidden frames included.
   Frames of a user's code that compiled code inlined are made real as the
   walk reaches them (runtime/jit/exit.c). */
struct lu_frame *lu_frame_walk(struct lu_frame *from, int (*visit)(struct lu_frame *, void *),
                               void *arg);

/* The task's running frame as its own code sees it: compiled code's
   inlined frames, kernel ones too, are made real first. NULL without one,
   or with MemoryError when they cannot be made. */
struct lu_frame *lu_frame_current(PyThreadState *ts);

/* The code of the frame the task runs, read without making anything: an
   inlined frame's when compiled code is in a call from one. */
PyCodeObject *lu_frame_running_code(struct lu_frame *f);

/* What a user sees of the stack: frames of Python code, not C entry markers
   and not kernel builtins, which look like C. The `depth`th such frame at or
   outward of `from` (0 is the first), or NULL. */
struct lu_frame *lu_frame_user(struct lu_frame *from, Py_ssize_t depth);

/* The builtins of lu_frame_user(from, 0), read without making anything,
   as CPython reads f_builtins; NULL when there is none, or compiled code
   holds that frame's function as a value. */
PyObject *lu_frame_user_builtins(struct lu_frame *from);

/* The frame's PyFrameObject, made on first use and kept by the record. It
   owns nothing and reads through the record until the frame stops executing.
   Returns a new reference, or NULL with an exception set. */
PyObject *lu_frame_object(struct lu_frame *f);

/* The tracing state of a frame object: where f_trace lives, f_trace_lines
   and f_trace_opcodes. */
PyObject **lu_frame_trace_slot(PyObject *frame);
int lu_frame_trace_lines(PyObject *frame);
int lu_frame_trace_opcodes(PyObject *frame);

/* The code a frame object ran. */
PyCodeObject *lu_frame_code(PyFrameObject *frame);

/* The frame is about to stop executing (popped, or its generator finished).
   Its frame object, if observers still hold one, takes the locals, stack,
   function and locals dict (_PyFrame_ClearExceptCode's take_ownership) and
   a share of the code; 1 then, and the caller must not release those. Else
   0, with the frame object dropped. */
int lu_frame_retire(struct lu_frame *f);
/* The record `from` is replaced by `to` (an inlined frame a walker made
   real moves onto the datastack): its frame object, if it has one, reads
   through `to` from now on. */
void lu_frame_rehome(struct lu_frame *from, struct lu_frame *to);

/* frame.clear() (frame_clear_impl): 0, or -1 with RuntimeError for a frame
   that is executing or suspended. */
int lu_frame_clear(PyFrameObject *frame);

/* Kernel code stands in for C. CPython counts Python frames against the
   recursion limit and bounds C frames by the C stack: a kernel frame leaves
   ts->py_recursion_remaining alone and takes its words from the task's
   kernel_room instead. */
static inline int
lu_frame_is_kernel(PyCodeObject *code)
{
    return (code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN)) != 0;
}

int lu_depth_error(PyThreadState *ts, PyCodeObject *code);

/* lu_depth_enter's charge, unchecked: for room made earlier. */
static inline void
lu_depth_take(PyThreadState *ts, PyCodeObject *code)
{
    if (lu_frame_is_kernel(code)) {
        ((struct lu_task *)ts)->kernel_room -= code->co_framesize;
    }
    else {
        ts->py_recursion_remaining--;
    }
}

/* Whether a frame of `code` may be counted. */
static inline int
lu_depth_room(PyThreadState *ts, PyCodeObject *code)
{
    return lu_frame_is_kernel(code) ? ((struct lu_task *)ts)->kernel_room >= code->co_framesize
        : ts->py_recursion_remaining > 0;
}

/* A frame of `code` is counted: 0, or -1 with RecursionError. */
static inline int
lu_depth_enter(PyThreadState *ts, PyCodeObject *code)
{
    if (!lu_depth_room(ts, code)) {
        return lu_depth_error(ts, code);
    }
    lu_depth_take(ts, code);
    return 0;
}

/* What lu_depth_enter took, given back. */
static inline void
lu_depth_leave(PyThreadState *ts, PyCodeObject *code)
{
    if (lu_frame_is_kernel(code)) {
        ((struct lu_task *)ts)->kernel_room += code->co_framesize;
    }
    else {
        ts->py_recursion_remaining++;
    }
}

/* Pushes a frame for `func` on the current task's datastack, with every
   local unbound. Returns NULL with RecursionError or MemoryError set. */
struct lu_frame *lu_frame_push(PyThreadState *ts, PyFunctionObject *func, PyObject *locals);

/* A reference to func's code. Another thread may replace __code__ at any
   time, so a frame reads it once, here, and keeps what it read: the size,
   the depth charge and the reference all belong to that code. The try
   fails only for a code already replaced and dead, whose slot then holds
   another (functions mark their code shared, so other threads' tries succeed). */
static inline PyCodeObject *
lu_func_code_ref(PyFunctionObject *func)
{
    PyObject *code;
    while ((code = lu_try_xget_ref(&func->func_code)) == NULL) {
    }
    return (PyCodeObject *)code;
}

/* The fields of a new frame for func running code at f; func and code
   borrowed, its locals not set. */
static inline Py_ALWAYS_INLINE struct lu_frame *
lu_frame_fill(struct lu_frame *f, PyFunctionObject *func, PyCodeObject *code)
{
    f->code = code;
    f->previous = NULL;
    f->func = (PyObject *)func;
    f->globals = func->func_globals;
    f->builtins = func->func_builtins;
    f->locals = NULL;
    f->frame_obj = NULL;
    f->instr = f->last = lu_code_start(code);
    f->owner = lu_frame_owner_for(code);
    f->ret = LU_RETURN_PUSH;
    f->jit_desc = NULL;
    f->jit_forced = NULL;
    f->stackpointer = f->localsplus + code->co_nlocalsplus;
    return f;
}

/* CALL's exact-arguments frame (CPython's _INIT_CALL_PY_EXACT_ARGS): code,
   func's as the caller read it once, takes exactly the n positional
   arguments args[0..n), owned, which become its first locals; the frame
   takes the caller's references to func and to code. NULL with nothing
   done when the depth or the current stack chunk has no room: the general
   call raises or grows it. */
static inline Py_ALWAYS_INLINE struct lu_frame *
lu_frame_push_exact(PyThreadState *ts, PyFunctionObject *func, PyCodeObject *code, PyObject **args, int n)
{
    PyObject **base = ts->datastack_top;
    if (!lu_depth_room(ts, code) || base == NULL || ts->datastack_limit - base < code->co_framesize) {
        return NULL;
    }
    lu_depth_take(ts, code);
    ts->datastack_top = base + code->co_framesize;
    struct lu_frame *f = lu_frame_fill((struct lu_frame *)base, func, code);
    for (int i = 0; i < n; i++) {
        f->localsplus[i] = args[i];
    }
    for (int i = n; i < code->co_nlocalsplus; i++) {
        f->localsplus[i] = NULL;
    }
    return f;
}

/* A stopping frame leaves the stack before its references go (GH-99729):
   a finalizer they run sees its caller on top, never the frame. */
static inline Py_ALWAYS_INLINE void
lu_frame_unlink(PyThreadState *ts, struct lu_frame *f)
{
    if (ts->current_frame == (struct _PyInterpreterFrame *)f) {
        ts->current_frame = (struct _PyInterpreterFrame *)f->previous;
    }
}

/* The references a frame holds, except its code (CPython's
   _PyFrame_ClearLocals, then the function). The frame shows no values
   first, then they go from the top of its stack down to its first local,
   as CPython releases them; a finalizer that runs meanwhile cannot reach
   the frame, which has left the stack, so its words are not cleared. */
static inline Py_ALWAYS_INLINE void
lu_frame_release_refs(struct lu_frame *f)
{
    PyObject **p = f->stackpointer;
    f->stackpointer = f->localsplus;
    while (p > f->localsplus) {
        lu_xdecref(*--p);
    }
    PyObject *locals = f->locals, *func = f->func;
    f->locals = NULL;
    f->func = NULL;
    lu_xdecref(locals);
    lu_decref(func);
}

/* The frame's references, except its code, go to its frame object if
   observers hold one (lu_frame_retire), else are released. */
void lu_frame_release(struct lu_frame *f);

/* Gives back the datastack space of a frame that began its chunk. */
void lu_frame_free_chunk(PyThreadState *ts);

/* Releases an unlinked frame's references and its datastack space. The
   frame must be the task's topmost. */
static inline Py_ALWAYS_INLINE void
lu_frame_drop(PyThreadState *ts, struct lu_frame *f)
{
    if (f->frame_obj == NULL) {
        lu_frame_release_refs(f);
    }
    else {
        lu_frame_release(f);
    }
    lu_depth_leave(ts, f->code);
    lu_decref((PyObject *)f->code);
    if ((PyObject **)f != &ts->datastack_chunk->data[0]) {
        ts->datastack_top = (PyObject **)f;
    }
    else {
        lu_frame_free_chunk(ts);
    }
}

/* Unlinks the frame, then releases its references and its datastack space.
   The frame must be the task's topmost. */
static inline Py_ALWAYS_INLINE void
lu_frame_pop(PyThreadState *ts, struct lu_frame *f)
{
    lu_frame_unlink(ts, f);
    lu_frame_drop(ts, f);
}

/* Room for `words` more datastack words in the current chunk, which a new
   one gives when it has none: -1 when out of memory. */
int lu_frame_reserve(PyThreadState *ts, size_t words);

/* Pushes a frame for `func` and binds the call arguments to it
   (initialize_locals): args[0..nargs) are positional, then one value per
   name in `kwnames` (a tuple of str, or NULL). Takes the references in
   args on every path. Returns NULL with an exception set, the frame
   already popped (runtime/interp/call.c). */
struct lu_frame *lu_frame_call(PyThreadState *ts, PyFunctionObject *func, PyObject **args,
                               Py_ssize_t nargs, PyObject *kwnames);

/* CALL_FUNCTION_EX and DICT_MERGE operands (runtime/interp/call.c). The array
   from lu_unpack_call holds owned references in vectorcall layout (positional
   items, then keyword values) with *kwnames a new tuple or NULL, and a spare
   word before it; the caller hands the references on and frees it with
   PyMem_Free(array - 1). */
PyObject **lu_unpack_call(PyObject *callargs, PyObject *kwargs, Py_ssize_t *nargs,
                          PyObject **kwnames);
PyObject *lu_args_as_tuple(PyObject *func, PyObject *args);
int lu_dict_merge(PyObject *func, PyObject *dict, PyObject *update);
/* _Py_CalculateSuggestions: the closest of `count` candidate names to `name`, or NULL. */
PyObject *lu_suggest_keyword(PyObject *const *candidates, Py_ssize_t count, PyObject *name);
/* "module.qualname()" as the argument errors name a callable. */
const char *lu_function_str(PyObject *func, char *buf, size_t size);

/* Runs `f`, already pushed and bound, in a new activation of the eval loop
   until it returns. C callers only: the interpreter runs Python calls in its
   own activation. Returns a new reference, or NULL with an exception set.
   An exception pending at entry is set again on success. */
PyObject *lu_eval(PyThreadState *ts, struct lu_frame *f);

/* CONTAINS_OP's native answer for an exact set, frozenset, dict, tuple or
   list: 1, 0, or -1 when the kernel decides (runtime/interp/ceval.c). */
int lu_contains_native(PyObject *container, PyObject *item);

/* Runs the task's current frame, from its saved instruction (or unwinding
   the exception set, if `error`), in a new activation until a frame returns
   into the C-entry marker the caller linked below it. Returns what lu_eval
   would. For callers that ran part of the frame themselves (compiled code). */
PyObject *lu_eval_continue(PyThreadState *ts, int error);

/* Re-enters the loop for a task whose outermost activation parked, at its
   top frame, on the carrier that now runs it. Returns what lu_eval would:
   the outermost frame's result, or LU_EVAL_PARKED if it parks again. */
PyObject *lu_resume(PyThreadState *ts);

/* Every Python frame entry (FOUNDATIONS 2.18). Returns 1 when the caller's
   loop should run `f` inline; otherwise a PEP 523 hook ran it, under a pin,
   and *result holds what it returned. Kernel frames are the runtime's own
   machinery (CPython runs it in C, where no hook sees it), and a hook's C
   API calls - PyImport_GetModule over sys.modules, say - enter them, so
   routing them through the hook would re-enter the hook itself. `exc` is
   the throwflag a resumed generator enters with. */
static inline int
lu_enter_frame(PyThreadState *ts, struct lu_frame *f, int exc, PyObject **result)
{
    _PyFrameEvalFunction hook = ts->interp->eval_frame;
    if (hook == NULL || f->owner == LU_FRAME_KERNEL_HIDDEN) {
        return 1;
    }
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    *result = hook(ts, (struct _PyInterpreterFrame *)f, exc);
    t->pin--;
    return 0;
}

#endif
