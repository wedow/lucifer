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

struct lu_frame *
lu_frame_push(PyThreadState *ts, PyFunctionObject *func, PyObject *locals)
{
    PyCodeObject *code = (PyCodeObject *)func->func_code;
    size_t words = (size_t)code->co_framesize;
    if (ts->py_recursion_remaining-- <= 0) {
        ts->py_recursion_remaining++;
        lu_err_format(PyExc_RecursionError, "maximum recursion depth exceeded");
        return NULL;
    }
    PyObject **base = ts->datastack_top;
    if (base == NULL || (size_t)(ts->datastack_limit - base) < words) {
        base = push_chunk(ts, words);
        if (base == NULL) {
            ts->py_recursion_remaining++;
            PyErr_NoMemory();
            return NULL;
        }
    }
    ts->datastack_top = base + words;
    struct lu_frame *f = (struct lu_frame *)base;
    f->code = (PyCodeObject *)Py_NewRef(code);
    f->previous = NULL;
    f->func = Py_NewRef(func);
    f->globals = func->func_globals;
    f->builtins = func->func_builtins;
    f->locals = Py_XNewRef(locals);
    f->frame_obj = NULL;
    f->instr = f->last = lu_code_start(code);
    f->owner = (code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN)) ? LU_FRAME_KERNEL_HIDDEN
                                                                : LU_FRAME_INTERPRETER;
    f->ret = LU_RETURN_PUSH;
    for (int i = 0; i < code->co_nlocalsplus; i++) {
        f->localsplus[i] = NULL;
    }
    f->stackpointer = f->localsplus + code->co_nlocalsplus;
    return f;
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
lu_frame_pop(PyThreadState *ts, struct lu_frame *f)
{
    if (!lu_frame_retire(f)) {
        release_refs(f);
    }
    Py_DECREF(f->code);
    ts->py_recursion_remaining++;
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

/* The walker (FOUNDATIONS 2.2). Links are single-store pushes and pops, so a
   reader that interrupts the owner sees a valid chain. */

struct lu_frame *
lu_frame_walk(struct lu_frame *from, int (*visit)(struct lu_frame *, void *), void *arg)
{
    for (struct lu_frame *f = from; f != NULL; f = f->previous) {
        if (visit(f, arg)) {
            return f;
        }
    }
    return NULL;
}

static int
is_nth_user_frame(struct lu_frame *f, void *depth)
{
    if (f->owner == LU_FRAME_C_ENTRY || f->owner == LU_FRAME_KERNEL_HIDDEN
        || (f->code->co_flags & LU_CO_KERNEL)) {
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

struct lu_frameobj {
    PyObject_HEAD
    struct lu_frame *f_frame;   /* the record it reads through: live, or `own` */
    PyObject *f_back;           /* when it owns its frame: the frame object that called it */
    PyObject *f_trace;
    PyObject *f_extra_locals;   /* dict: names the proxy was given that are not fast locals */
    PyObject *f_overwritten;    /* list: fast-local values the proxy replaced, kept alive */
    char f_trace_lines, f_trace_opcodes;
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
    if (f->owner == LU_FRAME_GENERATOR) {
        return lu_gen_frame_clear(f);
    }
    if (f->owner != LU_FRAME_FRAME_OBJECT) {
        lu_err_format(PyExc_RuntimeError, "cannot clear an executing frame");
        return -1;
    }
    Py_CLEAR(fo->f_trace);
    Py_CLEAR(fo->f_extra_locals);
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
    struct lu_frame *f = FO(op)->f_frame;
    int line = PyCode_Addr2Line(f->code, (int)(lu_frame_lasti(f) * sizeof(lu_codeunit)));
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
    return Py_NewRef(f->owner == LU_FRAME_GENERATOR ? (PyObject *)lu_gen_of(f) : Py_None);
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

/* The value of fast local `i`: a new reference, or NULL when unbound. */
static PyObject *
fast_value(struct lu_frame *f, int i)
{
    PyObject *v = f->localsplus[i];
    unsigned char kind = (unsigned char)PyBytes_AS_STRING(f->code->co_localspluskinds)[i];
    if (v != NULL && (kind & (CO_FAST_CELL | CO_FAST_FREE)) && PyCell_Check(v)) {
        v = ((PyCellObject *)v)->ob_ref;
    }
    return Py_XNewRef(v);
}

static int
fast_name_is(PyCodeObject *co, int i, PyObject *key)
{
    PyObject *name = PyTuple_GET_ITEM(co->co_localsplusnames, i);
    return name == key || (PyUnicode_Check(key) && lu_str_eq(name, key));
}

/* frame_fast_get(frame, key): a bound fast local's value, else MISSING. */
static PyObject *
prim_frame_fast_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    struct lu_frame *f = FO(args[0])->f_frame;
    for (int i = 0; i < f->code->co_nlocalsplus; i++) {
        if (fast_name_is(f->code, i, args[1])) {
            PyObject *v = fast_value(f, i);
            if (v != NULL) {
                return v;
            }
        }
    }
    return Py_NewRef(LU_MISSING);
}

/* frame_fast_set(frame, key, value): writes a fast local that is not hidden
   (through its cell), 1; 0 when there is none. MISSING as the value is a
   deletion, which a local refuses. */
static PyObject *
prim_frame_fast_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3);
    struct lu_frameobj *fo = FO(args[0]);
    struct lu_frame *f = fo->f_frame;
    PyObject *value = args[2];
    for (int i = 0; i < f->code->co_nlocalsplus; i++) {
        unsigned char kind = (unsigned char)PyBytes_AS_STRING(f->code->co_localspluskinds)[i];
        if (!fast_name_is(f->code, i, args[1]) || (kind & CO_FAST_HIDDEN)) {
            continue;
        }
        if (value == LU_MISSING) {
            lu_err_format(PyExc_ValueError, "cannot remove local variables from FrameLocalsProxy");
            return NULL;
        }
        PyObject **slot = &f->localsplus[i];
        if (*slot != NULL && PyCell_Check(*slot) && (kind & (CO_FAST_CELL | CO_FAST_FREE))) {
            PyObject *old = ((PyCellObject *)*slot)->ob_ref;
            ((PyCellObject *)*slot)->ob_ref = Py_NewRef(value);
            Py_XDECREF(old);
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

/* frame_fast_items(frame): [(name, value)] of the bound fast locals, in order. */
static PyObject *
prim_frame_fast_items(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    struct lu_frame *f = FO(args[0])->f_frame;
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

int
PyFrame_GetLineNumber(PyFrameObject *f)
{
    struct lu_frame *lf = FO(f)->f_frame;
    return PyCode_Addr2Line(lf->code, PyFrame_GetLasti(f));
}

PyObject *
PyFrame_GetVar(PyFrameObject *f, PyObject *name)
{
    struct lu_frame *lf = FO(f)->f_frame;
    for (int i = 0; i < lf->code->co_nlocalsplus; i++) {
        PyObject *v = fast_name_is(lf->code, i, name) ? fast_value(lf, i) : NULL;
        if (v != NULL) {
            return v;
        }
    }
    lu_err_format(PyExc_NameError, "variable '%s' does not exist",
                  PyUnicode_Check(name) ? PyUnicode_AsUTF8(name) : "?");
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
    pthread_mutex_lock(&interp->tasks_lock.mutex);
    for (PyThreadState *p = interp->tasks; p != NULL; p = p->next) {
        int depth = p->py_recursion_limit - p->py_recursion_remaining;
        p->py_recursion_limit = limit;
        p->py_recursion_remaining = limit - depth;
    }
    pthread_mutex_unlock(&interp->tasks_lock.mutex);
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
