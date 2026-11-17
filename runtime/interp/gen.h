/* Generators, coroutines and async generators (runtime/interp/gen.c): the
   object owns its frame, and resuming one pushes that frame on the running
   eval loop, never a new activation (DESIGN 5.2, FOUNDATIONS 2.2). Their
   methods are kernel Python (lib/kernel/generators.py). */
#ifndef LU_GEN_H
#define LU_GEN_H

#include "interp/frame.h"

/* gi_frame_state (Include/internal/pycore_frame.h). */
enum {
    LU_GEN_CREATED = -3,
    LU_GEN_SUSPENDED = -2,
    LU_GEN_SUSPENDED_YIELD_FROM = -1,
    LU_GEN_EXECUTING = 0,
    LU_GEN_COMPLETED = 1,
    LU_GEN_CLEARED = 4,
};

struct lu_gen {
    PyObject_HEAD
    PyObject *weakreflist;
    PyObject *name, *qualname;
    _PyErr_StackItem exc_state;         /* linked into the task's chain while running */
    PyObject *origin_or_finalizer;      /* cr_origin, or an async generator's finalizer */
    char hooks_inited, closed, running_async;   /* async generators */
    int8_t frame_state;
    struct lu_frame frame;              /* last: its localsplus follow */
};

static inline struct lu_gen *
lu_gen_of(struct lu_frame *f)
{
    return (struct lu_gen *)((char *)f - offsetof(struct lu_gen, frame));
}

/* RETURN_GENERATOR: a generator, coroutine or async generator that takes
   over the frame `f`'s contents; `f` is left empty for lu_frame_pop. */
PyObject *lu_gen_new(PyThreadState *ts, struct lu_frame *f);

/* Whether callable is gen_throw, whose frames come linked down a
   delegation chain as the interpreter runs it. */
int lu_gen_throws(PyObject *callable);

/* For the tier, which makes a generator its code kept virtual
   (runtime/jit/exit.c): lu_gen_blank gives a generator for a call of func
   with an empty frame, for the caller to fill; lu_gen_stand says where
   the filled frame stands (the code unit it resumes at, as a yield or
   RETURN_GENERATOR left it; below 0: finished, whatever it held released);
   lu_gen_enter starts it running above `previous`, as a resumption does
   before it pushes the value sent, and gives its frame; the frame is not
   counted against the recursion limit, and its exception state is not
   yet the task's. lu_gen_enter_exc makes it so, once: only where the
   task's current one is that of the code that resumed it (an exit; a
   walker runs with its own callers' states above). */
PyObject *lu_gen_blank(PyFunctionObject *func);
void lu_gen_stand(PyObject *gen, int pc);
struct lu_frame *lu_gen_enter(PyThreadState *ts, PyObject *gen, struct lu_frame *previous);
void lu_gen_enter_exc(PyThreadState *ts, PyObject *gen);

/* For the tier, which runs the body of a generator that exists in line
   (runtime/jit/eval_gen.c). lu_gen_at: the code unit an exact generator
   would resume at, when compiled code may run it so (suspended at a
   yield of its own or not yet started, no frame object, no locals
   mapping); else -1. Compiled code checks the same in line and takes it:
   it is running, its frame compiled code's (LU_FRAME_JIT), the frame's
   words stale from there, the code holding the references they held.
   lu_gen_give: compiled code stored what the frame holds; it is
   suspended at code unit pc with a stack of `depth` (in line too, where
   the depth did not change). lu_gen_over: it returned; compiled code
   released what the frame held. */
long lu_gen_at(PyObject *gen);
void lu_gen_give(PyObject *gen, long pc, long depth);
void lu_gen_over(PyObject *gen);

/* Resumes `gen` (an exact generator, coroutine or async generator) with
   `arg` pushed on its frame (owned; NULL resumes as next() does): the frame
   is linked above the task's current frame, its exception state above the
   task's, and returned for the loop to run with ret = PUSH. NULL with an
   error when it cannot run; NULL without one when it is finished and the
   resumption returns None. */
struct lu_frame *lu_gen_resume(PyThreadState *ts, PyObject *gen, PyObject *arg);

/* YIELD_VALUE and the frame's exit: the generator stops running, as
   suspended (oparg 0 or 1 of YIELD_VALUE) or finished. */
/* RESUME's oparg says where the code resumes: its low bits are 0 at a
   call, 1 after a yield, 2 and 3 inside a delegation (yield from, await). */
#define RESUME_AFTER_YIELD_FROM 2
#define RESUME_OPARG_LOCATION_MASK 0x3
#define RESUME_OPARG_DEPTH1_MASK 0x4      /* after a yield outside every try and with block */

static inline void
lu_gen_suspend(PyThreadState *ts, struct lu_frame *f, int oparg)
{
    struct lu_gen *gen = lu_gen_of(f);
    gen->frame_state = (int8_t)(LU_GEN_SUSPENDED + oparg);
    ts->exc_info = gen->exc_state.previous_item;
    gen->exc_state.previous_item = NULL;
    ts->py_recursion_remaining++;
    f->previous = NULL;
}
void lu_gen_finish(PyThreadState *ts, struct lu_frame *f);

/* frame.clear() on a generator's frame: refused while it runs or is suspended;
   a generator not yet started is closed. */
int lu_gen_frame_clear(struct lu_frame *f);

/* ASYNC_GEN_WRAP: an async generator's yielded value, as asend unwraps it. */
PyObject *lu_async_gen_wrap(PyObject *value);

extern PyTypeObject lu_coro_wrapper_type, lu_async_gen_athrow_type, lu_async_gen_wrapped_type;

#endif
