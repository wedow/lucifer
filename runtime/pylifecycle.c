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

#include "jit/jit.h"
#include "interp/exceptions.h"
#include "interp/frozen.h"
#include "interp/instrument.h"
#include "prim/prim.h"
#include "marshal.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

/* file_is_closed */
static int
stream_closed(PyObject *stream)
{
    PyObject *closed = PyObject_GetAttrString(stream, "closed");
    if (closed == NULL) {
        PyErr_Clear();
        return 0;
    }
    int result = lu_is_true(closed);
    Py_DECREF(closed);
    PyErr_Clear();
    return result > 0;
}

/* Flushes sys.NAME unless it is unset, None or closed: 0, or -1 with the
   exception set. */
static int
flush_std_stream(const char *name)
{
    PyObject *key = PyUnicode_InternFromString(name);
    PyObject *stream = key != NULL ? lu_ns_lookup(_PyRuntime.main_interp.sysdict, key) : NULL;
    Py_XDECREF(key);
    if (stream == NULL || stream == Py_None) {
        return 0;
    }
    Py_INCREF(stream);
    int status = 0;
    if (!stream_closed(stream)) {
        PyObject *flush = PyObject_GetAttrString(stream, "flush");
        PyObject *result = flush != NULL ? PyObject_CallNoArgs(flush) : NULL;
        Py_XDECREF(flush);
        Py_XDECREF(result);
        status = result == NULL ? -1 : 0;
    }
    Py_DECREF(stream);
    return status;
}

/* pythonrun.c flush_io: after the program ran, stderr then stdout, and the
   exception being handled survives. */
static void
flush_io(void)
{
    PyObject *exc = PyErr_GetRaisedException();
    flush_std_stream("stderr");
    PyErr_Clear();
    flush_std_stream("stdout");
    PyErr_Clear();
    PyErr_SetRaisedException(exc);
}

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
                PyObject *str = _PyType_LookupRef(Py_TYPE(code), LU_ID(__str__));
                s = str != NULL ? lu_call(str, &code, 1, NULL) : NULL;
                Py_XDECREF(str);
                PyErr_Clear();
            }
            const char *text = s != NULL && PyUnicode_Check(s) ? PyUnicode_AsUTF8(s) : "";
            lu_stderr_write(text, strlen(text));
            lu_stderr_write("\n", 1);
            Py_XDECREF(s);
        }
    }
    else {
        if (PyType_IsSubtype(Py_TYPE(exc), (PyTypeObject *)PyExc_KeyboardInterrupt)) {
            _PyRuntime.signals.unhandled_keyboard_interrupt = 1;
        }
        /* sys.excepthook(type, value, traceback); without one, the default display */
        PyObject *hook = lu_ns_lookup(_PyRuntime.main_interp.sysdict, PyUnicode_InternFromString("excepthook"));
        PyObject *tb = ((PyBaseExceptionObject *)exc)->traceback;
        PyObject *args[3] = { (PyObject *)Py_TYPE(exc), exc, tb != NULL ? tb : Py_None };
        PyObject *result = hook != NULL && hook != Py_None ? lu_call(hook, args, 3, NULL) : NULL;
        if (result == NULL) {
            PyErr_Clear();
            lu_err_display(exc);
        }
        Py_XDECREF(result);
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
    if (lu_types_init(interp) < 0 || lu_fileobject_init() < 0
        || add_builtins(interp->builtins) < 0 || lu_cc_init() < 0) {
        Py_DECREF(builtins);
        Py_DECREF(sys);
        return -1;
    }
    lu_errors_init(interp);     /* the last-resort MemoryError, before anything can run out */
    PyObject *modules_name = PyUnicode_InternFromString("modules");
    if (modules_name == NULL || lu_ns_insert(interp->sysdict, modules_name, interp->modules) < 0) {
        Py_XDECREF(modules_name);
        Py_DECREF(builtins);
        Py_DECREF(sys);
        return -1;
    }
    Py_DECREF(modules_name);
    PyMethodDef *const sys_tables[] = { lu_sys_int_methods, lu_sys_frame_methods, lu_sys_native_methods, lu_sys_tracing_methods };
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
    PyObject *monitoring = lu_monitoring_module_new();
    PyObject *monitoring_name = monitoring != NULL ? PyUnicode_InternFromString("monitoring") : NULL;
    int monitoring_err = monitoring_name == NULL || lu_ns_insert(interp->sysdict, monitoring_name, monitoring) < 0;
    Py_XDECREF(monitoring_name);
    Py_XDECREF(monitoring);
    if (monitoring_err) {
        return -1;
    }
    if (add_module(interp, builtins) < 0 || add_module(interp, sys) < 0) {
        return -1;
    }
    return add_module(interp, lu_module_from_def(&lu_lucifer_module));
}

/* __main__ as pyrun_file leaves it for a program file: __file__, __cached__
   and the loader importlib's external half provides for the file's kind
   (SourceFileLoader, or SourcelessFileLoader for a .pyc). */
static int
set_main_file(struct _is *interp, PyObject *main, const wchar_t *path, const char *loader_class)
{
    PyObject *file = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, path, (Py_ssize_t)wcslen(path));
    PyObject *external = lu_ns_lookup(interp->modules, PyUnicode_InternFromString("_frozen_importlib_external"));
    PyObject *cls = external != NULL
        ? lu_ns_lookup(PyModule_GetDict(external), PyUnicode_InternFromString(loader_class)) : NULL;
    PyObject *args[2] = { LU_ID(__main__), file };
    PyObject *loader = file != NULL && cls != NULL ? lu_call(cls, args, 2, NULL) : NULL;
    PyObject *ns = PyModule_GetDict(main);
    int err = loader == NULL || lu_ns_insert(ns, LU_ID(__loader__), loader) < 0
        || lu_ns_insert(ns, PyUnicode_InternFromString("__file__"), file) < 0
        || lu_ns_insert(ns, PyUnicode_InternFromString("__cached__"), Py_None) < 0;
    Py_XDECREF(file);
    Py_XDECREF(loader);
    return err ? -1 : 0;
}

/* Everything the stream holds, or NULL with an exception set. */
static PyObject *
read_all(FILE *fp)
{
    char *data = NULL;
    size_t size = 0, cap = 0;
    for (;;) {
        if (size == cap) {
            cap = cap ? cap * 2 : 65536;
            char *grown = realloc(data, cap);
            if (grown == NULL) {
                free(data);
                return PyErr_NoMemory();
            }
            data = grown;
        }
        size_t n = fread(data + size, 1, cap - size, fp);
        if (n == 0) {
            break;
        }
        size += n;
    }
    PyObject *bytes = PyBytes_FromStringAndSize(data, (Py_ssize_t)size);
    free(data);
    return bytes;
}

/* maybe_pyc_file: a .pyc by name or by its magic number. */
static int
is_pyc(const char *path, PyObject *data)
{
    size_t n = strlen(path);
    const unsigned char *h = (const unsigned char *)PyBytes_AS_STRING(data);
    return (n >= 4 && strcmp(path + n - 4, ".pyc") == 0)
        || (PyBytes_GET_SIZE(data) >= 2 && (h[0] | h[1] << 8) == (LU_PYC_MAGIC & 0xffff));
}

/* run_pyc_file's reading: the 16-byte header, then the code object. */
static PyObject *
code_of_pyc(PyObject *data)
{
    const unsigned char *h = (const unsigned char *)PyBytes_AS_STRING(data);
    Py_ssize_t size = PyBytes_GET_SIZE(data);
    if (size < 16 || (h[0] | h[1] << 8 | (long)h[2] << 16 | (long)h[3] << 24) != LU_PYC_MAGIC) {
        PyErr_SetString(PyExc_RuntimeError, "Bad magic number in .pyc file");
        return NULL;
    }
    PyObject *code = PyMarshal_ReadObjectFromString((const char *)h + 16, size - 16);
    if (code != NULL && !PyCode_Check(code)) {
        Py_CLEAR(code);
        PyErr_SetString(PyExc_RuntimeError, "Bad code object in .pyc file");
    }
    return code;
}

/* builtins.compile(source, filename, "exec"), inheriting nothing. */
static PyObject *
compile_main(struct _is *interp, PyObject *source, PyObject *filename)
{
    PyObject *builtins = lu_ns_lookup(interp->modules, LU_ID(builtins));
    PyObject *compile = builtins != NULL
        ? lu_ns_lookup(PyModule_GetDict(builtins), PyUnicode_InternFromString("compile")) : NULL;
    PyObject *mode = PyUnicode_FromString("exec");
    PyObject *args[5] = { source, filename, mode, PyLong_FromLong(0), Py_True };
    PyObject *code = compile != NULL && mode != NULL && args[3] != NULL ? lu_call(compile, args, 5, NULL) : NULL;
    Py_XDECREF(mode);
    Py_XDECREF(args[3]);
    return code;
}

/* A fresh __main__ in sys.modules, as pyrun_file leaves it: __builtins__,
   and the loader of the file it came from, or the builtin importer for -c,
   standard input and embedded strings (loader_class NULL). Borrowed. */
static PyObject *
new_main(struct _is *interp, const wchar_t *path, const char *loader_class)
{
    PyObject *main = lu_module_new("__main__");
    if (main == NULL) {
        return NULL;
    }
    PyObject *ns = PyModule_GetDict(main);
    PyObject *builtins = lu_ns_lookup(interp->modules, LU_ID(builtins));
    PyObject *importlib = lu_ns_lookup(interp->modules, PyUnicode_InternFromString("_frozen_importlib"));
    PyObject *builtin_importer = importlib != NULL
        ? lu_ns_lookup(PyModule_GetDict(importlib), PyUnicode_InternFromString("BuiltinImporter")) : NULL;
    int err = lu_ns_insert(ns, LU_ID(__builtins__), builtins) < 0
        || lu_ns_insert(interp->modules, LU_ID(__main__), main) < 0
        || (loader_class != NULL ? set_main_file(interp, main, path, loader_class) < 0
            : builtin_importer != NULL && lu_ns_insert(ns, LU_ID(__loader__), builtin_importer) < 0);
    Py_DECREF(main);                    /* sys.modules holds it */
    return err ? NULL : main;
}

/* What follows the main code's activation: the exit status of its result. */
static int
finish_main(PyObject *result)
{
    flush_io();
    if (result == NULL) {
        return report_uncaught();
    }
    Py_DECREF(result);
    return 0;
}

/* Runs code as __main__. loader_class is NULL for -c and standard input,
   which have no file. */
static int
run_main_code(struct _is *interp, PyObject *code, const wchar_t *path, const char *loader_class)
{
    PyObject *main = new_main(interp, path, loader_class);
    return finish_main(main != NULL ? PyEval_EvalCode(code, PyModule_GetDict(main), PyModule_GetDict(main)) : NULL);
}

/* -m: runpy runs the module as __main__. */
static int
run_module(struct _is *interp, const wchar_t *name)
{
    PyObject *runpy = PyImport_ImportModule("runpy");
    PyObject *module = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, name, (Py_ssize_t)wcslen(name));
    PyObject *result = NULL;
    if (runpy != NULL && module != NULL && new_main(interp, NULL, NULL) != NULL) {
        result = PyObject_CallMethod(runpy, "_run_module_as_main", "OO", module, Py_True);
    }
    Py_XDECREF(runpy);
    Py_XDECREF(module);
    return finish_main(result);
}

/* The program the config names: -m's module, -c's command, a file (source or
   .pyc), or standard input. */
static int
run_program(struct _is *interp, const char *program, const char *path)
{
    const PyConfig *config = &interp->config;
    PyObject *code = NULL;
    const wchar_t *file = NULL;
    const char *loader = NULL;
    if (config->run_module != NULL) {
        return run_module(interp, config->run_module);
    }
    if (config->run_command != NULL) {
        PyObject *source = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, config->run_command,
                                                     (Py_ssize_t)wcslen(config->run_command));
        PyObject *name = PyUnicode_FromString("<string>");
        code = source != NULL && name != NULL ? compile_main(interp, source, name) : NULL;
        Py_XDECREF(source);
        Py_XDECREF(name);
    }
    else if (config->run_filename == NULL || wcscmp(config->run_filename, L"-") == 0) {
        PyObject *source = read_all(stdin);
        PyObject *name = PyUnicode_FromString("<stdin>");
        code = source != NULL && name != NULL ? compile_main(interp, source, name) : NULL;
        Py_XDECREF(source);
        Py_XDECREF(name);
    }
    else {
        FILE *fp = fopen(path, "rb");
        if (fp == NULL) {
            int err = errno;
            fprintf(stderr, "%s: can't open file '%s': [Errno %d] %s\n", program, path, err, strerror(err));
            return 2;
        }
        PyObject *data = read_all(fp);
        fclose(fp);
        file = config->run_filename;
        if (data != NULL && is_pyc(path, data)) {
            loader = "SourcelessFileLoader";
            code = code_of_pyc(data);
        }
        else if (data != NULL) {
            loader = "SourceFileLoader";
            PyObject *name = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, file, (Py_ssize_t)wcslen(file));
            code = name != NULL ? compile_main(interp, data, name) : NULL;
            Py_XDECREF(name);
        }
        Py_XDECREF(data);
    }
    if (code == NULL) {
        return report();
    }
    int status = run_main_code(interp, code, file, loader);
    Py_DECREF(code);
    return status;
}

/* -X faulthandler, PYTHONFAULTHANDLER and the dev mode enable the fault handler
   once sys.stderr exists. */
static void
faulthandler_start(struct _is *interp)
{
    const PyConfig *config = &interp->config;
    int on = config->faulthandler > 0 || config->dev_mode > 0;
    for (Py_ssize_t i = 0; i < config->xoptions.length; i++) {
        on |= wcscmp(config->xoptions.items[i], L"faulthandler") == 0;
    }
    const char *env = config->use_environment ? getenv("PYTHONFAULTHANDLER") : NULL;
    on |= env != NULL && *env != '\0';
    if (!on) {
        return;
    }
    PyObject *name = PyUnicode_FromString("faulthandler");
    PyObject *module = name != NULL ? lu_call(interp->import_func, &name, 1, NULL) : NULL;
    PyObject *result = module != NULL ? PyObject_CallMethod(module, "enable", NULL) : NULL;
    if (result == NULL) {
        PyErr_Clear();
    }
    Py_XDECREF(result);
    Py_XDECREF(module);
    Py_XDECREF(name);
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
    if (config.install_signal_handlers) {
        /* init_signals: a write to a closed pipe or past a file size limit fails with EPIPE or
           EFBIG, as OSError, instead of killing the process. */
        signal(SIGPIPE, SIG_IGN);
        signal(SIGXFSZ, SIG_IGN);
    }
    __atomic_store_n(&_PyRuntime.finalizing, 0, __ATOMIC_RELAXED);
    if (lu_hash_secret_init(config.use_hash_seed, config.hash_seed) < 0) {
        return PyStatus_Error("_Py_HashRandomization_Init: failed to get random numbers to initialize Python");
    }
    PyThreadState *main_task = PyThreadState_New(interp);
    if (main_task == NULL) {
        return PyStatus_Error("lucifer: no memory for the main task");
    }
    main_task->_whence = LU_WHENCE_INIT;
    __atomic_store_n(&_PyRuntime.main_task, (struct lu_task *)main_task, __ATOMIC_RELEASE);
    PyEval_RestoreThread(main_task);
    lu_layouts_init();
    if (boot_native(interp) < 0 || lu_kernel_boot(interp) < 0 || lu_sys_configure(interp) < 0
        || lu_import_init(interp) < 0 || lu_sys_init_streams(interp) < 0) {
        report();
        return PyStatus_Exit(1);
    }
    faulthandler_start(interp);
    if (_PyRuntime.handoff.on) {
        lu_sched_start();
    }
    return PyStatus_Ok();
}

/* flush_std_files: stdout (a failure is reported and counts), then stderr
   (a failure is ignored). */
static int
flush_std_files(void)
{
    int status = 0;
    if (flush_std_stream("stdout") < 0) {
        lu_err_unraisable_text("Exception ignored while flushing sys.stdout", NULL);
        status = -1;
    }
    if (flush_std_stream("stderr") < 0) {
        PyErr_Clear();
    }
    return status;
}

/* finalize_modules: the __del__ of what only __main__ held runs while the
   namespace is still intact, as the shutdown collection's finalizer pass
   does in CPython, and then the namespace is emptied. The other modules
   stay: their code (weakref callbacks, stream finalizers) still runs during
   teardown. */
static void
finalize_modules(struct _is *interp)
{
    PyObject *main = lu_ns_lookup(interp->modules, LU_ID(__main__));
    if (main != NULL && PyModule_Check(main)) {
        PyObject *dict = PyModule_GetDict(main);
        PyObject *values = dict != NULL ? PyDict_Values(dict) : NULL;
        if (values != NULL) {
            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(values); i++) {
                PyObject_CallFinalizer(PyList_GET_ITEM(values, i));
            }
            PyDict_Clear(dict);
            Py_DECREF(values);
        }
        PyErr_Clear();
    }
}

/* The interpreter ends: its task is deleted and its state forgotten, so that
   Py_Initialize starts a new one. The objects it made are not freed: modules
   and types reach each other in cycles, and the collector does not run yet. */
static void lu_atexit_call(PyInterpreterState *);
static void
release_interpreter(struct _is *interp)
{
    lu_atexit_call(interp);         /* PyUnstable_AtExit callbacks, while C calls still work */
    PyThreadState *main_task = PyThreadState_Get();
    PyThreadState_Clear(main_task);
    PyEval_SaveThread();
    PyThreadState_Delete(main_task);
    for (int i = 0; i < LU_NSIG; i++) {
        _PyRuntime.signals.handlers[i].func = NULL;
        _PyRuntime.signals.handlers[i].tripped = 0;
    }
    _PyRuntime.signals.tripped = 0;
    interp->tasks_closed = 0;
    interp->stw.mutex = (PyMutex){ 0 };     /* held for good since the other tasks were shut down */
    interp->modules = interp->builtins = interp->sysdict = interp->interned = NULL;
    interp->dict = interp->last_resort_memory_error = NULL;
    interp->co_extra_user_count = 0;
    memset(interp->co_extra_freefuncs, 0, sizeof interp->co_extra_freefuncs);
    interp->co_extra_lock = (PyMutex){ 0 };
    interp->importlib = interp->import_func = NULL;
    interp->eval_frame = NULL;
    interp->specializing = 0;
    memset(interp->mcache, 0, sizeof interp->mcache);
    interp->kernel_booted = 0;
    memset(interp->static_types, 0, sizeof interp->static_types);
    memset(interp->kernel, 0, sizeof interp->kernel);
    interp->kernel_rebound = NULL;
    PyConfig_Clear(&interp->config);
}

/* Py_FinalizeEx, as far as there is something to finalize: the non-daemon
   threads are joined, atexit's callbacks run, the other threads are shut down,
   the runtime counts as finalizing, then the standard streams are flushed.
   CPython flushes once more after tearing the modules down; there is no
   teardown to print from yet. */
int
Py_FinalizeEx(void)
{
    struct _is *interp = &_PyRuntime.main_interp;
    /* wait_for_thread_shutdown: threading._shutdown joins the non-daemon threads. */
    PyObject *threading = lu_ns_lookup(interp->modules, PyUnicode_InternFromString("threading"));
    PyObject *shutdown = threading != NULL
        ? lu_ns_lookup(PyModule_GetDict(threading), PyUnicode_InternFromString("_shutdown")) : NULL;
    if (shutdown != NULL) {
        PyObject *result = lu_call(shutdown, NULL, 0, NULL);
        if (result == NULL) {
            lu_err_unraisable_text("Exception ignored on threading shutdown", NULL);
        }
        Py_XDECREF(result);
    }
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
    lu_sched_stop();
    /* What is left are daemon threads: they hang when they next attach (FOUNDATIONS 2.13 step 8), so
       the teardown runs without them. From here a join of a thread that has not finished raises
       PythonFinalizationError, so the __del__ of what only cycles held cannot hang on a daemon
       thread (gh-130402), and a stream lock a daemon froze holding fails after a grace period. */
    lu_task_shutdown_others(interp);
    __atomic_store_n(&_PyRuntime.finalizing, 1, __ATOMIC_RELAXED);
    int status = flush_std_files();
    lu_jit_report();
    finalize_modules(interp);
    lu_gc_collect_shutdown(PyThreadState_Get());
    flush_std_stream("stdout");
    flush_std_stream("stderr");
    PyErr_Clear();
    release_interpreter(interp);
    return status;
}

/* Modules/main.c exit_sigint: leave by SIGINT's default action so that the caller learns of
   the interrupt; the status is the fallback if the signal does not end the process. */
static int
exit_sigint(void)
{
    signal(SIGINT, SIG_DFL);
    kill(getpid(), SIGINT);
    return SIGINT + 128;
}

static int
finalize_status(int status)
{
    status = Py_FinalizeEx() < 0 && status == 0 ? 120 : status;
    return __atomic_load_n(&_PyRuntime.signals.unhandled_keyboard_interrupt, __ATOMIC_RELAXED) ? exit_sigint() : status;
}

/* Modules/main.c: run the file the config names, then finalize. */
int
Py_RunMain(void)
{
    struct _is *interp = &_PyRuntime.main_interp;
    const PyConfig *config = &interp->config;
    char *program = Py_EncodeLocale(config->orig_argv.length > 0 ? config->orig_argv.items[0] : L"lucifer", NULL);
    char *path = config->run_filename != NULL ? Py_EncodeLocale(config->run_filename, NULL) : NULL;
    int status = run_program(interp, program, path);
    PyMem_Free(program);
    PyMem_Free(path);
    return finalize_status(status);
}

void
lu_main_finished(PyObject *result)
{
    exit(finalize_status(finish_main(result)));
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

int
Py_IsInitialized(void)
{
    return _PyRuntime.main_interp.builtins != NULL;
}

void
Py_Finalize(void)
{
    Py_FinalizeEx();
}

/* pythonrun.c PyRun_SimpleStringFlags: the string runs in __main__, which is
   made on first use and kept for the next call. An uncaught exception is
   printed (SystemExit leaves quietly) and the result is -1. */
int
PyRun_SimpleStringFlags(const char *command, PyCompilerFlags *flags)
{
    struct _is *interp = &_PyRuntime.main_interp;
    PyObject *main = lu_ns_lookup(interp->modules, LU_ID(__main__));
    main = main != NULL ? main : new_main(interp, NULL, NULL);
    PyObject *source = main != NULL ? PyUnicode_FromString(command) : NULL;
    PyObject *name = source != NULL ? PyUnicode_FromString("<string>") : NULL;
    PyObject *code = name != NULL ? compile_main(interp, source, name) : NULL;
    PyObject *result = code != NULL ? PyEval_EvalCode(code, PyModule_GetDict(main), PyModule_GetDict(main)) : NULL;
    Py_XDECREF(source);
    Py_XDECREF(name);
    Py_XDECREF(code);
    flush_io();
    if (result == NULL) {
        report_uncaught();
        return -1;
    }
    Py_DECREF(result);
    return 0;
}

#undef PyRun_SimpleString
int
PyRun_SimpleString(const char *command)
{
    return PyRun_SimpleStringFlags(command, NULL);
}

/* Py_NewInterpreter: lucifer has one interpreter per process. Extensions
   written against CPython, where this function never fails in-band,
   dereference what it returns (its ->interp, say), so NULL is not an
   option: an inert static thread state comes back instead. It is never
   attached and runs no code; the thread state the caller swapped out is put
   back first, so the failure paths of _testcapi.run_in_subinterp and
   friends keep working. */

static struct _is stub_interp;          /* zeroed: nothing ever runs on it */
static struct lu_task stub_task;        /* its thread state; a full task, so
                                           a stray task cast stays in bounds */

PyThreadState *
Py_NewInterpreter(void)
{
    struct lu_task *t = lu_current_task();
    if (t != NULL && t->ts.state == _Py_THREAD_DETACHED) {
        lu_task_attach(&t->ts);
    }
    stub_task.ts.interp = &stub_interp;
    return &stub_task.ts;
}

/* Py_EndInterpreter: ending the inert interpreter runs the atexit callbacks
   registered on it, which is its whole lifecycle. Ending anything else
   would mean a real subinterpreter, which lucifer does not have. */
void
Py_EndInterpreter(PyThreadState *tstate)
{
    if (tstate == &stub_task.ts) {
        lu_atexit_call(&stub_interp);
    }
    else if (tstate != NULL) {
        Py_FatalError("Py_EndInterpreter: subinterpreters are not supported");
    }
}

/* PyUnstable_AtExit (Modules/atexitmodule.c): a C callback an interpreter
   runs when it ends, the last registered first, on its own list. */
struct lu_atexit {
    atexit_datacallbackfunc func;
    void *data;
    struct lu_atexit *next;
};

int
PyUnstable_AtExit(PyInterpreterState *interp, atexit_datacallbackfunc func, void *data)
{
    struct lu_atexit *callback = PyMem_Malloc(sizeof *callback);
    if (callback == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    callback->func = func;
    callback->data = data;
    PyMutex_Lock(&interp->atexit_lock);
    callback->next = interp->atexit;
    interp->atexit = callback;
    PyMutex_Unlock(&interp->atexit_lock);
    return 0;
}

/* _PyAtExit_Fini's half for C callbacks: each runs once and is freed. */
static void
lu_atexit_call(PyInterpreterState *interp)
{
    PyMutex_Lock(&interp->atexit_lock);
    struct lu_atexit *next = interp->atexit;
    interp->atexit = NULL;
    PyMutex_Unlock(&interp->atexit_lock);
    while (next != NULL) {
        struct lu_atexit *callback = next;
        next = callback->next;
        atexit_datacallbackfunc func = callback->func;
        void *data = callback->data;
        PyMem_Free(callback);
        func(data);
    }
}
