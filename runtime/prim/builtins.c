/* The _lucifer primitives under the builtins module (DESIGN 4.4): the
   identity of an object, the locals of the user frame that called a
   builtin, and running a code object in given namespaces. The rest of
   exec, eval, locals and id is lib/kernel/builtins.py. */

#include "prim/prim.h"
#include "interp/frame.h"
#include "interp/code.h"

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)

/* id(obj): the address, as CPython's id(). */
static PyObject *
prim_object_id(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyLong_FromLongLong((long long)(uintptr_t)args[0]);
}

/* suggest_keyword(name, names): the closest of the tuple names, or None. */
static PyObject *
prim_suggest_keyword(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (!PyUnicode_CheckExact(args[0]) || !PyTuple_CheckExact(args[1])) {
        lu_err_format(PyExc_TypeError, "suggest_keyword(name, names)");
        return NULL;
    }
    PyObject *s = lu_suggest_keyword(&PyTuple_GET_ITEM(args[1], 0), PyTuple_GET_SIZE(args[1]), args[0]);
    return Py_NewRef(s != NULL ? s : Py_None);
}

/* The frame of the user code that called a builtin: the nearest one that
   is not kernel-hidden. */
static struct lu_frame *
user_frame(PyThreadState *ts)
{
    for (struct lu_frame *f = (struct lu_frame *)ts->current_frame; f != NULL; f = f->previous) {
        if (f->owner == LU_FRAME_INTERPRETER || f->owner == LU_FRAME_GENERATOR) {
            return f;
        }
    }
    return NULL;
}

/* caller_locals(): the namespace a module, class body or exec runs in, or
   for a function a new dict of its bound variables (PEP 667's snapshot). */
static PyObject *
prim_caller_locals(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    struct lu_frame *f = user_frame(PyThreadState_Get());
    if (f == NULL) {
        return Py_NewRef(Py_None);
    }
    if (f->locals != NULL) {
        return Py_NewRef(f->locals);
    }
    PyCodeObject *co = f->code;
    PyObject *d = PyDict_New();
    for (int i = 0; d != NULL && i < co->co_nlocalsplus; i++) {
        unsigned char kind = (unsigned char)PyBytes_AS_STRING(co->co_localspluskinds)[i];
        PyObject *v = f->localsplus[i];
        if ((kind & CO_FAST_HIDDEN) || v == NULL) {
            continue;
        }
        if ((kind & (CO_FAST_CELL | CO_FAST_FREE)) && PyCell_Check(v)) {
            v = ((PyCellObject *)v)->ob_ref;
        }
        if (v != NULL && lu_ns_insert(d, PyTuple_GET_ITEM(co->co_localsplusnames, i), v) < 0) {
            Py_CLEAR(d);
        }
    }
    return d;
}

/* eval_code(code, globals, locals): a frame primitive that runs a code
   object, which takes no closure, as exec and eval do; globals gets
   __builtins__ when it has none. */
static struct lu_frame *
prim_eval_code(PyThreadState *ts, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3 || !PyCode_Check(args[0]) || !PyDict_Check(args[1]) || !PyDict_Check(args[2])) {
        lu_err_format(PyExc_TypeError, "eval_code(code, globals, locals)");
        return NULL;
    }
    PyCodeObject *co = (PyCodeObject *)args[0];
    if (co->co_nfreevars != 0) {
        lu_err_format(PyExc_TypeError, "code object requires a closure of exactly length %d",
                      co->co_nfreevars);
        return NULL;
    }
    if (lu_ns_lookup(args[1], LU_ID(__builtins__)) == NULL
        && lu_ns_insert(args[1], LU_ID(__builtins__),
                        lu_ns_lookup(ts->interp->modules, LU_ID(builtins))) < 0) {
        return NULL;
    }
    PyObject *func = lu_function_new(args[0], args[1]);
    if (func == NULL) {
        return NULL;
    }
    struct lu_frame *f = lu_frame_push(ts, (PyFunctionObject *)func, args[2]);
    Py_DECREF(func);
    return f;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_builtins_methods[] = {
    FAST(object_id), FAST(caller_locals), FAST(suggest_keyword),
    { "eval_code", (PyCFunction)(void (*)(void))prim_eval_code, METH_FASTCALL | LU_METH_FRAME, NULL },
    { NULL, NULL, 0, NULL },
};
