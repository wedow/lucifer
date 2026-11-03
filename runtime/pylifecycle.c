/* Startup (Python/pylifecycle.c, Modules/main.c): the native bootstrap order
   of DESIGN 4.4, then the frozen kernel, then the program. Command-line
   handling is minimal until the config ticket (K1n): -I, -S and a .pyc path.

   Native order:
   1. the runtime record, the main interpreter, the main carrier and task,
      and the hash secret before anything is hashed;
   2. layouts and singletons;
   3. interned names, sys.modules, _lucifer, builtins and sys;
   4. from here a failure prints the exception's type and message, exit 1. */

#include "interp/frozen.h"
#include "marshal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LU_MAGIC (3627 | ((long)'\r' << 16) | ((long)'\n' << 24))

struct lu_options {
    int isolated;               /* -I: also -E and -s */
    int no_site;                /* -S */
    const char *script;
};

static int
usage(const char *program, const char *message)
{
    fprintf(stderr, "%s\nusage: %s [-I] [-S] file.pyc\n", message, program);
    return 2;
}

/* config_init_hash_seed: PYTHONHASHSEED unless -E or -I. */
static int
hash_seed(const struct lu_options *opts, int *use_seed, unsigned long *seed)
{
    const char *env = opts->isolated ? NULL : getenv("PYTHONHASHSEED");
    *use_seed = 0;
    if (env == NULL || *env == '\0' || strcmp(env, "random") == 0) {
        return 0;
    }
    char *end;
    errno = 0;
    unsigned long value = strtoul(env, &end, 10);
    if (*end != '\0' || errno != 0 || value > 4294967295UL || *env == '-') {
        fprintf(stderr, "Fatal Python error: config_init_hash_seed: PYTHONHASHSEED must be "
                "\"random\" or an integer in range [0; 4294967295]\n");
        return -1;
    }
    *use_seed = 1;
    *seed = value;
    return 0;
}

/* Prints "Type: message" for the raised exception and returns 1. The
   traceback module renders uncaught exceptions once it is frozen (K1k). */
static int
report(void)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL) {
        fprintf(stderr, "lucifer: error return without an exception set\n");
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

static int
boot_native(struct _is *interp)
{
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
    PyObject *modules_name = PyUnicode_InternFromString("modules");
    if (modules_name == NULL || lu_ns_insert(interp->sysdict, modules_name, interp->modules) < 0) {
        Py_XDECREF(modules_name);
        Py_DECREF(builtins);
        Py_DECREF(sys);
        return -1;
    }
    Py_DECREF(modules_name);
    if (add_module(interp, builtins) < 0 || add_module(interp, sys) < 0) {
        return -1;
    }
    return add_module(interp, lu_module_from_def(&lu_lucifer_module));
}

/* run_pyc_file: the 16-byte header, then the code object. */
static int
run_pyc(struct _is *interp, const char *program, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        char cwd[4096];
        int err = errno;
        int relative = path[0] != '/' && getcwd(cwd, sizeof cwd) != NULL;
        fprintf(stderr, "%s: can't open file '%s%s%s': [Errno %d] %s\n", program,
                relative ? cwd : "", relative ? "/" : "", path, err, strerror(err));
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
            && lu_ns_insert(interp->modules, LU_ID(__main__), main) == 0) {
            result = PyEval_EvalCode(code, ns, ns);
        }
        Py_DECREF(main);
    }
    Py_DECREF(code);
    if (result == NULL) {
        return report();
    }
    Py_DECREF(result);
    return 0;
}

int
Py_BytesMain(int argc, char **argv)
{
    struct lu_options opts = { 0 };
    const char *program = argc > 0 ? argv[0] : "lucifer";
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        for (const char *c = argv[i] + 1; *c; c++) {
            if (*c == 'I') {
                opts.isolated = 1;
            }
            else if (*c == 'S') {
                opts.no_site = 1;
            }
            else {
                char message[64];
                snprintf(message, sizeof message, "Unknown option: -%c", *c);
                return usage(program, message);
            }
        }
    }
    if (i >= argc) {
        return usage(program, "lucifer: a .pyc file to run is required");
    }
    opts.script = argv[i];

    int use_seed;
    unsigned long seed = 0;
    if (hash_seed(&opts, &use_seed, &seed) < 0) {
        return 1;
    }
    if (lu_hash_secret_init(use_seed, seed) < 0) {
        fprintf(stderr, "Fatal Python error: _Py_HashRandomization_Init: failed to get "
                "random numbers to initialize Python\n");
        return 1;
    }
    struct _is *interp = &_PyRuntime.main_interp;
    if (lu_task_new(interp) == NULL) {
        fprintf(stderr, "Fatal Python error: lucifer: no memory for the main task\n");
        return 1;
    }
    lu_layouts_init();
    if (boot_native(interp) < 0 || lu_kernel_boot(interp) < 0) {
        return report();
    }
    return run_pyc(interp, program, opts.script);
}
