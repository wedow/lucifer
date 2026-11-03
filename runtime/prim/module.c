/* Module objects (Objects/moduleobject.c, layout half) and the _lucifer
   builtin module, the primitive boundary kernel Python calls (DESIGN 4.4).
   _lucifer uses multi-phase init with module state and keeps no file-scope
   mutable data (FOUNDATIONS 2.1). */

#include "prim/bytes.h"

static void
module_dealloc(PyObject *op)
{
    PyModuleObject *m = (PyModuleObject *)op;
    if (m->md_def != NULL && m->md_def->m_free != NULL) {
        m->md_def->m_free(op);
    }
    PyMem_Free(m->md_state);
    Py_XDECREF(m->md_dict);
    Py_XDECREF(m->md_name);
    lu_object_free(op);
}

static PyMemberDef module_members[] = {
    { "__dict__", _Py_T_OBJECT, offsetof(PyModuleObject, md_dict), Py_READONLY, NULL },
    { NULL },
};

void
lu_module_init(void)
{
    PyModule_Type.tp_basicsize = sizeof(PyModuleObject);
    PyModule_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyModule_Type.tp_dealloc = module_dealloc;
    PyModule_Type.tp_dictoffset = offsetof(PyModuleObject, md_dict);
    PyModule_Type.tp_members = module_members;
}

/* module_init_dict: name the module and give its dict __name__, __doc__,
   __package__, __loader__ and __spec__. */
static int
module_init_dict(PyModuleObject *m, PyObject *name, PyObject *doc)
{
    Py_XSETREF(m->md_name, Py_NewRef(name));
    if (m->md_dict == NULL && (m->md_dict = PyDict_New()) == NULL) {
        return -1;
    }
    return lu_ns_insert(m->md_dict, LU_ID(__name__), name) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__doc__), doc) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__package__), Py_None) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__loader__), Py_None) < 0
        || lu_ns_insert(m->md_dict, LU_ID(__spec__), Py_None) < 0 ? -1 : 0;
}

/* PyModule_NewObject */
PyObject *
PyModule_NewObject(PyObject *name)
{
    PyModuleObject *m = (PyModuleObject *)lu_object_new(&PyModule_Type, sizeof(PyModuleObject));
    if (m != NULL && module_init_dict(m, name, Py_None) < 0) {
        Py_CLEAR(m);
    }
    return (PyObject *)m;
}

PyObject *
PyModule_New(const char *name)
{
    PyObject *s = PyUnicode_FromString(name);
    if (s == NULL) {
        return NULL;
    }
    PyObject *m = PyModule_NewObject(s);
    Py_DECREF(s);
    return m;
}

PyObject *
lu_module_new(const char *name)
{
    PyObject *s = PyUnicode_InternFromString(name);
    if (s == NULL) {
        return NULL;
    }
    PyObject *m = PyModule_NewObject(s);
    Py_DECREF(s);
    return m;
}

PyObject *
PyModule_GetDict(PyObject *m)
{
    return ((PyModuleObject *)m)->md_dict;
}

void *
PyModule_GetState(PyObject *m)
{
    return ((PyModuleObject *)m)->md_state;
}

/* PyModule_AddFunctions with interned names. */
static int
add_functions(PyObject *m, PyMethodDef *defs)
{
    PyModuleObject *mo = (PyModuleObject *)m;
    for (PyMethodDef *ml = defs; ml->ml_name != NULL; ml++) {
        PyObject *f = PyCFunction_NewEx(ml, m, mo->md_name);
        PyObject *name = f != NULL ? PyUnicode_InternFromString(ml->ml_name) : NULL;
        int err = name == NULL || lu_ns_insert(mo->md_dict, name, f) < 0;
        Py_XDECREF(name);
        Py_XDECREF(f);
        if (err) {
            return -1;
        }
    }
    return 0;
}

/* PyModule_FromDefAndSpec and PyModule_ExecDef, for builtin modules made
   natively before importlib exists (no spec, no Py_mod_create). */
PyObject *
lu_module_from_def(PyModuleDef *def)
{
    PyObject *m = lu_module_new(def->m_name);
    if (m == NULL) {
        return NULL;
    }
    PyModuleObject *mo = (PyModuleObject *)m;
    mo->md_def = def;
    if (def->m_size > 0) {
        mo->md_state = PyMem_Calloc(1, def->m_size);
        if (mo->md_state == NULL) {
            PyErr_NoMemory();
            goto fail;
        }
    }
    if (def->m_methods != NULL && add_functions(m, def->m_methods) < 0) {
        goto fail;
    }
    for (PyModuleDef_Slot *slot = def->m_slots; slot != NULL && slot->slot != 0; slot++) {
        if (slot->slot == Py_mod_create) {
            lu_err_format(PyExc_SystemError, "module %s: Py_mod_create needs a spec", def->m_name);
            goto fail;
        }
        if (slot->slot == Py_mod_exec && ((int (*)(PyObject *))slot->value)(m) < 0) {
            goto fail;
        }
    }
    return m;
fail:
    Py_DECREF(m);
    return NULL;
}

/* _lucifer */

static int
lucifer_exec(PyObject *m)
{
    struct lu_prim_state *state = PyModule_GetState(m);
    state->interp = PyInterpreterState_Get();
    if (add_functions(m, lu_prim_type_methods) < 0 || add_functions(m, lu_prim_descr_methods) < 0
        || add_functions(m, lu_prim_slot_methods) < 0 || add_functions(m, lu_prim_int_methods) < 0
        || add_functions(m, lu_prim_float_methods) < 0 || add_functions(m, lu_prim_complex_methods) < 0
        || add_functions(m, lu_prim_str_methods) < 0 || add_functions(m, lu_prim_bytes_methods) < 0
        || add_functions(m, lu_prim_mv_methods) < 0
        || add_functions(m, lu_prim_exc_methods) < 0
        || add_functions(m, lu_prim_storage_methods) < 0
        || add_functions(m, lu_prim_dict_methods) < 0
        || add_functions(m, lu_prim_set_methods) < 0
        || add_functions(m, lu_prim_gen_methods) < 0
        || add_functions(m, lu_prim_builtins_methods) < 0
        || add_functions(m, lu_prim_import_methods) < 0
        || add_functions(m, lu_prim_thread_methods) < 0
        || add_functions(m, lu_prim_weakref_methods) < 0
        || add_functions(m, lu_prim_os_methods) < 0) {
        return -1;
    }
    PyObject *name = PyUnicode_InternFromString("MISSING");
    int err = name == NULL || lu_ns_insert(PyModule_GetDict(m), name, LU_MISSING) < 0;
    Py_XDECREF(name);
    return err ? -1 : 0;
}

/* module_init(module, name, doc): module.__init__'s dict. */
static PyObject *
prim_module_init(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 3 || !PyModule_Check(args[0]) || !PyUnicode_Check(args[1])) {
        lu_err_format(PyExc_TypeError, "module_init(module, name, doc)");
        return NULL;
    }
    return module_init_dict((PyModuleObject *)args[0], args[1], args[2]) < 0 ? NULL : Py_NewRef(Py_None);
}

static PyMethodDef lucifer_methods[] = {
    { "module_init", (PyCFunction)(void (*)(void))prim_module_init, METH_FASTCALL,
      "module_init(module, name, doc): the namespace of a new module." },
    { "write", (PyCFunction)(void (*)(void))lu_prim_write, METH_FASTCALL,
      "write(fd, str): write str to fd as UTF-8, all of it." },
    { "str_join", (PyCFunction)(void (*)(void))lu_prim_str_join, METH_FASTCALL,
      "str_join(sep, items): the exact strs of a tuple, joined by sep." },
    { "text_repr", (PyCFunction)(void (*)(void))lu_prim_text_repr, METH_FASTCALL,
      "text_repr(obj, limit): repr() of a str or bytes, cut to limit characters." },
    { NULL, NULL, 0, NULL },
};

static PyModuleDef_Slot lucifer_slots[] = {
    { Py_mod_exec, (void *)lucifer_exec },
    { Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED },
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

PyModuleDef lu_lucifer_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_lucifer",
    .m_doc = "Native primitives under the kernel's Python builtins.",
    .m_size = sizeof(struct lu_prim_state),
    .m_methods = lucifer_methods,
    .m_slots = lucifer_slots,
};
