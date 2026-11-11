/* Generators, coroutines and async generators (Objects/genobject.c): the
   layouts, resumption and the control primitives the kernel's methods use.

   A generator owns its frame. Resuming it links that frame above the task's
   current frame and hands it to the running eval loop, which dispatches into
   it as it does into a call: FOR_ITER and SEND resume exact generators
   themselves, and send/throw/close are kernel Python over the frame
   primitives gen_send and gen_throw. Nothing recurses in C, so a chain of
   `yield from` or `await` grows only the frame chain (DESIGN 5.2).

   A throw into a `yield from` chain resumes the whole chain at once: each
   outer frame is put back inside its SEND, as if it had just resumed the
   next, and the exception is raised in the innermost. Its unwinding then
   reaches each outer frame the way CPython's recursive _gen_throw does. */

#include "interp/gen.h"
#include "jit/jit.h"
#include "interp/code.h"
#include "interp/exceptions.h"
#include "prim/prim.h"

#include "opcode_ids.h"

static int
is_gen(PyObject *o)
{
    PyTypeObject *tp = Py_TYPE(o);
    return tp == &PyGen_Type || tp == &PyCoro_Type || tp == &PyAsyncGen_Type;
}

static const char *
kind_of(PyObject *o)
{
    return Py_IS_TYPE(o, &PyCoro_Type) ? "coroutine"
         : Py_IS_TYPE(o, &PyAsyncGen_Type) ? "async generator" : "generator";
}

static int
finished(struct lu_gen *gen)
{
    return gen->frame_state >= LU_GEN_COMPLETED;
}

static int
suspended(struct lu_gen *gen)
{
    return gen->frame_state == LU_GEN_SUSPENDED || gen->frame_state == LU_GEN_SUSPENDED_YIELD_FROM;
}

PyObject *
lu_gen_new(PyThreadState *ts, struct lu_frame *f)
{
    PyCodeObject *code = f->code;
    PyTypeObject *tp = (code->co_flags & CO_COROUTINE) ? &PyCoro_Type
                     : (code->co_flags & CO_ASYNC_GENERATOR) ? &PyAsyncGen_Type : &PyGen_Type;
    struct lu_gen *gen = (struct lu_gen *)lu_object_new(
        tp, offsetof(struct lu_gen, frame) + (size_t)code->co_framesize * sizeof(PyObject *));
    if (gen == NULL) {
        return NULL;
    }
    PyFunctionObject *func = (PyFunctionObject *)f->func;
    gen->name = Py_NewRef(func->func_name);
    gen->qualname = Py_NewRef(func->func_qualname);
    /* The frame's words move; f keeps the references lu_frame_pop drops. */
    size_t used = (size_t)((char *)f->stackpointer - (char *)f);
    memcpy(&gen->frame, f, used);
    gen->frame.stackpointer = (PyObject **)((char *)&gen->frame + used);
    gen->frame.previous = NULL;
    gen->frame.owner = LU_FRAME_GENERATOR;
    gen->frame_state = LU_GEN_CREATED;
    Py_INCREF(f->code);
    Py_INCREF(f->func);
    Py_XINCREF(f->locals);
    f->stackpointer = f->localsplus;
    return (PyObject *)gen;
}

/* Starts gen running in the frame chain above `previous`, its exception
   state above `exc_prev`: the recursion check, then the links. */
static int
start(PyThreadState *ts, struct lu_gen *gen, struct lu_frame *previous, _PyErr_StackItem *exc_prev)
{
    if (ts->py_recursion_remaining-- <= 0) {
        ts->py_recursion_remaining++;
        lu_err_format(PyExc_RecursionError, "maximum recursion depth exceeded");
        return -1;
    }
    gen->frame.previous = previous;
    gen->frame.ret = LU_RETURN_PUSH;
    gen->frame_state = LU_GEN_EXECUTING;
    gen->exc_state.previous_item = exc_prev;
    return 0;
}

void
lu_gen_relink(PyThreadState *ts, struct lu_frame *f, struct lu_frame *previous, int ret)
{
    struct lu_gen *gen = lu_gen_of(f);
    ts->py_recursion_remaining--;
    gen->frame.previous = previous;
    gen->frame.ret = (uint8_t)ret;
    gen->frame_state = LU_GEN_EXECUTING;
    gen->exc_state.previous_item = ts->exc_info;
    ts->exc_info = &gen->exc_state;
}

/* gen_send_ex2's refusals: -1 with an error, 1 when gen is finished and
   the caller sees None, 0 when it can run. */
static int
check_resumable(struct lu_gen *gen, PyObject *arg)
{
    if (gen->frame_state == LU_GEN_CREATED && arg != NULL && arg != Py_None) {
        lu_err_format(PyExc_TypeError, "can't send non-None value to a just-started %s",
                      kind_of((PyObject *)gen));
        return -1;
    }
    if (gen->frame_state == LU_GEN_EXECUTING) {
        lu_err_format(PyExc_ValueError, "%s already executing", kind_of((PyObject *)gen));
        return -1;
    }
    if (finished(gen)) {
        if (Py_IS_TYPE(gen, &PyCoro_Type)) {
            lu_err_format(PyExc_RuntimeError, "cannot reuse already awaited coroutine");
            return -1;
        }
        return 1;
    }
    return 0;
}

struct lu_frame *
lu_gen_resume(PyThreadState *ts, PyObject *o, PyObject *arg)
{
    struct lu_gen *gen = (struct lu_gen *)o;
    if (check_resumable(gen, arg) != 0
        || start(ts, gen, (struct lu_frame *)ts->current_frame, ts->exc_info) < 0) {
        Py_XDECREF(arg);
        return NULL;
    }
    *gen->frame.stackpointer++ = arg != NULL ? arg : Py_NewRef(Py_None);
    ts->exc_info = &gen->exc_state;
    if (_PyRuntime.jit.stats) {
        lu_jit_note_resume();
    }
    return &gen->frame;
}

void
lu_gen_suspend(PyThreadState *ts, struct lu_frame *f, int oparg)
{
    struct lu_gen *gen = lu_gen_of(f);
    gen->frame_state = (int8_t)(LU_GEN_SUSPENDED + oparg);
    ts->exc_info = gen->exc_state.previous_item;
    gen->exc_state.previous_item = NULL;
    ts->py_recursion_remaining++;
    f->previous = NULL;
}

/* gen_clear_frame: what the frame holds goes, except its code, which
   gi_code still reports. A frame object that outlives it takes the rest. */
static void
clear_frame(struct lu_gen *gen)
{
    struct lu_frame *f = &gen->frame;
    gen->frame_state = LU_GEN_CLEARED;
    int taken = lu_frame_retire(f);
    f->previous = NULL;
    PyObject **top = f->stackpointer;
    f->stackpointer = f->localsplus;
    if (taken) {
        f->locals = NULL;
        f->func = NULL;
    }
    else {
        for (PyObject **p = f->localsplus; p < top; p++) {
            Py_XDECREF(*p);
        }
        Py_CLEAR(f->locals);
        Py_CLEAR(f->func);
    }
    Py_CLEAR(gen->exc_state.exc_value);
}

void
lu_gen_finish_in_line(PyObject *o)
{
    struct lu_gen *gen = (struct lu_gen *)o;
    gen->frame.stackpointer = gen->frame.localsplus;    /* compiled code released them */
    clear_frame(gen);
}

int64_t
lu_gen_enter_in_line(PyObject *o, PyObject *code)
{
    struct lu_gen *gen = (struct lu_gen *)o;
    if (!Py_IS_TYPE(o, &PyGen_Type) || (PyObject *)gen->frame.code != code || gen->frame.frame_obj != NULL
        || gen->frame_state == LU_GEN_EXECUTING || gen->frame_state == LU_GEN_SUSPENDED_YIELD_FROM) {
        return -2;
    }
    if (finished(gen)) {
        return -1;
    }
    PyCodeObject *co = (PyCodeObject *)code;
    lu_codeunit *start = lu_code_start(co);
    int pc = (int)(gen->frame.instr - start);
    if (gen->frame_state == LU_GEN_CREATED) {
        pc += 1 + lu_opcode_caches[POP_TOP];    /* past the POP_TOP of the value a resume sends it */
    }
    gen->frame_state = LU_GEN_EXECUTING;
    return pc + 1 + lu_opcode_caches[RESUME];
}

void
lu_gen_yielded_in_line(PyObject *o, int64_t depth, int64_t resume)
{
    struct lu_gen *gen = (struct lu_gen *)o;
    lu_codeunit *at = lu_code_start(gen->frame.code) + resume;
    gen->frame.stackpointer = gen->frame.localsplus + depth;
    gen->frame.instr = at;
    gen->frame.last = at - 1 - lu_opcode_caches[YIELD_VALUE];
    gen->frame_state = LU_GEN_SUSPENDED;
}

void
lu_gen_finish(PyThreadState *ts, struct lu_frame *f)
{
    struct lu_gen *gen = lu_gen_of(f);
    ts->exc_info = gen->exc_state.previous_item;
    gen->exc_state.previous_item = NULL;
    ts->py_recursion_remaining++;
    clear_frame(gen);
}

int
lu_gen_frame_clear(struct lu_frame *f)
{
    struct lu_gen *gen = lu_gen_of(f);
    if (gen->frame_state == LU_GEN_EXECUTING || suspended(gen)) {
        lu_err_format(PyExc_RuntimeError, "cannot clear %s frame",
                      gen->frame_state == LU_GEN_EXECUTING ? "an executing" : "a suspended");
        return -1;
    }
    if (gen->frame_state == LU_GEN_CREATED) {
        clear_frame(gen);
    }
    return 0;
}

/* _PyGen_yf: what a generator suspended in `yield from` or `await` delegates
   to, borrowed, or NULL. */
static PyObject *
yield_from(struct lu_gen *gen)
{
    return gen->frame_state == LU_GEN_SUSPENDED_YIELD_FROM ? gen->frame.stackpointer[-1] : NULL;
}

/* Finalization (_PyGen_Finalize): a generator dropped while suspended is
   closed, or handed to its async generator finalizer, from its dealloc. */
static void
gen_finalize(PyObject *self)
{
    struct lu_gen *gen = (struct lu_gen *)self;
    if (!suspended(gen)) {
        return;
    }
    PyObject *exc = PyErr_GetRaisedException();
    struct lu_task *t = lu_current_task();
    t->pin++;
    int finalizer = Py_IS_TYPE(self, &PyAsyncGen_Type) && gen->origin_or_finalizer != NULL
        && !gen->closed;
    PyObject *res = finalizer ? lu_call(gen->origin_or_finalizer, &self, 1, NULL)
                              : lu_call_kernel(LU_K_gen_close, &self, 1);
    if (res == NULL) {
        lu_err_unraisable(finalizer ? "finalizing generator" : "closing generator", self);
    }
    Py_XDECREF(res);
    t->pin--;
    PyErr_SetRaisedException(exc);
}

static int
gen_traverse(PyObject *self, visitproc visit, void *arg)
{
    struct lu_gen *gen = (struct lu_gen *)self;
    Py_VISIT(gen->name);
    Py_VISIT(gen->qualname);
    Py_VISIT(gen->exc_state.exc_value);
    Py_VISIT(gen->origin_or_finalizer);
    Py_VISIT(gen->frame.code);
    /* The frame of a running generator is a task's, its stack pointer valid
       only where that task published it; the generator is held by its caller
       and so are the frame's references. */
    if (gen->frame_state != LU_GEN_CLEARED && gen->frame_state != LU_GEN_EXECUTING) {
        Py_VISIT(gen->frame.func);
        Py_VISIT(gen->frame.locals);
        for (PyObject **p = gen->frame.localsplus; p < gen->frame.stackpointer; p++) {
            Py_VISIT(*p);
        }
    }
    return 0;
}

/* A generator in a cycle is closed by its finalizer; one that never started,
   or was finalized already, has its frame cleared here so the cycle breaks. */
static int
gen_clear(PyObject *self)
{
    struct lu_gen *gen = (struct lu_gen *)self;
    if (gen->frame_state != LU_GEN_EXECUTING && gen->frame_state != LU_GEN_CLEARED) {
        clear_frame(gen);
    }
    return 0;
}

static void
gen_dealloc(PyObject *self)
{
    struct lu_gen *gen = (struct lu_gen *)self;
    if (suspended(gen) && PyObject_CallFinalizerFromDealloc(self) < 0) {
        return;                         /* resurrected */
    }
    lu_clear_weakrefs(self);
    if (gen->frame_state != LU_GEN_CLEARED) {
        clear_frame(gen);
    }
    Py_CLEAR(gen->frame.code);
    Py_CLEAR(gen->name);
    Py_CLEAR(gen->qualname);
    Py_CLEAR(gen->origin_or_finalizer);
    lu_object_free(self);
}

/* Attributes (gen_getsetlist and the coroutine and async generator ones). */

static PyObject *
gen_get_name(PyObject *self, void *closure)
{
    struct lu_gen *gen = (struct lu_gen *)self;
    return Py_NewRef(closure != NULL ? gen->qualname : gen->name);
}

static int
gen_set_name(PyObject *self, PyObject *value, void *closure)
{
    struct lu_gen *gen = (struct lu_gen *)self;
    if (value == NULL || !PyUnicode_Check(value)) {
        lu_err_format(PyExc_TypeError, "%s must be set to a string object",
                      closure != NULL ? "__qualname__" : "__name__");
        return -1;
    }
    Py_XSETREF(*(closure != NULL ? &gen->qualname : &gen->name), Py_NewRef(value));
    return 0;
}

static PyObject *
gen_get_yieldfrom(PyObject *self, void *closure)
{
    PyObject *yf = yield_from((struct lu_gen *)self);
    return Py_NewRef(yf != NULL ? yf : Py_None);
}

static PyObject *
gen_get_running(PyObject *self, void *closure)
{
    return Py_NewRef(((struct lu_gen *)self)->frame_state == LU_GEN_EXECUTING ? Py_True : Py_False);
}

static PyObject *
gen_get_suspended(PyObject *self, void *closure)
{
    return Py_NewRef(suspended((struct lu_gen *)self) ? Py_True : Py_False);
}

static PyObject *
gen_get_frame(PyObject *self, void *closure)
{
    struct lu_gen *gen = (struct lu_gen *)self;
    return finished(gen) ? Py_NewRef(Py_None) : lu_frame_object(&gen->frame);
}

static PyObject *
gen_get_code(PyObject *self, void *closure)
{
    return Py_NewRef((PyObject *)((struct lu_gen *)self)->frame.code);
}

/* PyGen_GetCode: a new reference to the generator's code (genobject.c). */
PyCodeObject *
PyGen_GetCode(PyGenObject *gen)
{
    return (PyCodeObject *)Py_NewRef((PyObject *)((struct lu_gen *)gen)->frame.code);
}

#define NAME_GETSETS \
    { "__name__", gen_get_name, gen_set_name, NULL, NULL }, \
    { "__qualname__", gen_get_name, gen_set_name, NULL, "q" }

static PyGetSetDef gen_getset[] = {
    NAME_GETSETS,
    { "gi_yieldfrom", gen_get_yieldfrom, NULL, NULL, NULL },
    { "gi_running", gen_get_running, NULL, NULL, NULL },
    { "gi_frame", gen_get_frame, NULL, NULL, NULL },
    { "gi_suspended", gen_get_suspended, NULL, NULL, NULL },
    { "gi_code", gen_get_code, NULL, NULL, NULL },
    { NULL },
};

static PyGetSetDef coro_getset[] = {
    NAME_GETSETS,
    { "cr_await", gen_get_yieldfrom, NULL, NULL, NULL },
    { "cr_running", gen_get_running, NULL, NULL, NULL },
    { "cr_frame", gen_get_frame, NULL, NULL, NULL },
    { "cr_code", gen_get_code, NULL, NULL, NULL },
    { "cr_suspended", gen_get_suspended, NULL, NULL, NULL },
    { NULL },
};

static PyMemberDef coro_members[] = {
    { "cr_origin", _Py_T_OBJECT, offsetof(struct lu_gen, origin_or_finalizer), Py_READONLY, NULL },
    { NULL },
};

static PyGetSetDef async_gen_getset[] = {
    NAME_GETSETS,
    { "ag_await", gen_get_yieldfrom, NULL, NULL, NULL },
    { "ag_frame", gen_get_frame, NULL, NULL, NULL },
    { "ag_code", gen_get_code, NULL, NULL, NULL },
    { "ag_suspended", gen_get_suspended, NULL, NULL, NULL },
    { NULL },
};

static PyMemberDef async_gen_members[] = {
    { "ag_running", Py_T_BOOL, offsetof(struct lu_gen, running_async), Py_READONLY, NULL },
    { NULL },
};

/* The kernel-Python awaitables: coroutine_wrapper (the coroutine),
   async_generator_asend (generator, value sent, state), async_generator_athrow
   (generator, arguments, state) and the wrapped value of an async yield.
   Their fields are reached with field_get and field_set. */
struct lu_fields {
    PyObject_HEAD
    PyObject *field[3];
};

PyTypeObject lu_coro_wrapper_type = { PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "coroutine_wrapper", .tp_base = &PyBaseObject_Type };
PyTypeObject lu_async_gen_athrow_type = { PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "async_generator_athrow", .tp_base = &PyBaseObject_Type };
PyTypeObject lu_async_gen_wrapped_type = { PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "async_generator_wrapped_value", .tp_base = &PyBaseObject_Type };

LU_GC_FIELDS(fields, struct lu_fields, field[0], field[2])

static void
fields_dealloc(PyObject *self)
{
    fields_clear(self);
    lu_object_free(self);
}

PyObject *
lu_async_gen_wrap(PyObject *value)
{
    struct lu_fields *w = (struct lu_fields *)lu_object_new(&lu_async_gen_wrapped_type,
                                                            sizeof(struct lu_fields));
    if (w != NULL) {
        w->field[0] = Py_NewRef(value);
    }
    return (PyObject *)w;
}

static void
gen_layout(PyTypeObject *tp, PyGetSetDef *getset, PyMemberDef *members)
{
    tp->tp_basicsize = (Py_ssize_t)offsetof(struct lu_gen, frame.localsplus);
    tp->tp_itemsize = sizeof(PyObject *);
    tp->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    tp->tp_dealloc = gen_dealloc;
    tp->tp_traverse = gen_traverse;
    tp->tp_weaklistoffset = offsetof(struct lu_gen, weakreflist);
    tp->tp_clear = gen_clear;
    tp->tp_finalize = gen_finalize;
    tp->tp_getset = getset;
    tp->tp_members = members;
}

void
lu_gen_init(void)
{
    gen_layout(&PyGen_Type, gen_getset, NULL);
    gen_layout(&PyCoro_Type, coro_getset, coro_members);
    gen_layout(&PyAsyncGen_Type, async_gen_getset, async_gen_members);
    PyTypeObject *const fields[] = { &lu_coro_wrapper_type, &_PyAsyncGenASend_Type,
                                     &lu_async_gen_athrow_type, &lu_async_gen_wrapped_type,
                                     &PyFrameLocalsProxy_Type /* field 0 is its frame */ };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        fields[i]->tp_basicsize = sizeof(struct lu_fields);
        fields[i]->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        fields[i]->tp_dealloc = fields_dealloc;
        fields[i]->tp_traverse = fields_traverse;
        fields[i]->tp_clear = fields_clear;
    }
}

/* The _lucifer primitives. */

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)
#define GEN_ARG() do { if (!is_gen(args[0])) { \
        lu_err_format(PyExc_TypeError, "%s() needs a generator, not '%.200s'", __func__ + 5, \
                      Py_TYPE(args[0])->tp_name); \
        return NULL; } } while (0)

/* gen_send(gen, value): a frame primitive that resumes gen with value, or
   as next() does when value is MISSING. The kernel tells a yield from a
   return by gen_state afterwards. A finished generator raises StopIteration
   (StopAsyncIteration for an async generator). */
static struct lu_frame *
prim_gen_send(PyThreadState *ts, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); GEN_ARG();
    struct lu_frame *f = lu_gen_resume(ts, args[0], args[1] == LU_MISSING ? NULL : Py_NewRef(args[1]));
    if (f == NULL && !PyErr_Occurred()) {
        PyErr_SetNone(Py_IS_TYPE(args[0], &PyAsyncGen_Type) ? PyExc_StopAsyncIteration
                                                             : PyExc_StopIteration);
    }
    return f;
}

/* gen_throw(gen, exc, depth): a frame primitive that raises the exception
   exc in the generator `depth` steps down gen's `yield from` chain, with
   the generators above it resumed inside their SEND. The loop enters the
   frame it returns by unwinding the exception from there. */
static struct lu_frame *
prim_gen_throw(PyThreadState *ts, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); GEN_ARG();
    struct lu_gen *gen = (struct lu_gen *)args[0];
    PyObject *exc = args[1];
    long depth;
    if (!PyExceptionInstance_Check(exc) || lu_long_as_long(args[2], &depth) < 0 || depth < 0) {
        lu_err_format(PyExc_SystemError, "gen_throw(gen, exception, depth)");
        return NULL;
    }
    if (depth == 0 && finished(gen) && !Py_IS_TYPE(gen, &PyCoro_Type)) {
        PyErr_SetRaisedException(Py_NewRef(exc));
        return NULL;
    }
    struct lu_gen *g = gen;
    for (long i = 0; i < depth; i++) {
        PyObject *yf = yield_from(g);
        if (yf == NULL || !is_gen(yf)) {
            lu_err_format(PyExc_SystemError, "gen_throw: no generator %ld steps down", i + 1);
            return NULL;
        }
        if (finished((struct lu_gen *)yf) && !Py_IS_TYPE(yf, &PyCoro_Type)) {
            depth = i;      /* a finished generator raises exc back to its delegator */
            break;
        }
        g = (struct lu_gen *)yf;
    }
    if (check_resumable(g, NULL) != 0) {
        return NULL;
    }
    if (ts->py_recursion_remaining <= depth) {
        lu_err_format(PyExc_RecursionError, "maximum recursion depth exceeded");
        return NULL;
    }
    struct lu_frame *previous = (struct lu_frame *)ts->current_frame;
    _PyErr_StackItem *exc_prev = ts->exc_info;
    g = gen;
    for (long i = 0;; i++) {
        struct lu_gen *below = i < depth ? (struct lu_gen *)yield_from(g) : NULL;
        start(ts, g, previous, exc_prev);
        if (i > 0) {
            g->frame.ret = LU_RETURN_SEND;
        }
        if (i == depth) {
            break;
        }
        /* Back inside SEND, at the YIELD_VALUE that yields on whatever the
           generator below yields. */
        g->frame.instr--;
        previous = &g->frame;
        exc_prev = &g->exc_state;
        g = below;
    }
    *g->frame.stackpointer++ = Py_NewRef(Py_None);
    ts->exc_info = &g->exc_state;
    if (g->exc_state.exc_value != NULL && g->exc_state.exc_value != Py_None) {
        lu_exc_chain_context(exc);      /* _PyErr_ChainStackItem */
    }
    PyErr_SetRaisedException(Py_NewRef(exc));
    return &g->frame;
}

/* gen_state(gen): gi_frame_state. */
static PyObject *
prim_gen_state(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); GEN_ARG();
    return PyLong_FromLong(((struct lu_gen *)args[0])->frame_state);
}

/* gen_yieldfrom(gen): gi_yieldfrom. */
static PyObject *
prim_gen_yieldfrom(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); GEN_ARG();
    return gen_get_yieldfrom(args[0], NULL);
}

/* gen_set_running(gen, depth, on): gen and the `depth` generators below it
   in its `yield from` chain run (on) or are suspended there again, while
   the kernel closes or throws into what the last of them delegates to. */
static PyObject *
prim_gen_set_running(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); GEN_ARG();
    long depth;
    if (lu_long_as_long(args[1], &depth) < 0) {
        return NULL;
    }
    /* The chain is read off the stacks: a running generator's
       gi_yieldfrom is None. */
    struct lu_gen *g = (struct lu_gen *)args[0];
    for (long i = 0; i <= depth; i++) {
        if (!is_gen((PyObject *)g) || g->frame.stackpointer == g->frame.localsplus + g->frame.code->co_nlocalsplus) {
            lu_err_format(PyExc_SystemError, "gen_set_running: no generator %ld steps down", i);
            return NULL;
        }
        struct lu_gen *below = (struct lu_gen *)g->frame.stackpointer[-1];
        g->frame_state = args[2] == Py_True ? LU_GEN_EXECUTING : LU_GEN_SUSPENDED_YIELD_FROM;
        g = below;
    }
    return Py_NewRef(Py_None);
}

/* gen_clear(gen): close() of a generator that never started. */
static PyObject *
prim_gen_clear(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); GEN_ARG();
    clear_frame((struct lu_gen *)args[0]);
    return Py_NewRef(Py_None);
}

/* gen_flag(agen, which[, value]): an async generator's ag_hooks_inited (0),
   ag_closed (1) or ag_running_async (2), as a bool, set when value is given. */
static PyObject *
prim_gen_flag(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long which;
    if ((nargs != 2 && nargs != 3) || !Py_IS_TYPE(args[0], &PyAsyncGen_Type)
        || lu_long_as_long(args[1], &which) < 0 || which < 0 || which > 2) {
        lu_err_format(PyExc_SystemError, "gen_flag(async_generator, 0..2[, value])");
        return NULL;
    }
    struct lu_gen *gen = (struct lu_gen *)args[0];
    char *flag = which == 0 ? &gen->hooks_inited : which == 1 ? &gen->closed : &gen->running_async;
    if (nargs == 3) {
        *flag = args[2] == Py_True;
    }
    return Py_NewRef(*flag ? Py_True : Py_False);
}

/* asyncgen_hooks(): the task's (firstiter, finalizer), each None if unset.
   set_asyncgen_hooks(firstiter, finalizer) sets them, MISSING keeping one. */
static PyObject *
prim_asyncgen_hooks(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    PyThreadState *ts = PyThreadState_Get();
    return PyTuple_Pack(2, ts->async_gen_firstiter ?: Py_None, ts->async_gen_finalizer ?: Py_None);
}

static PyObject *
prim_set_asyncgen_hooks(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyThreadState *ts = PyThreadState_Get();
    PyObject **hooks[] = { &ts->async_gen_firstiter, &ts->async_gen_finalizer };
    for (int i = 0; i < 2; i++) {
        if (args[i] != LU_MISSING) {
            Py_XSETREF(*hooks[i], args[i] == Py_None ? NULL : Py_NewRef(args[i]));
        }
    }
    return Py_NewRef(Py_None);
}

/* gen_set_finalizer(agen, f): the finalizer an async generator captured
   from its task at its first iteration. */
static PyObject *
prim_gen_set_finalizer(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (!Py_IS_TYPE(args[0], &PyAsyncGen_Type)) {
        lu_err_format(PyExc_SystemError, "gen_set_finalizer needs an async generator");
        return NULL;
    }
    Py_XSETREF(((struct lu_gen *)args[0])->origin_or_finalizer, Py_NewRef(args[1]));
    return Py_NewRef(Py_None);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
#define FRAME(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL | LU_METH_FRAME, NULL }
PyMethodDef lu_prim_gen_methods[] = {
    FRAME(gen_send), FRAME(gen_throw),
    FAST(gen_state), FAST(gen_yieldfrom), FAST(gen_set_running), FAST(gen_clear), FAST(gen_flag),
    FAST(gen_set_finalizer), FAST(asyncgen_hooks), FAST(set_asyncgen_hooks),
    { NULL, NULL, 0, NULL },
};
