/* Frames on the task's chunked datastack (Python/pystate.c push_chunk,
   _PyThreadState_PushFrame, _PyThreadState_PopFrame) and argument binding
   (Python/ceval.c initialize_locals, the positional and default subset; the
   rest, with CPython's exact messages, is K1b). */

#include "interp/frame.h"

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
    PyObject **base = ts->datastack_top;
    if (base == NULL || (size_t)(ts->datastack_limit - base) < words) {
        base = push_chunk(ts, words);
        if (base == NULL) {
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
    f->instr = lu_code_start(code);
    f->owner = LU_FRAME_INTERPRETER;
    for (int i = 0; i < code->co_nlocalsplus; i++) {
        f->localsplus[i] = NULL;
    }
    f->stackpointer = f->localsplus + code->co_nlocalsplus;
    return f;
}

void
lu_frame_pop(PyThreadState *ts, struct lu_frame *f)
{
    for (PyObject **p = f->localsplus; p < f->stackpointer; p++) {
        Py_XDECREF(*p);
    }
    Py_XDECREF(f->locals);
    Py_DECREF(f->func);
    Py_DECREF(f->code);
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

static void
too_many(PyFunctionObject *func, int argcount, Py_ssize_t given)
{
    lu_err_format(PyExc_TypeError, "%s() takes %d positional argument%s but %zd %s given",
                  PyUnicode_AsUTF8(func->func_qualname), argcount, argcount == 1 ? "" : "s",
                  given, given == 1 ? "was" : "were");
}

static void
missing(PyFunctionObject *func, PyObject *name, int kwonly)
{
    lu_err_format(PyExc_TypeError, "%s() missing 1 required %s argument: '%s'",
                  PyUnicode_AsUTF8(func->func_qualname), kwonly ? "keyword-only" : "positional",
                  PyUnicode_AsUTF8(name));
}

int
lu_frame_bind(PyThreadState *ts, struct lu_frame *f, PyObject **args, Py_ssize_t nargs)
{
    PyCodeObject *co = f->code;
    PyFunctionObject *func = (PyFunctionObject *)f->func;
    PyObject **locals = f->localsplus;
    int argcount = co->co_argcount;
    int total = argcount + co->co_kwonlyargcount;
    Py_ssize_t n = nargs < argcount ? nargs : argcount;
    for (Py_ssize_t i = 0; i < n; i++) {
        locals[i] = args[i];
    }
    if (co->co_flags & CO_VARARGS) {
        PyObject *rest = PyTuple_New(nargs - n);
        if (rest == NULL) {
            goto drop_rest;
        }
        for (Py_ssize_t i = n; i < nargs; i++) {
            PyTuple_SET_ITEM(rest, i - n, args[i]);
        }
        locals[total] = rest;
    }
    else if (nargs > argcount) {
        too_many(func, argcount, nargs);
        goto drop_rest;
    }
    if (co->co_flags & CO_VARKEYWORDS) {
        int slot = total + ((co->co_flags & CO_VARARGS) != 0);
        locals[slot] = PyDict_New();
        if (locals[slot] == NULL) {
            return -1;
        }
    }
    PyObject *defaults = func->func_defaults;
    Py_ssize_t ndefaults = defaults != NULL ? PyTuple_GET_SIZE(defaults) : 0;
    for (Py_ssize_t i = n; i < argcount; i++) {
        Py_ssize_t d = i - (argcount - ndefaults);
        if (d < 0) {
            missing(func, PyTuple_GET_ITEM(co->co_localsplusnames, i), 0);
            return -1;
        }
        locals[i] = Py_NewRef(PyTuple_GET_ITEM(defaults, d));
    }
    for (int i = argcount; i < total; i++) {
        PyObject *name = PyTuple_GET_ITEM(co->co_localsplusnames, i);
        PyObject *value = func->func_kwdefaults != NULL
            ? lu_ns_lookup(func->func_kwdefaults, name) : NULL;
        if (value == NULL) {
            missing(func, name, 1);
            return -1;
        }
        locals[i] = Py_NewRef(value);
    }
    return 0;

drop_rest:
    for (Py_ssize_t i = n; i < nargs; i++) {
        Py_DECREF(args[i]);
    }
    return -1;
}
