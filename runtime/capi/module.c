/* The module C-API (Objects/moduleobject.c, Python/modsupport.c): multi-phase
   initialization from a PyModuleDef, and the helpers an extension's exec
   function uses. PyModule_New, GetDict and GetState are in runtime/prim/module.c,
   beside the layout. */

#include "lucifer.h"

PyObject *
PyModuleDef_Init(PyModuleDef *def)
{
    if (def->m_base.m_index == 0) {
        Py_SET_REFCNT((PyObject *)def, 1);
        Py_SET_TYPE((PyObject *)def, &PyModuleDef_Type);
        def->m_base.m_index = __atomic_add_fetch(&_PyRuntime.next_module_index, 1, __ATOMIC_RELAXED);
    }
    return (PyObject *)def;
}

/* PyErr_Format with the exception that was set as its __cause__ and __context__. */
static void
err_format_from_cause(PyObject *type, const char *format, const char *name)
{
    PyObject *cause = PyErr_GetRaisedException();
    lu_err_format(type, format, name);
    PyObject *exc = PyErr_GetRaisedException();
    if (exc != NULL && cause != NULL) {
        PyException_SetCause(exc, Py_NewRef(cause));
        PyException_SetContext(exc, cause);
        cause = NULL;
    }
    Py_XDECREF(cause);
    PyErr_SetRaisedException(exc);
}

int
PyModule_AddFunctions(PyObject *m, PyMethodDef *functions)
{
    if (PyModule_CheckExact(m)) {
        return lu_module_add_functions(m, functions);
    }
    PyObject *name = PyModule_GetNameObject(m);
    if (name == NULL) {
        return -1;
    }
    int rc = 0;
    for (PyMethodDef *ml = functions; rc == 0 && ml->ml_name != NULL; ml++) {
        if (ml->ml_flags & (METH_CLASS | METH_STATIC)) {
            PyErr_SetString(PyExc_ValueError, "module functions cannot set METH_CLASS or METH_STATIC");
            rc = -1;
            break;
        }
        PyObject *f = PyCFunction_NewEx(ml, m, name);
        rc = f == NULL || PyObject_SetAttrString(m, ml->ml_name, f) < 0 ? -1 : 0;
        Py_XDECREF(f);
    }
    Py_DECREF(name);
    return rc;
}

int
PyModule_SetDocString(PyObject *m, const char *doc)
{
    PyObject *v = PyUnicode_FromString(doc);
    int err = v == NULL || PyObject_SetAttr(m, LU_ID(__doc__), v) < 0;
    Py_XDECREF(v);
    return err ? -1 : 0;
}

PyObject *
PyModule_GetNameObject(PyObject *m)
{
    if (!PyModule_Check(m)) {
        PyErr_SetString(PyExc_TypeError, "bad argument type for built-in operation");
        return NULL;
    }
    PyObject *dict = ((PyModuleObject *)m)->md_dict;
    PyObject *name = dict != NULL && PyDict_Check(dict) ? lu_ns_lookup(dict, LU_ID(__name__)) : NULL;
    if (name == NULL || !PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_SystemError, "nameless module");
        return NULL;
    }
    return Py_NewRef(name);
}

const char *
PyModule_GetName(PyObject *m)
{
    PyObject *name = PyModule_GetNameObject(m);
    if (name == NULL) {
        return NULL;
    }
    Py_DECREF(name);    /* the module's dict still has it */
    return PyUnicode_AsUTF8(name);
}

PyObject *
PyModule_GetFilenameObject(PyObject *m)
{
    if (!PyModule_Check(m)) {
        PyErr_SetString(PyExc_TypeError, "bad argument type for built-in operation");
        return NULL;
    }
    PyObject *dict = ((PyModuleObject *)m)->md_dict;
    PyObject *file = dict != NULL ? lu_ns_lookup(dict, PyUnicode_InternFromString("__file__")) : NULL;
    if (file == NULL || !PyUnicode_Check(file)) {
        PyErr_SetString(PyExc_SystemError, "module filename missing");
        return NULL;
    }
    return Py_NewRef(file);
}

const char *
PyModule_GetFilename(PyObject *m)
{
    PyObject *file = PyModule_GetFilenameObject(m);
    if (file == NULL) {
        return NULL;
    }
    Py_DECREF(file);
    return PyUnicode_AsUTF8(file);
}

PyModuleDef *
PyModule_GetDef(PyObject *m)
{
    if (!PyModule_Check(m)) {
        PyErr_SetString(PyExc_TypeError, "bad argument type for built-in operation");
        return NULL;
    }
    return ((PyModuleObject *)m)->md_def;
}

/* Single-phase initialization: the module is made, filled and returned by PyInit_<name>. */
PyObject *
PyModule_Create2(PyModuleDef *def, int apiver)
{
    PyModuleDef_Init(def);
    PyObject *name = PyUnicode_FromString(def->m_name);
    PyObject *m = name != NULL ? PyModule_NewObject(name) : NULL;
    Py_XDECREF(name);
    if (m == NULL) {
        return NULL;
    }
    PyModuleObject *mo = (PyModuleObject *)m;
    mo->md_def = def;
    mo->md_gil = Py_MOD_GIL_USED;
    if (def->m_size > 0) {
        mo->md_state = PyMem_Calloc(1, def->m_size);
        if (mo->md_state == NULL) {
            Py_DECREF(m);
            return PyErr_NoMemory();
        }
    }
    if ((def->m_methods != NULL && PyModule_AddFunctions(m, def->m_methods) < 0)
        || (def->m_doc != NULL && PyModule_SetDocString(m, def->m_doc) < 0)) {
        Py_DECREF(m);
        return NULL;
    }
    return m;
}

/* Python/getargs-free helpers for what an exec function adds to its module. */

int
PyModule_AddObjectRef(PyObject *m, const char *name, PyObject *value)
{
    if (!PyModule_Check(m)) {
        PyErr_SetString(PyExc_TypeError, "PyModule_AddObjectRef() first argument must be a module");
        return -1;
    }
    if (value == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "PyModule_AddObjectRef() must be called "
                            "with an exception raised if value is NULL");
        }
        return -1;
    }
    PyObject *dict = PyModule_GetDict(m);
    PyObject *key = PyUnicode_InternFromString(name);
    int err = key == NULL || lu_ns_insert(dict, key, value) < 0;
    Py_XDECREF(key);
    return err ? -1 : 0;
}

int
PyModule_Add(PyObject *m, const char *name, PyObject *value)
{
    int res = PyModule_AddObjectRef(m, name, value);
    Py_XDECREF(value);
    return res;
}

int
PyModule_AddObject(PyObject *m, const char *name, PyObject *value)
{
    int res = PyModule_AddObjectRef(m, name, value);
    if (res == 0) {
        Py_DECREF(value);
    }
    return res;
}

int
PyModule_AddIntConstant(PyObject *m, const char *name, long value)
{
    return PyModule_Add(m, name, PyLong_FromLong(value));
}

int
PyModule_AddStringConstant(PyObject *m, const char *name, const char *value)
{
    return PyModule_Add(m, name, PyUnicode_FromString(value));
}

/* The module a spec describes, from its definition (PyModule_FromDefAndSpec2). */
PyObject *
PyModule_FromDefAndSpec2(PyModuleDef *def, PyObject *spec, int module_api_version)
{
    PyObject *(*create)(PyObject *, PyModuleDef *) = NULL;
    int has_interpreters = 0, has_gil = 0, has_exec = 0;
    void *gil = Py_MOD_GIL_USED;
    PyModuleDef_Init(def);

    PyObject *nameobj = PyObject_GetAttrString(spec, "name");
    const char *name = nameobj != NULL ? PyUnicode_AsUTF8(nameobj) : NULL;
    if (name == NULL) {
        Py_XDECREF(nameobj);
        return NULL;
    }
    PyObject *m = NULL;
    if (def->m_size < 0) {
        PyErr_Format(PyExc_SystemError, "module %s: m_size may not be negative for multi-phase "
                     "initialization", name);
        goto error;
    }
    for (PyModuleDef_Slot *slot = def->m_slots; slot != NULL && slot->slot; slot++) {
        switch (slot->slot) {
        case Py_mod_create:
            if (create) {
                PyErr_Format(PyExc_SystemError, "module %s has multiple create slots", name);
                goto error;
            }
            create = slot->value;
            break;
        case Py_mod_exec:
            has_exec = 1;
            break;
        case Py_mod_multiple_interpreters:
            if (has_interpreters) {
                PyErr_Format(PyExc_SystemError, "module %s has more than one 'multiple interpreters' "
                             "slots", name);
                goto error;
            }
            has_interpreters = 1;
            break;
        case Py_mod_gil:
            if (has_gil) {
                PyErr_Format(PyExc_SystemError, "module %s has more than one 'gil' slot", name);
                goto error;
            }
            gil = slot->value;
            has_gil = 1;
            break;
        default:
            PyErr_Format(PyExc_SystemError, "module %s uses unknown slot ID %i", name, slot->slot);
            goto error;
        }
    }
    if (create) {
        m = create(spec, def);
        if (m == NULL) {
            if (!PyErr_Occurred()) {
                PyErr_Format(PyExc_SystemError, "creation of module %s failed without setting an "
                             "exception", name);
            }
            goto error;
        }
        if (PyErr_Occurred()) {
            err_format_from_cause(PyExc_SystemError, "creation of module %s raised unreported exception",
                                  name);
            goto error;
        }
    }
    else {
        m = PyModule_NewObject(nameobj);
        if (m == NULL) {
            goto error;
        }
    }
    if (PyModule_Check(m)) {
        ((PyModuleObject *)m)->md_state = NULL;
        ((PyModuleObject *)m)->md_def = def;
        ((PyModuleObject *)m)->md_gil = gil;
    }
    else if (def->m_size > 0 || def->m_traverse || def->m_clear || def->m_free) {
        PyErr_Format(PyExc_SystemError, "module %s is not a module object, but requests module state",
                     name);
        goto error;
    }
    else if (has_exec) {
        PyErr_Format(PyExc_SystemError, "module %s specifies execution slots, but did not create "
                     "a ModuleType instance", name);
        goto error;
    }
    if ((def->m_methods != NULL && PyModule_AddFunctions(m, def->m_methods) < 0)
        || (def->m_doc != NULL && PyModule_SetDocString(m, def->m_doc) < 0)) {
        goto error;
    }
    Py_DECREF(nameobj);
    return m;
error:
    Py_DECREF(nameobj);
    Py_XDECREF(m);
    return NULL;
}

int
PyModule_ExecDef(PyObject *m, PyModuleDef *def)
{
    const char *name = PyModule_GetName(m);
    if (name == NULL) {
        return -1;
    }
    PyModuleObject *mo = (PyModuleObject *)m;
    if (def->m_size >= 0 && mo->md_state == NULL) {
        /* Always set, even for size 0: a set state marks the module executed. */
        mo->md_state = PyMem_Malloc(def->m_size);
        if (mo->md_state == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        memset(mo->md_state, 0, def->m_size);
    }
    for (PyModuleDef_Slot *slot = def->m_slots; slot != NULL && slot->slot; slot++) {
        switch (slot->slot) {
        case Py_mod_create:
        case Py_mod_multiple_interpreters:
        case Py_mod_gil:
            break;                      /* handled in PyModule_FromDefAndSpec2 */
        case Py_mod_exec:
            if (((int (*)(PyObject *))slot->value)(m) != 0) {
                if (!PyErr_Occurred()) {
                    PyErr_Format(PyExc_SystemError, "execution of module %s failed without setting "
                                 "an exception", name);
                }
                return -1;
            }
            if (PyErr_Occurred()) {
                err_format_from_cause(PyExc_SystemError, "execution of module %s raised unreported "
                                      "exception", name);
                return -1;
            }
            break;
        default:
            PyErr_Format(PyExc_SystemError, "module %s initialized with unknown slot %i", name,
                         slot->slot);
            return -1;
        }
    }
    return 0;
}

int
PyUnstable_Module_SetGIL(PyObject *module, void *gil)
{
    if (!PyModule_Check(module)) {
        PyErr_BadInternalCall();
        return -1;
    }
    ((PyModuleObject *)module)->md_gil = gil;
    return 0;
}
