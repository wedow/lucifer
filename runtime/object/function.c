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
    Py_ssize_t total = nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    PyThreadState *ts = PyThreadState_Get();
    PyObject *small[16];
    PyObject **owned = total <= 16 ? small : PyMem_Malloc(total * sizeof(PyObject *));
    if (owned == NULL) {
        return PyErr_NoMemory();
    }
    for (Py_ssize_t i = 0; i < total; i++) {
        owned[i] = Py_NewRef(args[i]);
    }
    struct lu_frame *f = lu_frame_call(ts, func, owned, nargs, kwnames);
    if (owned != small) {
        PyMem_Free(owned);
    }
    if (f == NULL) {
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
    PyMethodDef *ml = CFUNC(callable)->m_ml;
    if (!(ml->ml_flags & METH_KEYWORDS) && no_keywords(callable, kwnames) < 0) {
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
    PyObject *kwargs = NULL;
    Py_ssize_t nkw = kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0;
    if (nkw) {
        kwargs = PyDict_New();
        for (Py_ssize_t i = 0; kwargs != NULL && i < nkw; i++) {
            if (lu_ns_insert(kwargs, PyTuple_GET_ITEM(kwnames, i), args[nargs + i]) < 0) {
                Py_CLEAR(kwargs);
            }
        }
        if (kwargs == NULL) {
            Py_DECREF(tuple);
            return NULL;
        }
    }
    PyObject *result = (ml->ml_flags & METH_KEYWORDS)
        ? ((PyCFunctionWithKeywords)(void (*)(void))ml->ml_meth)(CFUNC(callable)->m_self, tuple, kwargs)
        : ml->ml_meth(CFUNC(callable)->m_self, tuple);
    Py_XDECREF(kwargs);
    Py_DECREF(tuple);
    return result;
}

/* A frame primitive called from C runs its frame in a nested eval loop. */
static PyObject *
vectorcall_frame(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    if (no_keywords(callable, kwnames) < 0) {
        return NULL;
    }
    struct lu_frame *(*prim)(PyThreadState *, PyObject *const *, Py_ssize_t) =
        (struct lu_frame *(*)(PyThreadState *, PyObject *const *, Py_ssize_t))(void (*)(void))
        CFUNC(callable)->m_ml->ml_meth;
    PyThreadState *ts = PyThreadState_Get();
    struct lu_frame *f = prim(ts, args, PyVectorcall_NARGS(nargsf));
    if (f == NULL) {
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

PyObject *
PyCMethod_New(PyMethodDef *ml, PyObject *self, PyObject *module, PyTypeObject *cls)
{
    vectorcallfunc vectorcall;
    switch (ml->ml_flags & (LU_METH_FRAME | METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O
                            | METH_KEYWORDS | METH_METHOD)) {
    case METH_VARARGS:
    case METH_VARARGS | METH_KEYWORDS: vectorcall = vectorcall_varargs; break;
    case METH_FASTCALL: vectorcall = vectorcall_fastcall; break;
    case METH_FASTCALL | METH_KEYWORDS: vectorcall = vectorcall_fastcall_keywords; break;
    case METH_NOARGS: vectorcall = vectorcall_noargs; break;
    case METH_O: vectorcall = vectorcall_o; break;
    case LU_METH_FRAME | METH_FASTCALL: vectorcall = vectorcall_frame; break;
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

/* function attributes over the layout (Objects/funcobject.c func_memberlist
   and func_getsetlist). */

static PyObject *
func_get_name(PyObject *op, void *closure)
{
    return Py_NewRef(((PyFunctionObject *)op)->func_name);
}

static int
func_set_name(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL || !PyUnicode_Check(value)) {
        lu_err_format(PyExc_TypeError, "__name__ must be set to a string object");
        return -1;
    }
    Py_XSETREF(((PyFunctionObject *)op)->func_name, Py_NewRef(value));
    return 0;
}

static PyObject *
func_get_qualname(PyObject *op, void *closure)
{
    return Py_NewRef(((PyFunctionObject *)op)->func_qualname);
}

static int
func_set_qualname(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL || !PyUnicode_Check(value)) {
        lu_err_format(PyExc_TypeError, "__qualname__ must be set to a string object");
        return -1;
    }
    Py_XSETREF(((PyFunctionObject *)op)->func_qualname, Py_NewRef(value));
    return 0;
}

static PyObject *
func_get_code(PyObject *op, void *closure)
{
    return Py_NewRef(((PyFunctionObject *)op)->func_code);
}

static PyObject *
func_get_defaults(PyObject *op, void *closure)
{
    PyObject *d = ((PyFunctionObject *)op)->func_defaults;
    return Py_NewRef(d != NULL ? d : Py_None);
}

static PyObject *
func_get_kwdefaults(PyObject *op, void *closure)
{
    PyObject *d = ((PyFunctionObject *)op)->func_kwdefaults;
    return Py_NewRef(d != NULL ? d : Py_None);
}

static PyMemberDef func_members[] = {
    { "__closure__", _Py_T_OBJECT, offsetof(PyFunctionObject, func_closure), Py_READONLY, NULL },
    { "__doc__", _Py_T_OBJECT, offsetof(PyFunctionObject, func_doc), 0, NULL },
    { "__globals__", _Py_T_OBJECT, offsetof(PyFunctionObject, func_globals), Py_READONLY, NULL },
    { "__module__", _Py_T_OBJECT, offsetof(PyFunctionObject, func_module), 0, NULL },
    { "__builtins__", _Py_T_OBJECT, offsetof(PyFunctionObject, func_builtins), Py_READONLY, NULL },
    { NULL },
};

static PyGetSetDef func_getset[] = {
    { "__code__", func_get_code, NULL, NULL, NULL },
    { "__defaults__", func_get_defaults, NULL, NULL, NULL },
    { "__kwdefaults__", func_get_kwdefaults, NULL, NULL, NULL },
    { "__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL },
    { "__name__", func_get_name, func_set_name, NULL, NULL },
    { "__qualname__", func_get_qualname, func_set_qualname, NULL, NULL },
    { NULL },
};

void
lu_function_init(void)
{
    PyFunction_Type.tp_members = func_members;
    PyFunction_Type.tp_getset = func_getset;
    PyFunction_Type.tp_dictoffset = offsetof(PyFunctionObject, func_dict);
    PyFunction_Type.tp_call = PyVectorcall_Call;
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
