/* More of the C-API over the kernel's types: sets, iterators, lists, bytes,
   weak references and the str codecs, each as a call of the type's own
   method (or constructor), which is where CPython's semantics live here. */

#include "lucifer.h"

#include <string.h>

static PyObject *
call_method(PyObject *self, const char *name, const char *format, ...)
{
    PyObject *method = PyObject_GetAttrString(self, name);
    va_list ap;
    va_start(ap, format);
    PyObject *args = method != NULL ? Py_VaBuildValue(format, ap) : NULL;
    va_end(ap);
    PyObject *r = args != NULL ? PyObject_CallObject(method, args) : NULL;
    Py_XDECREF(method);
    Py_XDECREF(args);
    return r;
}

static PyObject *
builtin(const char *name)
{
    return PyDict_GetItemString(PyInterpreterState_Get()->builtins, name);
}

/* Sets. */

PyObject *
PySet_New(PyObject *iterable)
{
    return iterable != NULL ? PyObject_CallOneArg((PyObject *)&PySet_Type, iterable)
                            : PyObject_CallNoArgs((PyObject *)&PySet_Type);
}

PyObject *
PyFrozenSet_New(PyObject *iterable)
{
    return iterable != NULL ? PyObject_CallOneArg((PyObject *)&PyFrozenSet_Type, iterable)
                            : PyObject_CallNoArgs((PyObject *)&PyFrozenSet_Type);
}

int
PySet_Add(PyObject *anyset, PyObject *key)
{
    if (!PySet_Check(anyset) && (!PyFrozenSet_Check(anyset) || Py_REFCNT(anyset) != 1)) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyObject *args[2] = { anyset, key };
    PyObject *r = lu_call_kernel(LU_K_set_add, args, 2);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PySet_Contains(PyObject *anyset, PyObject *key)
{
    if (!PyAnySet_Check(anyset)) {
        PyErr_BadInternalCall();
        return -1;
    }
    /* The key is hashed, never compared by equality: an unhashable key is a
       TypeError (set_contains_key), not a scan. */
    if (PyObject_Hash(key) == -1) {
        return -1;
    }
    return PySequence_Contains(anyset, key);
}

int
PySet_Discard(PyObject *set, PyObject *key)
{
    if (!PySet_Check(set)) {
        PyErr_BadInternalCall();
        return -1;
    }
    int found = PySequence_Contains(set, key);
    if (found <= 0) {
        return found;
    }
    PyObject *r = call_method(set, "discard", "(O)", key);
    Py_XDECREF(r);
    return r == NULL ? -1 : 1;
}

PyObject *
PySet_Pop(PyObject *set)
{
    if (!PySet_Check(set)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return call_method(set, "pop", "()");
}

int
PySet_Clear(PyObject *set)
{
    if (!PySet_Check(set)) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyObject *r = call_method(set, "clear", "()");
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

Py_ssize_t
PySet_Size(PyObject *anyset)
{
    if (!PyAnySet_Check(anyset)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return PyObject_Size(anyset);
}

/* Iterators. */

int
PyIter_NextItem(PyObject *iter, PyObject **item)
{
    if (!PyIter_Check(iter)) {
        PyErr_Format(PyExc_TypeError, "expected an iterator, got '%.100s'", Py_TYPE(iter)->tp_name);
        *item = NULL;
        return -1;
    }
    *item = PyIter_Next(iter);
    return *item != NULL ? 1 : PyErr_Occurred() ? -1 : 0;
}

PySendResult
PyIter_Send(PyObject *iter, PyObject *arg, PyObject **result)
{
    PyObject *r = arg == Py_None && PyIter_Check(iter) ? PyIter_Next(iter) : call_method(iter, "send", "(O)", arg);
    if (r != NULL) {
        *result = r;
        return PYGEN_NEXT;
    }
    if (PyErr_ExceptionMatches(PyExc_StopIteration)) {
        PyObject *exc = PyErr_GetRaisedException();
        *result = PyObject_GetAttrString(exc, "value");
        Py_DECREF(exc);
        return *result != NULL ? PYGEN_RETURN : PYGEN_ERROR;
    }
    if (!PyErr_Occurred()) {                    /* PyIter_Next: exhausted without a value */
        *result = Py_NewRef(Py_None);
        return PYGEN_RETURN;
    }
    *result = NULL;
    return PYGEN_ERROR;
}

PyObject *
PyObject_GetAIter(PyObject *o)
{
    return lu_call_kernel(LU_K_get_aiter, &o, 1);
}

/* Lists, dicts, objects. */

int
PyList_Clear(PyObject *list)
{
    PyObject *r = PyList_Check(list) ? call_method(list, "clear", "()") : (PyErr_BadInternalCall(), NULL);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PyList_Extend(PyObject *list, PyObject *iterable)
{
    PyObject *r = PyList_Check(list) ? call_method(list, "extend", "(O)", iterable) : (PyErr_BadInternalCall(), NULL);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PyDict_PopString(PyObject *dict, const char *key, PyObject **result)
{
    PyObject *k = PyUnicode_FromString(key);
    if (k == NULL) {
        if (result != NULL) {
            *result = NULL;     /* every failure leaves it NULL, CPython's contract */
        }
        return -1;
    }
    int r = PyDict_Pop(dict, k, result);
    Py_DECREF(k);
    return r;
}

int
PyDict_MergeFromSeq2(PyObject *d, PyObject *seq2, int override)
{
    PyObject *it = PyObject_GetIter(seq2), *item;
    if (it == NULL) {
        return -1;
    }
    int status = 0;
    for (Py_ssize_t i = 0; status == 0 && (item = PyIter_Next(it)) != NULL; i++) {
        PyObject *fast = PySequence_Fast(item, "cannot convert dictionary update sequence element to a sequence");
        if (fast == NULL) {
            status = -1;
        }
        else if (PySequence_Fast_GET_SIZE(fast) != 2) {
            PyErr_Format(PyExc_ValueError, "dictionary update sequence element #%zd has length %zd; 2 is required",
                         i, PySequence_Fast_GET_SIZE(fast));
            status = -1;
        }
        else {
            PyObject *key = PySequence_Fast_GET_ITEM(fast, 0), *value = PySequence_Fast_GET_ITEM(fast, 1);
            if (override || PyDict_Contains(d, key) == 0) {
                status = PyDict_SetItem(d, key, value);
            }
        }
        Py_XDECREF(fast);
        Py_DECREF(item);
    }
    Py_DECREF(it);
    return status < 0 || PyErr_Occurred() ? -1 : 0;
}

int
PyObject_DelItemString(PyObject *o, const char *key)
{
    if (o == NULL || key == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyObject *k = PyUnicode_FromString(key);
    int r = k != NULL ? PyObject_DelItem(o, k) : -1;
    Py_XDECREF(k);
    return r;
}

int
PyObject_GetOptionalAttrString(PyObject *obj, const char *name, PyObject **result)
{
    *result = NULL;      /* every failure leaves it NULL, CPython's contract */
    if (obj == NULL || name == NULL) {
        if (!PyErr_Occurred()) {
            PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
        }
        return -1;
    }
    PyObject *n = PyUnicode_FromString(name);
    int r = n != NULL ? PyObject_GetOptionalAttr(obj, n, result) : -1;
    Py_XDECREF(n);
    return r;
}

int
PyObject_IS_GC(PyObject *obj)
{
    return PyType_IS_GC(Py_TYPE(obj)) && (Py_TYPE(obj)->tp_is_gc == NULL || Py_TYPE(obj)->tp_is_gc(obj));
}

PyObject *
PyNumber_ToBase(PyObject *n, int base)
{
    if (!(base == 2 || base == 8 || base == 10 || base == 16)) {
        PyErr_SetString(PyExc_SystemError, "PyNumber_ToBase: base must be 2, 8, 10 or 16");
        return NULL;
    }
    PyObject *index = PyNumber_Index(n);
    if (index == NULL) {
        return NULL;
    }
    const char *name = base == 2 ? "bin" : base == 8 ? "oct" : base == 16 ? "hex" : NULL;
    PyObject *res = name != NULL ? PyObject_CallOneArg(builtin(name), index) : PyObject_Str(index);
    Py_DECREF(index);
    return res;
}

PyObject *
PyMethod_Function(PyObject *m)
{
    return ((PyMethodObject *)m)->im_func;
}

PyObject *
PyMethod_Self(PyObject *m)
{
    return ((PyMethodObject *)m)->im_self;
}

PyCFunction
PyCFunction_GetFunction(PyObject *op)
{
    return ((PyCFunctionObject *)op)->m_ml->ml_meth;
}

PyObject *
PyCFunction_GetSelf(PyObject *op)
{
    return (((PyCFunctionObject *)op)->m_ml->ml_flags & METH_STATIC) ? NULL : ((PyCFunctionObject *)op)->m_self;
}

int
PyCFunction_GetFlags(PyObject *op)
{
    return ((PyCFunctionObject *)op)->m_ml->ml_flags;
}

LU_EXPORT PyObject *
PyCFunction_Call(PyObject *callable, PyObject *args, PyObject *kwargs)
{
    return PyObject_Call(callable, args, kwargs);
}

/* Bytes. PyBytes_FromObject, PyBytes_Join and PyBytes_Repr are the
   Objects/bytesobject.c ports in runtime/prim/bytes.c. */

PyObject *
PyByteArray_FromObject(PyObject *x)
{
    return PyObject_CallOneArg((PyObject *)&PyByteArray_Type, x);
}

/* Objects/bytearrayobject.c PyByteArray_Concat: raw buffers, so a strided
   view or a non-buffer raises rather than going through the constructor. */
PyObject *
PyByteArray_Concat(PyObject *a, PyObject *b)
{
    Py_buffer va, vb;
    PyByteArrayObject *result = NULL;

    va.len = -1;
    vb.len = -1;
    if (PyObject_GetBuffer(a, &va, PyBUF_SIMPLE) != 0 ||
        PyObject_GetBuffer(b, &vb, PyBUF_SIMPLE) != 0) {
            PyErr_Format(PyExc_TypeError, "can't concat %.100s to %.100s",
                         Py_TYPE(b)->tp_name, Py_TYPE(a)->tp_name);
            goto done;
    }

    if (va.len > PY_SSIZE_T_MAX - vb.len) {
        PyErr_NoMemory();
        goto done;
    }

    result = (PyByteArrayObject *) \
        PyByteArray_FromStringAndSize(NULL, va.len + vb.len);
    /* result->ob_bytes is NULL if result is an empty bytearray. */
    if (result != NULL && result->ob_bytes != NULL) {
        memcpy(result->ob_bytes, va.buf, va.len);
        memcpy(result->ob_bytes + va.len, vb.buf, vb.len);
    }

  done:
    if (va.len != -1)
        PyBuffer_Release(&va);
    if (vb.len != -1)
        PyBuffer_Release(&vb);
    return (PyObject *)result;
}

PyObject *
PyBytes_FromFormatV(const char *format, va_list vargs)
{
    PyObject *text = PyUnicode_FromFormatV(format, vargs);
    PyObject *r = text != NULL ? PyUnicode_AsEncodedString(text, "utf-8", "surrogateescape") : NULL;
    Py_XDECREF(text);
    return r;
}

PyObject *
PyBytes_FromFormat(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyObject *r = PyBytes_FromFormatV(format, ap);
    va_end(ap);
    return r;
}

/* _PyBytes_Find (Objects/bytesobject.c): the first index of needle in
   haystack, plus offset, or -1. */
LU_EXPORT Py_ssize_t
_PyBytes_Find(const char *haystack, Py_ssize_t len_haystack,
              const char *needle, Py_ssize_t len_needle, Py_ssize_t offset)
{
    if (len_needle == 0) {
        return offset;
    }
    const char *found = memmem(haystack, len_haystack, needle, len_needle);
    return found != NULL ? found - haystack + offset : -1;
}

/* Weak references. */

PyObject *
PyWeakref_NewRef(PyObject *ob, PyObject *callback)
{
    return lu_weakref_new(&_PyWeakref_RefType, ob, callback == Py_None ? NULL : callback);
}

/* A CallableProxyType for a callable referent (Objects/weakrefobject.c). */
PyObject *
PyWeakref_NewProxy(PyObject *ob, PyObject *callback)
{
    PyTypeObject *type = PyCallable_Check(ob) ? &_PyWeakref_CallableProxyType : &_PyWeakref_ProxyType;
    return lu_weakref_new(type, ob, callback == Py_None ? NULL : callback);
}

/* A borrowed reference, as the contract says (Objects/weakrefobject.c): the
   strong one taken here is dropped at once, which only a referent kept alive
   by another thread's references survives. */
PyObject *
PyWeakref_GetObject(PyObject *ref)
{
    if (ref == NULL || !PyWeakref_Check(ref)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    PyObject *ob = lu_weakref_get(ref);
    if (ob == NULL) {
        return Py_None;
    }
    Py_DECREF(ob);
    return ob;
}

int
PyWeakref_GetRef(PyObject *ref, PyObject **pobj)
{
    if (ref == NULL) {
        *pobj = NULL;
        PyErr_BadInternalCall();
        return -1;
    }
    if (!PyWeakref_Check(ref)) {
        *pobj = NULL;
        PyErr_SetString(PyExc_TypeError, "expected a weakref");
        return -1;
    }
    *pobj = lu_weakref_get(ref);
    return *pobj != NULL;
}

/* 1 when the referent of the weak reference is gone, 0 while it lives. */
LU_EXPORT int
_PyWeakref_IsDead(PyObject *ref)
{
    PyObject *obj;
    int alive = PyWeakref_GetRef(ref, &obj);
    if (alive < 0) {
        return -1;
    }
    Py_XDECREF(obj);
    return !alive;
}

int
PyWeakref_IsDead(PyObject *ref)
{
    if (ref == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (!PyWeakref_Check(ref)) {
        PyErr_Format(PyExc_TypeError, "expected a weakref, got %s", Py_TYPE(ref)->tp_name);
        return -1;
    }
    PyObject *ob = lu_weakref_get(ref);
    Py_XDECREF(ob);
    return ob == NULL;
}

/* str codecs: decoding is bytes(...).decode(encoding, errors). */

static PyObject *
decode(const char *s, Py_ssize_t size, const char *encoding, const char *errors)
{
    PyObject *b = PyBytes_FromStringAndSize(s, size);
    PyObject *r = b != NULL ? call_method(b, "decode", "(ss)", encoding, errors != NULL ? errors : "strict") : NULL;
    Py_XDECREF(b);
    return r;
}

PyObject *
PyUnicode_Decode(const char *s, Py_ssize_t size, const char *encoding, const char *errors)
{
    return decode(s, size, encoding != NULL ? encoding : "utf-8", errors);
}

PyObject *
PyUnicode_DecodeASCII(const char *s, Py_ssize_t size, const char *errors)
{
    return decode(s, size, "ascii", errors);
}

PyObject *
PyUnicode_DecodeLatin1(const char *s, Py_ssize_t size, const char *errors)
{
    return decode(s, size, "latin-1", errors);
}

PyObject *
PyUnicode_DecodeUnicodeEscape(const char *s, Py_ssize_t size, const char *errors)
{
    return decode(s, size, "unicode_escape", errors);
}

PyObject *
PyUnicode_DecodeRawUnicodeEscape(const char *s, Py_ssize_t size, const char *errors)
{
    return decode(s, size, "raw_unicode_escape", errors);
}

PyObject *
PyUnicode_DecodeFSDefaultAndSize(const char *s, Py_ssize_t size)
{
    return decode(s, size, "utf-8", "surrogateescape");
}

PyObject *
PyUnicode_DecodeFSDefault(const char *s)
{
    return PyUnicode_DecodeFSDefaultAndSize(s, (Py_ssize_t)strlen(s));
}

PyObject *
PyUnicode_DecodeLocaleAndSize(const char *str, Py_ssize_t len, const char *errors)
{
    return decode(str, len, "utf-8", errors != NULL && !strcmp(errors, "surrogateescape") ? errors : "strict");
}

PyObject *
PyUnicode_DecodeLocale(const char *str, const char *errors)
{
    return PyUnicode_DecodeLocaleAndSize(str, (Py_ssize_t)strlen(str), errors);
}

PyObject *
PyUnicode_EncodeFSDefault(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "utf-8", "surrogateescape");
}

PyObject *
PyUnicode_EncodeLocale(PyObject *unicode, const char *errors)
{
    return PyUnicode_AsEncodedString(unicode, "utf-8", errors);
}

PyObject *
PyUnicode_AsUTF16String(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "utf-16", NULL);
}

PyObject *
PyUnicode_AsUTF32String(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "utf-32", NULL);
}

PyObject *
PyUnicode_AsUnicodeEscapeString(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "unicode_escape", NULL);
}

PyObject *
PyUnicode_AsRawUnicodeEscapeString(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "raw_unicode_escape", NULL);
}

PyObject *
PyUnicode_FromWideChar(const wchar_t *w, Py_ssize_t size)
{
    if (w == NULL && size != 0) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (size == -1) {
        size = (Py_ssize_t)wcslen(w);
    }
    if (size < 0) {
        PyErr_SetString(PyExc_SystemError, "Negative size passed to PyUnicode_New");
        return NULL;
    }
    for (Py_ssize_t i = 0; i < size; i++) {
        if ((Py_UCS4)w[i] > 0x10ffff) {
            PyErr_Format(PyExc_ValueError, "character U+%x is not in range [U+0000; U+10ffff]",
                         (unsigned int)w[i]);
            return NULL;
        }
    }
    return PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, w, size);
}

Py_ssize_t
PyUnicode_AsWideChar(PyObject *unicode, wchar_t *w, Py_ssize_t size)
{
    if (unicode == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return -1;
    }
    Py_ssize_t len = PyUnicode_GET_LENGTH(unicode);
    int kind = PyUnicode_KIND(unicode);
    const void *data = PyUnicode_DATA(unicode);
    Py_ssize_t n = size < len ? size : len;
    for (Py_ssize_t i = 0; w != NULL && i < n; i++) {
        w[i] = (wchar_t)PyUnicode_READ(kind, data, i);
    }
    if (w != NULL && size > len) {
        w[len] = 0;
    }
    return w == NULL ? len + 1 : n;
}

wchar_t *
PyUnicode_AsWideCharString(PyObject *unicode, Py_ssize_t *size)
{
    if (unicode == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    Py_ssize_t len = PyUnicode_GET_LENGTH(unicode);
    wchar_t *buffer = PyMem_New(wchar_t, len + 1);
    if (buffer == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    PyUnicode_AsWideChar(unicode, buffer, len + 1);
    if (size != NULL) {
        *size = len;
    }
    else if (wcslen(buffer) != (size_t)len) {
        PyMem_Free(buffer);
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        return NULL;
    }
    return buffer;
}

int
PyUnicode_IsIdentifier(PyObject *unicode)
{
    PyObject *r = call_method(unicode, "isidentifier", "()");
    int ok = r == Py_True;
    Py_XDECREF(r);
    return r == NULL ? -1 : ok;
}

Py_UCS4
PyUnicode_ReadChar(PyObject *unicode, Py_ssize_t index)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return (Py_UCS4)-1;
    }
    if (index < 0 || index >= PyUnicode_GET_LENGTH(unicode)) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return (Py_UCS4)-1;
    }
    return PyUnicode_READ(PyUnicode_KIND(unicode), PyUnicode_DATA(unicode), index);
}

LU_EXPORT Py_ssize_t
PyUnicode_GetSize(PyObject *unicode)
{
    return PyUnicode_GetLength(unicode);
}

const char *
PyUnicode_GetDefaultEncoding(void)
{
    return "utf-8";
}
