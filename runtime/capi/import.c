/* The import API (Python/import.c): imports go through builtins.__import__,
   the kernel's, which runs importlib; sys.modules is the interpreter's. */

#include "lucifer.h"
#include <marshal.h>
#include "interp/frozen.h"

PyObject *
PyImport_GetModuleDict(void)
{
    return PyInterpreterState_Get()->modules;
}

PyObject *
PyImport_GetModule(PyObject *name)
{
    PyObject *m = PyObject_GetItem(PyImport_GetModuleDict(), name);
    if (m == NULL) {
        if (PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_Clear();
        }
        return NULL;
    }
    if (m == Py_None) {
        Py_DECREF(m);
        return NULL;
    }
    return m;
}

PyObject *
PyImport_AddModuleRef(const char *name)
{
    PyObject *modules = PyImport_GetModuleDict(), *key = PyUnicode_FromString(name);
    if (key == NULL) {
        return NULL;
    }
    PyObject *m = PyImport_GetModule(key);
    if (m == NULL && !PyErr_Occurred()) {
        m = PyModule_NewObject(key);
        if (m != NULL && PyObject_SetItem(modules, key, m) < 0) {
            Py_CLEAR(m);
        }
    }
    Py_DECREF(key);
    return m;
}

PyObject *
PyImport_ImportModuleLevelObject(PyObject *name, PyObject *globals, PyObject *locals,
                                 PyObject *fromlist, int level)
{
    if (name == NULL) {
        PyErr_SetString(PyExc_ValueError, "Empty module name");
        return NULL;
    }
    if (!PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_TypeError, "module name must be a string");
        return NULL;
    }
    if (level < 0) {
        PyErr_SetString(PyExc_ValueError, "level must be >= 0");
        return NULL;
    }
    if (PyUnicode_GET_LENGTH(name) == 0 && level == 0) {
        PyErr_SetString(PyExc_ValueError, "Empty module name");
        return NULL;
    }
    PyObject *empty = fromlist == NULL ? PyTuple_New(0) : NULL;
    PyObject *args[5] = { name, globals != NULL ? globals : Py_None, locals != NULL ? locals : Py_None,
                          fromlist != NULL ? fromlist : empty, PyLong_FromLong(level) };
    PyObject *result = NULL;
    if (args[3] != NULL && args[4] != NULL) {
        result = lu_call(PyInterpreterState_Get()->import_func, args, 5, NULL);
    }
    Py_XDECREF(empty);
    Py_XDECREF(args[4]);
    return result;
}

PyObject *
PyImport_Import(PyObject *name)
{
    if (name == NULL) {
        /* CPython's CallFunction hits the NULL-object check of Py_BuildValue */
        PyErr_SetString(PyExc_SystemError, "NULL object passed to Py_BuildValue");
        return NULL;
    }
    PyObject *globals = PyDict_New(), *fromlist = PyList_New(0), *item = PyUnicode_FromString("__doc__");
    PyObject *result = NULL;
    if (globals != NULL && fromlist != NULL && item != NULL && PyList_Append(fromlist, item) == 0) {
        PyObject *mod = PyImport_ImportModuleLevelObject(name, globals, globals, fromlist, 0);
        if (mod != NULL) {
            Py_DECREF(mod);
            result = PyImport_GetModule(name);
            if (result == NULL && !PyErr_Occurred()) {
                PyErr_Format(PyExc_KeyError, "%U not in sys.modules as expected", name);
            }
        }
    }
    Py_XDECREF(globals);
    Py_XDECREF(fromlist);
    Py_XDECREF(item);
    return result;
}

PyObject *
PyImport_ImportModule(const char *name)
{
    PyObject *pname = PyUnicode_FromString(name);
    if (pname == NULL) {
        return NULL;
    }
    PyObject *result = PyImport_Import(pname);
    Py_DECREF(pname);
    return result;
}

PyObject *
PySys_GetObject(const char *name)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *v = PyDict_GetItemString(PyInterpreterState_Get()->sysdict, name);
    PyErr_SetRaisedException(exc);
    return v;
}

PyObject *
PyImport_ImportModuleLevel(const char *name, PyObject *globals, PyObject *locals, PyObject *fromlist,
                           int level)
{
    PyObject *pname = PyUnicode_FromString(name);
    if (pname == NULL) {
        return NULL;
    }
    PyObject *result = PyImport_ImportModuleLevelObject(pname, globals, locals, fromlist, level);
    Py_DECREF(pname);
    return result;
}

PyObject *
PyImport_ImportModuleNoBlock(const char *name)
{
    if (PyErr_WarnEx(PyExc_DeprecationWarning,
                     "PyImport_ImportModuleNoBlock() is deprecated and scheduled for "
                     "removal in Python 3.15. Use PyImport_ImportModule() instead.", 1) < 0) {
        return NULL;
    }
    return PyImport_ImportModule(name);
}

PyObject *
PyImport_AddModuleObject(PyObject *name)
{
    PyObject *m = PyImport_GetModule(name);
    if (m == NULL && !PyErr_Occurred()) {
        m = PyModule_NewObject(name);
        if (m != NULL && PyObject_SetItem(PyImport_GetModuleDict(), name, m) < 0) {
            Py_CLEAR(m);
        }
    }
    Py_XDECREF(m);              /* sys.modules holds it: borrowed */
    return m;
}

PyObject *
PyImport_AddModule(const char *name)
{
    PyObject *pname = PyUnicode_FromString(name);
    PyObject *m = pname != NULL ? PyImport_AddModuleObject(pname) : NULL;
    Py_XDECREF(pname);
    return m;
}

/* A helper of importlib._bootstrap_external, the frozen module that owns
   __file__/__path__/__spec__ handling for code the C API executes. */
static PyObject *
call_importlib_external(const char *name, PyObject *const *args, size_t nargs)
{
    PyObject *external = PyObject_GetAttrString(PyInterpreterState_Get()->importlib,
                                                "_bootstrap_external");
    PyObject *func = external != NULL ? PyObject_GetAttrString(external, name) : NULL;
    PyObject *result = func != NULL ? lu_call(func, args, nargs, NULL) : NULL;
    Py_XDECREF(func);
    Py_XDECREF(external);
    return result;
}

/* module_dict_for_exec: the module's dict, a new reference — the module is
   added if need be (a reload reuses the one in sys.modules), with
   __builtins__ in place. */
static PyObject *
module_dict_for_exec(PyObject *name)
{
    PyObject *m = PyImport_AddModuleObject(name);     /* borrowed: sys.modules holds it */
    if (m == NULL) {
        return NULL;
    }
    PyObject *d = PyModule_GetDict(m);
    if (lu_ns_lookup(d, LU_ID(__builtins__)) == NULL
        && lu_ns_store(d, LU_ID(__builtins__), PyEval_GetBuiltins()) < 0) {
        return NULL;
    }
    return Py_NewRef(d);
}

/* exec_code_in_module: run the code in the dict; a failure removes the module
   from sys.modules, as CPython drops damaged modules, and the module found
   there afterwards is the result. */
static PyObject *
exec_code_in_module(PyObject *name, PyObject *d, PyObject *code)
{
    PyObject *v = PyEval_EvalCode(code, d, d);
    if (v == NULL) {
        lu_ns_delete(PyImport_GetModuleDict(), name);
        return NULL;
    }
    Py_DECREF(v);
    PyObject *m = PyImport_GetModule(name);
    if (m == NULL && !PyErr_Occurred()) {
        PyErr_Format(PyExc_ImportError, "Loaded module %R not found in sys.modules", name);
    }
    return m;
}

PyObject *
PyImport_ExecCodeModuleObject(PyObject *name, PyObject *co, PyObject *pathname,
                              PyObject *cpathname)
{
    PyObject *d = module_dict_for_exec(name);
    if (d == NULL) {
        return NULL;
    }
    if (pathname == NULL) {
        pathname = ((PyCodeObject *)co)->co_filename;
    }
    /* _fix_up_module gives the dict its __spec__, __loader__, __file__ and
       __cached__ (a NULL cpathname is omitted, as CallMethodObjArgs omits it) */
    PyObject *args[4] = { d, name, pathname, cpathname };
    PyObject *res = call_importlib_external("_fix_up_module", args, cpathname != NULL ? 4 : 3);
    if (res != NULL) {
        Py_DECREF(res);
        res = exec_code_in_module(name, d, co);
    }
    Py_DECREF(d);
    return res;
}

PyObject *
PyImport_ExecCodeModuleWithPathnames(const char *name, PyObject *co,
                                     const char *pathname, const char *cpathname)
{
    PyObject *nameobject = PyUnicode_FromString(name);
    PyObject *cpathobj = cpathname != NULL ? PyUnicode_DecodeFSDefault(cpathname) : NULL;
    PyObject *pathobj = pathname != NULL ? PyUnicode_DecodeFSDefault(pathname) : NULL;
    PyObject *m = NULL;
    if (nameobject != NULL && (pathname == NULL || pathobj != NULL)
        && (cpathname == NULL || cpathobj != NULL)) {
        if (pathname == NULL && cpathobj != NULL) {
            /* the source file the compiled file came from, as best importlib
               can tell it (this shim's callers pass only the compiled path) */
            PyObject *args[1] = { cpathobj };
            pathobj = call_importlib_external("_get_sourcefile", args, 1);
            if (pathobj == NULL) {
                PyErr_Clear();
            }
        }
        m = PyImport_ExecCodeModuleObject(nameobject, co, pathobj, cpathobj);
    }
    Py_XDECREF(nameobject);
    Py_XDECREF(pathobj);
    Py_XDECREF(cpathobj);
    return m;
}

PyObject *
PyImport_ExecCodeModule(const char *name, PyObject *co)
{
    return PyImport_ExecCodeModuleWithPathnames(name, co, NULL, NULL);
}

PyObject *
PyImport_ExecCodeModuleEx(const char *name, PyObject *co, const char *pathname)
{
    return PyImport_ExecCodeModuleWithPathnames(name, co, pathname, NULL);
}

int
PyImport_ImportFrozenModuleObject(PyObject *name)
{
    const struct lu_frozen *f = lu_frozen_find(name);
    if (f == NULL) {
        return 0;
    }
    PyObject *code = PyMarshal_ReadObjectFromString((const char *)f->data, f->size);
    if (code == NULL) {
        return -1;
    }
    PyObject *d = NULL;
    int r = -1;
    if (f->package) {
        /* the empty __path__ marks the module a package before its code runs */
        PyObject *m = PyImport_AddModuleObject(name);
        PyObject *path = m != NULL ? PyList_New(0) : NULL;
        if (path != NULL
            && lu_ns_store(PyModule_GetDict(m), PyUnicode_InternFromString("__path__"), path) == 0) {
            d = module_dict_for_exec(name);
        }
        Py_XDECREF(path);
    }
    else {
        d = module_dict_for_exec(name);
    }
    if (d != NULL) {
        PyObject *m = exec_code_in_module(name, d, code);
        if (m != NULL) {
            /* __origname__, which importlib's FrozenImporter would consume */
            PyObject *origname = f->orig != NULL ? PyUnicode_FromString(f->orig) : Py_NewRef(Py_None);
            if (origname != NULL
                && lu_ns_store(d, PyUnicode_InternFromString("__origname__"), origname) == 0) {
                r = 1;
            }
            Py_XDECREF(origname);
            Py_DECREF(m);
        }
        Py_DECREF(d);
    }
    Py_DECREF(code);
    return r;
}

int
PyImport_ImportFrozenModule(const char *name)
{
    PyObject *nameobject = PyUnicode_FromString(name);
    if (nameobject == NULL) {
        return -1;
    }
    int r = PyImport_ImportFrozenModuleObject(nameobject);
    Py_DECREF(nameobject);
    return r;
}

PyObject *
PyImport_ImportModuleAttr(PyObject *modname, PyObject *attrname)
{
    PyObject *mod = PyImport_Import(modname);
    if (mod == NULL) {
        return NULL;
    }
    PyObject *attr = PyObject_GetAttr(mod, attrname);
    Py_DECREF(mod);
    return attr;
}

long
PyImport_GetMagicNumber(void)
{
    return LU_PYC_MAGIC;
}

const char *
PyImport_GetMagicTag(void)
{
    PyObject *impl = PySys_GetObject("implementation");
    PyObject *tag = impl != NULL ? PyObject_GetAttrString(impl, "cache_tag") : NULL;
    if (tag == NULL || !PyUnicode_Check(tag)) {
        PyErr_Clear();
        return "";
    }
    /* sys.implementation holds the str for the interpreter's lifetime */
    const char *utf8 = PyUnicode_AsUTF8(tag);
    Py_DECREF(tag);
    if (utf8 == NULL) {
        PyErr_Clear();
        return "";
    }
    return utf8;
}

PyObject *
PyImport_ReloadModule(PyObject *m)
{
    PyObject *importlib = PyImport_ImportModule("importlib");
    PyObject *r = importlib != NULL ? PyObject_CallMethod(importlib, "reload", "(O)", m) : NULL;
    Py_XDECREF(importlib);
    return r;
}

/* sys.modules[name] = module. */
LU_EXPORT int
_PyImport_SetModule(PyObject *name, PyObject *module)
{
    return PyObject_SetItem(PyImport_GetModuleDict(), name, module);
}
