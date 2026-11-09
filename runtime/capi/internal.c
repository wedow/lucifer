/* Functions of CPython's internal headers (Include/internal) that the
   standard library's extension modules, built from the tag's own Modules/
   sources, call: _pickle above all. The _PyBytesWriter functions are
   Objects/bytesobject.c's, adapted; the rest are the same semantics over the
   public API. */

#include "lucifer.h"
#include <string.h>

/* _Py_ID and _Py_LATIN1_CHR as the vendored pycore_global_strings.h defines
   them: an immortal interned str. */
LU_EXPORT PyObject *
lu_static_str(const char *s)
{
    PyObject *str = PyUnicode_InternFromString(s);
    if (str == NULL) {
        Py_FatalError("lu_static_str: out of memory");
    }
    lu_set_immortal(str);
    return str;
}

LU_EXPORT PyObject *
lu_latin1_chr(unsigned int ch)
{
    Py_UCS1 c = (Py_UCS1)ch;
    PyObject *str = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, &c, 1);
    if (str == NULL) {
        Py_FatalError("lu_latin1_chr: out of memory");
    }
    lu_set_immortal(str);
    return str;
}

/* --- _PyBytesWriter (pycore_bytesobject.h) --- */

typedef struct {
    PyObject *buffer;           /* bytes, bytearray or NULL (when the small buffer is used) */
    Py_ssize_t allocated;
    Py_ssize_t min_size;        /* incremented by _PyBytesWriter_Prepare() */
    int use_bytearray;
    int overallocate;
    int use_small_buffer;
    char small_buffer[512];
} _PyBytesWriter;

#define OVERALLOCATE_FACTOR 4

LU_EXPORT void
_PyBytesWriter_Init(_PyBytesWriter *writer)
{
    memset(writer, 0, offsetof(_PyBytesWriter, small_buffer));
}

LU_EXPORT void
_PyBytesWriter_Dealloc(_PyBytesWriter *writer)
{
    Py_CLEAR(writer->buffer);
}

static char *
writer_start(_PyBytesWriter *writer)
{
    if (writer->use_small_buffer) {
        return writer->small_buffer;
    }
    return writer->use_bytearray ? PyByteArray_AS_STRING(writer->buffer) : PyBytes_AS_STRING(writer->buffer);
}

LU_EXPORT void *
_PyBytesWriter_Resize(_PyBytesWriter *writer, void *str, Py_ssize_t size)
{
    Py_ssize_t allocated = size, pos = (char *)str - writer_start(writer);
    if (writer->overallocate && allocated <= PY_SSIZE_T_MAX - allocated / OVERALLOCATE_FACTOR) {
        allocated += allocated / OVERALLOCATE_FACTOR;
    }
    if (!writer->use_small_buffer) {
        if (writer->use_bytearray ? PyByteArray_Resize(writer->buffer, allocated) < 0
                                  : _PyBytes_Resize(&writer->buffer, allocated) < 0) {
            goto error;
        }
    }
    else {
        writer->buffer = writer->use_bytearray ? PyByteArray_FromStringAndSize(NULL, allocated)
                                               : PyBytes_FromStringAndSize(NULL, allocated);
        if (writer->buffer == NULL) {
            goto error;
        }
        writer->use_small_buffer = 0;
        if (pos != 0) {
            memcpy(writer_start(writer), writer->small_buffer, pos);
        }
    }
    writer->allocated = allocated;
    return writer_start(writer) + pos;
error:
    _PyBytesWriter_Dealloc(writer);
    return NULL;
}

LU_EXPORT void *
_PyBytesWriter_Prepare(_PyBytesWriter *writer, void *str, Py_ssize_t size)
{
    if (size == 0) {
        return str;
    }
    if (writer->min_size > PY_SSIZE_T_MAX - size) {
        PyErr_NoMemory();
        _PyBytesWriter_Dealloc(writer);
        return NULL;
    }
    Py_ssize_t new_min_size = writer->min_size + size;
    if (new_min_size > writer->allocated) {
        str = _PyBytesWriter_Resize(writer, str, new_min_size);
    }
    writer->min_size = new_min_size;
    return str;
}

LU_EXPORT void *
_PyBytesWriter_Alloc(_PyBytesWriter *writer, Py_ssize_t size)
{
    writer->use_small_buffer = 1;
    writer->allocated = sizeof(writer->small_buffer);
    return _PyBytesWriter_Prepare(writer, writer->small_buffer, size);
}

LU_EXPORT PyObject *
_PyBytesWriter_Finish(_PyBytesWriter *writer, void *str)
{
    Py_ssize_t size = (char *)str - writer_start(writer);
    if (size == 0 && !writer->use_bytearray) {
        Py_CLEAR(writer->buffer);
        return PyBytes_FromStringAndSize(NULL, 0);
    }
    if (writer->use_small_buffer) {
        return writer->use_bytearray ? PyByteArray_FromStringAndSize(writer->small_buffer, size)
                                     : PyBytes_FromStringAndSize(writer->small_buffer, size);
    }
    PyObject *result = writer->buffer;
    writer->buffer = NULL;
    if (size != writer->allocated) {
        if (writer->use_bytearray) {
            if (PyByteArray_Resize(result, size) < 0) {
                Py_DECREF(result);
                return NULL;
            }
        }
        else if (_PyBytes_Resize(&result, size) < 0) {
            return NULL;
        }
    }
    return result;
}

LU_EXPORT void *
_PyBytesWriter_WriteBytes(_PyBytesWriter *writer, void *ptr, const void *bytes, Py_ssize_t size)
{
    char *str = _PyBytesWriter_Prepare(writer, ptr, size);
    if (str == NULL) {
        return NULL;
    }
    memcpy(str, bytes, size);
    return str + size;
}

/* --- the rest --- */

/* Adds a note to the current exception (Python/errors.c). */
LU_EXPORT void
_PyErr_FormatNote(const char *format, ...)
{
    PyObject *exc = PyErr_GetRaisedException();
    if (exc == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, format);
    PyObject *note = PyUnicode_FromFormatV(format, ap);
    va_end(ap);
    PyObject *r = note == NULL ? NULL : PyObject_CallMethod(exc, "add_note", "O", note);
    Py_XDECREF(note);
    if (r == NULL) {
        _PyErr_ChainExceptions1(exc);
        return;
    }
    Py_DECREF(r);
    PyErr_SetRaisedException(exc);
}

/* builtins.NAME, or AttributeError. */
LU_EXPORT PyObject *
_PyEval_GetBuiltin(PyObject *name)
{
    PyObject *attr;
    if (PyMapping_GetOptionalItem(PyEval_GetBuiltins(), name, &attr) == 0) {
        PyErr_SetObject(PyExc_AttributeError, name);
    }
    return attr;
}

LU_EXPORT char *
_PyMem_Strdup(const char *str)
{
    size_t size = strlen(str) + 1;
    char *copy = PyMem_Malloc(size);
    if (copy != NULL) {
        memcpy(copy, str, size);
    }
    return copy;
}

/* Generic attribute lookup with DICT, when given, in place of the instance
   dict; with SUPPRESS, a missing attribute is NULL without an exception. */
PyObject *
_PyObject_GenericGetAttrWithDict(PyObject *obj, PyObject *name, PyObject *dict, int suppress)
{
    PyObject *result;
    if (dict == NULL) {
        result = PyObject_GenericGetAttr(obj, name);
    }
    else {
        PyTypeObject *tp = Py_TYPE(obj);
        PyObject *descr = _PyType_LookupRef(tp, name);
        descrgetfunc get = descr != NULL ? Py_TYPE(descr)->tp_descr_get : NULL;
        if (get != NULL && Py_TYPE(descr)->tp_descr_set != NULL) {
            result = get(descr, obj, (PyObject *)tp);
        }
        else if (PyDict_GetItemRef(dict, name, &result) != 0) {
            /* found, or an error */
        }
        else if (get != NULL) {
            result = get(descr, obj, (PyObject *)tp);
        }
        else if (descr != NULL) {
            result = Py_NewRef(descr);
        }
        else {
            PyErr_Format(PyExc_AttributeError, "'%.100s' object has no attribute '%U'", tp->tp_name, name);
        }
        Py_XDECREF(descr);
    }
    if (result == NULL && suppress && PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
    }
    return result;
}

/* sys.NAME, or RuntimeError when it is gone. */
LU_EXPORT PyObject *
_PySys_GetRequiredAttr(PyObject *name)
{
    if (!PyUnicode_Check(name)) {
        PyErr_Format(PyExc_TypeError, "attribute name must be string, not '%.200s'", Py_TYPE(name)->tp_name);
        return NULL;
    }
    PyObject *value = PySys_GetObject(PyUnicode_AsUTF8(name));
    if (value == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_Format(PyExc_RuntimeError, "lost sys.%U", name);
        }
        return NULL;
    }
    return Py_NewRef(value);
}

/* sys.getsizeof(o), or (size_t)-1 with an exception. */
LU_EXPORT size_t
_PySys_GetSizeOf(PyObject *o)
{
    PyObject *sys = PyImport_ImportModule("sys");
    PyObject *r = sys == NULL ? NULL : PyObject_CallMethod(sys, "getsizeof", "O", o);
    Py_XDECREF(sys);
    if (r == NULL) {
        return (size_t)-1;
    }
    size_t size = PyLong_AsSize_t(r);
    Py_DECREF(r);
    return size;
}

LU_EXPORT void
_PyUnicode_InternMortal(PyInterpreterState *interp, PyObject **p)
{
    PyUnicode_InternInPlace(p);
}
