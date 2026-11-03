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

/* Resumes `gen` (an exact generator, coroutine or async generator) with
   `arg` pushed on its frame (owned; NULL resumes as next() does): the frame
   is linked above the task's current frame, its exception state above the
   task's, and returned for the loop to run with ret = PUSH. NULL with an
   error when it cannot run; NULL without one when it is finished and the
   resumption returns None. */
struct lu_frame *lu_gen_resume(PyThreadState *ts, PyObject *gen, PyObject *arg);

/* YIELD_VALUE and the frame's exit: the generator stops running, as
   suspended (oparg 0 or 1 of YIELD_VALUE) or finished. */
void lu_gen_suspend(PyThreadState *ts, struct lu_frame *f, int oparg);
void lu_gen_finish(PyThreadState *ts, struct lu_frame *f);

/* ASYNC_GEN_WRAP: an async generator's yielded value, as asend unwraps it. */
PyObject *lu_async_gen_wrap(PyObject *value);

extern PyTypeObject lu_coro_wrapper_type, lu_async_gen_athrow_type, lu_async_gen_wrapped_type;

#endif
