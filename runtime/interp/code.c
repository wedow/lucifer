/* Code objects (Objects/codeobject.c _PyCode_New, layout half): the exact
   PyCodeObject of cpython/code.h, bytecode copied into co_code_adaptive.
   Instrumentation starts at version 0 with no monitoring data, which equals
   an interpreter that never enabled monitoring (FOUNDATIONS 2.3). */

#include "interp/code.h"
#include "interp/frame.h"

#include <string.h>

static void
code_dealloc(PyObject *op)
{
    PyCodeObject *co = (PyCodeObject *)op;
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
    return (PyObject *)co;
}
