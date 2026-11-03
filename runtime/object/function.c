/* Function, cell and builtin-function layouts (Objects/funcobject.c,
   cellobject.c, methodobject.c, layout halves). A Python function called
   from C runs in a nested eval-loop activation, which pins the task
   (DESIGN 5.2); the interpreter itself calls functions inline. */

#include "interp/frame.h"

#include <string.h>

/* function */

static PyObject *
function_vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    PyFunctionObject *func = (PyFunctionObject *)callable;
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (kwnames != NULL && PyTuple_GET_SIZE(kwnames) != 0) {
        lu_err_format(PyExc_SystemError, "keyword arguments arrive with K1b");
        return NULL;
    }
    PyThreadState *ts = PyThreadState_Get();
    struct lu_frame *f = lu_frame_push(ts, func, NULL);
    if (f == NULL) {
        return NULL;
    }
    PyObject *owned[nargs > 0 ? nargs : 1];
    for (Py_ssize_t i = 0; i < nargs; i++) {
        owned[i] = Py_NewRef(args[i]);
    }
    if (lu_frame_bind(ts, f, owned, nargs) < 0) {
        lu_frame_pop(ts, f);
        return NULL;
    }
    PyObject *result;
    if (lu_enter_frame(ts, f, &result)) {
        struct lu_task *t = (struct lu_task *)ts;
        t->pin++;
        result = lu_eval(ts, f);
        t->pin--;
    }
    return result;
}

/* _PyEval_BuiltinsFromGlobals. */
static PyObject *
builtins_of(PyObject *globals)
{
    PyObject *b = lu_ns_lookup(globals, LU_ID(__builtins__));
    if (b == NULL) {
        return PyInterpreterState_Get()->builtins;
    }
    return PyModule_CheckExact(b) ? ((PyModuleObject *)b)->md_dict : b;
}

PyObject *
lu_function_new(PyObject *code, PyObject *globals)
{
    PyCodeObject *co = (PyCodeObject *)code;
    PyFunctionObject *op = (PyFunctionObject *)lu_object_new(&PyFunction_Type,
                                                             sizeof(PyFunctionObject));
    if (op == NULL) {
        return NULL;
    }
    op->func_globals = Py_NewRef(globals);
    op->func_builtins = Py_NewRef(builtins_of(globals));
    op->func_name = Py_NewRef(co->co_name);
    op->func_qualname = Py_NewRef(co->co_qualname);
    op->func_code = Py_NewRef(code);
    PyObject *doc = (co->co_flags & CO_HAS_DOCSTRING) ? PyTuple_GET_ITEM(co->co_consts, 0)
                                                      : Py_None;
    op->func_doc = Py_NewRef(doc);
    op->func_module = Py_XNewRef(lu_ns_lookup(globals, LU_ID(__name__)));
    op->vectorcall = function_vectorcall;
    return (PyObject *)op;
}

static void
function_dealloc(PyObject *op)
{
    PyFunctionObject *f = (PyFunctionObject *)op;
    Py_XDECREF(f->func_globals);
    Py_XDECREF(f->func_builtins);
    Py_XDECREF(f->func_name);
    Py_XDECREF(f->func_qualname);
    Py_XDECREF(f->func_code);
    Py_XDECREF(f->func_defaults);
    Py_XDECREF(f->func_kwdefaults);
    Py_XDECREF(f->func_closure);
    Py_XDECREF(f->func_doc);
    Py_XDECREF(f->func_dict);
    Py_XDECREF(f->func_module);
    Py_XDECREF(f->func_annotations);
    Py_XDECREF(f->func_annotate);
    Py_XDECREF(f->func_typeparams);
    lu_object_free(op);
}

/* cell */

PyObject *
PyCell_New(PyObject *obj)
{
    PyCellObject *op = (PyCellObject *)lu_object_new(&PyCell_Type, sizeof(PyCellObject));
    if (op != NULL) {
        op->ob_ref = Py_XNewRef(obj);
    }
    return (PyObject *)op;
}

static void
cell_dealloc(PyObject *op)
{
    Py_XDECREF(((PyCellObject *)op)->ob_ref);
    lu_object_free(op);
}

/* builtin_function_or_method */

#define CFUNC(op) ((PyCFunctionObject *)(op))

static int
no_keywords(PyObject *callable, PyObject *kwnames)
{
    if (kwnames != NULL && PyTuple_GET_SIZE(kwnames) != 0) {
        lu_err_format(PyExc_TypeError, "%.200s() takes no keyword arguments",
                      CFUNC(callable)->m_ml->ml_name);
        return -1;
    }
    return 0;
}

static PyObject *
vectorcall_noargs(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (no_keywords(callable, kwnames) < 0) {
        return NULL;
    }
    if (nargs != 0) {
        lu_err_format(PyExc_TypeError, "%.200s() takes no arguments (%zd given)",
                      CFUNC(callable)->m_ml->ml_name, nargs);
        return NULL;
    }
    return CFUNC(callable)->m_ml->ml_meth(CFUNC(callable)->m_self, NULL);
}

static PyObject *
vectorcall_o(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    if (no_keywords(callable, kwnames) < 0) {
        return NULL;
    }
    if (nargs != 1) {
        lu_err_format(PyExc_TypeError, "%.200s() takes exactly one argument (%zd given)",
                      CFUNC(callable)->m_ml->ml_name, nargs);
        return NULL;
    }
    return CFUNC(callable)->m_ml->ml_meth(CFUNC(callable)->m_self, args[0]);
}

static PyObject *
vectorcall_fastcall(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    if (no_keywords(callable, kwnames) < 0) {
        return NULL;
    }
    _PyCFunctionFast meth = (_PyCFunctionFast)(void (*)(void))CFUNC(callable)->m_ml->ml_meth;
    return meth(CFUNC(callable)->m_self, args, PyVectorcall_NARGS(nargsf));
}

static PyObject *
vectorcall_fastcall_keywords(PyObject *callable, PyObject *const *args, size_t nargsf,
                             PyObject *kwnames)
{
    _PyCFunctionFastWithKeywords meth =
        (_PyCFunctionFastWithKeywords)(void (*)(void))CFUNC(callable)->m_ml->ml_meth;
    return meth(CFUNC(callable)->m_self, args, PyVectorcall_NARGS(nargsf), kwnames);
}

static PyObject *
vectorcall_varargs(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    if (no_keywords(callable, kwnames) < 0) {
        return NULL;
    }
    Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
    PyObject *tuple = PyTuple_New(nargs);
    if (tuple == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < nargs; i++) {
        PyTuple_SET_ITEM(tuple, i, Py_NewRef(args[i]));
    }
    PyMethodDef *ml = CFUNC(callable)->m_ml;
    PyObject *result = (ml->ml_flags & METH_KEYWORDS)
        ? ((PyCFunctionWithKeywords)(void (*)(void))ml->ml_meth)(CFUNC(callable)->m_self, tuple, NULL)
        : ml->ml_meth(CFUNC(callable)->m_self, tuple);
    Py_DECREF(tuple);
    return result;
}

PyObject *
PyCMethod_New(PyMethodDef *ml, PyObject *self, PyObject *module, PyTypeObject *cls)
{
    vectorcallfunc vectorcall;
    switch (ml->ml_flags & (METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O
                            | METH_KEYWORDS | METH_METHOD)) {
    case METH_VARARGS:
    case METH_VARARGS | METH_KEYWORDS: vectorcall = vectorcall_varargs; break;
    case METH_FASTCALL: vectorcall = vectorcall_fastcall; break;
    case METH_FASTCALL | METH_KEYWORDS: vectorcall = vectorcall_fastcall_keywords; break;
    case METH_NOARGS: vectorcall = vectorcall_noargs; break;
    case METH_O: vectorcall = vectorcall_o; break;
    default:
        lu_err_format(PyExc_SystemError, "%s() method: bad call flags", ml->ml_name);
        return NULL;
    }
    if (cls != NULL) {
        lu_err_format(PyExc_SystemError, "PyCMethod_New: METH_METHOD arrives with K1c");
        return NULL;
    }
    PyCFunctionObject *op = (PyCFunctionObject *)lu_object_new(&PyCFunction_Type,
                                                               sizeof(PyCFunctionObject));
    if (op == NULL) {
        return NULL;
    }
    op->m_ml = ml;
    op->m_self = Py_XNewRef(self);
    op->m_module = Py_XNewRef(module);
    op->vectorcall = vectorcall;
    return (PyObject *)op;
}

#undef PyCFunction_NewEx
PyObject *
PyCFunction_NewEx(PyMethodDef *ml, PyObject *self, PyObject *module)
{
    return PyCMethod_New(ml, self, module, NULL);
}

#undef PyCFunction_New
PyObject *
PyCFunction_New(PyMethodDef *ml, PyObject *self)
{
    return PyCMethod_New(ml, self, NULL, NULL);
}

static void
cfunction_dealloc(PyObject *op)
{
    Py_XDECREF(CFUNC(op)->m_self);
    Py_XDECREF(CFUNC(op)->m_module);
    lu_object_free(op);
}

void
lu_function_init(void)
{
    unsigned long flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_VECTORCALL;
    PyFunction_Type.tp_basicsize = sizeof(PyFunctionObject);
    PyFunction_Type.tp_flags |= flags | Py_TPFLAGS_METHOD_DESCRIPTOR;
    PyFunction_Type.tp_vectorcall_offset = offsetof(PyFunctionObject, vectorcall);
    PyFunction_Type.tp_dealloc = function_dealloc;
    PyCFunction_Type.tp_basicsize = sizeof(PyCFunctionObject);
    PyCFunction_Type.tp_flags |= flags;
    PyCFunction_Type.tp_vectorcall_offset = offsetof(PyCFunctionObject, vectorcall);
    PyCFunction_Type.tp_dealloc = cfunction_dealloc;
    PyCell_Type.tp_basicsize = sizeof(PyCellObject);
    PyCell_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyCell_Type.tp_dealloc = cell_dealloc;
}

/* Objects/call.c. */
vectorcallfunc
PyVectorcall_Function(PyObject *callable)
{
    PyTypeObject *tp = Py_TYPE(callable);
    if (!PyType_HasFeature(tp, Py_TPFLAGS_HAVE_VECTORCALL)) {
        return NULL;
    }
    vectorcallfunc f;
    memcpy(&f, (char *)callable + tp->tp_vectorcall_offset, sizeof f);
    return f;
}
