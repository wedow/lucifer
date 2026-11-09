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

/* PyUnicodeWriter and _PyUnicodeWriter (Objects/unicodeobject.c): as in
   CPython the public writer is the private one, whose fields extensions read
   and write through the header's macros: a str buffer, widened and grown as
   characters arrive, and the number written. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#define WRITER_OVERALLOCATE_FACTOR 4

static void
writer_update(_PyUnicodeWriter *w)
{
    w->maxchar = PyUnicode_MAX_CHAR_VALUE(w->buffer);
    w->data = PyUnicode_DATA(w->buffer);
    w->kind = PyUnicode_KIND(w->buffer);
    w->size = PyUnicode_GET_LENGTH(w->buffer);
}

void
_PyUnicodeWriter_Init(_PyUnicodeWriter *w)
{
    memset(w, 0, sizeof(*w));
    w->min_char = 127;
}

void
_PyUnicodeWriter_Dealloc(_PyUnicodeWriter *w)
{
    Py_CLEAR(w->buffer);
}

int
_PyUnicodeWriter_PrepareInternal(_PyUnicodeWriter *w, Py_ssize_t length, Py_UCS4 maxchar)
{
    if (length > PY_SSIZE_T_MAX - w->pos) {
        PyErr_NoMemory();
        return -1;
    }
    Py_ssize_t newlen = w->pos + length;
    maxchar = Py_MAX(maxchar, w->min_char);
    if (w->buffer != NULL && newlen <= w->size && maxchar <= w->maxchar) {
        return 0;
    }
    if (w->buffer == NULL || newlen > w->size) {
        if (w->overallocate && newlen <= PY_SSIZE_T_MAX - newlen / WRITER_OVERALLOCATE_FACTOR) {
            newlen += newlen / WRITER_OVERALLOCATE_FACTOR;
        }
        newlen = Py_MAX(newlen, w->min_length);
    }
    else {
        newlen = w->size;
    }
    if (w->buffer != NULL) {
        maxchar = Py_MAX(maxchar, w->maxchar);
    }
    PyObject *grown = PyUnicode_New(newlen, maxchar);
    if (grown == NULL) {
        return -1;
    }
    if (w->buffer != NULL) {
        PyUnicode_CopyCharacters(grown, 0, w->buffer, 0, w->pos);
        Py_DECREF(w->buffer);
    }
    w->buffer = grown;
    w->readonly = 0;
    writer_update(w);
    return 0;
}

int
_PyUnicodeWriter_PrepareKindInternal(_PyUnicodeWriter *w, int kind)
{
    Py_UCS4 maxchar = kind == PyUnicode_1BYTE_KIND ? 0xff : kind == PyUnicode_2BYTE_KIND ? 0xffff : 0x10ffff;
    return _PyUnicodeWriter_PrepareInternal(w, 0, maxchar);
}

int
_PyUnicodeWriter_WriteChar(_PyUnicodeWriter *w, Py_UCS4 ch)
{
    if (_PyUnicodeWriter_Prepare(w, 1, ch) < 0) {
        return -1;
    }
    PyUnicode_WRITE(w->kind, w->data, w->pos, ch);
    w->pos++;
    return 0;
}

int
_PyUnicodeWriter_WriteSubstring(_PyUnicodeWriter *w, PyObject *str, Py_ssize_t start, Py_ssize_t end)
{
    if (end <= start) {
        return 0;
    }
    int kind = PyUnicode_KIND(str);
    const void *data = PyUnicode_DATA(str);
    Py_UCS4 maxchar = 0;
    for (Py_ssize_t i = start; i < end; i++) {
        maxchar = Py_MAX(maxchar, PyUnicode_READ(kind, data, i));
    }
    if (_PyUnicodeWriter_Prepare(w, end - start, maxchar) < 0) {
        return -1;
    }
    for (Py_ssize_t i = start; i < end; i++) {
        PyUnicode_WRITE(w->kind, w->data, w->pos++, PyUnicode_READ(kind, data, i));
    }
    return 0;
}

int
_PyUnicodeWriter_WriteStr(_PyUnicodeWriter *w, PyObject *str)
{
    return _PyUnicodeWriter_WriteSubstring(w, str, 0, PyUnicode_GET_LENGTH(str));
}

int
_PyUnicodeWriter_WriteLatin1String(_PyUnicodeWriter *w, const char *str, Py_ssize_t size)
{
    Py_UCS4 maxchar = 0;
    for (Py_ssize_t i = 0; i < size; i++) {
        maxchar = Py_MAX(maxchar, (unsigned char)str[i]);
    }
    if (size == 0) {
        return 0;
    }
    if (_PyUnicodeWriter_Prepare(w, size, maxchar) < 0) {
        return -1;
    }
    for (Py_ssize_t i = 0; i < size; i++) {
        PyUnicode_WRITE(w->kind, w->data, w->pos++, (unsigned char)str[i]);
    }
    return 0;
}

/* The caller vouches the bytes are ASCII: they are taken as they are. */
int
_PyUnicodeWriter_WriteASCIIString(_PyUnicodeWriter *w, const char *str, Py_ssize_t size)
{
    return _PyUnicodeWriter_WriteLatin1String(w, str, size);
}

PyObject *
_PyUnicodeWriter_Finish(_PyUnicodeWriter *w)
{
    PyObject *str = w->buffer;
    w->buffer = NULL;
    if (w->pos == 0) {
        Py_XDECREF(str);
        return PyUnicode_New(0, 0);
    }
    if (PyUnicode_Resize(&str, w->pos) < 0) {
        Py_DECREF(str);
        return NULL;
    }
    return str;
}

PyUnicodeWriter *
PyUnicodeWriter_Create(Py_ssize_t length)
{
    if (length < 0) {
        PyErr_SetString(PyExc_ValueError, "length must be positive");
        return NULL;
    }
    _PyUnicodeWriter *w = PyMem_Malloc(sizeof(*w));
    if (w == NULL) {
        return (PyUnicodeWriter *)PyErr_NoMemory();
    }
    _PyUnicodeWriter_Init(w);
    if (_PyUnicodeWriter_Prepare(w, length, 127) < 0) {
        PyMem_Free(w);
        return NULL;
    }
    w->overallocate = 1;
    return (PyUnicodeWriter *)w;
}

void
PyUnicodeWriter_Discard(PyUnicodeWriter *w)
{
    if (w != NULL) {
        _PyUnicodeWriter_Dealloc((_PyUnicodeWriter *)w);
        PyMem_Free(w);
    }
}

PyObject *
PyUnicodeWriter_Finish(PyUnicodeWriter *w)
{
    PyObject *s = _PyUnicodeWriter_Finish((_PyUnicodeWriter *)w);
    PyMem_Free(w);
    return s;
}

int
PyUnicodeWriter_WriteChar(PyUnicodeWriter *w, Py_UCS4 ch)
{
    if (ch > 0x10ffff) {
        PyErr_SetString(PyExc_ValueError, "character must be in range(0x110000)");
        return -1;
    }
    return _PyUnicodeWriter_WriteChar((_PyUnicodeWriter *)w, ch);
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
    return _PyUnicodeWriter_WriteSubstring((_PyUnicodeWriter *)w, str, start, end);
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

/* The caller vouches the bytes are ASCII: they are taken as they are. */
int
PyUnicodeWriter_WriteASCII(PyUnicodeWriter *w, const char *str, Py_ssize_t size)
{
    if (size < 0) {
        size = (Py_ssize_t)strlen(str);
    }
    return _PyUnicodeWriter_WriteLatin1String((_PyUnicodeWriter *)w, str, size);
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
    Py_UCS4 maxchar = 0;
    for (Py_ssize_t i = 0; i < size; i++) {
        maxchar = Py_MAX(maxchar, str[i]);
    }
    _PyUnicodeWriter *pw = (_PyUnicodeWriter *)w;
    if (_PyUnicodeWriter_Prepare(pw, size, maxchar) < 0) {
        return -1;
    }
    for (Py_ssize_t i = 0; i < size; i++) {
        PyUnicode_WRITE(pw->kind, pw->data, pw->pos++, str[i]);
    }
    return 0;
}

#pragma GCC diagnostic pop

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
