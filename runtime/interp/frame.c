/* Frames on the task's chunked datastack (Python/pystate.c push_chunk,
   _PyThreadState_PushFrame, _PyThreadState_PopFrame). Binding arguments to
   a frame is runtime/interp/call.c. */

#include "interp/frame.h"
#include "interp/code.h"

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
    f->instr = lu_code_start(code);
    f->owner = (code->co_flags & LU_CO_KERNEL) ? LU_FRAME_KERNEL_HIDDEN : LU_FRAME_INTERPRETER;
    f->ret = LU_RETURN_PUSH;
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
