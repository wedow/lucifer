/* Frames on the task's chunked datastack (Python/pystate.c push_chunk,
   _PyThreadState_PushFrame, _PyThreadState_PopFrame), the walker over them
   and the lazy frame objects (Objects/frameobject.c). Binding arguments to
   a frame is runtime/interp/call.c. */

#include "interp/frame.h"
#include "interp/instrument.h"
#include "interp/code.h"
#include "interp/gen.h"
#include "prim/prim.h"

static PyObject **
push_chunk(PyThreadState *ts, size_t words)
{
    size_t bytes = offsetof(_PyStackChunk, data) + words * sizeof(PyObject *);
    bytes = bytes > _PY_DATA_STACK_CHUNK_SIZE ? bytes : _PY_DATA_STACK_CHUNK_SIZE;
    _PyStackChunk *c = ts->datastack_cached_chunk;
    if (c != NULL && c->size >= bytes) {
        ts->datastack_cached_chunk = NULL;
    }
    else {
        c = PyMem_RawMalloc(bytes);
        if (c == NULL) {
            return NULL;
        }
        c->size = bytes;
    }
    if (ts->datastack_chunk != NULL) {
        ts->datastack_chunk->top = ts->datastack_top - &ts->datastack_chunk->data[0];
    }
    c->previous = ts->datastack_chunk;
    c->top = 0;
    ts->datastack_chunk = c;
    ts->datastack_limit = (PyObject **)((char *)c + c->size);
    return &c->data[0];
}

/* A frame for func on the datastack, its fields borrowed and its locals
   not set; the recursion count is the caller's. NULL when out of memory. */
static struct lu_frame *
alloc_frame(PyThreadState *ts, PyFunctionObject *func)
{
    PyCodeObject *code = (PyCodeObject *)func->func_code;
    size_t words = (size_t)code->co_framesize;
    PyObject **base = ts->datastack_top;
    if (base == NULL || (size_t)(ts->datastack_limit - base) < words) {
        base = push_chunk(ts, words);
        if (base == NULL) {
            return NULL;
        }
    }
    ts->datastack_top = base + words;
    struct lu_frame *f = (struct lu_frame *)base;
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

/* Gives the topmost frame's datastack space back. */
static void
free_frame(PyThreadState *ts, struct lu_frame *f)
{
    _PyStackChunk *c = ts->datastack_chunk;
    if ((PyObject **)f != &c->data[0]) {
        ts->datastack_top = (PyObject **)f;
        return;
    }
    _PyStackChunk *prev = c->previous;
    PyMem_RawFree(ts->datastack_cached_chunk);
    ts->datastack_cached_chunk = c;
    ts->datastack_chunk = prev;
    ts->datastack_top = prev != NULL ? &prev->data[prev->top] : NULL;
    ts->datastack_limit = prev != NULL ? (PyObject **)((char *)prev + prev->size) : NULL;
}

/* What CPython's C check would add to a stack overflow's message: the
   `_recursion_where` of the innermost kernel function running that has one
   (to_repr's " while getting the repr of an object"), a new reference, or
   NULL. The kernel functions that stand for a C function with a check
   (_Py_EnterRecursiveCall) carry its text. Compiled code's frames end the
   walk: their records are compiled code's own. */
static PyObject *
overflow_where(PyThreadState *ts)
{
    for (struct lu_frame *f = (struct lu_frame *)ts->current_frame; f != NULL && f->owner != LU_FRAME_JIT;
         f = f->previous) {
        if (f->owner == LU_FRAME_C_ENTRY) {
            continue;
        }
        if (!lu_frame_is_kernel(f->code)) {
            break;
        }
        PyObject *dict = f->func != NULL && PyFunction_Check(f->func) ? ((PyFunctionObject *)f->func)->func_dict : NULL;
        PyObject *key, *where;
        /* A walk, not a lookup: hashing the key would run kernel code, which has no room left. */
        for (Py_ssize_t pos = 0; dict != NULL && PyDict_Next(dict, &pos, &key, &where);) {
            if (PyUnicode_Check(key) && PyUnicode_Check(where) && PyUnicode_EqualToUTF8(key, "_recursion_where")) {
                return Py_NewRef(where);
            }
        }
    }
    return NULL;
}

int
lu_depth_error(PyThreadState *ts, PyCodeObject *code)
{
    if (lu_frame_is_kernel(code)) {
        struct lu_task *t = (struct lu_task *)ts;
        Py_ssize_t size = lu_kernel_room(t->c_stack_init_base, t->c_stack_init_top);
        PyObject *where = overflow_where(ts);
        lu_err_format(PyExc_RecursionError, "Stack overflow (used %d kB)%s", (int)((size - t->kernel_room) / 1024),
                      where != NULL ? PyUnicode_AsUTF8(where) : "");
        Py_XDECREF(where);
    }
    else {
        lu_err_format(PyExc_RecursionError, "maximum recursion depth exceeded");
    }
    return -1;
}

struct lu_frame *
lu_frame_push(PyThreadState *ts, PyFunctionObject *func, PyObject *locals)
{
    PyCodeObject *code = (PyCodeObject *)func->func_code;
    if (lu_depth_enter(ts, code) < 0) {
        return NULL;
    }
    struct lu_frame *f = alloc_frame(ts, func);
    if (f == NULL) {
        lu_depth_leave(ts, code);
        PyErr_NoMemory();
        return NULL;
    }
    Py_INCREF(code);
    Py_INCREF(func);
    f->locals = Py_XNewRef(locals);
    for (int i = 0; i < code->co_nlocalsplus; i++) {
        f->localsplus[i] = NULL;
    }
    return f;
}

int
lu_frame_reserve(PyThreadState *ts, size_t words)
{
    if (ts->datastack_top != NULL && (size_t)(ts->datastack_limit - ts->datastack_top) >= words) {
        return 0;
    }
    PyObject **base = push_chunk(ts, words);
    if (base == NULL) {
        return -1;
    }
    ts->datastack_top = base;
    return 0;
}

/* The references a frame holds, except its code. */
static void
release_refs(struct lu_frame *f)
{
    PyObject **top = f->stackpointer;
    f->stackpointer = f->localsplus;
    for (PyObject **p = f->localsplus; p < top; p++) {
        Py_XDECREF(*p);
        *p = NULL;
    }
    Py_CLEAR(f->locals);
    Py_CLEAR(f->func);
}

void
lu_frame_unlink(PyThreadState *ts, struct lu_frame *f)
{
    if (ts->current_frame == (struct _PyInterpreterFrame *)f) {
        ts->current_frame = (struct _PyInterpreterFrame *)f->previous;
    }
}

void
lu_frame_release(struct lu_frame *f)
{
    if (lu_frame_retire(f)) {
        f->stackpointer = f->localsplus;
        f->locals = NULL;
        f->func = NULL;
    }
    else {
        release_refs(f);
    }
}

void
lu_frame_pop(PyThreadState *ts, struct lu_frame *f)
{
    lu_frame_unlink(ts, f);
    lu_frame_release(f);
    lu_depth_leave(ts, f->code);
    Py_DECREF(f->code);
    free_frame(ts, f);
}

/* Frames compiled code runs (LU_FRAME_JIT). Their own task reads them only
   from inside a call they made, where they published where their values
   are (runtime/jit/exit.c). Another task stops the world first: the owner
   is then inside such a call too, or has left compiled code at a poll,
   which made its frames the interpreter's. */

PyObject *lu_jit_slot(struct lu_frame *f, int i);
int lu_jit_surface(struct lu_frame *f);
int lu_jit_sync(struct lu_frame *f);

static int
on_own_chain(struct lu_frame *f)
{
    for (struct lu_frame *p = (struct lu_frame *)PyThreadState_Get()->current_frame; p != NULL; p = p->previous) {
        if (p == f) {
            return 1;
        }
    }
    return 0;
}

PyObject *
lu_frame_slot(struct lu_frame *f, int i)
{
    if (__atomic_load_n(&f->owner, __ATOMIC_ACQUIRE) == LU_FRAME_JIT && (on_own_chain(f) || !lu_jit_surface(f))) {
        return lu_jit_slot(f, i);
    }
    return Py_XNewRef(f->localsplus[i]);
}

/* Before a write to f's locals (held as for lu_frame_slot): a frame compiled
   code runs gets real slots, and compiled code leaves it when the call it is
   in returns. 0, or -1 with an exception set. */
static int
frame_sync(struct lu_frame *f)
{
    if (__atomic_load_n(&f->owner, __ATOMIC_ACQUIRE) == LU_FRAME_JIT && (on_own_chain(f) || !lu_jit_surface(f))) {
        return lu_jit_sync(f);
    }
    return 0;
}

/* The walker (FOUNDATIONS 2.2). Links are single-store pushes and pops, so a
   reader that interrupts the owner sees a valid chain. */

struct lu_frame *lu_jit_force(struct lu_frame *f, int all);
PyCodeObject *lu_jit_running_code(struct lu_frame *f);
int lu_jit_user_builtins(struct lu_frame *f, PyObject **out);

/* The frame a walk goes on with at f: a frame compiled code runs stands
   for the user frames it inlined, which are made real here (the walker's
   own task, inside a call the code made) and linked below `above`. One
   that cannot be made is passed over: the walk sees the root alone. */
static struct lu_frame *
seen(struct lu_frame *f, struct lu_frame *above)
{
    if (f == NULL || __atomic_load_n(&f->owner, __ATOMIC_ACQUIRE) != LU_FRAME_JIT) {
        return f;
    }
    PyObject *exc = PyErr_GetRaisedException();     /* a walk with an exception in flight keeps it */
    struct lu_frame *top = lu_jit_force(f, 0);
    if (top == NULL) {
        PyErr_Clear();
        top = f;
    }
    PyErr_SetRaisedException(exc);
    if (top == f) {
        return f;
    }
    if (top != f && above != NULL) {
        __atomic_store_n(&above->previous, top, __ATOMIC_RELEASE);
    }
    return top;
}

struct lu_frame *
lu_frame_walk(struct lu_frame *from, int (*visit)(struct lu_frame *, void *), void *arg)
{
    struct lu_frame *above = NULL;
    for (struct lu_frame *f = seen(from, NULL); f != NULL; above = f, f = seen(f->previous, above)) {
        if (visit(f, arg)) {
            return f;
        }
    }
    return NULL;
}

struct lu_frame *
lu_frame_current(PyThreadState *ts)
{
    struct lu_frame *f = (struct lu_frame *)ts->current_frame;
    if (f == NULL || f->owner != LU_FRAME_JIT) {
        return f;
    }
    return lu_jit_force(f, 1);
}

PyCodeObject *
lu_frame_running_code(struct lu_frame *f)
{
    return f->owner == LU_FRAME_JIT ? lu_jit_running_code(f) : f->code;
}

PyObject *
lu_frame_user_builtins(struct lu_frame *f)
{
    for (; f != NULL; f = f->previous) {
        PyObject *b;
        int r = f->owner == LU_FRAME_JIT ? lu_jit_user_builtins(f, &b) : 0;
        if (r != 0) {
            return r > 0 ? b : NULL;
        }
        if (f->owner != LU_FRAME_JIT && f->owner != LU_FRAME_C_ENTRY
            && !(f->code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN))) {
            return f->builtins;
        }
    }
    return NULL;
}

static int
is_nth_user_frame(struct lu_frame *f, void *depth)
{
    if (f->owner == LU_FRAME_C_ENTRY || (f->code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN))) {
        return 0;
    }
    return (*(Py_ssize_t *)depth)-- == 0;
}

struct lu_frame *
lu_frame_user(struct lu_frame *from, Py_ssize_t depth)
{
    return lu_frame_walk(from, is_nth_user_frame, &depth);
}

/* PyFrameObject */

/* The first fields lie where CPython's PyFrameObject has them: Cython writes
   f_lineno of the frame it makes with PyFrame_New, at that offset. */
struct lu_frameobj {
    PyObject_HEAD
    PyObject *f_back;           /* when it owns its frame: the frame object that called it */
    struct lu_frame *f_frame;   /* the record it reads through: live, or `own` */
    PyObject *f_trace;
    int f_lineno;               /* when not 0, the line number to report */
    char f_trace_lines, f_trace_opcodes;
    PyObject *f_extra_locals;   /* dict: names the proxy was given that are not fast locals */
    PyObject *f_locals_cache;   /* dict PyEval_GetLocals keeps for a fast-locals frame */
    PyObject *f_overwritten;    /* list: fast-local values the proxy replaced, kept alive */
    struct lu_frame own;        /* last: its localsplus follow */
};

#define FO(op) ((struct lu_frameobj *)(op))

/* The tracing state of a frame object (runtime/interp/legacy_tracing.c). */
PyObject **
lu_frame_trace_slot(PyObject *op)
{
    return &FO(op)->f_trace;
}

int
lu_frame_trace_lines(PyObject *op)
{
    return FO(op)->f_trace_lines;
}

int
lu_frame_trace_opcodes(PyObject *op)
{
    return FO(op)->f_trace_opcodes;
}

PyObject *
lu_frame_object(struct lu_frame *f)
{
    if (f->frame_obj == NULL) {
        size_t words = (size_t)f->code->co_framesize;
        struct lu_frameobj *fo = (struct lu_frameobj *)lu_object_new(
            &PyFrame_Type, offsetof(struct lu_frameobj, own) + words * sizeof(PyObject *));
        if (fo == NULL) {
            return NULL;
        }
        fo->f_frame = f;
        fo->f_trace_lines = 1;
        f->frame_obj = (PyFrameObject *)fo;
    }
    return Py_NewRef((PyObject *)f->frame_obj);
}

void
lu_frame_rehome(struct lu_frame *from, struct lu_frame *to)
{
    to->frame_obj = from->frame_obj;
    from->frame_obj = NULL;
    if (to->frame_obj != NULL) {
        FO(to->frame_obj)->f_frame = to;
    }
}

/* PyFrame_New: a frame that is not executing and owns its own record, as a
   frame object does once its frame has stopped. */
LU_EXPORT PyFrameObject *
PyFrame_New(PyThreadState *tstate, PyCodeObject *code, PyObject *globals, PyObject *locals)
{
    PyObject *func = lu_function_for_frame((PyObject *)code, globals);
    if (func == NULL) {
        return NULL;
    }
    size_t words = (size_t)code->co_framesize;
    struct lu_frameobj *fo = (struct lu_frameobj *)lu_object_new(
        &PyFrame_Type, offsetof(struct lu_frameobj, own) + words * sizeof(PyObject *));
    if (fo == NULL) {
        Py_DECREF(func);
        return NULL;
    }
    struct lu_frame *f = &fo->own;
    f->code = (PyCodeObject *)Py_NewRef(code);
    f->func = func;
    f->globals = ((PyFunctionObject *)func)->func_globals;
    f->builtins = ((PyFunctionObject *)func)->func_builtins;
    f->locals = Py_XNewRef(locals);
    f->instr = f->last = lu_code_start(code);
    f->owner = LU_FRAME_FRAME_OBJECT;
    f->stackpointer = f->localsplus + code->co_nlocalsplus;
    fo->f_frame = f;
    fo->f_trace_lines = 1;
    return (PyFrameObject *)fo;
}

PyCodeObject *
lu_frame_code(PyFrameObject *frame)
{
    return FO(frame)->f_frame->code;
}

int
lu_frame_retire(struct lu_frame *f)
{
    struct lu_frameobj *fo = FO(f->frame_obj);
    if (fo == NULL) {
        return 0;
    }
    f->frame_obj = NULL;
    int taken = Py_REFCNT(fo) > 1;
    if (taken) {
        /* The caller's frame object is made with any exception in flight set aside. */
        PyObject *exc = PyErr_GetRaisedException();
        struct lu_frame *back = f->previous != NULL ? lu_frame_user(f->previous, 0) : NULL;
        fo->f_back = back != NULL ? lu_frame_object(back) : NULL;
        PyErr_Clear();
        PyErr_SetRaisedException(exc);
        memcpy(&fo->own, f, (size_t)((char *)f->stackpointer - (char *)f));
        fo->own.stackpointer = fo->own.localsplus + (f->stackpointer - f->localsplus);
        fo->own.owner = LU_FRAME_FRAME_OBJECT;
        fo->own.previous = NULL;
        fo->own.frame_obj = NULL;
        Py_INCREF(f->code);
        fo->f_frame = &fo->own;
    }
    Py_DECREF(fo);
    return taken;
}


static int
frame_traverse(PyObject *op, visitproc visit, void *arg)
{
    struct lu_frameobj *fo = FO(op);
    Py_VISIT(fo->f_back);
    Py_VISIT(fo->f_trace);
    Py_VISIT(fo->f_extra_locals);
    Py_VISIT(fo->f_locals_cache);
    Py_VISIT(fo->f_overwritten);
    /* A frame object that does not own its frame holds nothing of it. */
    if (fo->f_frame == &fo->own) {
        Py_VISIT(fo->own.code);
        Py_VISIT(fo->own.func);
        Py_VISIT(fo->own.locals);
        for (PyObject **p = fo->own.localsplus; p < fo->own.stackpointer; p++) {
            Py_VISIT(*p);
        }
    }
    return 0;
}

static int
frame_clear(PyObject *op)
{
    struct lu_frameobj *fo = FO(op);
    Py_CLEAR(fo->f_back);
    Py_CLEAR(fo->f_trace);
    Py_CLEAR(fo->f_extra_locals);
    Py_CLEAR(fo->f_locals_cache);
    Py_CLEAR(fo->f_overwritten);
    if (fo->f_frame == &fo->own) {
        release_refs(&fo->own);
    }
    return 0;
}

/* A dying frame object frees the chain of callers only it holds in a loop. */
static void
frame_dealloc(PyObject *op)
{
    while (op != NULL) {
        struct lu_frameobj *fo = FO(op);
        PyObject *back = fo->f_back;
        if (fo->f_frame == &fo->own) {
            release_refs(&fo->own);
            Py_DECREF(fo->own.code);
        }
        Py_XDECREF(fo->f_trace);
        Py_XDECREF(fo->f_extra_locals);
        Py_XDECREF(fo->f_locals_cache);
        Py_XDECREF(fo->f_overwritten);
        lu_object_free(op);
        op = back != NULL && lu_release_last(back) ? back : NULL;
    }
}

int
lu_frame_clear(PyFrameObject *frame)
{
    struct lu_frameobj *fo = FO(frame);
    struct lu_frame *f = fo->f_frame;
    if (lu_frame_in_gen(f)) {
        return lu_gen_frame_clear(f);
    }
    if (f->owner != LU_FRAME_FRAME_OBJECT) {
        lu_err_format(PyExc_RuntimeError, "cannot clear an executing frame");
        return -1;
    }
    Py_CLEAR(fo->f_trace);
    Py_CLEAR(fo->f_extra_locals);
    Py_CLEAR(fo->f_locals_cache);
    Py_CLEAR(fo->f_overwritten);
    PyObject *func = f->func;
    f->func = NULL;
    release_refs(f);
    f->func = func;
    return 0;
}

/* Attributes */

static PyObject *
frame_get_code(PyObject *op, void *closure)
{
    return Py_NewRef((PyObject *)FO(op)->f_frame->code);
}

static PyObject *
frame_get_globals(PyObject *op, void *closure)
{
    return Py_NewRef(FO(op)->f_frame->globals ?: Py_None);
}

static PyObject *
frame_get_builtins(PyObject *op, void *closure)
{
    return Py_NewRef(FO(op)->f_frame->builtins ?: Py_None);
}

static PyObject *
frame_get_lasti(PyObject *op, void *closure)
{
    return PyLong_FromLong((long)lu_frame_lasti(FO(op)->f_frame) * (long)sizeof(lu_codeunit));
}

static PyObject *
frame_get_lineno(PyObject *op, void *closure)
{
    int line = PyFrame_GetLineNumber((PyFrameObject *)op);
    return line < 0 ? Py_NewRef(Py_None) : PyLong_FromLong(line);
}

static int
frame_set_lineno(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL) {
        lu_err_format(PyExc_AttributeError, "cannot delete attribute");
        return -1;
    }
    return lu_frame_set_lineno(FO(op)->f_frame, value);
}

static PyObject *
frame_get_back(PyObject *op, void *closure)
{
    struct lu_frameobj *fo = FO(op);
    if (fo->f_frame->owner == LU_FRAME_FRAME_OBJECT) {
        return Py_NewRef(fo->f_back ?: Py_None);
    }
    struct lu_frame *back = lu_frame_user(fo->f_frame->previous, 0);
    return back != NULL ? lu_frame_object(back) : Py_NewRef(Py_None);
}

/* Whether a comprehension inlined into this code has an iteration variable bound. */
static int
has_hidden_locals(struct lu_frame *f)
{
    PyCodeObject *co = f->code;
    for (int i = 0; i < co->co_nlocalsplus; i++) {
        if ((PyBytes_AS_STRING(co->co_localspluskinds)[i] & CO_FAST_HIDDEN) && f->localsplus[i] != NULL) {
            return 1;
        }
    }
    return 0;
}

/* A dict for code that runs in a namespace, else the FrameLocalsProxy (PEP 667). */
static PyObject *
frame_get_locals(PyObject *op, void *closure)
{
    struct lu_frame *f = FO(op)->f_frame;
    if (!(f->code->co_flags & CO_OPTIMIZED) && !has_hidden_locals(f)) {
        if (f->locals == NULL) {
            f->locals = PyDict_New();
        }
        return Py_XNewRef(f->locals);
    }
    return lu_call((PyObject *)&PyFrameLocalsProxy_Type, &op, 1, NULL);
}

static PyObject *
frame_get_trace(PyObject *op, void *closure)
{
    return Py_NewRef(FO(op)->f_trace ?: Py_None);
}

static int
frame_set_trace(PyObject *op, PyObject *value, void *closure)
{
    PyObject *old = FO(op)->f_trace;
    FO(op)->f_trace = value == NULL || value == Py_None ? NULL : Py_NewRef(value);
    Py_XDECREF(old);
    if (FO(op)->f_trace != NULL && FO(op)->f_trace_opcodes) {
        return lu_set_opcode_trace(op, 1);
    }
    return 0;
}

static PyObject *
frame_get_trace_opcodes(PyObject *op, void *closure)
{
    return Py_NewRef(FO(op)->f_trace_opcodes ? Py_True : Py_False);
}

static int
frame_set_trace_opcodes(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL || !PyBool_Check(value)) {
        lu_err_format(value == NULL ? PyExc_AttributeError : PyExc_TypeError,
                      value == NULL ? "cannot delete attribute" : "attribute value type must be bool");
        return -1;
    }
    FO(op)->f_trace_opcodes = value == Py_True;
    if (value == Py_True) {
        return FO(op)->f_trace != NULL ? lu_set_opcode_trace(op, 1) : 0;
    }
    return lu_set_opcode_trace(op, 0);
}

static PyObject *
frame_get_generator(PyObject *op, void *closure)
{
    struct lu_frame *f = FO(op)->f_frame;
    return Py_NewRef(lu_frame_in_gen(f) ? (PyObject *)lu_gen_of(f) : Py_None);
}

static PyMemberDef frame_members[] = {
    { "f_trace_lines", Py_T_BOOL, offsetof(struct lu_frameobj, f_trace_lines), 0, NULL },
    { NULL },
};

static PyGetSetDef frame_getset[] = {
    { "f_back", frame_get_back, NULL, NULL, NULL },
    { "f_locals", frame_get_locals, NULL, NULL, NULL },
    { "f_lineno", frame_get_lineno, frame_set_lineno, NULL, NULL },
    { "f_trace", frame_get_trace, frame_set_trace, NULL, NULL },
    { "f_lasti", frame_get_lasti, NULL, NULL, NULL },
    { "f_globals", frame_get_globals, NULL, NULL, NULL },
    { "f_builtins", frame_get_builtins, NULL, NULL, NULL },
    { "f_code", frame_get_code, NULL, NULL, NULL },
    { "f_trace_opcodes", frame_get_trace_opcodes, frame_set_trace_opcodes, NULL, NULL },
    { "f_generator", frame_get_generator, NULL, NULL, NULL },
    { NULL },
};

void
lu_frame_init(void)
{
    PyFrame_Type.tp_basicsize = offsetof(struct lu_frameobj, own.localsplus);
    PyFrame_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyFrame_Type.tp_dealloc = frame_dealloc;
    PyFrame_Type.tp_traverse = frame_traverse;
    PyFrame_Type.tp_clear = frame_clear;
    PyFrame_Type.tp_members = frame_members;
    PyFrame_Type.tp_getset = frame_getset;
    PyFrameLocalsProxy_Type.tp_flags |= Py_TPFLAGS_MAPPING;
}

/* The _lucifer primitives: the fast locals for FrameLocalsProxy
   (lib/kernel/frame.py), sys._getframe and frame.clear. */

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)

/* frame_at(depth): the frame object `depth` user frames out from the caller
   of the running kernel function, or None. */
static PyObject *
prim_frame_at(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    long depth;
    if (lu_long_as_long(args[0], &depth) < 0) {
        return NULL;
    }
    struct lu_frame *f = lu_frame_user((struct lu_frame *)PyThreadState_Get()->current_frame,
                                       depth < 0 ? 0 : depth);
    return f != NULL ? lu_frame_object(f) : Py_NewRef(Py_None);
}

static PyObject *
prim_is_frame(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef(PyFrame_Check(args[0]) ? Py_True : Py_False);
}

static PyObject *
prim_frame_clear(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return lu_frame_clear((PyFrameObject *)args[0]) < 0 ? NULL : Py_NewRef(Py_None);
}

/* The record a frame object reads through, held still while its locals are
   read or written. Another task may run it, or resume it (a generator), and
   its stores free the values they replace, so unless the record is on this
   task's chain every other task stops until frame_release. CPython reads
   without holding (framelocalsproxy_getval) and can take a value as it dies. */
static struct lu_frame *
frame_hold(PyObject *op, int *stopped)
{
    *stopped = !on_own_chain(FO(op)->f_frame);
    if (*stopped) {
        lu_stw_stop(PyThreadState_Get()->interp);
    }
    return FO(op)->f_frame;     /* again: the frame may have finished before the world stopped */
}

static void
frame_release(int stopped)
{
    if (stopped) {
        lu_stw_start(PyThreadState_Get()->interp);
    }
}

/* The value of fast local `i`: a new reference, or NULL when unbound. */
static PyObject *
fast_value(struct lu_frame *f, int i)
{
    PyObject *v = lu_frame_slot(f, i);
    unsigned char kind = (unsigned char)PyBytes_AS_STRING(f->code->co_localspluskinds)[i];
    if (v != NULL && (kind & (CO_FAST_CELL | CO_FAST_FREE)) && PyCell_Check(v)) {
        Py_SETREF(v, lu_cell_get(v));
    }
    return v;
}

static int
fast_name_is(PyCodeObject *co, int i, PyObject *key)
{
    PyObject *name = PyTuple_GET_ITEM(co->co_localsplusnames, i);
    return name == key || (PyUnicode_Check(key) && lu_str_eq(name, key));
}

/* The value of the bound fast local named key: a new reference, or NULL. */
static PyObject *
fast_lookup(PyObject *op, PyObject *key)
{
    int stopped;
    struct lu_frame *f = frame_hold(op, &stopped);
    PyObject *v = NULL;
    for (int i = 0; v == NULL && i < f->code->co_nlocalsplus; i++) {
        if (fast_name_is(f->code, i, key)) {
            v = fast_value(f, i);
        }
    }
    frame_release(stopped);
    return v;
}

/* frame_fast_get(frame, key): a bound fast local's value, else MISSING. */
static PyObject *
prim_frame_fast_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyObject *v = fast_lookup(args[0], args[1]);
    return v != NULL ? v : Py_NewRef(LU_MISSING);
}

/* Writes the fast local named key of fo's record f, held: True, False when
   there is none, NULL with an exception set. */
static PyObject *
fast_store(struct lu_frameobj *fo, struct lu_frame *f, PyObject *key, PyObject *value)
{
    if (frame_sync(f) < 0) {
        return NULL;
    }
    for (int i = 0; i < f->code->co_nlocalsplus; i++) {
        unsigned char kind = (unsigned char)PyBytes_AS_STRING(f->code->co_localspluskinds)[i];
        if (!fast_name_is(f->code, i, key) || (kind & CO_FAST_HIDDEN)) {
            continue;
        }
        if (value == LU_MISSING) {
            lu_err_format(PyExc_ValueError, "cannot remove local variables from FrameLocalsProxy");
            return NULL;
        }
        PyObject **slot = &f->localsplus[i];
        if (*slot != NULL && PyCell_Check(*slot) && (kind & (CO_FAST_CELL | CO_FAST_FREE))) {
            Py_XDECREF(lu_cell_exchange(*slot, Py_NewRef(value)));
        }
        else if (*slot != value) {
            /* The running code may hold the old value without a reference of its own. */
            PyObject *old = *slot;
            if (old != NULL) {
                if (fo->f_overwritten == NULL && (fo->f_overwritten = PyList_New(0)) == NULL) {
                    return NULL;
                }
                if (lu_list_append(fo->f_overwritten, old) < 0) {
                    return NULL;
                }
            }
            *slot = Py_NewRef(value);
            Py_XDECREF(old);
        }
        return Py_NewRef(Py_True);
    }
    return Py_NewRef(Py_False);
}

/* frame_fast_set(frame, key, value): writes a fast local that is not hidden
   (through its cell), 1; 0 when there is none. MISSING as the value is a
   deletion, which a local refuses. */
static PyObject *
prim_frame_fast_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    int stopped;
    struct lu_frame *f = frame_hold(args[0], &stopped);
    PyObject *r = fast_store(FO(args[0]), f, args[1], args[2]);
    frame_release(stopped);
    return r;
}

/* frame_fast_items(frame): [(name, value)] of the bound fast locals, in order. */
static PyObject *
prim_frame_fast_items(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    int stopped;
    struct lu_frame *f = frame_hold(args[0], &stopped);
    PyObject *items = PyList_New(0);
    for (int i = 0; items != NULL && i < f->code->co_nlocalsplus; i++) {
        PyObject *v = fast_value(f, i);
        if (v == NULL) {
            continue;
        }
        PyObject *pair = PyTuple_Pack(2, PyTuple_GET_ITEM(f->code->co_localsplusnames, i), v);
        Py_DECREF(v);
        if (pair == NULL || lu_list_append(items, pair) < 0) {
            Py_XDECREF(pair);
            Py_CLEAR(items);
            break;
        }
        Py_DECREF(pair);
    }
    frame_release(stopped);
    return items;
}

/* frame_extra(frame): the dict of names given to the proxy that are not fast locals. */
static PyObject *
prim_frame_extra(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    struct lu_frameobj *fo = FO(args[0]);
    if (fo->f_extra_locals == NULL) {
        fo->f_extra_locals = PyDict_New();
    }
    return Py_XNewRef(fo->f_extra_locals);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_frame_methods[] = {
    FAST(frame_at), FAST(is_frame), FAST(frame_clear), FAST(frame_fast_get), FAST(frame_fast_set),
    FAST(frame_fast_items), FAST(frame_extra),
    { NULL, NULL, 0, NULL },
};

/* The C-API over frame objects */

PyCodeObject *
PyFrame_GetCode(PyFrameObject *f)
{
    return (PyCodeObject *)Py_NewRef((PyObject *)FO(f)->f_frame->code);
}

PyFrameObject *
PyFrame_GetBack(PyFrameObject *f)
{
    PyObject *back = frame_get_back((PyObject *)f, NULL);
    if (back == Py_None) {
        Py_DECREF(back);
        return NULL;
    }
    return (PyFrameObject *)back;
}

PyObject *
PyFrame_GetGlobals(PyFrameObject *f)
{
    return frame_get_globals((PyObject *)f, NULL);
}

PyObject *
PyFrame_GetBuiltins(PyFrameObject *f)
{
    return frame_get_builtins((PyObject *)f, NULL);
}

PyObject *
PyFrame_GetLocals(PyFrameObject *f)
{
    return frame_get_locals((PyObject *)f, NULL);
}

PyObject *
PyFrame_GetGenerator(PyFrameObject *f)
{
    PyObject *gen = frame_get_generator((PyObject *)f, NULL);
    if (gen == Py_None) {
        Py_DECREF(gen);
        return NULL;
    }
    return gen;
}

int
PyFrame_GetLasti(PyFrameObject *f)
{
    return (int)(lu_frame_lasti(FO(f)->f_frame) * (Py_ssize_t)sizeof(lu_codeunit));
}

/* The frame-record queries of PEP 523's custom evaluators
   (Python/frame.c): a frame object's record is the same struct. */

PyObject *
PyUnstable_InterpreterFrame_GetCode(struct _PyInterpreterFrame *frame)
{
    return Py_NewRef((PyObject *)((struct lu_frame *)frame)->code);
}

int
PyUnstable_InterpreterFrame_GetLasti(struct _PyInterpreterFrame *frame)
{
    struct lu_frame *f = (struct lu_frame *)frame;
    return (int)(lu_frame_lasti(f) * (Py_ssize_t)sizeof(lu_codeunit));
}

int
PyUnstable_InterpreterFrame_GetLine(struct _PyInterpreterFrame *frame)
{
    struct lu_frame *f = (struct lu_frame *)frame;
    return PyCode_Addr2Line(f->code,
                            lu_frame_lasti(f) * (Py_ssize_t)sizeof(lu_codeunit));
}

int
PyFrame_GetLineNumber(PyFrameObject *f)
{
    if (FO(f)->f_lineno > 0) {
        return FO(f)->f_lineno;
    }
    return PyCode_Addr2Line(FO(f)->f_frame->code, PyFrame_GetLasti(f));
}

PyObject *
PyFrame_GetVar(PyFrameObject *f, PyObject *name)
{
    if (!PyUnicode_Check(name)) {
        lu_err_format(PyExc_TypeError, "name must be str, not %s",
                      Py_TYPE(name)->tp_name);
        return NULL;
    }
    PyObject *v = fast_lookup((PyObject *)f, name);
    if (v != NULL) {
        return v;
    }
    lu_err_format(PyExc_NameError, "variable '%s' does not exist",
                  PyUnicode_AsUTF8(name));
    return NULL;
}

PyObject *
PyFrame_GetVarString(PyFrameObject *f, const char *name)
{
    PyObject *key = PyUnicode_FromString(name);
    PyObject *v = key != NULL ? PyFrame_GetVar(f, key) : NULL;
    Py_XDECREF(key);
    return v;
}

PyFrameObject *
PyThreadState_GetFrame(PyThreadState *ts)
{
    struct lu_frame *f = lu_frame_user((struct lu_frame *)ts->current_frame, 0);
    return f != NULL ? (PyFrameObject *)lu_frame_object(f) : NULL;
}

/* A borrowed reference: the record keeps its frame object. */
PyFrameObject *
PyEval_GetFrame(void)
{
    PyFrameObject *f = PyThreadState_GetFrame(PyThreadState_Get());
    Py_XDECREF(f);
    return f;
}

/* PyEval_GetLocals (Python/ceval.c): the caller's locals, borrowed. A
   fast-locals frame's proxy is merged into a dict the frame object keeps,
   as CPython's PyFrameObject.f_locals_cache does. */
PyObject *
PyEval_GetLocals(void)
{
    PyFrameObject *frame = PyEval_GetFrame();
    if (frame == NULL) {
        lu_err_format(PyExc_SystemError, "frame does not exist");
        return NULL;
    }
    PyObject *locals = PyFrame_GetLocals(frame);
    if (locals == NULL) {
        return NULL;
    }
    if (PyFrameLocalsProxy_Check(locals)) {
        PyObject *ret = FO(frame)->f_locals_cache;
        if (ret == NULL) {
            ret = PyDict_New();
            if (ret == NULL) {
                Py_DECREF(locals);
                return NULL;
            }
            FO(frame)->f_locals_cache = ret;
        }
        if (PyDict_Update(ret, locals) < 0) {
            ret = NULL;             /* a cache that broke stays broken, as in CPython */
        }
        Py_DECREF(locals);
        return ret;
    }
    Py_DECREF(locals);              /* the frame keeps it: safe to borrow */
    return locals;
}

/* The recursion limit (Python/ceval.c Py_SetRecursionLimit, sys's
   setrecursionlimit): each task keeps its depth when the limit moves. */
int
Py_GetRecursionLimit(void)
{
    return PyThreadState_Get()->py_recursion_limit;
}

void
Py_SetRecursionLimit(int limit)
{
    struct _is *interp = PyThreadState_Get()->interp;
    /* The world is stopped: a running task changes its own count without a lock. */
    lu_stw_stop(interp);
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    for (PyThreadState *p = interp->tasks; p != NULL; p = p->next) {
        int depth = p->py_recursion_limit - p->py_recursion_remaining;
        p->py_recursion_limit = limit;
        p->py_recursion_remaining = limit - depth;
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
    lu_stw_start(interp);
}

static PyObject *
sys_getrecursionlimit(PyObject *m, PyObject *unused)
{
    return PyLong_FromLong(Py_GetRecursionLimit());
}

static PyObject *
sys_setrecursionlimit(PyObject *m, PyObject *arg)
{
    long limit;
    if (!PyLong_Check(arg)) {
        lu_err_format(PyExc_TypeError, "'%.200s' object cannot be interpreted as an integer",
                      Py_TYPE(arg)->tp_name);
        return NULL;
    }
    if (lu_long_as_long(arg, &limit) < 0 || limit > INT_MAX || limit < INT_MIN) {
        lu_err_format(PyExc_OverflowError, "Python int too large to convert to C int");
        return NULL;
    }
    if (limit < 1) {
        lu_err_format(PyExc_ValueError, "recursion limit must be greater or equal than 1");
        return NULL;
    }
    PyThreadState *ts = PyThreadState_Get();
    int depth = ts->py_recursion_limit - ts->py_recursion_remaining;
    if (depth >= limit) {
        lu_err_format(PyExc_RecursionError, "cannot set the recursion limit to %ld at the "
                      "recursion depth %d: the limit is too low", limit, depth);
        return NULL;
    }
    Py_SetRecursionLimit((int)limit);
    return Py_NewRef(Py_None);
}

PyMethodDef lu_sys_frame_methods[] = {
    { "getrecursionlimit", sys_getrecursionlimit, METH_NOARGS, NULL },
    { "setrecursionlimit", sys_setrecursionlimit, METH_O, NULL },
    { NULL, NULL, 0, NULL },
};
