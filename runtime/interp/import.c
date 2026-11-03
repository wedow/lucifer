/* Imports (Python/import.c, Python/frozen.c): the builtin modules, which are
   lib/modules ports run on first import; the frozen modules importlib finds;
   and the install of CPython's own frozen importlib bootstrap (FOUNDATIONS
   2.15). __import__ and import_from are kernel Python (lib/kernel/builtins.py);
   what they and the _imp port need from the runtime is here, as _lucifer
   primitives. */

#include "interp/frozen.h"
#include "interp/code.h"
#include "prim/prim.h"

#include "marshal.h"

#include <dlfcn.h>
#include <string.h>

static const struct lu_frozen *
find_cname(const struct lu_frozen *table, const char *name)
{
    for (const struct lu_frozen *m = table; name != NULL && m->name != NULL; m++) {
        if (strcmp(m->name, name) == 0) {
            return m;
        }
    }
    return NULL;
}

static const struct lu_frozen *
find_module(const struct lu_frozen *table, PyObject *name)
{
    return PyUnicode_Check(name) ? find_cname(table, PyUnicode_AsUTF8(name)) : NULL;
}

static PyObject *
load_code(const struct lu_frozen *m)
{
    return PyMarshal_ReadObjectFromString((const char *)m->data, m->size);
}

/* Runs a module's code in its namespace, as exec_code_in_module does. */
static int
exec_in_module(PyObject *module, PyObject *code)
{
    PyObject *ns = PyModule_GetDict(module);
    PyObject *builtins = lu_ns_lookup(PyInterpreterState_Get()->modules, LU_ID(builtins));
    if (lu_ns_lookup(ns, LU_ID(__builtins__)) == NULL
        && lu_ns_insert(ns, LU_ID(__builtins__), builtins) < 0) {
        return -1;
    }
    PyObject *result = PyEval_EvalCode(code, ns, ns);
    Py_XDECREF(result);
    return result == NULL ? -1 : 0;
}

/* The builtin modules that are not ports: made before any Python runs. */
static int
is_native_module(PyObject *name)
{
    const char *s = PyUnicode_Check(name) ? PyUnicode_AsUTF8(name) : "";
    return strcmp(s, "builtins") == 0 || strcmp(s, "_lucifer") == 0 || strcmp(s, "sys") == 0;
}

/* port_exists(name): is name a builtin module (sys.builtin_module_names)? */
static PyObject *
prim_port_exists(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    int yes = nargs == 1 && (is_native_module(args[0]) || find_module(lu_frozen_builtin, args[0]) != NULL);
    return Py_NewRef(yes ? Py_True : Py_False);
}

/* port_create(name): a new, unrun module for a port, or None for a name
   that is not one; the native builtin modules are the ones in sys.modules. */
static PyObject *
prim_port_create(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1 || !PyUnicode_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "port_create(name)");
        return NULL;
    }
    PyObject *native = lu_ns_lookup(PyInterpreterState_Get()->modules, args[0]);
    if (native != NULL && PyModule_CheckExact(native) && is_native_module(args[0])) {
        return Py_NewRef(native);
    }
    if (find_module(lu_frozen_builtin, args[0]) == NULL) {
        return Py_NewRef(Py_None);
    }
    return lu_module_new(PyUnicode_AsUTF8(args[0]));
}

/* port_exec(module): runs the port that implements the module. */
static PyObject *
prim_port_exec(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const struct lu_frozen *port = nargs == 1 && PyModule_CheckExact(args[0])
        ? find_module(lu_frozen_builtin, ((PyModuleObject *)args[0])->md_name) : NULL;
    if (port == NULL) {
        return Py_NewRef(Py_None);      /* a native module has nothing to run */
    }
    PyObject *code = load_code(port);
    if (code == NULL) {
        return NULL;
    }
    lu_hide_frames(code);
    int err = exec_in_module(args[0], code);
    Py_DECREF(code);
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* frozen_find(name): the module a frozen name was frozen from, or None. */
static PyObject *
prim_frozen_find(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const struct lu_frozen *f = nargs == 1 ? find_module(lu_frozen_modules, args[0]) : NULL;
    return f == NULL ? Py_NewRef(Py_None) : PyUnicode_FromString(f->orig != NULL ? f->orig : f->name);
}

/* frozen_is_package(name): is the frozen module a package? */
static PyObject *
prim_frozen_is_package(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const struct lu_frozen *f = nargs == 1 ? find_module(lu_frozen_modules, args[0]) : NULL;
    return Py_NewRef(f != NULL && f->package ? Py_True : Py_False);
}

/* frozen_code(name): the code object of a frozen module, or None. */
static PyObject *
prim_frozen_code(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    const struct lu_frozen *f = nargs == 1 ? find_module(lu_frozen_modules, args[0]) : NULL;
    PyObject *code = f == NULL ? Py_NewRef(Py_None) : load_code(f);
    if (code != NULL && f != NULL && f->hidden) {
        lu_hide_frames(code);
    }
    return code;
}

/* frozen_names(): the names of the frozen modules, in a tuple. */
static PyObject *
prim_frozen_names(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    Py_ssize_t n = 0;
    while (lu_frozen_modules[n].name != NULL) {
        n++;
    }
    PyObject *names = PyTuple_New(n);
    for (Py_ssize_t i = 0; names != NULL && i < n; i++) {
        PyObject *name = PyUnicode_FromString(lu_frozen_modules[i].name);
        if (name == NULL) {
            Py_CLEAR(names);
            break;
        }
        PyTuple_SET_ITEM(names, i, name);
    }
    return names;
}

/* import_state(): (_frozen_importlib, the original builtins.__import__). */
static PyObject *
prim_import_state(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    struct _is *interp = PyInterpreterState_Get();
    return PyTuple_Pack(2, interp->importlib != NULL ? interp->importlib : Py_None,
                        interp->import_func != NULL ? interp->import_func : Py_None);
}

/* update_compiled_module: the code objects that came from the file `old`
   take the new file name. */
static void
fix_filename(PyCodeObject *co, PyObject *old, PyObject *name)
{
    if (!lu_str_eq(co->co_filename, old)) {
        return;
    }
    Py_SETREF(co->co_filename, Py_NewRef(name));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(co->co_consts); i++) {
        PyObject *c = PyTuple_GET_ITEM(co->co_consts, i);
        if (PyCode_Check(c)) {
            fix_filename((PyCodeObject *)c, old, name);
        }
    }
}

/* fix_co_filename(code, path) */
static PyObject *
prim_fix_co_filename(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2 || !PyCode_Check(args[0]) || !PyUnicode_CheckExact(args[1])) {
        lu_err_format(PyExc_TypeError, "fix_co_filename(code, path)");
        return NULL;
    }
    PyObject *old = Py_NewRef(((PyCodeObject *)args[0])->co_filename);
    if (!lu_str_eq(old, args[1])) {
        fix_filename((PyCodeObject *)args[0], old, args[1]);
    }
    Py_DECREF(old);
    return Py_NewRef(Py_None);
}

/* marshal_loads(bytes): the object marshal.loads returns. */
static PyObject *
prim_marshal_loads(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1 || !PyBytes_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "marshal_loads(bytes)");
        return NULL;
    }
    return PyMarshal_ReadObjectFromString(PyBytes_AS_STRING(args[0]), PyBytes_GET_SIZE(args[0]));
}

/* marshal_dumps(obj, version, allow_code): the bytes marshal.dumps returns. */
static PyObject *
prim_marshal_dumps(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    long version;
    if (nargs != 3 || lu_long_as_long(args[1], &version) < 0) {
        lu_err_format(PyExc_TypeError, "marshal_dumps(obj, version, allow_code)");
        return NULL;
    }
    return lu_marshal_dumps(args[0], (int)version, lu_is_true(args[2]));
}

/* Extension modules (Python/importdl.c). */

static void
import_error(const char *what, PyObject *name, PyObject *path)
{
    PyObject *msg = PyUnicode_FromString(what);
    PyObject *kwnames = PyTuple_Pack(2, PyUnicode_InternFromString("name"),
                                     PyUnicode_InternFromString("path"));
    PyObject *args[3] = { msg, name, path };
    PyObject *exc = msg != NULL && kwnames != NULL ? lu_call(PyExc_ImportError, args, 1, kwnames) : NULL;
    Py_XDECREF(kwnames);
    Py_XDECREF(msg);
    if (exc != NULL) {
        PyErr_SetObject((PyObject *)Py_TYPE(exc), exc);
        Py_DECREF(exc);
    }
}

/* ext_create(spec): the module a shared object's PyInit_<name> describes,
   made from its definition and the spec. */
static PyObject *
prim_ext_create(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *spec = nargs == 1 ? args[0] : NULL;
    PyObject *name = spec != NULL ? PyObject_GetAttrString(spec, "name") : NULL;
    PyObject *path = name != NULL ? PyObject_GetAttrString(spec, "origin") : NULL;
    PyObject *module = NULL;
    if (path == NULL) {
        goto done;
    }
    if (!PyUnicode_Check(name) || !PyUnicode_Check(path)) {
        lu_err_format(PyExc_TypeError, "create_dynamic(spec): name and origin must be str");
        goto done;
    }
    const char *full = PyUnicode_AsUTF8(name);
    const char *last = strrchr(full, '.');
    last = last != NULL ? last + 1 : full;
    for (const char *c = last; *c; c++) {
        if ((unsigned char)*c >= 0x80) {
            lu_err_format(PyExc_ImportError, "extension module names must be ASCII here: %s", full);
            goto done;
        }
    }
    void *handle = dlopen(PyUnicode_AsUTF8(path), RTLD_NOW);
    if (handle == NULL) {
        import_error(dlerror(), name, path);
        goto done;
    }
    char hook[300];
    snprintf(hook, sizeof hook, "PyInit_%s", last);
    PyObject *(*init)(void) = (PyObject *(*)(void))dlsym(handle, hook);
    if (init == NULL) {
        char msg[400];
        snprintf(msg, sizeof msg, "dynamic module does not define module export function (%s)", hook);
        import_error(msg, name, path);
        goto done;
    }
    PyObject *def = init();
    if (def == NULL) {
        if (!PyErr_Occurred()) {
            lu_err_format(PyExc_SystemError, "initialization of %s failed without raising an exception",
                          last);
        }
        goto done;
    }
    if (PyErr_Occurred()) {
        PyErr_Clear();
        lu_err_format(PyExc_SystemError, "initialization of %s raised unreported exception", last);
    }
    else if (!Py_IS_TYPE(def, &PyModuleDef_Type)) {
        lu_err_format(PyExc_ImportError, "initialization of %s is not multi-phase, which this "
                      "runtime does not load yet", last);
    }
    else {
        module = PyModule_FromDefAndSpec2((PyModuleDef *)def, spec, PYTHON_API_VERSION);
    }
    if (!Py_IS_TYPE(def, &PyModuleDef_Type)) {
        Py_DECREF(def);
    }
done:
    Py_XDECREF(path);
    Py_XDECREF(name);
    return module;
}

/* ext_exec(module): runs a module's exec slots once; a module that has state
   already ran them (reload does nothing), and a module with no definition has none. */
static PyObject *
prim_ext_exec(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 1 || !PyModule_Check(args[0])) {
        return Py_NewRef(Py_None);
    }
    PyModuleObject *mo = (PyModuleObject *)args[0];
    if (mo->md_def == NULL || mo->md_state != NULL) {
        return Py_NewRef(Py_None);
    }
    return PyModule_ExecDef(args[0], mo->md_def) < 0 ? NULL : Py_NewRef(Py_None);
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_import_methods[] = {
    FAST(ext_create), FAST(ext_exec),
    FAST(port_exists), FAST(port_create), FAST(port_exec), FAST(frozen_find), FAST(frozen_is_package),
    FAST(frozen_code),
    FAST(frozen_names), FAST(import_state), FAST(fix_co_filename), FAST(marshal_loads), FAST(marshal_dumps),
    { NULL, NULL, 0, NULL },
};

/* Python/pylifecycle.c init_importlib: the builtin _imp, then the frozen
   _frozen_importlib with its _install(sys, _imp), then the path-based
   importers. */

static PyObject *
call_function(PyObject *module, const char *name, PyObject *const *args, size_t nargs)
{
    PyObject *key = PyUnicode_InternFromString(name);
    PyObject *f = key != NULL ? lu_ns_lookup(PyModule_GetDict(module), key) : NULL;
    Py_XDECREF(key);
    if (f == NULL) {
        lu_err_format(PyExc_ImportError, "%s has no %s", PyUnicode_AsUTF8(((PyModuleObject *)module)->md_name),
                      name);
        return NULL;
    }
    return lu_call(f, args, nargs, NULL);
}

/* A module made from a table entry and added to sys.modules. */
static PyObject *
start_module(struct _is *interp, const struct lu_frozen *m, int frozen)
{
    PyObject *module = lu_module_new(m->name);
    PyObject *code = module != NULL ? load_code(m) : NULL;
    if (code == NULL) {
        Py_XDECREF(module);
        return NULL;
    }
    PyObject *origname = frozen ? PyUnicode_FromString(m->orig != NULL ? m->orig : m->name) : NULL;
    int err = (frozen && (origname == NULL
               || lu_ns_insert(PyModule_GetDict(module), PyUnicode_InternFromString("__origname__"), origname) < 0))
        || lu_ns_insert(interp->modules, ((PyModuleObject *)module)->md_name, module) < 0
        || exec_in_module(module, code) < 0;
    Py_XDECREF(origname);
    Py_DECREF(code);
    if (err) {
        lu_ns_delete(interp->modules, ((PyModuleObject *)module)->md_name);
        Py_CLEAR(module);
    }
    return module;
}

/* _PyImportZip_Init: zipimport's path hook goes first in sys.path_hooks. A
   failure leaves sys.path without zip support, as CPython does. */
static void
install_zipimport(struct _is *interp)
{
    PyObject *name = PyUnicode_InternFromString("zipimport");
    PyObject *module = name != NULL ? lu_call(interp->import_func, &name, 1, NULL) : NULL;
    PyObject *hook = module != NULL ? PyObject_GetAttrString(module, "zipimporter") : NULL;
    PyObject *hooks = lu_ns_lookup(interp->sysdict, PyUnicode_InternFromString("path_hooks"));
    PyObject *insert = hook != NULL && hooks != NULL ? PyObject_GetAttrString(hooks, "insert") : NULL;
    PyObject *args[2] = { PyLong_FromLong(0), hook };
    PyObject *result = insert != NULL && args[0] != NULL ? lu_call(insert, args, 2, NULL) : NULL;
    if (result == NULL) {
        PyErr_Clear();
    }
    Py_XDECREF(result);
    Py_XDECREF(args[0]);
    Py_XDECREF(insert);
    Py_XDECREF(hook);
    Py_XDECREF(module);
    Py_XDECREF(name);
}

int
lu_import_init(struct _is *interp)
{
    PyObject *import_name = PyUnicode_InternFromString("__import__");
    interp->import_func = Py_XNewRef(lu_ns_lookup(interp->builtins, import_name));
    Py_XDECREF(import_name);

    const struct lu_frozen *imp = find_cname(lu_frozen_builtin, "_imp");
    const struct lu_frozen *bootstrap = find_cname(lu_frozen_modules, "_frozen_importlib");
    if (imp == NULL || bootstrap == NULL) {
        lu_err_format(PyExc_SystemError, "the build froze no _imp or _frozen_importlib");
        return -1;
    }
    PyObject *imp_module = start_module(interp, imp, 0);
    PyObject *importlib = imp_module != NULL ? start_module(interp, bootstrap, 1) : NULL;
    if (importlib == NULL) {
        Py_XDECREF(imp_module);
        return -1;
    }
    interp->importlib = importlib;
    PyObject *sys = lu_ns_lookup(interp->modules, PyUnicode_InternFromString("sys"));
    PyObject *args[2] = { sys, imp_module };
    PyObject *result = call_function(importlib, "_install", args, 2);
    Py_DECREF(imp_module);
    if (result == NULL) {
        return -1;
    }
    Py_DECREF(result);
    result = call_function(importlib, "_install_external_importers", NULL, 0);
    if (result == NULL) {
        return -1;
    }
    Py_DECREF(result);
    install_zipimport(interp);
    return 0;
}
