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

/* The code unit of the instruction the frame last started: the one before
   instr, caches skipped. */
static inline Py_ssize_t
lu_frame_lasti(struct lu_frame *f)
{
    lu_codeunit *at = f->instr;
    while ((at - 1)->op.code == CACHE) {
        at--;
    }
    return at - 1 - lu_code_start(f->code);
}

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
/* "module.qualname()" as the argument errors name a callable. */
const char *lu_function_str(PyObject *func, char *buf, size_t size);

/* Runs `f`, already pushed and bound, in a new activation of the eval loop
   until it returns. C callers only: the interpreter runs Python calls in its
   own activation. Returns a new reference, or NULL with an exception set. */
PyObject *lu_eval(PyThreadState *ts, struct lu_frame *f);

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
