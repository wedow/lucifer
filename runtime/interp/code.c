/* Code objects (Objects/codeobject.c _PyCode_New, layout half): the exact
   PyCodeObject of cpython/code.h, bytecode copied into co_code_adaptive.
   Instrumentation starts at version 0 with no monitoring data, which equals
   an interpreter that never enabled monitoring (FOUNDATIONS 2.3). */

#include "interp/code.h"
#include "interp/exceptions.h"
#include "interp/frame.h"
#include "object/set.h"

#include "opcode.h"
#include "opcode_ids.h"

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

static void
code_dealloc(PyObject *op)
{
    PyCodeObject *co = (PyCodeObject *)op;
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
    lu_object_free(op);
}

/* The attributes the exception machinery and kernel read; the rest of
   code's surface arrives with the code object's own ticket. */
static PyMemberDef code_members[] = {
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
    PyCode_Type.tp_basicsize = offsetof(PyCodeObject, co_code_adaptive);
    PyCode_Type.tp_itemsize = sizeof(lu_codeunit);
    PyCode_Type.tp_flags |= Py_TPFLAGS_DEFAULT;
    PyCode_Type.tp_dealloc = code_dealloc;
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
    Py_SET_SIZE(co, nbytes / (Py_ssize_t)sizeof(lu_codeunit));
    co->co_consts = Py_NewRef(c->consts);
    co->co_names = Py_NewRef(c->names);
    co->co_exceptiontable = Py_NewRef(c->exceptiontable);
    co->co_flags = c->flags;
    co->co_argcount = c->argcount;
    co->co_posonlyargcount = c->posonlyargcount;
    co->co_kwonlyargcount = c->kwonlyargcount;
    co->co_stacksize = c->stacksize;
    co->co_firstlineno = c->firstlineno;
    co->co_localsplusnames = Py_NewRef(c->localsplusnames);
    co->co_localspluskinds = Py_NewRef(c->localspluskinds);
    co->co_filename = Py_NewRef(c->filename);
    co->co_name = Py_NewRef(c->name);
    co->co_qualname = Py_NewRef(c->qualname);
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
    co->co_framesize = nlocalsplus + c->stacksize + (int)LU_FRAME_SPECIALS;
    memcpy(co->co_code_adaptive, PyBytes_AS_STRING(c->code), nbytes);
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
