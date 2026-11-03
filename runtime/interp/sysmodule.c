/* sys (Python/sysmodule.c), the native half: the functions that need the
   runtime's layouts, and the dict of what the PyConfig decided, which the
   kernel's sys.py turns into flags, argv and the paths. */

#include "interp/frozen.h"
#include "prim/prim.h"

#include <stdlib.h>
#include <string.h>

/* sys.intern(string) */
static PyObject *
sys_intern(PyObject *m, PyObject *s)
{
    if (!PyUnicode_CheckExact(s)) {
        lu_err_format(PyExc_TypeError, "intern() argument must be str, not %.200s", Py_TYPE(s)->tp_name);
        return NULL;
    }
    PyObject *t = Py_NewRef(s);
    lu_intern(PyInterpreterState_Get(), &t);
    return t;
}

/* sys.getrefcount(object): the count the free-threaded build reports, local
   plus shared, immortal objects at their saturated value. */
static PyObject *
sys_getrefcount(PyObject *m, PyObject *o)
{
    return PyLong_FromLongLong(Py_REFCNT(o));
}

int
Py_IsFinalizing(void)
{
    return __atomic_load_n(&_PyRuntime.finalizing, __ATOMIC_RELAXED);
}

static PyObject *
sys_is_finalizing(PyObject *m, PyObject *unused)
{
    return Py_NewRef(Py_IsFinalizing() ? Py_True : Py_False);
}

PyMethodDef lu_sys_native_methods[] = {
    { "intern", sys_intern, METH_O, NULL },
    { "getrefcount", sys_getrefcount, METH_O, NULL },
    { "is_finalizing", sys_is_finalizing, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL },
};

/* The config dict */

static PyObject *
wide_str(const wchar_t *w)
{
    return w == NULL ? Py_NewRef(Py_None)
                     : PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, w, (Py_ssize_t)wcslen(w));
}

static PyObject *
wide_list(const PyWideStringList *l)
{
    PyObject *list = PyList_New(l->length);
    for (Py_ssize_t i = 0; list != NULL && i < l->length; i++) {
        PyObject *s = wide_str(l->items[i]);
        if (s == NULL) {
            Py_CLEAR(list);
            break;
        }
        PyList_SET_ITEM(list, i, s);
    }
    return list;
}

static int
put(PyObject *d, const char *key, PyObject *value)
{
    PyObject *k = value != NULL ? PyUnicode_InternFromString(key) : NULL;
    int err = k == NULL || lu_ns_insert(d, k, value) < 0;
    Py_XDECREF(k);
    Py_XDECREF(value);
    return err ? -1 : 0;
}

#define CONFIG_INTS(X) \
    X(parser_debug) X(inspect) X(interactive) X(optimization_level) X(write_bytecode) \
    X(user_site_directory) X(site_import) X(use_environment) X(verbose) X(bytes_warning) \
    X(quiet) X(use_hash_seed) X(isolated) X(dev_mode) X(warn_default_encoding) X(safe_path) \
    X(int_max_str_digits) X(enable_gil) X(thread_inherit_context) X(context_aware_warnings)

static int
append_name(PyObject *list, const char *name)
{
    PyObject *s = list != NULL ? PyUnicode_FromString(name) : NULL;
    int err = s == NULL || lu_list_append(list, s) < 0;
    Py_XDECREF(s);
    return err;
}

static PyObject *
config_dict(struct _is *interp)
{
    const PyConfig *c = &interp->config;
    PyObject *d = PyDict_New();
    if (d == NULL) {
        return NULL;
    }
    int err = 0;
#define PUT_INT(name) err = err || put(d, #name, PyLong_FromLongLong(c->name));
    CONFIG_INTS(PUT_INT)
#undef PUT_INT
    PyObject *names = PyList_New(0);
    for (const struct lu_frozen *m = lu_frozen_builtin; names != NULL && m->name != NULL; m++) {
        err = err || append_name(names, m->name);
    }
    err = err || append_name(names, "builtins") || append_name(names, "sys")
        || append_name(names, "_lucifer");
    err = err || put(d, "hash_seed", PyLong_FromLongLong((long long)c->hash_seed))
        || put(d, "utf8_mode", PyLong_FromLongLong(lu_utf8_mode(c)))
        || put(d, "argv", wide_list(&c->argv)) || put(d, "orig_argv", wide_list(&c->orig_argv))
        || put(d, "xoptions", wide_list(&c->xoptions))
        || put(d, "warnoptions", wide_list(&c->warnoptions))
        || put(d, "executable", wide_str(c->executable))
        || put(d, "pycache_prefix", wide_str(c->pycache_prefix))
        || put(d, "sys_path_0", wide_str(c->sys_path_0))
        || put(d, "pythonpath", c->pythonpath_env != NULL ? wide_str(c->pythonpath_env)
                                                          : PyUnicode_FromString(""))
        || put(d, "builtin_module_names", names);
    if (err) {
        Py_CLEAR(d);
    }
    return d;
}

/* _PySys_UpdateConfig: calls the kernel's sys.configure with the config dict. */
int
lu_sys_configure(struct _is *interp)
{
    PyObject *lucifer = lu_ns_lookup(interp->modules, LU_ID(_lucifer));
    PyObject *sys_name = PyUnicode_InternFromString("sys");
    PyObject *configure_name = PyUnicode_InternFromString("configure");
    PyObject *kernel_sys = sys_name != NULL
        ? lu_ns_lookup(PyModule_GetDict(lucifer), sys_name) : NULL;
    PyObject *configure = kernel_sys != NULL && configure_name != NULL
        ? lu_ns_lookup(PyModule_GetDict(kernel_sys), configure_name) : NULL;
    Py_XDECREF(sys_name);
    Py_XDECREF(configure_name);
    if (configure == NULL) {
        lu_err_format(PyExc_SystemError, "the kernel has no sys.configure");
        return -1;
    }
    PyObject *dict = config_dict(interp);
    PyObject *result = dict != NULL ? lu_call(configure, &dict, 1, NULL) : NULL;
    Py_XDECREF(dict);
    Py_XDECREF(result);
    return result == NULL ? -1 : 0;
}
