/* Startup (Python/pylifecycle.c, Modules/main.c): Py_BytesMain reads a
   PyConfig, Py_InitializeFromConfig runs the native bootstrap order of DESIGN
   4.4, the frozen kernel, sys and importlib, and Py_RunMain runs the program
   and finalizes.

   Native order:
   1. the config, the runtime record, the main interpreter, the main carrier
      and task, and the hash secret before anything is hashed;
   2. layouts and singletons;
   3. interned names, sys.modules, _lucifer, builtins and sys;
   4. from here a failure prints the exception's type and message, exit 1;
   5. the kernel, sys.configure, importlib. */

#include "interp/exceptions.h"
#include "interp/frozen.h"
#include "prim/prim.h"
#include "marshal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

#define LU_MAGIC (3627 | ((long)'\r' << 16) | ((long)'\n' << 24))

/* Prints "Type: message" for an exception raised before the program runs
   (the kernel is not up, or the file is bad), or its traceback when it has
   one, and returns 1. */
static int
report(void)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL) {
        fprintf(stderr, "lucifer: error return without an exception set\n");
        return 1;
    }
    if (((PyBaseExceptionObject *)exc)->traceback != NULL) {
        lu_err_display(exc);            /* the kernel or importlib failed: show where */
        Py_DECREF(exc);
        return 1;
    }
    PyObject *args = ((PyBaseExceptionObject *)exc)->args;
    PyObject *msg = args != NULL && PyTuple_GET_SIZE(args) > 0 ? PyTuple_GET_ITEM(args, 0) : NULL;
    const char *text = msg != NULL && PyUnicode_Check(msg) ? PyUnicode_AsUTF8(msg) : NULL;
    if (text != NULL) {
        fprintf(stderr, "%s: %s\n", Py_TYPE(exc)->tp_name, text);
    }
    else {
        PyErr_Clear();
        fprintf(stderr, "%s\n", Py_TYPE(exc)->tp_name);
    }
    Py_DECREF(exc);
    return 1;
}

/* The status of a program that ended by an exception nothing caught
   (Python/pythonrun.c _Py_HandleSystemExit, then the excepthook): SystemExit
   leaves quietly with its code, anything else prints a traceback. */
static int
report_uncaught(void)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL) {
        fprintf(stderr, "lucifer: error return without an exception set\n");
        return 1;
    }
    int status = 1;
    if (PyType_IsSubtype(Py_TYPE(exc), (PyTypeObject *)PyExc_SystemExit)) {
        PyObject *code = ((PySystemExitObject *)exc)->code;
        long value;
        if (code == NULL || code == Py_None) {
            status = 0;
        }
        else if (PyLong_Check(code) && lu_long_as_long(code, &value) == 0) {
            status = (int)value;
        }
        else {
            PyObject *s = PyUnicode_Check(code) ? Py_NewRef(code) : NULL;
            if (s == NULL) {
                PyObject *str = _PyType_Lookup(Py_TYPE(code), LU_ID(__str__));
                s = str != NULL ? lu_call(str, &code, 1, NULL) : NULL;
                PyErr_Clear();
            }
            fprintf(stderr, "%s\n", s != NULL && PyUnicode_Check(s) ? PyUnicode_AsUTF8(s) : "");
            Py_XDECREF(s);
        }
    }
    else {
        lu_err_display(exc);
    }
    Py_DECREF(exc);
    return status;
}

static int
add_module(struct _is *interp, PyObject *module)
{
    if (module == NULL) {
        return -1;
    }
    int err = lu_ns_insert(interp->modules, ((PyModuleObject *)module)->md_name, module);
    Py_DECREF(module);
    return err;
}

extern PyTypeObject *const lu_exception_types[];
extern PyTypeObject _PyExc_IncompleteInputError;

/* _PyBuiltin_Init and _PyBuiltins_AddExceptions: the static objects and
   types builtins names. */
static int
add_builtins(PyObject *builtins)
{
    static const struct { const char *name; PyObject *value; } objects[] = {
        { "None", Py_None }, { "Ellipsis", Py_Ellipsis }, { "NotImplemented", Py_NotImplemented },
        { "False", Py_False }, { "True", Py_True }, { "__debug__", Py_True },
        { "bool", (PyObject *)&PyBool_Type }, { "memoryview", (PyObject *)&PyMemoryView_Type },
        { "bytearray", (PyObject *)&PyByteArray_Type }, { "bytes", (PyObject *)&PyBytes_Type },
        { "classmethod", (PyObject *)&PyClassMethod_Type },
        { "complex", (PyObject *)&PyComplex_Type }, { "dict", (PyObject *)&PyDict_Type },
        { "enumerate", (PyObject *)&PyEnum_Type }, { "filter", (PyObject *)&PyFilter_Type },
        { "float", (PyObject *)&PyFloat_Type }, { "frozenset", (PyObject *)&PyFrozenSet_Type },
        { "property", (PyObject *)&PyProperty_Type }, { "int", (PyObject *)&PyLong_Type },
        { "list", (PyObject *)&PyList_Type }, { "map", (PyObject *)&PyMap_Type },
        { "object", (PyObject *)&PyBaseObject_Type }, { "range", (PyObject *)&PyRange_Type },
        { "reversed", (PyObject *)&PyReversed_Type }, { "set", (PyObject *)&PySet_Type },
        { "slice", (PyObject *)&PySlice_Type }, { "staticmethod", (PyObject *)&PyStaticMethod_Type },
        { "str", (PyObject *)&PyUnicode_Type }, { "super", (PyObject *)&PySuper_Type },
        { "tuple", (PyObject *)&PyTuple_Type }, { "type", (PyObject *)&PyType_Type },
        { "zip", (PyObject *)&PyZip_Type }, { "EnvironmentError", NULL }, { "IOError", NULL },
        { "_IncompleteInputError", (PyObject *)&_PyExc_IncompleteInputError },
    };
    for (size_t i = 0; i < sizeof objects / sizeof objects[0]; i++) {
        PyObject *name = PyUnicode_InternFromString(objects[i].name);
        PyObject *value = objects[i].value != NULL ? objects[i].value : PyExc_OSError;
        int err = name == NULL || lu_ns_insert(builtins, name, value) < 0;
        Py_XDECREF(name);
        if (err) {
            return -1;
        }
    }
    for (PyTypeObject *const *tp = lu_exception_types; *tp != NULL; tp++) {
        if (strchr((*tp)->tp_name, '.') != NULL) {
            continue;                   /* concurrent.interpreters' own */
        }
        PyObject *name = PyUnicode_InternFromString((*tp)->tp_name);
        int err = name == NULL || lu_ns_insert(builtins, name, (PyObject *)*tp) < 0;
        Py_XDECREF(name);
        if (err) {
            return -1;
        }
    }
    return 0;
}

static int
boot_native(struct _is *interp)
{
    interp->max_str_digits = interp->config.int_max_str_digits;
    if ((interp->interned = PyDict_New()) == NULL) {
        return -1;
    }
#define LU_ID_INIT(name) \
    if ((LU_ID(name) = PyUnicode_InternFromString(#name)) == NULL) { \
        return -1; \
    }
    LU_IDENTIFIERS(LU_ID_INIT)
#undef LU_ID_INIT
    if ((interp->modules = PyDict_New()) == NULL) {
        return -1;
    }
    PyObject *builtins = lu_module_new("builtins");
    PyObject *sys = lu_module_new("sys");
    if (builtins == NULL || sys == NULL) {
        Py_XDECREF(builtins);
        Py_XDECREF(sys);
        return -1;
    }
    interp->builtins = Py_NewRef(PyModule_GetDict(builtins));
    interp->sysdict = Py_NewRef(PyModule_GetDict(sys));
    if (lu_types_init(interp) < 0 || add_builtins(interp->builtins) < 0) {
        Py_DECREF(builtins);
        Py_DECREF(sys);
        return -1;
    }
    PyObject *modules_name = PyUnicode_InternFromString("modules");
    if (modules_name == NULL || lu_ns_insert(interp->sysdict, modules_name, interp->modules) < 0) {
        Py_XDECREF(modules_name);
        Py_DECREF(builtins);
        Py_DECREF(sys);
        return -1;
    }
    Py_DECREF(modules_name);
    PyMethodDef *const sys_tables[] = { lu_sys_int_methods, lu_sys_frame_methods, lu_sys_native_methods };
    for (size_t i = 0; i < sizeof sys_tables / sizeof sys_tables[0]; i++) {
        for (PyMethodDef *ml = sys_tables[i]; ml->ml_name != NULL; ml++) {
            PyObject *f = PyCFunction_NewEx(ml, sys, NULL);
            PyObject *name = f != NULL ? PyUnicode_InternFromString(ml->ml_name) : NULL;
            int err = name == NULL || lu_ns_insert(interp->sysdict, name, f) < 0;
            Py_XDECREF(name);
            Py_XDECREF(f);
            if (err) {
                return -1;
            }
        }
    }
    if (add_module(interp, builtins) < 0 || add_module(interp, sys) < 0) {
        return -1;
    }
    return add_module(interp, lu_module_from_def(&lu_lucifer_module));
}

/* __main__ as pyrun_file leaves it for a .pyc: __file__ and the
   SourcelessFileLoader importlib's external half provides. */
static int
set_main_file(struct _is *interp, PyObject *main, const wchar_t *path)
{
    PyObject *file = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, path, (Py_ssize_t)wcslen(path));
    PyObject *external = lu_ns_lookup(interp->modules, PyUnicode_InternFromString("_frozen_importlib_external"));
    PyObject *cls = external != NULL
        ? lu_ns_lookup(PyModule_GetDict(external), PyUnicode_InternFromString("SourcelessFileLoader")) : NULL;
    PyObject *args[2] = { LU_ID(__main__), file };
    PyObject *loader = file != NULL && cls != NULL ? lu_call(cls, args, 2, NULL) : NULL;
    PyObject *ns = PyModule_GetDict(main);
    int err = loader == NULL || lu_ns_insert(ns, LU_ID(__loader__), loader) < 0
        || lu_ns_insert(ns, PyUnicode_InternFromString("__file__"), file) < 0;
    Py_XDECREF(file);
    Py_XDECREF(loader);
    return err ? -1 : 0;
}

/* run_pyc_file: the 16-byte header, then the code object. */
static int
run_pyc(struct _is *interp, const char *program, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        int err = errno;
        fprintf(stderr, "%s: can't open file '%s': [Errno %d] %s\n", program, path, err, strerror(err));
        return 2;
    }
    char *data = NULL;
    size_t size = 0, cap = 0;
    for (;;) {
        if (size == cap) {
            cap = cap ? cap * 2 : 65536;
            char *grown = realloc(data, cap);
            if (grown == NULL) {
                free(data);
                fclose(fp);
                PyErr_NoMemory();
                return report();
            }
            data = grown;
        }
        size_t n = fread(data + size, 1, cap - size, fp);
        if (n == 0) {
            break;
        }
        size += n;
    }
    fclose(fp);
    PyObject *code = NULL;
    const unsigned char *h = (const unsigned char *)data;
    if (size < 16 || (h[0] | h[1] << 8 | (long)h[2] << 16 | (long)h[3] << 24) != LU_MAGIC) {
        PyErr_SetString(PyExc_RuntimeError, "Bad magic number in .pyc file");
    }
    else {
        code = PyMarshal_ReadObjectFromString(data + 16, (Py_ssize_t)(size - 16));
        if (code != NULL && !PyCode_Check(code)) {
            Py_CLEAR(code);
            PyErr_SetString(PyExc_RuntimeError, "Bad code object in .pyc file");
        }
    }
    free(data);
    if (code == NULL) {
        return report();
    }
    PyObject *main = lu_module_new("__main__");
    PyObject *result = NULL;
    if (main != NULL) {
        PyObject *ns = PyModule_GetDict(main);
        PyObject *builtins = lu_ns_lookup(interp->modules, LU_ID(builtins));
        if (lu_ns_insert(ns, LU_ID(__builtins__), builtins) == 0
            && lu_ns_insert(interp->modules, LU_ID(__main__), main) == 0
            && set_main_file(interp, main, interp->config.run_filename) == 0) {
            result = PyEval_EvalCode(code, ns, ns);
        }
        Py_DECREF(main);
    }
    Py_DECREF(code);
    if (result == NULL) {
        return report_uncaught();
    }
    Py_DECREF(result);
    return 0;
}

/* The native bootstrap, then the frozen kernel, sys and importlib. */
PyStatus
Py_InitializeFromConfig(const PyConfig *src)
{
    struct _is *interp = &_PyRuntime.main_interp;
    if (interp->builtins != NULL) {
        return PyStatus_Error("the runtime is already initialized");
    }
    PyConfig config;
    PyStatus status = lu_config_read_copy(&config, src);
    if (!PyStatus_Exception(status)) {
        status = lu_runtime_options(&config);
    }
    if (PyStatus_Exception(status)) {
        return status;
    }
    interp->config = config;
    if (lu_hash_secret_init(config.use_hash_seed, config.hash_seed) < 0) {
        return PyStatus_Error("_Py_HashRandomization_Init: failed to get random numbers to initialize Python");
    }
    PyThreadState *main_task = PyThreadState_New(interp);
    if (main_task == NULL) {
        return PyStatus_Error("lucifer: no memory for the main task");
    }
    main_task->_whence = LU_WHENCE_INIT;
    PyEval_RestoreThread(main_task);
    lu_layouts_init();
    if (boot_native(interp) < 0 || lu_kernel_boot(interp) < 0 || lu_sys_configure(interp) < 0
        || lu_import_init(interp) < 0) {
        report();
        return PyStatus_Exit(1);
    }
    return PyStatus_Ok();
}

/* Py_FinalizeEx, as far as there is something to finalize: atexit's
   callbacks run, then the runtime counts as finalizing. */
int
Py_FinalizeEx(void)
{
    struct _is *interp = &_PyRuntime.main_interp;
    PyObject *atexit = lu_ns_lookup(interp->modules, PyUnicode_InternFromString("atexit"));
    PyObject *run = atexit != NULL
        ? lu_ns_lookup(PyModule_GetDict(atexit), PyUnicode_InternFromString("_run_exitfuncs")) : NULL;
    if (run != NULL) {
        PyObject *result = lu_call(run, NULL, 0, NULL);
        if (result == NULL) {
            PyErr_Clear();
        }
        Py_XDECREF(result);
    }
    __atomic_store_n(&_PyRuntime.finalizing, 1, __ATOMIC_RELAXED);
    return 0;
}

/* Modules/main.c: run the file the config names, then finalize. */
int
Py_RunMain(void)
{
    struct _is *interp = &_PyRuntime.main_interp;
    const PyConfig *config = &interp->config;
    char *program = Py_EncodeLocale(config->orig_argv.length > 0 ? config->orig_argv.items[0] : L"lucifer", NULL);
    char *path = Py_EncodeLocale(config->run_filename, NULL);
    int status = run_pyc(interp, program, path);
    PyMem_Free(program);
    PyMem_Free(path);
    if (Py_FinalizeEx() < 0 && status == 0) {
        status = 120;
    }
    return status;
}

int
Py_BytesMain(int argc, char **argv)
{
    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    PyStatus status = PyConfig_SetBytesArgv(&config, argc, argv);
    if (!PyStatus_Exception(status)) {
        status = Py_InitializeFromConfig(&config);
    }
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        Py_ExitStatusException(status);
    }
    return Py_RunMain();
}

/* Embedding (Py_InitializeEx): the same startup without a program. A
   failure is fatal. */
void
Py_InitializeEx(int install_sigs)
{
    if (_PyRuntime.main_interp.builtins != NULL) {
        return;
    }
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.site_import = 0;
    PyStatus status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status)) {
        Py_ExitStatusException(status);
    }
}

void
Py_Initialize(void)
{
    Py_InitializeEx(1);
}
