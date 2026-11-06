/* Running source from strings and files (Python/pythonrun.c, CPython 3.14):
   parse to an AST, compile it, and evaluate it, or read a .pyc. The
   interactive forms parse one statement at a time from the file. Ported
   with the runtime's own lookups for sys attributes, the importlib loader
   classes and stream flushing. */
#include "lu_cc.h"
#include "lucifer.h"
#include "marshal.h"
#include "pycore_ast.h"
#include "pycore_compile.h"
#include "pycore_parser.h"
#include "pycore_pyarena.h"
#include "errcode.h"
#include <unistd.h>

static PyObject *run_mod(mod_ty, PyObject *, PyObject *, PyObject *, PyCompilerFlags *,
                         PyArena *, PyObject *, int);
static PyObject *run_pyc_file(FILE *, PyObject *, PyObject *, PyCompilerFlags *);
static PyObject *_PyRun_File(FILE *, PyObject *, int, PyObject *, PyObject *, int,
                             PyCompilerFlags *);

static int
check_start(int start)
{
    if (start == Py_single_input || start == Py_file_input
        || start == Py_eval_input || start == Py_func_type_input) {
        return 0;
    }
    PyErr_SetString(PyExc_ValueError, "invalid start argument");
    return -1;
}

/* sys.stdout and sys.stderr flushed, the exception in flight kept aside. */
static void
flush_io(void)
{
    PyObject *exc = PyErr_GetRaisedException();
    const char *names[] = { "stderr", "stdout" };
    for (int i = 0; i < 2; i++) {
        PyObject *f = PySys_GetObject(names[i]);
        PyObject *r = f != NULL && f != Py_None ? PyObject_CallMethod(f, "flush", NULL) : NULL;
        if (r == NULL) {
            PyErr_Clear();
        }
        Py_XDECREF(r);
    }
    PyErr_SetRaisedException(exc);
}

/* _Py_FdIsInteractive (Python/pylifecycle.c). */
static int
fd_is_interactive(FILE *fp, PyObject *filename)
{
    if (isatty(fileno(fp))) {
        return 1;
    }
    if (!_Py_GetConfig()->interactive) {
        return 0;
    }
    return filename == NULL || PyUnicode_CompareWithASCIIString(filename, "<stdin>") == 0
        || PyUnicode_CompareWithASCIIString(filename, "???") == 0;
}

/* A sys attribute as UTF-8 text, or `fallback`; *keep holds what backs it. */
static const char *
sys_text(const char *name, PyObject **keep, const char *fallback)
{
    PyObject *attr = PySys_GetObject(name);
    *keep = attr != NULL ? PyObject_Str(attr) : NULL;
    const char *text = *keep != NULL ? PyUnicode_AsUTF8(*keep) : NULL;
    if (text == NULL) {
        PyErr_Clear();
        return fallback;
    }
    return text;
}

static PyObject *_PyRun_InteractiveLoop(FILE *, PyObject *, PyCompilerFlags *);
static PyObject *_PyRun_SimpleFile(FILE *, PyObject *, int, PyCompilerFlags *);

static PyObject *
_PyRun_AnyFile(FILE *fp, PyObject *filename, int closeit, PyCompilerFlags *flags)
{
    int decref_filename = 0;
    if (filename == NULL) {
        filename = PyUnicode_FromString("???");
        if (filename == NULL) {
            return NULL;
        }
        decref_filename = 1;
    }
    PyObject *result;
    if (fd_is_interactive(fp, filename)) {
        result = _PyRun_InteractiveLoop(fp, filename, flags);
        if (closeit) {
            fclose(fp);
        }
    }
    else {
        result = _PyRun_SimpleFile(fp, filename, closeit, flags);
    }
    if (decref_filename) {
        Py_DECREF(filename);
    }
    return result;
}

int
PyRun_AnyFileExFlags(FILE *fp, const char *filename, int closeit, PyCompilerFlags *flags)
{
    PyObject *filename_obj = NULL;
    if (filename != NULL) {
        filename_obj = PyUnicode_DecodeFSDefault(filename);
        if (filename_obj == NULL) {
            PyErr_Print();
            return -1;
        }
    }
    PyObject *result = _PyRun_AnyFile(fp, filename_obj, closeit, flags);
    Py_XDECREF(filename_obj);
    if (result == NULL) {
        PyErr_Print();
        return -1;
    }
    Py_DECREF(result);
    return 0;
}

static int _PyRun_InteractiveOne(FILE *, PyObject *, PyCompilerFlags *);

static PyObject *
_PyRun_InteractiveLoop(FILE *fp, PyObject *filename, PyCompilerFlags *flags)
{
    PyCompilerFlags local_flags = _PyCompilerFlags_INIT;
    if (flags == NULL) {
        flags = &local_flags;
    }
    const char *prompts[2][2] = { { "ps1", ">>> " }, { "ps2", "... " } };
    for (int i = 0; i < 2; i++) {
        if (PySys_GetObject(prompts[i][0]) == NULL) {
            PyObject *v = PyUnicode_FromString(prompts[i][1]);
            if (v == NULL || PySys_SetObject(prompts[i][0], v) < 0) {
                PyErr_Clear();
            }
            Py_XDECREF(v);
        }
    }
    int nomem_count = 0;
    int ret;
    do {
        ret = _PyRun_InteractiveOne(fp, filename, flags);
        if (ret == -1 && PyErr_Occurred()) {
            /* Consecutive MemoryErrors end the loop rather than spin. */
            if (PyErr_ExceptionMatches(PyExc_MemoryError)) {
                if (++nomem_count > 16) {
                    return NULL;
                }
            }
            else {
                nomem_count = 0;
            }
            if (!_Py_GetConfig()->inspect && PyErr_ExceptionMatches(PyExc_SystemExit)) {
                return NULL;
            }
            PyErr_Print();
        }
        else {
            nomem_count = 0;
        }
    } while (ret != E_EOF);
    Py_RETURN_NONE;
}

int
PyRun_InteractiveLoopFlags(FILE *fp, const char *filename, PyCompilerFlags *flags)
{
    PyObject *filename_obj = PyUnicode_DecodeFSDefault(filename);
    if (filename_obj == NULL) {
        PyErr_Print();
        return -1;
    }
    PyObject *result = _PyRun_InteractiveLoop(fp, filename_obj, flags);
    Py_DECREF(filename_obj);
    if (result == NULL) {
        PyErr_Print();
        return -1;
    }
    Py_DECREF(result);
    return 0;
}

/* One statement parsed from fp with sys.stdin.encoding (when fp is stdin),
   sys.ps1 and sys.ps2. */
static int
pyrun_one_parse_ast(FILE *fp, PyObject *filename, PyCompilerFlags *flags, PyArena *arena,
                    mod_ty *pmod, PyObject **interactive_src)
{
    PyObject *encoding_obj = NULL;
    const char *encoding = NULL;
    if (fp == stdin) {
        PyObject *in = PySys_GetObject("stdin");
        if (in != NULL && in != Py_None) {
            encoding_obj = PyObject_GetAttrString(in, "encoding");
            encoding = encoding_obj != NULL && PyUnicode_Check(encoding_obj)
                ? PyUnicode_AsUTF8(encoding_obj) : NULL;
            if (encoding == NULL) {
                PyErr_Clear();
            }
        }
    }
    PyObject *ps1_obj, *ps2_obj;
    const char *ps1 = sys_text("ps1", &ps1_obj, "");
    const char *ps2 = sys_text("ps2", &ps2_obj, "");
    int errcode = 0;
    *pmod = _PyParser_InteractiveASTFromFile(fp, filename, encoding, Py_single_input, ps1, ps2,
                                             flags, &errcode, interactive_src, arena);
    Py_XDECREF(ps1_obj);
    Py_XDECREF(ps2_obj);
    Py_XDECREF(encoding_obj);
    if (*pmod == NULL) {
        if (errcode == E_EOF) {
            PyErr_Clear();
            return E_EOF;
        }
        return -1;
    }
    return 0;
}

/* PyRun_InteractiveOneObject without printing the error. */
static int
_PyRun_InteractiveOne(FILE *fp, PyObject *filename, PyCompilerFlags *flags)
{
    if (!PyUnicode_Check(filename)) {
        PyErr_Format(PyExc_TypeError, "expect str for filename, got %T", filename);
        return -1;
    }
    PyArena *arena = _PyArena_New();
    if (arena == NULL) {
        return -1;
    }
    mod_ty mod;
    PyObject *interactive_src;
    int parse_res = pyrun_one_parse_ast(fp, filename, flags, arena, &mod, &interactive_src);
    if (parse_res != 0) {
        _PyArena_Free(arena);
        return parse_res;
    }
    PyObject *main_module = PyImport_AddModuleRef("__main__");
    if (main_module == NULL) {
        _PyArena_Free(arena);
        return -1;
    }
    PyObject *main_dict = PyModule_GetDict(main_module);
    PyObject *res = run_mod(mod, filename, main_dict, main_dict, flags, arena, interactive_src, 1);
    _PyArena_Free(arena);
    Py_DECREF(main_module);
    if (res == NULL) {
        /* A SyntaxError's text is the line of the statement it is in. */
        PyObject *exc = PyErr_GetRaisedException();
        if (PyObject_TypeCheck(exc, (PyTypeObject *)PyExc_SyntaxError)) {
            PyObject *xs = PyUnicode_Splitlines(interactive_src, 1);
            PyObject *exc_lineno = xs != NULL ? PyObject_GetAttrString(exc, "lineno") : NULL;
            int n = exc_lineno != NULL ? PyLong_AsInt(exc_lineno) : 0;
            if (n > 0 && n <= PyList_GET_SIZE(xs)) {
                PyObject_SetAttrString(exc, "text", PyList_GET_ITEM(xs, n - 1));
            }
            PyErr_Clear();
            Py_XDECREF(exc_lineno);
            Py_XDECREF(xs);
        }
        Py_DECREF(interactive_src);
        PyErr_SetRaisedException(exc);
        return -1;
    }
    Py_DECREF(interactive_src);
    Py_DECREF(res);
    flush_io();
    return 0;
}

int
PyRun_InteractiveOneObject(FILE *fp, PyObject *filename, PyCompilerFlags *flags)
{
    int res = _PyRun_InteractiveOne(fp, filename, flags);
    if (res == -1) {
        PyErr_Print();
        flush_io();
    }
    return res;
}

int
PyRun_InteractiveOneFlags(FILE *fp, const char *filename_str, PyCompilerFlags *flags)
{
    PyObject *filename = PyUnicode_DecodeFSDefault(filename_str);
    if (filename == NULL) {
        PyErr_Print();
        return -1;
    }
    int res = PyRun_InteractiveOneObject(fp, filename, flags);
    Py_DECREF(filename);
    return res;
}

/* Whether a file may be a .pyc: its extension, or, when it may be closed
   (and so is seekable), the first two bytes of the magic. */
static int
maybe_pyc_file(FILE *fp, PyObject *filename, int closeit)
{
    PyObject *ext = PyUnicode_FromString(".pyc");
    if (ext == NULL) {
        return -1;
    }
    Py_ssize_t endswith = PyUnicode_Tailmatch(filename, ext, 0, PY_SSIZE_T_MAX, +1);
    Py_DECREF(ext);
    if (endswith) {
        return 1;
    }
    if (!closeit) {
        return 0;
    }
    unsigned int halfmagic = PyImport_GetMagicNumber() & 0xFFFF;
    unsigned char buf[2];
    int ispyc = 0;
    if (ftell(fp) == 0) {
        if (fread(buf, 1, 2, fp) == 2 && ((unsigned int)buf[1] << 8 | buf[0]) == halfmagic) {
            ispyc = 1;
        }
        rewind(fp);
    }
    return ispyc;
}

/* __main__.__loader__: an importlib loader of the given class for filename. */
static int
set_main_loader(PyObject *d, PyObject *filename, const char *loader_name)
{
    PyObject *loader_type = PyImport_ImportModuleAttrString("_frozen_importlib_external", loader_name);
    if (loader_type == NULL) {
        return -1;
    }
    PyObject *loader = PyObject_CallFunction(loader_type, "sO", "__main__", filename);
    Py_DECREF(loader_type);
    if (loader == NULL) {
        return -1;
    }
    int r = PyDict_SetItemString(d, "__loader__", loader);
    Py_DECREF(loader);
    return r;
}

static PyObject *
_PyRun_SimpleFile(FILE *fp, PyObject *filename, int closeit, PyCompilerFlags *flags)
{
    PyObject *res = NULL;
    int set_file_name = 0;
    PyObject *main_module = PyImport_AddModuleRef("__main__");
    if (main_module == NULL) {
        return NULL;
    }
    PyObject *dict = PyModule_GetDict(main_module);
    int has_file = PyDict_ContainsString(dict, "__file__");
    if (has_file < 0) {
        goto done;
    }
    if (!has_file) {
        if (PyDict_SetItemString(dict, "__file__", filename) < 0) {
            fprintf(stderr, "python: failed to set __main__.__file__\n");
            goto done;
        }
        if (PyDict_SetItemString(dict, "__cached__", Py_None) < 0) {
            goto done;
        }
        set_file_name = 1;
    }
    int pyc = maybe_pyc_file(fp, filename, closeit);
    if (pyc < 0) {
        goto done;
    }
    if (pyc) {
        /* A pyc file runs from a binary reopen. */
        if (closeit) {
            fclose(fp);
        }
        FILE *pyc_fp = Py_fopen(filename, "rb");
        if (pyc_fp == NULL) {
            fprintf(stderr, "python: Can't reopen .pyc file\n");
            goto done;
        }
        if (set_main_loader(dict, filename, "SourcelessFileLoader") < 0) {
            fprintf(stderr, "python: failed to set __main__.__loader__\n");
            fclose(pyc_fp);
            goto done;
        }
        res = run_pyc_file(pyc_fp, dict, dict, flags);
    }
    else {
        /* Run from stdin, __main__.__loader__ is left alone. */
        if ((!PyUnicode_Check(filename) || !PyUnicode_EqualToUTF8(filename, "<stdin>"))
            && set_main_loader(dict, filename, "SourceFileLoader") < 0) {
            fprintf(stderr, "python: failed to set __main__.__loader__\n");
            goto done;
        }
        res = _PyRun_File(fp, filename, Py_file_input, dict, dict, closeit, flags);
    }
    flush_io();
done:
    if (set_file_name) {
        if (PyDict_PopString(dict, "__file__", NULL) < 0) {
            fprintf(stderr, "python: failed to delete __main__.__file__\n");
            Py_CLEAR(res);
        }
        if (PyDict_PopString(dict, "__cached__", NULL) < 0) {
            PyErr_Print();
        }
    }
    Py_DECREF(main_module);
    return res;
}

int
PyRun_SimpleFileExFlags(FILE *fp, const char *filename, int closeit, PyCompilerFlags *flags)
{
    PyObject *filename_obj = PyUnicode_DecodeFSDefault(filename);
    if (filename_obj == NULL) {
        PyErr_Print();
        return -1;
    }
    PyObject *result = _PyRun_SimpleFile(fp, filename_obj, closeit, flags);
    Py_DECREF(filename_obj);
    if (result == NULL) {
        PyErr_Print();
        return -1;
    }
    Py_DECREF(result);
    return 0;
}

PyObject *
PyRun_StringFlags(const char *str, int start, PyObject *globals, PyObject *locals,
                  PyCompilerFlags *flags)
{
    if (check_start(start) < 0) {
        return NULL;
    }
    PyArena *arena = _PyArena_New();
    if (arena == NULL) {
        return NULL;
    }
    PyObject *name = PyUnicode_FromString("<string>");
    mod_ty mod = name != NULL ? _PyParser_ASTFromString(str, name, start, flags, arena) : NULL;
    PyObject *ret = mod != NULL ? run_mod(mod, name, globals, locals, flags, arena, NULL, 0) : NULL;
    Py_XDECREF(name);
    _PyArena_Free(arena);
    return ret;
}

static PyObject *
_PyRun_File(FILE *fp, PyObject *filename, int start, PyObject *globals, PyObject *locals,
            int closeit, PyCompilerFlags *flags)
{
    if (check_start(start) < 0) {
        return NULL;
    }
    PyArena *arena = _PyArena_New();
    if (arena == NULL) {
        return NULL;
    }
    mod_ty mod = _PyParser_ASTFromFile(fp, filename, NULL, start, NULL, NULL, flags, NULL, arena);
    if (closeit) {
        fclose(fp);
    }
    PyObject *ret = mod != NULL ? run_mod(mod, filename, globals, locals, flags, arena, NULL, 0) : NULL;
    _PyArena_Free(arena);
    return ret;
}

PyObject *
PyRun_FileExFlags(FILE *fp, const char *filename, int start, PyObject *globals,
                  PyObject *locals, int closeit, PyCompilerFlags *flags)
{
    PyObject *filename_obj = PyUnicode_DecodeFSDefault(filename);
    if (filename_obj == NULL) {
        PyErr_Print();
        return NULL;
    }
    PyObject *res = _PyRun_File(fp, filename_obj, start, globals, locals, closeit, flags);
    Py_DECREF(filename_obj);
    return res;
}

/* Sets globals['__builtins__'] when missing, then evaluates. */
static PyObject *
run_eval_code_obj(PyCodeObject *co, PyObject *globals, PyObject *locals)
{
    if (globals == NULL || !PyDict_Check(globals)) {
        PyErr_SetString(PyExc_SystemError, "globals must be a real dict");
        return NULL;
    }
    int has_builtins = PyDict_ContainsString(globals, "__builtins__");
    if (has_builtins < 0) {
        return NULL;
    }
    if (!has_builtins
        && PyDict_SetItemString(globals, "__builtins__", PyInterpreterState_Get()->builtins) < 0) {
        return NULL;
    }
    return PyEval_EvalCode((PyObject *)co, globals, locals);
}

/* An interactive statement's code is named filename-N (<name-N> for
   <name>), its source registered with linecache under that name. */
static PyObject *
get_interactive_filename(PyObject *filename, Py_ssize_t count)
{
    Py_ssize_t len = PyUnicode_GET_LENGTH(filename);
    if (len >= 2 && PyUnicode_ReadChar(filename, 0) == '<'
        && PyUnicode_ReadChar(filename, len - 1) == '>') {
        PyObject *middle = PyUnicode_Substring(filename, 1, len - 1);
        if (middle == NULL) {
            return NULL;
        }
        PyObject *result = PyUnicode_FromFormat("<%U-%zd>", middle, count);
        Py_DECREF(middle);
        return result;
    }
    return PyUnicode_FromFormat("%U-%zd", filename, count);
}

static PyObject *
run_mod(mod_ty mod, PyObject *filename, PyObject *globals, PyObject *locals,
        PyCompilerFlags *flags, PyArena *arena, PyObject *interactive_src,
        int generate_new_source)
{
    PyObject *interactive_filename = filename;
    if (interactive_src) {
        if (generate_new_source) {
            Py_ssize_t count = __atomic_fetch_add(&PyInterpreterState_Get()->interactive_src_count,
                                                  1, __ATOMIC_RELAXED);
            interactive_filename = get_interactive_filename(filename, count);
        }
        else {
            Py_INCREF(interactive_filename);
        }
        if (interactive_filename == NULL) {
            return NULL;
        }
    }
    PyCodeObject *co = _PyAST_Compile(mod, interactive_filename, flags, -1, arena);
    if (co == NULL) {
        if (interactive_src) {
            Py_DECREF(interactive_filename);
        }
        return NULL;
    }
    if (interactive_src) {
        PyObject *register_code = PyImport_ImportModuleAttrString("linecache", "_register_code");
        if (register_code == NULL) {
            Py_DECREF(co);
            Py_DECREF(interactive_filename);
            return NULL;
        }
        if (!PyCallable_Check(register_code)) {
            Py_DECREF(co);
            Py_DECREF(interactive_filename);
            Py_DECREF(register_code);
            PyErr_SetString(PyExc_ValueError, "linecache._register_code is not callable");
            return NULL;
        }
        PyObject *result = PyObject_CallFunction(register_code, "OOO", co, interactive_src,
                                                 filename);
        Py_DECREF(interactive_filename);
        Py_DECREF(register_code);
        if (result == NULL) {
            Py_DECREF(co);
            return NULL;
        }
        Py_DECREF(result);
    }
    if (PySys_Audit("exec", "O", co) < 0) {
        Py_DECREF(co);
        return NULL;
    }
    PyObject *v = run_eval_code_obj(co, globals, locals);
    Py_DECREF(co);
    return v;
}

/* A .pyc: its 16-byte header checked for the magic, then its code object. */
static PyObject *
run_pyc_file(FILE *fp, PyObject *globals, PyObject *locals, PyCompilerFlags *flags)
{
    PyObject *v = NULL;
    char *data = NULL;
    long size = fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
    if (size < 16 || fseek(fp, 0, SEEK_SET) != 0
        || (data = PyMem_Malloc(size)) == NULL || fread(data, 1, size, fp) != (size_t)size) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_RuntimeError, "Bad magic number in .pyc file");
        }
        goto error;
    }
    long magic = (unsigned char)data[0] | (unsigned char)data[1] << 8
        | (unsigned char)data[2] << 16 | (long)(unsigned char)data[3] << 24;
    if (magic != PyImport_GetMagicNumber()) {
        PyErr_SetString(PyExc_RuntimeError, "Bad magic number in .pyc file");
        goto error;
    }
    v = PyMarshal_ReadObjectFromString(data + 16, size - 16);
    if (v == NULL || !PyCode_Check(v)) {
        Py_XDECREF(v);
        PyErr_SetString(PyExc_RuntimeError, "Bad code object in .pyc file");
        goto error;
    }
    PyMem_Free(data);
    fclose(fp);
    PyCodeObject *co = (PyCodeObject *)v;
    v = run_eval_code_obj(co, globals, locals);
    if (v != NULL && flags != NULL) {
        flags->cf_flags |= co->co_flags & PyCF_MASK;
    }
    Py_DECREF(co);
    return v;
error:
    PyMem_Free(data);
    fclose(fp);
    return NULL;
}

PyObject *
Py_CompileStringObject(const char *str, PyObject *filename, int start, PyCompilerFlags *flags,
                       int optimize)
{
    if (check_start(start) < 0) {
        return NULL;
    }
    PyArena *arena = _PyArena_New();
    if (arena == NULL) {
        return NULL;
    }
    PyObject *result = NULL;
    mod_ty mod = _PyParser_ASTFromString(str, filename, start, flags, arena);
    if (mod != NULL && flags != NULL && (flags->cf_flags & PyCF_ONLY_AST)) {
        int syntax_check_only = (flags->cf_flags & PyCF_OPTIMIZED_AST) == PyCF_ONLY_AST;
        if (_PyCompile_AstPreprocess(mod, filename, flags, optimize, arena, syntax_check_only) == 0) {
            result = PyAST_mod2obj(mod);
        }
    }
    else if (mod != NULL) {
        result = (PyObject *)_PyAST_Compile(mod, filename, flags, optimize, arena);
    }
    _PyArena_Free(arena);
    return result;
}

PyObject *
Py_CompileStringExFlags(const char *str, const char *filename_str, int start,
                        PyCompilerFlags *flags, int optimize)
{
    PyObject *filename = PyUnicode_DecodeFSDefault(filename_str);
    if (filename == NULL) {
        return NULL;
    }
    PyObject *co = Py_CompileStringObject(str, filename, start, flags, optimize);
    Py_DECREF(filename);
    return co;
}

/* The function forms of the compatibility macros. */

#undef PyRun_AnyFile
int
PyRun_AnyFile(FILE *fp, const char *name)
{
    return PyRun_AnyFileExFlags(fp, name, 0, NULL);
}

#undef PyRun_AnyFileEx
int
PyRun_AnyFileEx(FILE *fp, const char *name, int closeit)
{
    return PyRun_AnyFileExFlags(fp, name, closeit, NULL);
}

#undef PyRun_AnyFileFlags
int
PyRun_AnyFileFlags(FILE *fp, const char *name, PyCompilerFlags *flags)
{
    return PyRun_AnyFileExFlags(fp, name, 0, flags);
}

#undef PyRun_File
PyObject *
PyRun_File(FILE *fp, const char *p, int s, PyObject *g, PyObject *l)
{
    return PyRun_FileExFlags(fp, p, s, g, l, 0, NULL);
}

#undef PyRun_FileEx
PyObject *
PyRun_FileEx(FILE *fp, const char *p, int s, PyObject *g, PyObject *l, int c)
{
    return PyRun_FileExFlags(fp, p, s, g, l, c, NULL);
}

#undef PyRun_FileFlags
PyObject *
PyRun_FileFlags(FILE *fp, const char *p, int s, PyObject *g, PyObject *l, PyCompilerFlags *flags)
{
    return PyRun_FileExFlags(fp, p, s, g, l, 0, flags);
}

#undef PyRun_SimpleFile
int
PyRun_SimpleFile(FILE *f, const char *p)
{
    return PyRun_SimpleFileExFlags(f, p, 0, NULL);
}

#undef PyRun_SimpleFileEx
int
PyRun_SimpleFileEx(FILE *f, const char *p, int c)
{
    return PyRun_SimpleFileExFlags(f, p, c, NULL);
}

#undef PyRun_String
PyObject *
PyRun_String(const char *str, int s, PyObject *g, PyObject *l)
{
    return PyRun_StringFlags(str, s, g, l, NULL);
}

#undef Py_CompileString
PyObject *
Py_CompileString(const char *str, const char *p, int s)
{
    return Py_CompileStringExFlags(str, p, s, NULL, -1);
}

#undef Py_CompileStringFlags
PyObject *
Py_CompileStringFlags(const char *str, const char *p, int s, PyCompilerFlags *flags)
{
    return Py_CompileStringExFlags(str, p, s, flags, -1);
}

#undef PyRun_InteractiveOne
int
PyRun_InteractiveOne(FILE *f, const char *p)
{
    return PyRun_InteractiveOneFlags(f, p, NULL);
}

#undef PyRun_InteractiveLoop
int
PyRun_InteractiveLoop(FILE *f, const char *p)
{
    return PyRun_InteractiveLoopFlags(f, p, NULL);
}
