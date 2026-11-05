/* Function, cell and builtin-function layouts (Objects/funcobject.c,
   cellobject.c, methodobject.c, layout halves). A Python function called
   from C runs in a nested eval-loop activation, which pins the task
   (DESIGN 5.2); the interpreter itself calls functions inline. */

#include "interp/code.h"
#include "interp/exceptions.h"
#include "interp/frame.h"

#include <string.h>

/* function */

/* Watchers and versions (FOUNDATIONS 2.4). Kernel functions are the
   runtime's own: they make no events, so a watcher sees what CPython's would. */

static const char *
event_name(PyFunction_WatchEvent event)
{
    switch (event) {
    case PyFunction_EVENT_CREATE: return "PyFunction_EVENT_CREATE";
    case PyFunction_EVENT_DESTROY: return "PyFunction_EVENT_DESTROY";
    case PyFunction_EVENT_MODIFY_CODE: return "PyFunction_EVENT_MODIFY_CODE";
    case PyFunction_EVENT_MODIFY_DEFAULTS: return "PyFunction_EVENT_MODIFY_DEFAULTS";
    case PyFunction_EVENT_MODIFY_KWDEFAULTS: return "PyFunction_EVENT_MODIFY_KWDEFAULTS";
    }
    return "PyFunction_EVENT_?";
}

void
lu_func_notify(PyFunction_WatchEvent event, PyFunctionObject *func, PyObject *new_value)
{
    if (((PyCodeObject *)func->func_code)->co_flags & LU_CO_KERNEL) {
        return;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    for (int i = 0; i < LU_MAX_WATCHERS; i++) {
        PyFunction_WatchCallback cb = (PyFunction_WatchCallback)interp->func_watchers[i];
        if (cb != NULL && cb(event, func, new_value) < 0) {
            char message[256];
            snprintf(message, sizeof message,
                     "Exception ignored in %s watcher callback for function %.100s at %p",
                     event_name(event), PyUnicode_AsUTF8(func->func_qualname), (void *)func);
            lu_err_unraisable_text(message, NULL);
        }
    }
}

void
lu_function_set_version(PyFunctionObject *func)
{
    func->func_version = ((PyCodeObject *)func->func_code)->co_version;
}

/* func_clear_version: a function whose code, defaults or keyword defaults
   changed is never specialized on again (FUNC_VERSION_CLEARED). */
static void
clear_version(PyFunctionObject *func)
{
    if (func->func_version >= 2) {
        __atomic_store_n(&func->func_version, 1, __ATOMIC_RELEASE);
    }
}

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

/* _PyEval_BuiltinsFromGlobals, as a new reference. */
static PyObject *
builtins_of(PyObject *globals)
{
    PyObject *b = lu_ns_get(globals, LU_ID(__builtins__));
    if (b == NULL) {
        return Py_NewRef(PyInterpreterState_Get()->builtins);
    }
    if (PyModule_CheckExact(b)) {
        PyObject *dict = Py_NewRef(((PyModuleObject *)b)->md_dict);
        Py_DECREF(b);
        return dict;
    }
    return b;
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
    op->func_builtins = builtins_of(globals);
    op->func_name = Py_NewRef(co->co_name);
    op->func_qualname = Py_NewRef(co->co_qualname);
    op->func_code = Py_NewRef(code);
    PyObject *doc = (co->co_flags & CO_HAS_DOCSTRING) ? PyTuple_GET_ITEM(co->co_consts, 0)
                                                      : Py_None;
    op->func_doc = Py_NewRef(doc);
    op->func_module = lu_ns_get(globals, LU_ID(__name__));
    op->vectorcall = function_vectorcall;
    if (!(co->co_flags & CO_NESTED) || (co->co_flags & CO_METHOD)) {
        /* cp314t: top-level functions and methods are freed by a collection
           only, which lets inline caches name them (specialize.c). */
        lu_set_deferred((PyObject *)op);
    }
    lu_func_notify(PyFunction_EVENT_CREATE, op, NULL);
    return (PyObject *)op;
}

PyObject *
PyFunction_NewWithQualName(PyObject *code, PyObject *globals, PyObject *qualname)
{
    PyObject *op = lu_function_new(code, globals);
    if (op != NULL && qualname != NULL) {
        Py_SETREF(((PyFunctionObject *)op)->func_qualname, Py_NewRef(qualname));
    }
    return op;
}

PyObject *
PyFunction_New(PyObject *code, PyObject *globals)
{
    return lu_function_new(code, globals);
}

static int
function_traverse(PyObject *op, visitproc visit, void *arg)
{
    PyFunctionObject *f = (PyFunctionObject *)op;
    Py_VISIT(f->func_code);
    Py_VISIT(f->func_globals);
    Py_VISIT(f->func_builtins);
    Py_VISIT(f->func_module);
    Py_VISIT(f->func_defaults);
    Py_VISIT(f->func_kwdefaults);
    Py_VISIT(f->func_doc);
    Py_VISIT(f->func_name);
    Py_VISIT(f->func_dict);
    Py_VISIT(f->func_closure);
    Py_VISIT(f->func_annotations);
    Py_VISIT(f->func_annotate);
    Py_VISIT(f->func_typeparams);
    Py_VISIT(f->func_qualname);
    return 0;
}

/* func_clear: the code stays (it is never NULL), and so do the names, which
   a str subclass could make a cycle, so they become the empty string. */
static int
function_clear(PyObject *op)
{
    PyFunctionObject *f = (PyFunctionObject *)op;
    Py_CLEAR(f->func_globals);
    Py_CLEAR(f->func_builtins);
    Py_CLEAR(f->func_module);
    Py_CLEAR(f->func_defaults);
    Py_CLEAR(f->func_kwdefaults);
    Py_CLEAR(f->func_doc);
    Py_CLEAR(f->func_dict);
    Py_CLEAR(f->func_closure);
    Py_CLEAR(f->func_annotations);
    Py_CLEAR(f->func_annotate);
    Py_CLEAR(f->func_typeparams);
    PyObject *empty = PyUnicode_FromStringAndSize("", 0);
    if (empty != NULL) {
        Py_SETREF(f->func_name, Py_NewRef(empty));
        Py_SETREF(f->func_qualname, empty);
    }
    return 0;
}

static void
function_dealloc(PyObject *op)
{
    PyFunctionObject *f = (PyFunctionObject *)op;
    lu_clear_weakrefs(op);
    lu_resurrect_start(op);
    lu_func_notify(PyFunction_EVENT_DESTROY, f, NULL);
    if (lu_resurrect_end(op) != 0) {
        return;
    }
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

static int
cell_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(((PyCellObject *)op)->ob_ref);
    return 0;
}

static int
cell_clear(PyObject *op)
{
    Py_CLEAR(((PyCellObject *)op)->ob_ref);
    return 0;
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
        char buf[300];
        lu_err_format(PyExc_TypeError, "%.200s takes no keyword arguments",
                      lu_function_str(callable, buf, sizeof buf));
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
        char buf[300];
        lu_err_format(PyExc_TypeError, "%.200s takes no arguments (%zd given)",
                      lu_function_str(callable, buf, sizeof buf), nargs);
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
        char buf[300];
        lu_err_format(PyExc_TypeError, "%.200s takes exactly one argument (%zd given)",
                      lu_function_str(callable, buf, sizeof buf), nargs);
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
            if (PyDict_SetItem(kwargs, PyTuple_GET_ITEM(kwnames, i), args[nargs + i]) < 0) {
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

static int
cfunction_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(CFUNC(op)->m_self);
    Py_VISIT(CFUNC(op)->m_module);
    return 0;
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

/* Replaces a critical field: the event, then the version, then the field.
   `value` is borrowed; NULL clears. */
static void
modify(PyFunctionObject *func, PyFunction_WatchEvent event, PyObject **field, PyObject *value)
{
    lu_func_notify(event, func, value);
    clear_version(func);
    PyObject *old = *field;
    *field = Py_XNewRef(value);
    Py_XDECREF(old);
}

static int
func_set_code(PyObject *op, PyObject *value, void *closure)
{
    PyFunctionObject *func = (PyFunctionObject *)op;
    if (value == NULL || !PyCode_Check(value)) {
        lu_err_format(PyExc_TypeError, "__code__ must be set to a code object");
        return -1;
    }
    int nfree = ((PyCodeObject *)value)->co_nfreevars;
    Py_ssize_t nclosure = func->func_closure == NULL ? 0 : PyTuple_GET_SIZE(func->func_closure);
    if (nclosure != nfree) {
        lu_err_format(PyExc_ValueError, "%.200s() requires a code object with %zd free vars, not %d",
                      PyUnicode_AsUTF8(func->func_name), nclosure, nfree);
        return -1;
    }
    /* CPython warns when the new code is a different kind (generator, coroutine);
       the warnings machinery arrives with its own ticket. */
    modify(func, PyFunction_EVENT_MODIFY_CODE, &func->func_code, value);
    return 0;
}

static int
func_set_defaults(PyObject *op, PyObject *value, void *closure)
{
    if (value == Py_None) {
        value = NULL;
    }
    if (value != NULL && !PyTuple_Check(value)) {
        lu_err_format(PyExc_TypeError, "__defaults__ must be set to a tuple object");
        return -1;
    }
    PyFunctionObject *func = (PyFunctionObject *)op;
    modify(func, PyFunction_EVENT_MODIFY_DEFAULTS, &func->func_defaults, value);
    return 0;
}

static int
func_set_kwdefaults(PyObject *op, PyObject *value, void *closure)
{
    if (value == Py_None) {
        value = NULL;
    }
    if (value != NULL && !PyDict_Check(value)) {
        lu_err_format(PyExc_TypeError, "__kwdefaults__ must be set to a dict object");
        return -1;
    }
    PyFunctionObject *func = (PyFunctionObject *)op;
    modify(func, PyFunction_EVENT_MODIFY_KWDEFAULTS, &func->func_kwdefaults, value);
    return 0;
}

/* The C-API setters (PyFunction_SetDefaults, PyFunction_SetKwDefaults). */

int
PyFunction_SetDefaults(PyObject *op, PyObject *defaults)
{
    if (!PyFunction_Check(op)) {
        lu_err_format(PyExc_SystemError, "bad argument to internal function");
        return -1;
    }
    if (defaults != Py_None && (defaults == NULL || !PyTuple_Check(defaults))) {
        lu_err_format(PyExc_SystemError, "non-tuple default args");
        return -1;
    }
    return func_set_defaults(op, defaults, NULL);
}

int
PyFunction_SetKwDefaults(PyObject *op, PyObject *defaults)
{
    if (!PyFunction_Check(op)) {
        lu_err_format(PyExc_SystemError, "bad argument to internal function");
        return -1;
    }
    if (defaults != Py_None && (defaults == NULL || !PyDict_Check(defaults))) {
        lu_err_format(PyExc_SystemError, "non-dict keyword only default args");
        return -1;
    }
    return func_set_kwdefaults(op, defaults, NULL);
}

/* CPython fires no watcher event for a closure change, only the version
   clear. */
int
PyFunction_SetClosure(PyObject *op, PyObject *closure)
{
    if (!PyFunction_Check(op)) {
        lu_err_format(PyExc_SystemError, "bad argument to internal function");
        return -1;
    }
    if (closure == Py_None) {
        closure = NULL;
    }
    else if (closure == NULL || !PyTuple_Check(closure)) {
        lu_err_format(PyExc_SystemError, "expected tuple for closure, got '%.100s'",
                      closure != NULL ? Py_TYPE(closure)->tp_name : "NULL");
        return -1;
    }
    PyFunctionObject *func = (PyFunctionObject *)op;
    clear_version(func);
    Py_XSETREF(func->func_closure, Py_XNewRef(closure));
    return 0;
}

#define FUNCTION_FIELD(name, field) \
    PyObject * \
    PyFunction_Get##name(PyObject *op) \
    { \
        if (!PyFunction_Check(op)) { \
            lu_err_format(PyExc_SystemError, "bad argument to internal function"); \
            return NULL; \
        } \
        return ((PyFunctionObject *)op)->field; \
    }
FUNCTION_FIELD(Code, func_code)
FUNCTION_FIELD(Globals, func_globals)
FUNCTION_FIELD(Module, func_module)
FUNCTION_FIELD(Defaults, func_defaults)
FUNCTION_FIELD(KwDefaults, func_kwdefaults)
FUNCTION_FIELD(Closure, func_closure)

/* The watcher API (funcobject.c). */

static lu_watcher *
func_watchers(void)
{
    return PyInterpreterState_Get()->func_watchers;
}

int
PyFunction_AddWatcher(PyFunction_WatchCallback callback)
{
    return lu_watcher_add(func_watchers(), 0, (lu_watcher)callback, "func");
}

int
PyFunction_ClearWatcher(int id)
{
    if (lu_watcher_check(func_watchers(), id, "invalid func watcher ID %d",
                         "no func watcher set for ID %d") < 0) {
        return -1;
    }
    func_watchers()[id] = NULL;
    return 0;
}

/* __annotations__ and __annotate__ (Objects/funcobject.c): the annotations
   are a dict, a tuple of pairs a MAKE_FUNCTION flag stored, or built on first
   use by calling __annotate__(1). */

static PyObject *
func_annotation_dict(PyFunctionObject *op)
{
    if (op->func_annotations == NULL) {
        if (op->func_annotate == NULL || !PyCallable_Check(op->func_annotate)) {
            Py_RETURN_NONE;
        }
        PyObject *one = PyLong_FromLong(1);
        PyObject *ann = one != NULL ? lu_call(op->func_annotate, &one, 1, NULL) : NULL;
        Py_XDECREF(one);
        if (ann != NULL && !PyDict_Check(ann)) {
            lu_err_format(PyExc_TypeError, "__annotate__ returned non-dict of type '%.100s'",
                          Py_TYPE(ann)->tp_name);
            Py_CLEAR(ann);
        }
        if (ann != NULL) {
            Py_XSETREF(op->func_annotations, Py_NewRef(ann));
        }
        return ann;
    }
    if (PyTuple_CheckExact(op->func_annotations)) {
        PyObject *pairs = op->func_annotations;
        PyObject *ann = PyDict_New();
        for (Py_ssize_t i = 0; ann != NULL && i < PyTuple_GET_SIZE(pairs); i += 2) {
            if (PyDict_SetItem(ann, PyTuple_GET_ITEM(pairs, i), PyTuple_GET_ITEM(pairs, i + 1)) < 0) {
                Py_CLEAR(ann);
            }
        }
        if (ann == NULL) {
            return NULL;
        }
        Py_SETREF(op->func_annotations, ann);
    }
    return Py_NewRef(op->func_annotations);
}

PyObject *
PyFunction_GetAnnotations(PyObject *op)
{
    if (!PyFunction_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    PyObject *ann = func_annotation_dict((PyFunctionObject *)op);
    Py_XDECREF(ann);                    /* borrowed, as the C-API returns it */
    return ann;
}

static PyObject *
func_get_annotations(PyObject *op, void *closure)
{
    PyFunctionObject *f = (PyFunctionObject *)op;
    if (f->func_annotations == NULL && (f->func_annotate == NULL || !PyCallable_Check(f->func_annotate))) {
        f->func_annotations = PyDict_New();
        if (f->func_annotations == NULL) {
            return NULL;
        }
    }
    return func_annotation_dict(f);
}

static int
func_set_annotations(PyObject *op, PyObject *value, void *closure)
{
    PyFunctionObject *f = (PyFunctionObject *)op;
    if (value == Py_None) {
        value = NULL;
    }
    if (value != NULL && !PyDict_Check(value)) {
        lu_err_format(PyExc_TypeError, "__annotations__ must be set to a dict object");
        return -1;
    }
    Py_XSETREF(f->func_annotations, Py_XNewRef(value));
    Py_CLEAR(f->func_annotate);
    return 0;
}

static PyObject *
func_get_annotate(PyObject *op, void *closure)
{
    PyObject *a = ((PyFunctionObject *)op)->func_annotate;
    return Py_NewRef(a != NULL ? a : Py_None);
}

static int
func_set_annotate(PyObject *op, PyObject *value, void *closure)
{
    PyFunctionObject *f = (PyFunctionObject *)op;
    if (value == NULL) {
        lu_err_format(PyExc_TypeError, "__annotate__ cannot be deleted");
        return -1;
    }
    if (value != Py_None && !PyCallable_Check(value)) {
        lu_err_format(PyExc_TypeError, "__annotate__ must be callable or None");
        return -1;
    }
    Py_XSETREF(f->func_annotate, Py_NewRef(value));
    if (value != Py_None) {
        Py_CLEAR(f->func_annotations);
    }
    return 0;
}

static PyObject *
func_get_type_params(PyObject *op, void *closure)
{
    PyObject *t = ((PyFunctionObject *)op)->func_typeparams;
    return t != NULL ? Py_NewRef(t) : PyTuple_New(0);
}

static int
func_set_type_params(PyObject *op, PyObject *value, void *closure)
{
    if (value == NULL || !PyTuple_Check(value)) {
        lu_err_format(PyExc_TypeError, "__type_params__ must be set to a tuple");
        return -1;
    }
    Py_XSETREF(((PyFunctionObject *)op)->func_typeparams, Py_NewRef(value));
    return 0;
}

/* function_new(code, globals, name, defaults, closure, kwdefaults): func_new_impl. */
PyObject *
lu_function_from_args(PyObject *const *a)
{
    PyObject *code = a[0], *globals = a[1], *name = a[2], *defaults = a[3], *closure = a[4], *kwdefaults = a[5];
    if (!PyCode_Check(code) || !PyDict_Check(globals)) {
        PyObject *bad = !PyCode_Check(code) ? code : globals;
        lu_err_format(PyExc_TypeError, "function() argument '%s' must be %s, not %.200s",
                      bad == code ? "code" : "globals", bad == code ? "code" : "dict", Py_TYPE(bad)->tp_name);
        return NULL;
    }
    int nfree = ((PyCodeObject *)code)->co_nfreevars;
    const char *bad = name != Py_None && !PyUnicode_Check(name) ? "arg 3 (name) must be None or string"
        : defaults != Py_None && !PyTuple_Check(defaults) ? "arg 4 (defaults) must be None or tuple"
        : !PyTuple_Check(closure) && nfree && closure == Py_None ? "arg 5 (closure) must be tuple"
        : !PyTuple_Check(closure) && closure != Py_None ? "arg 5 (closure) must be None or tuple"
        : kwdefaults != Py_None && !PyDict_Check(kwdefaults) ? "arg 6 (kwdefaults) must be None or dict"
        : NULL;
    if (bad != NULL) {
        lu_err_format(PyExc_TypeError, "%s", bad);
        return NULL;
    }
    Py_ssize_t nclosure = closure == Py_None ? 0 : PyTuple_GET_SIZE(closure);
    if (nfree != nclosure) {
        lu_err_format(PyExc_ValueError, "%s requires closure of length %d, not %zd",
                      PyUnicode_AsUTF8(((PyCodeObject *)code)->co_name), nfree, nclosure);
        return NULL;
    }
    for (Py_ssize_t i = 0; i < nclosure; i++) {
        PyObject *cell = PyTuple_GET_ITEM(closure, i);
        if (!PyCell_Check(cell)) {
            lu_err_format(PyExc_TypeError, "arg 5 (closure) expected cell, found %s", Py_TYPE(cell)->tp_name);
            return NULL;
        }
    }
    PyFunctionObject *f = (PyFunctionObject *)lu_function_new(code, globals);
    if (f == NULL) {
        return NULL;
    }
    if (name != Py_None) {
        Py_SETREF(f->func_name, Py_NewRef(name));
    }
    f->func_defaults = defaults != Py_None ? Py_NewRef(defaults) : NULL;
    f->func_closure = closure != Py_None ? Py_NewRef(closure) : NULL;
    f->func_kwdefaults = kwdefaults != Py_None ? Py_NewRef(kwdefaults) : NULL;
    return (PyObject *)f;
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
    { "__code__", func_get_code, func_set_code, NULL, NULL },
    { "__defaults__", func_get_defaults, func_set_defaults, NULL, NULL },
    { "__kwdefaults__", func_get_kwdefaults, func_set_kwdefaults, NULL, NULL },
    { "__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL },
    { "__name__", func_get_name, func_set_name, NULL, NULL },
    { "__qualname__", func_get_qualname, func_set_qualname, NULL, NULL },
    { "__annotations__", func_get_annotations, func_set_annotations, NULL, NULL },
    { "__annotate__", func_get_annotate, func_set_annotate, NULL, NULL },
    { "__type_params__", func_get_type_params, func_set_type_params, NULL, NULL },
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
    PyFunction_Type.tp_traverse = function_traverse;
    PyFunction_Type.tp_clear = function_clear;
    PyFunction_Type.tp_weaklistoffset = offsetof(PyFunctionObject, func_weakreflist);
    PyCFunction_Type.tp_basicsize = sizeof(PyCFunctionObject);
    PyCFunction_Type.tp_flags |= flags;
    PyCFunction_Type.tp_vectorcall_offset = offsetof(PyCFunctionObject, vectorcall);
    PyCFunction_Type.tp_dealloc = cfunction_dealloc;
    PyCFunction_Type.tp_traverse = cfunction_traverse;
    PyCell_Type.tp_basicsize = sizeof(PyCellObject);
    PyCell_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyCell_Type.tp_dealloc = cell_dealloc;
    PyCell_Type.tp_traverse = cell_traverse;
    PyCell_Type.tp_clear = cell_clear;
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
