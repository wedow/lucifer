/* Code objects (Objects/codeobject.c _PyCode_New, layout half): the exact
   PyCodeObject of cpython/code.h, bytecode copied into co_code_adaptive.
   Instrumentation starts at version 0 with no monitoring data, which equals
   an interpreter that never enabled monitoring (FOUNDATIONS 2.3). */

#include "jit/jit.h"
#include "interp/code.h"
#include "interp/instrument.h"
#include "interp/exceptions.h"
#include "interp/frame.h"
#include "object/set.h"

#include "opcode.h"
#include "opcode_ids.h"

#include <math.h>
#include <string.h>

/* Code watchers (codeobject.c): CREATE once a code object is built, DESTROY as
   it dies. The kernel's own code is made before any watcher exists and makes
   no events. */

static const char *
event_name(PyCodeEvent event)
{
    return event == PY_CODE_EVENT_CREATE ? "PY_CODE_EVENT_CREATE" : "PY_CODE_EVENT_DESTROY";
}

void
lu_code_notify(PyCodeEvent event, PyCodeObject *co)
{
    if (co->co_flags & LU_CO_KERNEL) {
        return;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    for (int i = 0; i < LU_MAX_WATCHERS; i++) {
        PyCode_WatchCallback cb = (PyCode_WatchCallback)interp->code_watchers[i];
        if (cb != NULL && cb(event, co) < 0) {
            char message[96];
            snprintf(message, sizeof message, "Exception ignored in %s watcher callback for",
                     event_name(event));
            lu_err_unraisable_text(message, (PyObject *)co);
        }
    }
}

static lu_watcher *
code_watchers(void)
{
    return PyInterpreterState_Get()->code_watchers;
}

int
PyCode_AddWatcher(PyCode_WatchCallback callback)
{
    return lu_watcher_add(code_watchers(), 0, (lu_watcher)callback, "code");
}

int
PyCode_ClearWatcher(int id)
{
    if (lu_watcher_check(code_watchers(), id, "Invalid code watcher ID %d",
                         "No code watcher set for ID %d") < 0) {
        return -1;
    }
    code_watchers()[id] = NULL;
    return 0;
}

/* The holder of the "extra" slots, and the API over it, live below
   code_traverse; dealloc only frees what they filled. */
typedef struct {
    Py_ssize_t ce_size;
    void *ce_extras[1];
} _PyCodeObjectExtra;

static void
code_dealloc(PyObject *op)
{
    PyCodeObject *co = (PyCodeObject *)op;
    lu_clear_weakrefs(op);
    lu_resurrect_start(op);
    lu_code_notify(PY_CODE_EVENT_DESTROY, co);
    if (lu_resurrect_end(op) != 0) {
        return;
    }
    Py_XDECREF(co->co_consts);
    Py_XDECREF(co->co_names);
    Py_XDECREF(co->co_exceptiontable);
    Py_XDECREF(co->co_localsplusnames);
    Py_XDECREF(co->co_localspluskinds);
    Py_XDECREF(co->co_filename);
    Py_XDECREF(co->co_name);
    Py_XDECREF(co->co_qualname);
    Py_XDECREF(co->co_linetable);
    if (co->co_extra != NULL) {
        PyInterpreterState *interp = PyInterpreterState_Get();
        _PyCodeObjectExtra *extra = co->co_extra;
        for (Py_ssize_t i = 0; i < extra->ce_size; i++) {
            if (extra->ce_extras[i] != NULL && interp->co_extra_freefuncs[i] != NULL) {
                interp->co_extra_freefuncs[i](extra->ce_extras[i]);
            }
        }
        PyMem_Free(extra);
    }
    lu_monitoring_free(co);
    lu_jit_code_free(co);
    lu_object_free(op);
}

/* co_consts is the only reference a code object holds that can be in a cycle. */
static int
code_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(((PyCodeObject *)op)->co_consts);
    return 0;
}

/* The "extra" slots of PEP 523 (Objects/codeobject.c): an index each user
   takes once, a per-code array of their void*, and a free function each. */

Py_ssize_t
PyUnstable_Eval_RequestCodeExtraIndex(freefunc free)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    PyMutex_Lock(&interp->co_extra_lock);
    Py_ssize_t index = -1;
    if (interp->co_extra_user_count < MAX_CO_EXTRA_USERS - 1) {
        index = interp->co_extra_user_count;
        interp->co_extra_freefuncs[index] = free;
        interp->co_extra_user_count = index + 1;
    }
    PyMutex_Unlock(&interp->co_extra_lock);
    return index;
}

int
PyUnstable_Code_GetExtra(PyObject *code, Py_ssize_t index, void **extra)
{
    if (!PyCode_Check(code)) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyCodeObject *co = (PyCodeObject *)code;
    _PyCodeObjectExtra *ce = co->co_extra;
    *extra = index >= 0 && ce != NULL && index < ce->ce_size ? ce->ce_extras[index] : NULL;
    return 0;
}

int
PyUnstable_Code_SetExtra(PyObject *code, Py_ssize_t index, void *extra)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    if (!PyCode_Check(code) || index < 0 || index >= interp->co_extra_user_count) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyCodeObject *co = (PyCodeObject *)code;
    PyMutex_Lock(&interp->co_extra_lock);
    _PyCodeObjectExtra *ce = co->co_extra;
    if (ce == NULL || index >= ce->ce_size) {
        Py_ssize_t size = index + 1;
        ce = PyMem_Realloc(ce, sizeof(*ce) + (size - 1) * sizeof(void *));
        if (ce == NULL) {
            PyMutex_Unlock(&interp->co_extra_lock);
            PyErr_NoMemory();
            return -1;
        }
        for (Py_ssize_t i = ce->ce_size; i < size; i++) {
            ce->ce_extras[i] = NULL;
        }
        ce->ce_size = size;
        co->co_extra = ce;
    }
    if (ce->ce_extras[index] != NULL && interp->co_extra_freefuncs[index] != NULL) {
        interp->co_extra_freefuncs[index](ce->ce_extras[index]);
    }
    ce->ce_extras[index] = extra;
    PyMutex_Unlock(&interp->co_extra_lock);
    return 0;
}

static int code_same(PyCodeObject *a, PyCodeObject *b);

/* co_varnames, co_cellvars and co_freevars: the names of one kind. */
static PyObject *
code_names_of_kind(PyCodeObject *co, int select)
{
    const char *kinds = PyBytes_AS_STRING(co->co_localspluskinds);
    Py_ssize_t n = 0;
    for (Py_ssize_t i = 0; i < co->co_nlocalsplus; i++) {
        n += (kinds[i] & select) != 0;
    }
    PyObject *tuple = PyTuple_New(n);
    for (Py_ssize_t i = 0, k = 0; tuple != NULL && i < co->co_nlocalsplus; i++) {
        if (kinds[i] & select) {
            PyTuple_SET_ITEM(tuple, k++, Py_NewRef(PyTuple_GET_ITEM(co->co_localsplusnames, i)));
        }
    }
    return tuple;
}

static PyObject *
code_get_varnames(PyObject *co, void *closure)
{
    return code_names_of_kind((PyCodeObject *)co, CO_FAST_LOCAL);
}

/* PyCode_GetCode / PyCode_GetVarnames: new references to the code object's
   own views (Objects/codeobject.c). */
PyObject *
PyCode_GetCode(PyCodeObject *co)
{
    return lu_code_bytes(co);
}

PyObject *
PyCode_GetVarnames(PyCodeObject *co)
{
    return code_names_of_kind(co, CO_FAST_LOCAL);
}

PyObject *
PyCode_GetCellvars(PyCodeObject *co)
{
    return code_names_of_kind(co, CO_FAST_CELL);
}

PyObject *
PyCode_GetFreevars(PyCodeObject *co)
{
    return code_names_of_kind(co, CO_FAST_FREE);
}

static PyObject *
code_get_cellvars(PyObject *co, void *closure)
{
    return code_names_of_kind((PyCodeObject *)co, CO_FAST_CELL);
}

static PyObject *
code_get_freevars(PyObject *co, void *closure)
{
    return code_names_of_kind((PyCodeObject *)co, CO_FAST_FREE);
}

static PyObject *
code_get_code(PyObject *co, void *closure)
{
    return lu_code_bytes((PyCodeObject *)co);
}

static PyGetSetDef code_getset[] = {
    { "co_code", code_get_code, NULL, NULL, NULL },
    { "co_varnames", code_get_varnames, NULL, NULL, NULL },
    { "co_cellvars", code_get_cellvars, NULL, NULL, NULL },
    { "co_freevars", code_get_freevars, NULL, NULL, NULL },
    { NULL },
};

/* The attributes the exception machinery, kernel and compiler read; co_lines,
   co_positions and replace arrive with the code object's own ticket. */
static PyMemberDef code_members[] = {
    { "co_argcount", Py_T_INT, offsetof(PyCodeObject, co_argcount), Py_READONLY, NULL },
    { "co_posonlyargcount", Py_T_INT, offsetof(PyCodeObject, co_posonlyargcount), Py_READONLY, NULL },
    { "co_kwonlyargcount", Py_T_INT, offsetof(PyCodeObject, co_kwonlyargcount), Py_READONLY, NULL },
    { "co_stacksize", Py_T_INT, offsetof(PyCodeObject, co_stacksize), Py_READONLY, NULL },
    { "co_nlocals", Py_T_INT, offsetof(PyCodeObject, co_nlocals), Py_READONLY, NULL },
    { "co_consts", _Py_T_OBJECT, offsetof(PyCodeObject, co_consts), Py_READONLY, NULL },
    { "co_names", _Py_T_OBJECT, offsetof(PyCodeObject, co_names), Py_READONLY, NULL },
    { "co_linetable", _Py_T_OBJECT, offsetof(PyCodeObject, co_linetable), Py_READONLY, NULL },
    { "co_exceptiontable", _Py_T_OBJECT, offsetof(PyCodeObject, co_exceptiontable), Py_READONLY, NULL },
    { "co_name", _Py_T_OBJECT, offsetof(PyCodeObject, co_name), Py_READONLY, NULL },
    { "co_qualname", _Py_T_OBJECT, offsetof(PyCodeObject, co_qualname), Py_READONLY, NULL },
    { "co_filename", _Py_T_OBJECT, offsetof(PyCodeObject, co_filename), Py_READONLY, NULL },
    { "co_firstlineno", Py_T_INT, offsetof(PyCodeObject, co_firstlineno), Py_READONLY, NULL },
    { "co_flags", Py_T_INT, offsetof(PyCodeObject, co_flags), Py_READONLY, NULL },
    { NULL },
};

void
lu_code_init(void)
{
    PyCode_Type.tp_members = code_members;
    PyCode_Type.tp_getset = code_getset;
    PyCode_Type.tp_basicsize = offsetof(PyCodeObject, co_code_adaptive);
    PyCode_Type.tp_itemsize = sizeof(lu_codeunit);
    PyCode_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyCode_Type.tp_weaklistoffset = offsetof(PyCodeObject, co_weakreflist);
    PyCode_Type.tp_dealloc = code_dealloc;
    PyCode_Type.tp_traverse = code_traverse;
}

/* intern_strings: the items of a names tuple become the interned strings, in place. */
static void
intern_strings(PyObject *tuple)
{
    for (Py_ssize_t i = PyTuple_GET_SIZE(tuple); --i >= 0; ) {
        lu_intern(PyInterpreterState_Get(), &PyTuple_GET_ITEM(tuple, i));
    }
}

/* intern_constants for the free-threaded build, which interns every string
   constant: in tuples, in place, and in frozensets, which are rebuilt when an
   item was replaced. *modified is set when an item of this tuple changed. */
static int
intern_constants(PyObject *tuple, int *modified)
{
    for (Py_ssize_t i = PyTuple_GET_SIZE(tuple); --i >= 0; ) {
        PyObject *v = PyTuple_GET_ITEM(tuple, i);
        if (PyUnicode_CheckExact(v)) {
            lu_intern(PyInterpreterState_Get(), &PyTuple_GET_ITEM(tuple, i));
            if (PyTuple_GET_ITEM(tuple, i) != v && modified != NULL) {
                *modified = 1;
            }
        }
        else if (PyTuple_CheckExact(v)) {
            if (intern_constants(v, NULL) < 0) {
                return -1;
            }
        }
        else if (PyFrozenSet_CheckExact(v)) {
            PySetObject *so = (PySetObject *)v;
            PyObject *items = PyTuple_New(PySet_GET_SIZE(v));
            Py_ssize_t n = 0;
            for (Py_ssize_t k = 0; items != NULL && k <= so->mask; k++) {
                PyObject *key = so->table[k].key;
                if (key != NULL && key != LU_SET_DUMMY && n < PyTuple_GET_SIZE(items)) {
                    PyTuple_SET_ITEM(items, n++, Py_NewRef(key));
                }
            }
            int changed = 0;
            if (items == NULL || intern_constants(items, &changed) < 0) {
                Py_XDECREF(items);
                return -1;
            }
            if (changed) {
                PyObject *set = lu_set_new(&PyFrozenSet_Type);
                for (Py_ssize_t k = 0; set != NULL && k < n; k++) {
                    if (lu_set_add(set, PyTuple_GET_ITEM(items, k)) < 0) {
                        Py_CLEAR(set);
                    }
                }
                if (set == NULL) {
                    Py_DECREF(items);
                    return -1;
                }
                Py_SETREF(PyTuple_GET_ITEM(tuple, i), set);
                if (modified != NULL) {
                    *modified = 1;
                }
            }
            Py_DECREF(items);
        }
    }
    return 0;
}

PyObject *
lu_code_new(const struct lu_code_fields *c)
{
    if (!PyBytes_CheckExact(c->code) || PyBytes_GET_SIZE(c->code) % sizeof(lu_codeunit)
        || !PyTuple_CheckExact(c->consts) || !PyTuple_CheckExact(c->names)
        || !PyTuple_CheckExact(c->localsplusnames) || !PyBytes_CheckExact(c->localspluskinds)
        || PyTuple_GET_SIZE(c->localsplusnames) != PyBytes_GET_SIZE(c->localspluskinds)
        || !PyUnicode_CheckExact(c->filename) || !PyUnicode_CheckExact(c->name)
        || !PyUnicode_CheckExact(c->qualname) || !PyBytes_CheckExact(c->linetable)
        || !PyBytes_CheckExact(c->exceptiontable)
        || c->argcount < c->posonlyargcount || c->posonlyargcount < 0
        || c->kwonlyargcount < 0 || c->stacksize < 0) {
        lu_err_format(PyExc_SystemError, "bad argument to internal function");
        return NULL;
    }
    intern_strings(c->names);
    intern_strings(c->localsplusnames);
    if (intern_constants(c->consts, NULL) < 0) {
        return NULL;
    }
    Py_ssize_t nbytes = PyBytes_GET_SIZE(c->code);
    PyCodeObject *co = (PyCodeObject *)lu_object_new(&PyCode_Type,
        offsetof(PyCodeObject, co_code_adaptive) + nbytes);
    if (co == NULL) {
        return NULL;
    }
    /* Deferred reference counting: frames count their code, but a count that
       reaches zero frees nothing; only a collection does. */
    lu_set_deferred((PyObject *)co);
    Py_SET_SIZE(co, nbytes / (Py_ssize_t)sizeof(lu_codeunit));
    co->co_consts = Py_NewRef(c->consts);
    co->co_names = Py_NewRef(c->names);
    co->co_exceptiontable = Py_NewRef(c->exceptiontable);
    co->co_flags = c->flags;
    co->co_argcount = c->argcount;
    co->co_posonlyargcount = c->posonlyargcount;
    co->co_kwonlyargcount = c->kwonlyargcount;
    co->co_stacksize = c->stacksize != 0 ? c->stacksize : 1;     /* init_code: a frame has a stack slot */
    co->co_firstlineno = c->firstlineno;
    co->co_localsplusnames = Py_NewRef(c->localsplusnames);
    co->co_localspluskinds = Py_NewRef(c->localspluskinds);
    co->co_filename = Py_NewRef(c->filename);
    co->co_name = Py_NewRef(c->name);
    co->co_qualname = Py_NewRef(c->qualname);
    lu_intern(PyInterpreterState_Get(), &co->co_filename);
    lu_intern(PyInterpreterState_Get(), &co->co_name);
    lu_intern(PyInterpreterState_Get(), &co->co_qualname);
    co->co_linetable = Py_NewRef(c->linetable);
    co->_co_unique_id = -1;
    int nlocalsplus = (int)PyTuple_GET_SIZE(c->localsplusnames);
    const char *kinds = PyBytes_AS_STRING(c->localspluskinds);
    for (int i = 0; i < nlocalsplus; i++) {
        co->co_nlocals += (kinds[i] & CO_FAST_LOCAL) != 0;
        co->co_ncellvars += (kinds[i] & CO_FAST_CELL) != 0;
        co->co_nfreevars += (kinds[i] & CO_FAST_FREE) != 0;
    }
    co->co_nlocalsplus = nlocalsplus;
    co->co_framesize = nlocalsplus + co->co_stacksize + (int)LU_FRAME_SPECIALS;
    memcpy(co->co_code_adaptive, PyBytes_AS_STRING(c->code), nbytes);
    /* Instrumentation starts at the first RESUME. */
    while (co->_co_firsttraceable < Py_SIZE(co)
           && lu_code_start(co)[co->_co_firsttraceable].op.code != RESUME) {
        co->_co_firsttraceable++;
    }
    PyInterpreterState *interp = PyInterpreterState_Get();
    co->co_version = interp->next_func_version;
    if (co->co_version != 0) {
        interp->next_func_version++;            /* stays 0 once spent */
    }
    lu_code_notify(PY_CODE_EVENT_CREATE, co);
    return (PyObject *)co;
}

/* PyCode_NewEmpty: a code object that raises AssertionError. */
PyCodeObject *
PyCode_NewEmpty(const char *filename, const char *funcname, int firstlineno)
{
    static const char assert0[6] = { RESUME, 0, LOAD_COMMON_CONSTANT, 0, RAISE_VARARGS, 1 };
    static const char linetable[2] = { (char)((1 << 7) | (PY_CODE_LOCATION_INFO_NO_COLUMNS << 3) | 2), 0 };
    PyObject *code = PyBytes_FromStringAndSize(assert0, sizeof assert0);
    PyObject *lines = PyBytes_FromStringAndSize(linetable, sizeof linetable);
    PyObject *empty_bytes = PyBytes_FromStringAndSize("", 0);
    PyObject *empty_tuple = PyTuple_New(0);
    PyObject *file = PyUnicode_FromString(filename);
    PyObject *name = PyUnicode_FromString(funcname);
    PyObject *result = NULL;
    if (code && lines && empty_bytes && empty_tuple && file && name) {
        struct lu_code_fields fields = {
            .filename = file, .name = name, .qualname = name,
            .flags = CO_OPTIMIZED | CO_NEWLOCALS, .code = code, .firstlineno = firstlineno,
            .linetable = lines, .consts = empty_tuple, .names = empty_tuple,
            .localsplusnames = empty_tuple, .localspluskinds = empty_bytes,
            .stacksize = 1, .exceptiontable = empty_bytes,
        };
        result = lu_code_new(&fields);
    }
    Py_XDECREF(code);
    Py_XDECREF(lines);
    Py_XDECREF(empty_bytes);
    Py_XDECREF(empty_tuple);
    Py_XDECREF(file);
    Py_XDECREF(name);
    return (PyCodeObject *)result;
}

/* code_new(filename, name, qualname, flags, code, firstlineno, linetable,
   consts, names, localsplusnames, localspluskinds, argcount, posonlyargcount,
   kwonlyargcount, stacksize, exceptiontable): _PyCode_New with exactly these
   fields, which the compiler's assembler has computed (types.CodeType would
   recompute some of them). */
static PyObject *
prim_code_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long ints[6];
    static const int int_args[6] = { 3, 5, 11, 12, 13, 14 };
    if (nargs != 16) {
        lu_err_format(PyExc_TypeError, "code_new() takes 16 arguments");
        return NULL;
    }
    for (int i = 0; i < 6; i++) {
        if (!PyLong_CheckExact(args[int_args[i]]) || lu_long_as_long(args[int_args[i]], &ints[i]) < 0) {
            PyErr_Clear();
            lu_err_format(PyExc_TypeError, "code_new(): argument %d must be an int", int_args[i] + 1);
            return NULL;
        }
    }
    struct lu_code_fields c = {
        .filename = args[0], .name = args[1], .qualname = args[2], .flags = (int)ints[0],
        .code = args[4], .firstlineno = (int)ints[1], .linetable = args[6], .consts = args[7],
        .names = args[8], .localsplusnames = args[9], .localspluskinds = args[10],
        .argcount = (int)ints[2], .posonlyargcount = (int)ints[3], .kwonlyargcount = (int)ints[4],
        .stacksize = (int)ints[5], .exceptiontable = args[15],
    };
    return lu_code_new(&c);
}

/* code_localsplus(code): (co_localsplusnames, co_localspluskinds) */
static PyObject *
prim_code_localsplus(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1 || !PyCode_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "code_localsplus(code)");
        return NULL;
    }
    PyCodeObject *co = (PyCodeObject *)args[0];
    return PyTuple_Pack(2, co->co_localsplusnames, co->co_localspluskinds);
}

/* code_set_localsplusnames(code, names): Py_SETREF(co_localsplusnames), which
   codegen_finish_annotations_scope does to rename the __annotate__ parameter. */
static PyObject *
prim_code_set_localsplusnames(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2 || !PyCode_Check(args[0]) || !PyTuple_CheckExact(args[1])
        || PyTuple_GET_SIZE(args[1]) != PyTuple_GET_SIZE(((PyCodeObject *)args[0])->co_localsplusnames)) {
        lu_err_format(PyExc_TypeError, "code_set_localsplusnames(code, names of the same length)");
        return NULL;
    }
    Py_SETREF(((PyCodeObject *)args[0])->co_localsplusnames, Py_NewRef(args[1]));
    return Py_NewRef(Py_None);
}

/* Whether two constants are interchangeable, by _PyCode_ConstantKey: of the
   same exact type and equal, with the zeros of floats and complex numbers
   told apart and NaNs equal only to themselves. */
static int
const_equal(PyObject *a, PyObject *b)
{
    if (a == b) {
        return !PyFloat_CheckExact(a) || ((PyFloatObject *)a)->ob_fval == ((PyFloatObject *)a)->ob_fval;
    }
    if (Py_TYPE(a) != Py_TYPE(b)) {
        return 0;
    }
    if (PyFloat_CheckExact(a)) {
        double x = ((PyFloatObject *)a)->ob_fval, y = ((PyFloatObject *)b)->ob_fval;
        return x == y && signbit(x) == signbit(y);
    }
    if (PyComplex_CheckExact(a)) {
        Py_complex x = ((PyComplexObject *)a)->cval, y = ((PyComplexObject *)b)->cval;
        return x.real == y.real && x.imag == y.imag && signbit(x.real) == signbit(y.real)
            && signbit(x.imag) == signbit(y.imag);
    }
    if (PyTuple_CheckExact(a)) {
        if (PyTuple_GET_SIZE(a) != PyTuple_GET_SIZE(b)) {
            return 0;
        }
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(a); i++) {
            if (!const_equal(PyTuple_GET_ITEM(a, i), PyTuple_GET_ITEM(b, i))) {
                return 0;
            }
        }
        return 1;
    }
    if (PyFrozenSet_CheckExact(a)) {
        PySetObject *x = (PySetObject *)a, *y = (PySetObject *)b;
        if (x->used != y->used) {
            return 0;
        }
        for (Py_ssize_t i = 0; i <= x->mask; i++) {
            PyObject *key = x->table[i].key;
            int found = 0;
            for (Py_ssize_t j = 0; key != NULL && key != LU_SET_DUMMY && j <= y->mask && !found; j++) {
                found = y->table[j].key != NULL && y->table[j].key != LU_SET_DUMMY
                    && const_equal(key, y->table[j].key);
            }
            if (key != NULL && key != LU_SET_DUMMY && !found) {
                return 0;
            }
        }
        return 1;
    }
    if (PySlice_Check(a)) {
        PySliceObject *x = (PySliceObject *)a, *y = (PySliceObject *)b;
        return const_equal(x->start, y->start) && const_equal(x->stop, y->stop) && const_equal(x->step, y->step);
    }
    if (PyCode_Check(a)) {
        return code_same((PyCodeObject *)a, (PyCodeObject *)b);
    }
    if (PyLong_CheckExact(a) || PyUnicode_CheckExact(a) || PyBytes_CheckExact(a)) {
        return lu_eq(a, b) > 0;
    }
    return 0;
}

/* code_richcompare's equality: the same name, arguments, flags, first line,
   bytecode, constants, names, locals and tables. */
static int
code_same(PyCodeObject *a, PyCodeObject *b)
{
    if (a == b) {
        return 1;
    }
    if (a->co_argcount != b->co_argcount || a->co_posonlyargcount != b->co_posonlyargcount
        || a->co_kwonlyargcount != b->co_kwonlyargcount
        || (a->co_flags & ~(LU_CO_KERNEL | LU_CO_HIDDEN)) != (b->co_flags & ~(LU_CO_KERNEL | LU_CO_HIDDEN))
        || a->co_firstlineno != b->co_firstlineno || Py_SIZE(a) != Py_SIZE(b)
        || !lu_str_eq(a->co_name, b->co_name)) {
        return 0;
    }
    PyObject *x = lu_code_bytes(a), *y = lu_code_bytes(b);
    int same = x != NULL && y != NULL && lu_eq(x, y) > 0;
    Py_XDECREF(x);
    Py_XDECREF(y);
    return same && const_equal(a->co_consts, b->co_consts) && lu_eq(a->co_names, b->co_names) > 0
        && lu_eq(a->co_localsplusnames, b->co_localsplusnames) > 0
        && lu_eq(a->co_linetable, b->co_linetable) > 0
        && lu_eq(a->co_exceptiontable, b->co_exceptiontable) > 0;
}

/* code_equal(a, b) */
static PyObject *
prim_code_equal(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2 || !PyCode_Check(args[0]) || !PyCode_Check(args[1])) {
        lu_err_format(PyExc_TypeError, "code_equal(a, b)");
        return NULL;
    }
    return Py_NewRef(code_same((PyCodeObject *)args[0], (PyCodeObject *)args[1]) ? Py_True : Py_False);
}

/* code_hash(code): consistent with code_equal, from the name, the argument
   counts and the names. */
static PyObject *
prim_code_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1 || !PyCode_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "code_hash(code)");
        return NULL;
    }
    PyCodeObject *co = (PyCodeObject *)args[0];
    Py_hash_t h = lu_str_hash(co->co_name) ^ lu_hash(co->co_names) ^ lu_hash(co->co_localsplusnames)
        ^ lu_hash(co->co_linetable) ^ lu_hash(co->co_exceptiontable) ^ co->co_argcount
        ^ co->co_posonlyargcount ^ co->co_kwonlyargcount ^ co->co_firstlineno;
    return PyLong_FromLong(h == -1 ? -2 : h);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_code_methods[] = {
    FAST(code_new), FAST(code_localsplus), FAST(code_set_localsplusnames), FAST(code_equal), FAST(code_hash),
    { NULL, NULL, 0, NULL },
};

/* PyUnstable_Code_NewWithPosOnlyArgs: the locals-plus tables of _PyCode_New
   from the three name tuples: a name is a local, a cell, a local that is also
   a cell, or a free variable. */
PyCodeObject *
PyUnstable_Code_NewWithPosOnlyArgs(int argcount, int posonlyargcount, int kwonlyargcount, int nlocals,
                                   int stacksize, int flags, PyObject *code, PyObject *consts,
                                   PyObject *names, PyObject *varnames, PyObject *freevars,
                                   PyObject *cellvars, PyObject *filename, PyObject *name,
                                   PyObject *qualname, int firstlineno, PyObject *linetable,
                                   PyObject *exceptiontable)
{
    Py_ssize_t nvar = PyTuple_GET_SIZE(varnames), ncell = PyTuple_GET_SIZE(cellvars);
    Py_ssize_t nfree = PyTuple_GET_SIZE(freevars);
    PyObject *all = PyList_New(0);
    PyObject *kinds = PyBytes_FromStringAndSize(NULL, nvar + ncell + nfree);
    PyObject *result = NULL;
    if (all == NULL || kinds == NULL) {
        goto done;
    }
    char *kind = PyBytes_AS_STRING(kinds);
    Py_ssize_t n = 0;
    for (Py_ssize_t i = 0; i < nvar; i++) {
        PyObject *v = PyTuple_GET_ITEM(varnames, i);
        int cell = PySequence_Contains(cellvars, v);
        if (cell < 0 || lu_list_append(all, v) < 0) {
            goto done;
        }
        kind[n++] = (char)(CO_FAST_LOCAL | (cell ? CO_FAST_CELL : 0));
    }
    for (Py_ssize_t i = 0; i < ncell; i++) {
        PyObject *v = PyTuple_GET_ITEM(cellvars, i);
        int local = PySequence_Contains(varnames, v);
        if (local < 0) {
            goto done;
        }
        if (!local) {
            if (lu_list_append(all, v) < 0) {
                goto done;
            }
            kind[n++] = (char)CO_FAST_CELL;
        }
    }
    for (Py_ssize_t i = 0; i < nfree; i++) {
        if (lu_list_append(all, PyTuple_GET_ITEM(freevars, i)) < 0) {
            goto done;
        }
        kind[n++] = (char)CO_FAST_FREE;
    }
    PyObject *localsplusnames = PyList_AsTuple(all);
    PyObject *localspluskinds = PyBytes_FromStringAndSize(kind, n);
    if (localsplusnames != NULL && localspluskinds != NULL) {
        struct lu_code_fields c = {
            .filename = filename, .name = name, .qualname = qualname, .flags = flags, .code = code,
            .firstlineno = firstlineno, .linetable = linetable, .consts = consts, .names = names,
            .localsplusnames = localsplusnames, .localspluskinds = localspluskinds,
            .argcount = argcount, .posonlyargcount = posonlyargcount, .kwonlyargcount = kwonlyargcount,
            .stacksize = stacksize, .exceptiontable = exceptiontable,
        };
        result = lu_code_new(&c);
    }
    Py_XDECREF(localsplusnames);
    Py_XDECREF(localspluskinds);
done:
    Py_XDECREF(all);
    Py_XDECREF(kinds);
    return (PyCodeObject *)result;
}
