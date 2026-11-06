/* C-API functions the vendored compiler (runtime/compiler) uses, which
   were unported: the rest of Python/errors.c's syntax-error helpers,
   PyUnicodeWriter, Py_ReprEnter, and small Objects/ functions. Derived from
   the CPython 3.14.10 functions of the same names. */

#include "lucifer.h"

#include <stdarg.h>
#include <wchar.h>

#include <pthread.h>
#include <string.h>

/* Py_GetConstant (Objects/object.c) */
PyObject *
Py_GetConstantBorrowed(unsigned int constant_id)
{
    switch (constant_id) {
    case Py_CONSTANT_NONE: return Py_None;
    case Py_CONSTANT_FALSE: return Py_False;
    case Py_CONSTANT_TRUE: return Py_True;
    case Py_CONSTANT_ELLIPSIS: return Py_Ellipsis;
    case Py_CONSTANT_NOT_IMPLEMENTED: return Py_NotImplemented;
    case Py_CONSTANT_ZERO: return Py_GetConstant(Py_CONSTANT_ZERO);
    case Py_CONSTANT_ONE: return Py_GetConstant(Py_CONSTANT_ONE);
    case Py_CONSTANT_EMPTY_STR: return Py_GetConstant(Py_CONSTANT_EMPTY_STR);
    case Py_CONSTANT_EMPTY_BYTES: return Py_GetConstant(Py_CONSTANT_EMPTY_BYTES);
    case Py_CONSTANT_EMPTY_TUPLE: return Py_GetConstant(Py_CONSTANT_EMPTY_TUPLE);
    default:
        PyErr_BadInternalCall();
        return NULL;
    }
}

PyObject *
Py_GetConstant(unsigned int constant_id)
{
    switch (constant_id) {
    case Py_CONSTANT_ZERO: return PyLong_FromLong(0);
    case Py_CONSTANT_ONE: return PyLong_FromLong(1);
    case Py_CONSTANT_EMPTY_STR: return PyUnicode_FromStringAndSize("", 0);
    case Py_CONSTANT_EMPTY_BYTES: return PyBytes_FromStringAndSize("", 0);
    case Py_CONSTANT_EMPTY_TUPLE: return PyTuple_New(0);
    default: {
        PyObject *c = Py_GetConstantBorrowed(constant_id);
        return Py_XNewRef(c);
    }
    }
}

PyObject *
PyImport_ImportModuleAttrString(const char *modname, const char *attrname)
{
    PyObject *module = PyImport_ImportModule(modname);
    if (module == NULL) {
        return NULL;
    }
    PyObject *attr = PyObject_GetAttrString(module, attrname);
    Py_DECREF(module);
    return attr;
}

/* Objects/typeobject.c: the type's name without its module. */
const char *
_PyType_Name(PyTypeObject *type)
{
    const char *s = strrchr(type->tp_name, '.');
    return s == NULL ? type->tp_name : s + 1;
}

/* Objects/unicodeobject.c */
void
PyUnicode_Append(PyObject **p_left, PyObject *right)
{
    PyObject *left = *p_left;
    if (left == NULL) {
        return;
    }
    if (right == NULL || !PyUnicode_Check(left) || !PyUnicode_Check(right)) {
        if (!PyErr_Occurred()) {
            PyErr_BadInternalCall();
        }
        Py_CLEAR(*p_left);
        return;
    }
    *p_left = PyUnicode_Concat(left, right);
    Py_DECREF(left);
}

void
PyUnicode_AppendAndDel(PyObject **pleft, PyObject *right)
{
    PyUnicode_Append(pleft, right);
    Py_XDECREF(right);
}

/* The filesystem path converters (Objects/unicodeobject.c): PyOS_FSPath to a
   str (FSDecoder) or to bytes in the filesystem encoding (FSConverter). */
int
PyUnicode_FSConverter(PyObject* arg, void* addr)
{
    if (arg == NULL) {
        Py_CLEAR(*(PyObject **)addr);
        return 1;
    }
    PyObject *path = PyOS_FSPath(arg);
    if (path == NULL) {
        return 0;
    }
    PyObject *output;
    if (PyBytes_Check(path)) {
        output = path;
    }
    else {  // PyOS_FSPath() guarantees its returned value is bytes or str.
        output = PyUnicode_EncodeFSDefault(path);
        Py_DECREF(path);
        if (output == NULL) {
            return 0;
        }
    }
    if ((size_t)PyBytes_GET_SIZE(output) != strlen(PyBytes_AS_STRING(output))) {
        PyErr_SetString(PyExc_ValueError, "embedded null byte");
        Py_DECREF(output);
        return 0;
    }
    *(PyObject **)addr = output;
    return Py_CLEANUP_SUPPORTED;
}

int
PyUnicode_FSDecoder(PyObject *arg, void *addr)
{
    if (arg == NULL) {
        Py_CLEAR(*(PyObject **)addr);
        return 1;
    }
    PyObject *path = PyOS_FSPath(arg);
    if (path == NULL) {
        return 0;
    }
    PyObject *output = PyBytes_Check(path) ? PyUnicode_DecodeFSDefaultAndSize(PyBytes_AS_STRING(path),
                                                                              PyBytes_GET_SIZE(path))
                                           : Py_NewRef(path);
    Py_DECREF(path);
    if (output == NULL) {
        return 0;
    }
    if (PyUnicode_GET_LENGTH(output) != (Py_ssize_t)strlen(PyUnicode_AsUTF8(output))) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        Py_DECREF(output);
        return 0;
    }
    *(PyObject **)addr = output;
    return Py_CLEANUP_SUPPORTED;
}

PyObject *
PyUnicodeDecodeError_Create(const char *encoding, const char *object, Py_ssize_t length,
                            Py_ssize_t start, Py_ssize_t end, const char *reason)
{
    return PyObject_CallFunction(PyExc_UnicodeDecodeError, "sy#nns", encoding, object, length, start, end,
                                 reason);
}

/* PyUnicodeWriter (Objects/unicodeobject.c): a buffer of code points. */
struct PyUnicodeWriter {
    Py_UCS4 *buf;
    Py_ssize_t len, cap;
};

PyUnicodeWriter *
PyUnicodeWriter_Create(Py_ssize_t length)
{
    PyUnicodeWriter *w = PyMem_Malloc(sizeof(*w));
    if (w == NULL) {
        return (PyUnicodeWriter *)PyErr_NoMemory();
    }
    w->len = 0;
    w->cap = length > 16 ? length : 16;
    w->buf = PyMem_Malloc(w->cap * sizeof(Py_UCS4));
    if (w->buf == NULL) {
        PyMem_Free(w);
        return (PyUnicodeWriter *)PyErr_NoMemory();
    }
    return w;
}

void
PyUnicodeWriter_Discard(PyUnicodeWriter *w)
{
    if (w != NULL) {
        PyMem_Free(w->buf);
        PyMem_Free(w);
    }
}

PyObject *
PyUnicodeWriter_Finish(PyUnicodeWriter *w)
{
    PyObject *s = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, w->buf, w->len);
    PyUnicodeWriter_Discard(w);
    return s;
}

static int
reserve(PyUnicodeWriter *w, Py_ssize_t n)
{
    if (w->len + n > w->cap) {
        Py_ssize_t cap = w->cap * 2 > w->len + n ? w->cap * 2 : w->len + n;
        Py_UCS4 *buf = PyMem_Realloc(w->buf, cap * sizeof(Py_UCS4));
        if (buf == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        w->buf = buf;
        w->cap = cap;
    }
    return 0;
}

int
PyUnicodeWriter_WriteChar(PyUnicodeWriter *w, Py_UCS4 ch)
{
    if (ch > 0x10ffff) {
        PyErr_SetString(PyExc_ValueError, "character must be in range(0x110000)");
        return -1;
    }
    if (reserve(w, 1) < 0) {
        return -1;
    }
    w->buf[w->len++] = ch;
    return 0;
}

int
PyUnicodeWriter_WriteSubstring(PyUnicodeWriter *w, PyObject *str, Py_ssize_t start, Py_ssize_t end)
{
    if (!PyUnicode_Check(str)) {
        PyErr_Format(PyExc_TypeError, "expect str, not %s", Py_TYPE(str)->tp_name);
        return -1;
    }
    if (start < 0 || start > end || end > PyUnicode_GET_LENGTH(str)) {
        PyErr_SetString(PyExc_ValueError, "invalid start or end");
        return -1;
    }
    if (reserve(w, end - start) < 0) {
        return -1;
    }
    int kind = PyUnicode_KIND(str);
    const void *data = PyUnicode_DATA(str);
    for (Py_ssize_t i = start; i < end; i++) {
        w->buf[w->len++] = PyUnicode_READ(kind, data, i);
    }
    return 0;
}

int
PyUnicodeWriter_WriteStr(PyUnicodeWriter *w, PyObject *obj)
{
    PyObject *str = PyObject_Str(obj);
    if (str == NULL) {
        return -1;
    }
    int r = PyUnicodeWriter_WriteSubstring(w, str, 0, PyUnicode_GET_LENGTH(str));
    Py_DECREF(str);
    return r;
}

int
PyUnicodeWriter_WriteRepr(PyUnicodeWriter *w, PyObject *obj)
{
    PyObject *str = PyObject_Repr(obj);
    if (str == NULL) {
        return -1;
    }
    int r = PyUnicodeWriter_WriteSubstring(w, str, 0, PyUnicode_GET_LENGTH(str));
    Py_DECREF(str);
    return r;
}

/* A decoded or formatted str, appended; NULL (its failure) writes nothing. */
static int
write_owned(PyUnicodeWriter *w, PyObject *s)
{
    if (s == NULL) {
        return -1;
    }
    int r = PyUnicodeWriter_WriteSubstring(w, s, 0, PyUnicode_GET_LENGTH(s));
    Py_DECREF(s);
    return r;
}

/* Strict UTF-8 (unicode_decode_utf8_writer with _Py_ERROR_STRICT). */
int
PyUnicodeWriter_WriteUTF8(PyUnicodeWriter *w, const char *str, Py_ssize_t size)
{
    if (size < 0) {
        size = (Py_ssize_t)strlen(str);
    }
    return size == 0 ? 0 : write_owned(w, PyUnicode_DecodeUTF8(str, size, NULL));
}

int
PyUnicodeWriter_DecodeUTF8Stateful(PyUnicodeWriter *w, const char *str, Py_ssize_t size,
                                   const char *errors, Py_ssize_t *consumed)
{
    if (size < 0) {
        size = (Py_ssize_t)strlen(str);
    }
    if (size == 0) {
        if (consumed != NULL) {
            *consumed = 0;
        }
        return 0;
    }
    int r = write_owned(w, PyUnicode_DecodeUTF8Stateful(str, size, errors, consumed));
    if (r < 0 && consumed != NULL) {
        *consumed = 0;
    }
    return r;
}

static int write_latin1(PyUnicodeWriter *w, const char *str, Py_ssize_t size);

/* The caller vouches the bytes are ASCII: they are taken as they are. */
int
PyUnicodeWriter_WriteASCII(PyUnicodeWriter *w, const char *str, Py_ssize_t size)
{
    if (size < 0) {
        size = (Py_ssize_t)strlen(str);
    }
    return write_latin1(w, str, size);
}

/* CPython's PyUnicodeWriter is its _PyUnicodeWriter, and _testcapi passes
   one to the private call: here only a writer PyUnicodeWriter_Create made. */
int
_PyUnicodeWriter_WriteLatin1String(_PyUnicodeWriter *writer, const char *str, Py_ssize_t size)
{
    return write_latin1((PyUnicodeWriter *)writer, str, size);
}

static int
write_latin1(PyUnicodeWriter *w, const char *str, Py_ssize_t size)
{
    if (size == 0) {
        return 0;
    }
    if (reserve(w, size) < 0) {
        return -1;
    }
    for (Py_ssize_t i = 0; i < size; i++) {
        w->buf[w->len++] = (unsigned char)str[i];
    }
    return 0;
}

/* The code points as given: a release build checks none (gh-158445). */
int
PyUnicodeWriter_WriteUCS4(PyUnicodeWriter *w, Py_UCS4 *str, Py_ssize_t size)
{
    if (size < 0) {
        PyErr_SetString(PyExc_ValueError, "size must be positive");
        return -1;
    }
    if (size == 0) {
        return 0;
    }
    if (reserve(w, size) < 0) {
        return -1;
    }
    memcpy(w->buf + w->len, str, size * sizeof(Py_UCS4));
    w->len += size;
    return 0;
}

int
PyUnicodeWriter_WriteWideChar(PyUnicodeWriter *w, const wchar_t *str, Py_ssize_t size)
{
    if (size < 0) {
        size = (Py_ssize_t)wcslen(str);
    }
    return size == 0 ? 0 : write_owned(w, PyUnicode_FromWideChar(str, size));
}

int
PyUnicodeWriter_Format(PyUnicodeWriter *w, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyObject *s = PyUnicode_FromFormatV(format, ap);
    va_end(ap);
    return write_owned(w, s);
}

/* Py_ReprEnter and Py_ReprLeave (Objects/object.c): the objects being repr'd
   by this thread, in its thread dict. */
int
Py_ReprEnter(PyObject *obj)
{
    PyObject *dict = PyThreadState_GetDict();
    if (dict == NULL) {
        return 0;
    }
    PyObject *key = PyUnicode_InternFromString("Py_Repr");
    PyObject *list = key == NULL ? NULL : PyDict_GetItemWithError(dict, key);
    if (list == NULL) {
        if (key == NULL || PyErr_Occurred() || (list = PyList_New(0)) == NULL
            || PyDict_SetItem(dict, key, list) < 0) {
            Py_XDECREF(key);
            return -1;
        }
        Py_DECREF(list);
    }
    Py_DECREF(key);
    for (Py_ssize_t i = PyList_GET_SIZE(list); --i >= 0;) {
        if (PyList_GET_ITEM(list, i) == obj) {
            return 1;
        }
    }
    return PyList_Append(list, obj) < 0 ? -1 : 0;
}

void
Py_ReprLeave(PyObject *obj)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *dict = PyThreadState_GetDict();
    PyObject *key = dict == NULL ? NULL : PyUnicode_InternFromString("Py_Repr");
    PyObject *list = key == NULL ? NULL : PyDict_GetItemWithError(dict, key);
    if (list != NULL && PyList_Check(list)) {
        for (Py_ssize_t i = PyList_GET_SIZE(list); --i >= 0;) {
            if (PyList_GET_ITEM(list, i) == obj) {
                PyList_SetSlice(list, i, i + 1, NULL);
                break;
            }
        }
    }
    Py_XDECREF(key);
    PyErr_SetRaisedException(exc);
}

/* Python/errors.c */
int
PyErr_WarnExplicitObject(PyObject *category, PyObject *message, PyObject *filename, int lineno,
                         PyObject *module, PyObject *registry)
{
    PyObject *warnings = PyImport_ImportModule("warnings");
    PyObject *r = NULL;
    if (warnings != NULL) {
        r = PyObject_CallMethod(warnings, "warn_explicit", "OOOiOO", message,
                                category != NULL ? category : PyExc_RuntimeWarning, filename, lineno,
                                module != NULL ? module : Py_None, registry != NULL ? registry : Py_None);
    }
    Py_XDECREF(warnings);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

PyObject *
PyErr_ProgramTextObject(PyObject *filename, int lineno)
{
    extern PyObject *_PyErr_ProgramDecodedTextObject(PyObject *, int, const char *);
    return _PyErr_ProgramDecodedTextObject(filename, lineno, NULL);
}

static void
set_attr(PyObject *exc, const char *name, PyObject *value)
{
    if (PyObject_SetAttrString(exc, name, value != NULL ? value : Py_None) < 0) {
        PyErr_Clear();
    }
}

static void
syntax_location(PyObject *filename, int lineno, int col_offset, int end_lineno, int end_col_offset)
{
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *v = PyLong_FromLong(lineno);
    set_attr(exc, "lineno", v);
    Py_XDECREF(v);
    int values[3] = { col_offset, end_lineno, end_col_offset };
    static const char *const names[3] = { "offset", "end_lineno", "end_offset" };
    for (int i = 0; i < 3; i++) {
        v = values[i] >= 0 ? PyLong_FromLong(values[i]) : NULL;
        set_attr(exc, names[i], v);
        Py_XDECREF(v);
    }
    if (filename != NULL) {
        set_attr(exc, "filename", filename);
        PyObject *text = PyErr_ProgramTextObject(filename, lineno);
        if (text != NULL) {
            set_attr(exc, "text", text);
            Py_DECREF(text);
        }
        else {
            PyErr_Clear();
        }
    }
    if ((PyObject *)Py_TYPE(exc) != PyExc_SyntaxError) {
        if (!PyObject_HasAttrString(exc, "msg")) {
            PyObject *s = PyObject_Str(exc);
            set_attr(exc, "msg", s);
            Py_XDECREF(s);
        }
        if (!PyObject_HasAttrString(exc, "print_file_and_line")) {
            set_attr(exc, "print_file_and_line", Py_None);
        }
    }
    PyErr_SetRaisedException(exc);
}

void
PyErr_SyntaxLocationObject(PyObject *filename, int lineno, int col_offset)
{
    syntax_location(filename, lineno, col_offset, lineno, -1);
}

void
PyErr_RangedSyntaxLocationObject(PyObject *filename, int lineno, int col_offset, int end_lineno,
                                 int end_col_offset)
{
    syntax_location(filename, lineno, col_offset, end_lineno, end_col_offset);
}
