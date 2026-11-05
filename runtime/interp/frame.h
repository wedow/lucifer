/* The frame record (DESIGN 5.2, FOUNDATIONS 2.2): one private struct for
   every Python activation, on the task's chunked datastack. Nothing about it
   lives on the C stack except a C-entry marker, so a task's state is its
   frames. PyFrameObject is created lazily on observation (K1s). */
#ifndef LU_FRAME_H
#define LU_FRAME_H

#include "lucifer.h"
#include "opcode_ids.h"

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
    LU_FRAME_JIT,               /* values published through safepoint descriptors */
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
    LU_RETURN_SEND,             /* a generator resumed by SEND: a yield pushes the value, a
                                   return pushes it and leaves the yield-from loop */
    LU_RETURN_SENT,             /* the kernel's SEND: (value, returned), as SEND's two outcomes */
    LU_RETURN_INIT,             /* an __init__ called for a new instance already on the stack: the
                                   value must be None and is dropped */
    LU_RETURN_NEW,              /* a class's __new__, below which the stack holds the class and the
                                   call's arguments: type.__call__ finishes the instance */
};

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

/* The instruction that ends just before `after`, caches skipped. */
static inline lu_codeunit *
lu_instr_before(PyCodeObject *code, lu_codeunit *after)
{
    lu_codeunit *at = lu_code_start(code);
    while (at + 1 + lu_caches_at(code, at) < after) {
        at += 1 + lu_caches_at(code, at);
    }
    return at;
}

/* The code unit of the instruction the frame last started: the one before
   instr, caches skipped. */
static inline Py_ssize_t
lu_frame_lasti(struct lu_frame *f)
{
    return lu_instr_before(f->code, f->instr) - lu_code_start(f->code);
}

/* The walker (FOUNDATIONS 2.2): calls visit(f, arg) on `from` and then each
   frame outward until it returns nonzero, and returns that frame, or NULL.
   Every owner appears, entry markers and kernel-hidden frames included. */
struct lu_frame *lu_frame_walk(struct lu_frame *from, int (*visit)(struct lu_frame *, void *),
                               void *arg);

/* What a user sees of the stack: frames of Python code, not C entry markers
   and not kernel builtins, which look like C. The `depth`th such frame at or
   outward of `from` (0 is the first), or NULL. */
struct lu_frame *lu_frame_user(struct lu_frame *from, Py_ssize_t depth);

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

/* frame.clear() (frame_clear_impl): 0, or -1 with RuntimeError for a frame
   that is executing or suspended. */
int lu_frame_clear(PyFrameObject *frame);

/* Pushes a frame for `func` on the current task's datastack, with every
   local unbound. Returns NULL with MemoryError set. */
struct lu_frame *lu_frame_push(PyThreadState *ts, PyFunctionObject *func, PyObject *locals);

/* Releases the frame's references and its datastack space. The frame must
   be the task's topmost. */
void lu_frame_pop(PyThreadState *ts, struct lu_frame *f);

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

/* Re-enters the loop for a task whose outermost activation parked, at its
   top frame, on the carrier that now runs it. Returns what lu_eval would:
   the outermost frame's result, or LU_EVAL_PARKED if it parks again. */
PyObject *lu_resume(PyThreadState *ts);

/* Every Python frame entry (FOUNDATIONS 2.18). Returns 1 when the caller's
   loop should run `f` inline; otherwise a PEP 523 hook ran it, under a pin,
   and *result holds what it returned. */
static inline int
lu_enter_frame(PyThreadState *ts, struct lu_frame *f, PyObject **result)
{
    _PyFrameEvalFunction hook = ts->interp->eval_frame;
    if (hook == NULL) {
        return 1;
    }
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    *result = hook(ts, (struct _PyInterpreterFrame *)f, 0);
    t->pin--;
    return 0;
}

#endif
