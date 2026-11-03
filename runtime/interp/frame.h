/* The frame record (DESIGN 5.2, FOUNDATIONS 2.2): one private struct for
   every Python activation, on the task's chunked datastack. Nothing about it
   lives on the C stack except a C-entry marker, so a task's state is its
   frames. PyFrameObject is created lazily on observation (K1s). */
#ifndef LU_FRAME_H
#define LU_FRAME_H

#include "lucifer.h"

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
    PyObject *localsplus[];     /* fast locals, cells and free variables, then the stack */
};

#define LU_FRAME_SPECIALS (offsetof(struct lu_frame, localsplus) / sizeof(PyObject *))

static inline lu_codeunit *
lu_code_start(PyCodeObject *code)
{
    return (lu_codeunit *)code->co_code_adaptive;
}

/* Pushes a frame for `func` on the current task's datastack, with every
   local unbound. Returns NULL with MemoryError set. */
struct lu_frame *lu_frame_push(PyThreadState *ts, PyFunctionObject *func, PyObject *locals);

/* Releases the frame's references and its datastack space. The frame must
   be the task's topmost. */
void lu_frame_pop(PyThreadState *ts, struct lu_frame *f);

/* Binds call arguments to the frame's locals, taking the references in
   args[0..nargs). Returns -1 with TypeError set (initialize_locals). */
int lu_frame_bind(PyThreadState *ts, struct lu_frame *f, PyObject **args, Py_ssize_t nargs);

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
