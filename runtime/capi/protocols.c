/* The object, number, sequence, mapping, dict, list and tuple protocols for C
   callers, over the kernel's implementations: each takes the kernel function
   or method the interpreter runs for the same operation, so C and Python
   callers see one behaviour. */

#include "lucifer.h"
#include "object/dict.h"
#include "opcode.h"

#include <string.h>

/* The headers name these for their older spellings; the symbols exist under both. */
#undef PyObject_Length
#undef PySequence_Length
#undef PySequence_In
#undef PyMapping_Length

/* Helpers. */

static PyObject *
builtin(const char *name)
{
    PyObject *key = PyUnicode_InternFromString(name);
    PyObject *f = key != NULL ? lu_ns_lookup(PyThreadState_Get()->interp->builtins, key) : NULL;
    Py_XDECREF(key);
    if (f == NULL && !PyErr_Occurred()) {
        PyErr_Format(PyExc_SystemError, "builtins.%s is missing", name);
    }
    return f;
}

static PyObject *
call_builtin(const char *name, PyObject *const *args, Py_ssize_t n)
{
    PyObject *f = builtin(name);
    return f != NULL ? lu_call(f, args, n, NULL) : NULL;
}

/* obj.name(*args) through the type's special method. */
static PyObject *
call_method(PyObject *obj, const char *name, PyObject *const *args, Py_ssize_t n)
{
    PyObject *key = PyUnicode_InternFromString(name);
    if (key == NULL) {
        return NULL;
    }
    PyObject *r = lu_call_special(obj, key, args, n);
    Py_DECREF(key);
    return r;
}

static PyObject *
null_error(void)
{
    if (!PyErr_Occurred()) {
        PyErr_SetString(PyExc_SystemError, "null argument to internal routine");
    }
    return NULL;
}

/* A kernel result that is an int, as a C ssize; -1 with the error set otherwise. */
static Py_ssize_t
as_ssize(PyObject *r)
{
    if (r == NULL) {
        return -1;
    }
    Py_ssize_t n = PyLong_AsSsize_t(r);
    Py_DECREF(r);
    return n;
}

static int
as_bool(PyObject *r)
{
    if (r == NULL) {
        return -1;
    }
    int b = r == Py_True;
    Py_DECREF(r);
    return b;
}

/* Objects. */

PyObject *
PyBool_FromLong(long ok)
{
    return Py_NewRef(ok ? Py_True : Py_False);
}

Py_ssize_t
PyObject_Size(PyObject *o)
{
    return o == NULL ? (null_error(), -1) : as_ssize(lu_call_kernel(LU_K_length, &o, 1));
}

Py_ssize_t
PyObject_Length(PyObject *o)
{
    return PyObject_Size(o);
}

PyObject *
PyObject_GetIter(PyObject *o)
{
    return o == NULL ? null_error() : lu_call_kernel(LU_K_get_iter, &o, 1);
}

int
PyIter_Check(PyObject *o)
{
    return _PyType_Lookup(Py_TYPE(o), LU_ID(__next__)) != NULL;
}

PyObject *
PyIter_Next(PyObject *iter)
{
    PyObject *r = lu_call_kernel(LU_K_next_item, &iter, 1);
    if (r == LU_MISSING) {
        Py_DECREF(r);
        return NULL;
    }
    return r;
}

PyObject *
PyObject_CallObject(PyObject *callable, PyObject *args)
{
    if (args == NULL) {
        return lu_call(callable, NULL, 0, NULL);
    }
    if (!PyTuple_Check(args)) {
        PyErr_SetString(PyExc_TypeError, "argument list must be a tuple");
        return NULL;
    }
    return lu_call(callable, &PyTuple_GET_ITEM(args, 0), PyTuple_GET_SIZE(args), NULL);
}

/* The call of `callable` with the values a format builds (Py_BuildValue's
   rule: a tuple is the argument list, anything else one argument). */
static PyObject *
call_built(PyObject *callable, const char *format, va_list ap)
{
    if (callable == NULL) {
        return null_error();
    }
    PyObject *args = format != NULL && *format ? Py_VaBuildValue(format, ap) : PyTuple_New(0);
    if (args == NULL) {
        return NULL;
    }
    PyObject *r = PyTuple_Check(args) ? PyObject_CallObject(callable, args) : lu_call(callable, &args, 1, NULL);
    Py_DECREF(args);
    return r;
}

PyObject *
PyObject_CallFunction(PyObject *callable, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    PyObject *r = call_built(callable, format, ap);
    va_end(ap);
    return r;
}

PyObject *
PyObject_CallMethod(PyObject *obj, const char *name, const char *format, ...)
{
    if (obj == NULL || name == NULL) {
        return null_error();
    }
    PyObject *method = PyObject_GetAttrString(obj, name);
    if (method == NULL) {
        return NULL;
    }
    va_list ap;
    va_start(ap, format);
    PyObject *r = call_built(method, format, ap);
    va_end(ap);
    Py_DECREF(method);
    return r;
}

Py_hash_t
PyObject_Hash(PyObject *o)
{
    PyObject *h = call_builtin("hash", &o, 1);
    if (h == NULL) {
        return -1;
    }
    Py_hash_t r = PyLong_AsSsize_t(h);
    Py_DECREF(h);
    return r;
}

int
PyObject_HasAttrWithError(PyObject *o, PyObject *name)
{
    PyObject *v = PyObject_GetAttr(o, name);
    if (v != NULL) {
        Py_DECREF(v);
        return 1;
    }
    if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
        PyErr_Clear();
        return 0;
    }
    return -1;
}

int
PyObject_HasAttr(PyObject *o, PyObject *name)
{
    int r = PyObject_HasAttrWithError(o, name);
    if (r < 0) {
        PyErr_Clear();
        return 0;
    }
    return r;
}

int
PyObject_HasAttrStringWithError(PyObject *o, const char *name)
{
    PyObject *key = PyUnicode_FromString(name);
    int r = key != NULL ? PyObject_HasAttrWithError(o, key) : -1;
    Py_XDECREF(key);
    return r;
}

int
PyObject_HasAttrString(PyObject *o, const char *name)
{
    int r = PyObject_HasAttrStringWithError(o, name);
    if (r < 0) {
        PyErr_Clear();
        return 0;
    }
    return r;
}

int
PyObject_DelAttrString(PyObject *o, const char *name)
{
    return PyObject_SetAttrString(o, name, NULL);
}

int
PyObject_IsInstance(PyObject *inst, PyObject *cls)
{
    PyObject *args[2] = { inst, cls };
    return as_bool(call_builtin("isinstance", args, 2));
}

int
PyObject_IsSubclass(PyObject *derived, PyObject *cls)
{
    PyObject *args[2] = { derived, cls };
    return as_bool(call_builtin("issubclass", args, 2));
}

PyObject *
PyObject_Type(PyObject *o)
{
    return o == NULL ? null_error() : Py_NewRef((PyObject *)Py_TYPE(o));
}

int
PyObject_Not(PyObject *o)
{
    int t = PyObject_IsTrue(o);
    return t < 0 ? t : !t;
}

PyObject *
PyObject_Bytes(PyObject *o)
{
    return o == NULL ? null_error() : call_builtin("bytes", &o, 1);
}

PyObject *
PyObject_Dir(PyObject *o)
{
    return call_builtin("dir", &o, o == NULL ? 0 : 1);
}

PyObject *
PyObject_Format(PyObject *o, PyObject *spec)
{
    PyObject *empty = NULL;
    if (spec == NULL) {
        spec = empty = PyUnicode_New(0, 0);
        if (spec == NULL) {
            return NULL;
        }
    }
    PyObject *args[2] = { o, spec };
    PyObject *r = lu_call_kernel(LU_K_format, args, 2);
    Py_XDECREF(empty);
    return r;
}

/* Numbers: the binary operators through the interpreter's reflection. */

static PyObject *
binary(PyObject *a, PyObject *b, int op)
{
    if (a == NULL || b == NULL) {
        return null_error();
    }
    PyObject *code = PyLong_FromLong(op);
    PyObject *args[3] = { a, b, code };
    PyObject *r = code != NULL ? lu_call_kernel(LU_K_binary_op, args, 3) : NULL;
    Py_XDECREF(code);
    return r;
}

#define BINARY(name, op) PyObject *name(PyObject *a, PyObject *b) { return binary(a, b, op); }
BINARY(PyNumber_Add, NB_ADD) BINARY(PyNumber_Subtract, NB_SUBTRACT)
BINARY(PyNumber_Multiply, NB_MULTIPLY) BINARY(PyNumber_MatrixMultiply, NB_MATRIX_MULTIPLY)
BINARY(PyNumber_FloorDivide, NB_FLOOR_DIVIDE) BINARY(PyNumber_TrueDivide, NB_TRUE_DIVIDE)
BINARY(PyNumber_Remainder, NB_REMAINDER) BINARY(PyNumber_Lshift, NB_LSHIFT)
BINARY(PyNumber_Rshift, NB_RSHIFT) BINARY(PyNumber_And, NB_AND) BINARY(PyNumber_Or, NB_OR)
BINARY(PyNumber_Xor, NB_XOR)
BINARY(PyNumber_InPlaceAdd, NB_INPLACE_ADD) BINARY(PyNumber_InPlaceSubtract, NB_INPLACE_SUBTRACT)
BINARY(PyNumber_InPlaceMultiply, NB_INPLACE_MULTIPLY)
BINARY(PyNumber_InPlaceMatrixMultiply, NB_INPLACE_MATRIX_MULTIPLY)
BINARY(PyNumber_InPlaceFloorDivide, NB_INPLACE_FLOOR_DIVIDE)
BINARY(PyNumber_InPlaceTrueDivide, NB_INPLACE_TRUE_DIVIDE)
BINARY(PyNumber_InPlaceRemainder, NB_INPLACE_REMAINDER) BINARY(PyNumber_InPlaceLshift, NB_INPLACE_LSHIFT)
BINARY(PyNumber_InPlaceRshift, NB_INPLACE_RSHIFT) BINARY(PyNumber_InPlaceAnd, NB_INPLACE_AND)
BINARY(PyNumber_InPlaceOr, NB_INPLACE_OR) BINARY(PyNumber_InPlaceXor, NB_INPLACE_XOR)

PyObject *
PyNumber_Divmod(PyObject *a, PyObject *b)
{
    PyObject *args[2] = { a, b };
    return call_builtin("divmod", args, 2);
}

PyObject *
PyNumber_Power(PyObject *a, PyObject *b, PyObject *c)
{
    PyObject *args[3] = { a, b, c };
    return c == Py_None ? binary(a, b, NB_POWER) : call_builtin("pow", args, 3);
}

PyObject *
PyNumber_InPlacePower(PyObject *a, PyObject *b, PyObject *c)
{
    return c == Py_None ? binary(a, b, NB_INPLACE_POWER) : PyNumber_Power(a, b, c);
}

PyObject *
PyNumber_Negative(PyObject *o)
{
    return o == NULL ? null_error() : lu_call_kernel(LU_K_negative, &o, 1);
}

PyObject *
PyNumber_Positive(PyObject *o)
{
    return o == NULL ? null_error() : lu_call_kernel(LU_K_positive, &o, 1);
}

PyObject *
PyNumber_Invert(PyObject *o)
{
    return o == NULL ? null_error() : lu_call_kernel(LU_K_invert, &o, 1);
}

PyObject *
PyNumber_Absolute(PyObject *o)
{
    return o == NULL ? null_error() : call_builtin("abs", &o, 1);
}

PyObject *
PyNumber_Float(PyObject *o)
{
    return o == NULL ? null_error() : call_builtin("float", &o, 1);
}

PyObject *
PyNumber_Long(PyObject *o)
{
    return o == NULL ? null_error() : call_builtin("int", &o, 1);
}

int
PyNumber_Check(PyObject *o)
{
    if (o == NULL) {
        return 0;
    }
    PyTypeObject *tp = Py_TYPE(o);
    return _PyType_Lookup(tp, LU_ID(__index__)) != NULL || _PyType_Lookup(tp, LU_ID(__int__)) != NULL
        || _PyType_Lookup(tp, LU_ID(__float__)) != NULL || PyComplex_Check(o);
}

/* Sequences. */

int
PySequence_Check(PyObject *s)
{
    return s != NULL && !PyDict_Check(s) && _PyType_Lookup(Py_TYPE(s), LU_ID(__getitem__)) != NULL;
}

Py_ssize_t
PySequence_Length(PyObject *s)
{
    return PySequence_Size(s);
}

PyObject *
PySequence_GetItem(PyObject *s, Py_ssize_t i)
{
    if (s == NULL) {
        return null_error();
    }
    if (!PySequence_Check(s)) {
        PyErr_Format(PyExc_TypeError, Py_TYPE(s)->tp_as_mapping != NULL && Py_TYPE(s)->tp_as_mapping->mp_subscript
                     ? "'%.200s' object does not support indexing" : "'%.200s' object is not a sequence",
                     Py_TYPE(s)->tp_name);
        return NULL;
    }
    if (i < 0 && _PyType_Lookup(Py_TYPE(s), LU_ID(__len__)) != NULL) {
        Py_ssize_t n = PyObject_Size(s);
        if (n < 0) {
            return NULL;
        }
        i += n;
    }
    PyObject *index = PyLong_FromSsize_t(i);
    PyObject *args[2] = { s, index };
    PyObject *r = index != NULL ? lu_call_kernel(LU_K_getitem, args, 2) : NULL;
    Py_XDECREF(index);
    return r;
}

static PyObject *
slice_of(Py_ssize_t i1, Py_ssize_t i2)
{
    PyObject *a = PyLong_FromSsize_t(i1), *b = PyLong_FromSsize_t(i2);
    PyObject *s = a != NULL && b != NULL ? PySlice_New(a, b, NULL) : NULL;
    Py_XDECREF(a);
    Py_XDECREF(b);
    return s;
}

PyObject *
PySequence_GetSlice(PyObject *s, Py_ssize_t i1, Py_ssize_t i2)
{
    PyObject *slice = slice_of(i1, i2);
    PyObject *r = slice != NULL ? PyObject_GetItem(s, slice) : NULL;
    Py_XDECREF(slice);
    return r;
}

int
PySequence_SetItem(PyObject *s, Py_ssize_t i, PyObject *v)
{
    PyObject *index = PyLong_FromSsize_t(i);
    int r = index != NULL ? (v != NULL ? PyObject_SetItem(s, index, v) : PyObject_DelItem(s, index)) : -1;
    Py_XDECREF(index);
    return r;
}

int
PySequence_DelItem(PyObject *s, Py_ssize_t i)
{
    return PySequence_SetItem(s, i, NULL);
}

int
PySequence_SetSlice(PyObject *s, Py_ssize_t i1, Py_ssize_t i2, PyObject *v)
{
    PyObject *slice = slice_of(i1, i2);
    int r = slice != NULL ? (v != NULL ? PyObject_SetItem(s, slice, v) : PyObject_DelItem(s, slice)) : -1;
    Py_XDECREF(slice);
    return r;
}

int
PySequence_DelSlice(PyObject *s, Py_ssize_t i1, Py_ssize_t i2)
{
    return PySequence_SetSlice(s, i1, i2, NULL);
}

int
PySequence_Contains(PyObject *seq, PyObject *ob)
{
    PyObject *invert = Py_False;
    PyObject *args[3] = { seq, ob, invert };
    return as_bool(lu_call_kernel(LU_K_contains, args, 3));
}

int
PySequence_In(PyObject *seq, PyObject *ob)
{
    return PySequence_Contains(seq, ob);
}

PyObject *
PySequence_Concat(PyObject *a, PyObject *b)
{
    return binary(a, b, NB_ADD);
}

PyObject *
PySequence_InPlaceConcat(PyObject *a, PyObject *b)
{
    return binary(a, b, NB_INPLACE_ADD);
}

PyObject *
PySequence_Repeat(PyObject *a, Py_ssize_t n)
{
    PyObject *count = PyLong_FromSsize_t(n);
    PyObject *r = count != NULL ? binary(a, count, NB_MULTIPLY) : NULL;
    Py_XDECREF(count);
    return r;
}

PyObject *
PySequence_InPlaceRepeat(PyObject *a, Py_ssize_t n)
{
    PyObject *count = PyLong_FromSsize_t(n);
    PyObject *r = count != NULL ? binary(a, count, NB_INPLACE_MULTIPLY) : NULL;
    Py_XDECREF(count);
    return r;
}

Py_ssize_t
PySequence_Count(PyObject *s, PyObject *v)
{
    return as_ssize(call_method(s, "count", &v, 1));
}

Py_ssize_t
PySequence_Index(PyObject *s, PyObject *v)
{
    return as_ssize(call_method(s, "index", &v, 1));
}

PyObject *
PySequence_List(PyObject *s)
{
    return s == NULL ? null_error() : call_builtin("list", &s, 1);
}

PyObject *
PySequence_Tuple(PyObject *s)
{
    return s == NULL ? null_error() : lu_call_kernel(LU_K_tuple_of, &s, 1);
}

PyObject *
PySequence_Fast(PyObject *s, const char *msg)
{
    if (s == NULL) {
        return null_error();
    }
    if (PyList_CheckExact(s) || PyTuple_CheckExact(s)) {
        return Py_NewRef(s);
    }
    PyObject *r = PySequence_List(s);
    if (r == NULL && PyErr_ExceptionMatches(PyExc_TypeError)) {
        PyErr_SetString(PyExc_TypeError, msg);
    }
    return r;
}

/* Mappings. */

int
PyMapping_Check(PyObject *o)
{
    return o != NULL && _PyType_Lookup(Py_TYPE(o), LU_ID(__getitem__)) != NULL;
}

Py_ssize_t
PyMapping_Size(PyObject *o)
{
    return PyObject_Size(o);
}

Py_ssize_t
PyMapping_Length(PyObject *o)
{
    return PyObject_Size(o);
}

PyObject *
PyMapping_GetItemString(PyObject *o, const char *key)
{
    PyObject *k = PyUnicode_FromString(key);
    PyObject *r = k != NULL ? PyObject_GetItem(o, k) : NULL;
    Py_XDECREF(k);
    return r;
}

int
PyMapping_SetItemString(PyObject *o, const char *key, PyObject *v)
{
    PyObject *k = PyUnicode_FromString(key);
    int r = k != NULL ? PyObject_SetItem(o, k, v) : -1;
    Py_XDECREF(k);
    return r;
}

int
PyMapping_GetOptionalItem(PyObject *o, PyObject *key, PyObject **result)
{
    *result = PyObject_GetItem(o, key);
    if (*result != NULL) {
        return 1;
    }
    if (PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyErr_Clear();
        return 0;
    }
    return -1;
}

int
PyMapping_GetOptionalItemString(PyObject *o, const char *key, PyObject **result)
{
    PyObject *k = PyUnicode_FromString(key);
    int r = k != NULL ? PyMapping_GetOptionalItem(o, k, result) : (*result = NULL, -1);
    Py_XDECREF(k);
    return r;
}

int
PyMapping_HasKeyWithError(PyObject *o, PyObject *key)
{
    PyObject *v;
    int r = PyMapping_GetOptionalItem(o, key, &v);
    Py_XDECREF(v);
    return r;
}

int
PyMapping_HasKey(PyObject *o, PyObject *key)
{
    int r = PyMapping_HasKeyWithError(o, key);
    if (r < 0) {
        PyErr_Clear();
        return 0;
    }
    return r;
}

int
PyMapping_HasKeyStringWithError(PyObject *o, const char *key)
{
    PyObject *v;
    int r = PyMapping_GetOptionalItemString(o, key, &v);
    Py_XDECREF(v);
    return r;
}

int
PyMapping_HasKeyString(PyObject *o, const char *key)
{
    int r = PyMapping_HasKeyStringWithError(o, key);
    if (r < 0) {
        PyErr_Clear();
        return 0;
    }
    return r;
}

static PyObject *
mapping_list(PyObject *o, const char *method)
{
    PyObject *view = call_method(o, method, NULL, 0);
    PyObject *r = view != NULL ? call_builtin("list", &view, 1) : NULL;
    Py_XDECREF(view);
    return r;
}

/* The dict's own keys/values/items as a list, whatever a subclass overrides:
   the method out of PyDict_Type's own dict (PyDict_Keys' contract). */
static PyObject *
dict_own_list(PyObject *o, const char *method)
{
    PyObject *name = PyUnicode_InternFromString(method);
    PyObject *func = name != NULL ? _PyType_Lookup(&PyDict_Type, name) : NULL;
    Py_XDECREF(name);
    PyObject *view = func != NULL ? lu_call(func, &o, 1, NULL) : NULL;
    if (view == NULL) {
        return NULL;
    }
    PyObject *r = call_builtin("list", &view, 1);
    Py_DECREF(view);
    return r;
}

PyObject *
PyMapping_Keys(PyObject *o)
{
    return mapping_list(o, "keys");
}

PyObject *
PyMapping_Values(PyObject *o)
{
    return mapping_list(o, "values");
}

PyObject *
PyMapping_Items(PyObject *o)
{
    return mapping_list(o, "items");
}

/* Dicts. */

static PyObject *
dict_arg(PyObject *op)
{
    if (op == NULL || !PyDict_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return op;
}

Py_ssize_t
PyDict_Size(PyObject *op)
{
    return dict_arg(op) != NULL ? ((PyDictObject *)op)->ma_used : -1;
}

int
PyDict_SetItemString(PyObject *op, const char *key, PyObject *v)
{
    PyObject *k = PyUnicode_FromString(key);
    int r = k != NULL ? PyDict_SetItem(op, k, v) : -1;
    Py_XDECREF(k);
    return r;
}

int
PyDict_DelItemString(PyObject *op, const char *key)
{
    PyObject *k = PyUnicode_FromString(key);
    int r = k != NULL ? PyDict_DelItem(op, k) : -1;
    Py_XDECREF(k);
    return r;
}

int
PyDict_Contains(PyObject *op, PyObject *key)
{
    PyObject *v;
    int r = PyDict_GetItemRef(op, key, &v);
    Py_XDECREF(v);
    return r;
}

int
PyDict_ContainsString(PyObject *op, const char *key)
{
    PyObject *k = PyUnicode_FromString(key);
    int r = k != NULL ? PyDict_Contains(op, k) : -1;
    Py_XDECREF(k);
    return r;
}

/* The dict's own lookup, as dict.get: a subclass's __getitem__ or __missing__ takes no part. */
int
PyDict_GetItemRef(PyObject *op, PyObject *key, PyObject **result)
{
    *result = NULL;
    if (dict_arg(op) == NULL) {
        return -1;
    }
    PyObject *args[3] = { op, key, LU_MISSING };
    PyObject *v = lu_call_kernel(LU_K_dict_get, args, 3);
    if (v == NULL) {
        return -1;
    }
    if (v == LU_MISSING) {
        Py_DECREF(v);
        return 0;
    }
    *result = v;
    return 1;
}

int
PyDict_GetItemStringRef(PyObject *op, const char *key, PyObject **result)
{
    PyObject *k = PyUnicode_FromString(key);
    int r = k != NULL ? PyDict_GetItemRef(op, k, result) : (*result = NULL, -1);
    Py_XDECREF(k);
    return r;
}

PyObject *
PyDict_GetItemWithError(PyObject *op, PyObject *key)
{
    PyObject *v;
    if (PyDict_GetItemRef(op, key, &v) <= 0) {
        return NULL;
    }
    Py_DECREF(v);                       /* the dict keeps it */
    return v;
}

/* PyDict_GetItem swallows every error, reporting one that is not a missing
   key as unraisable, the hash failure raw (Objects/dictobject.c
   dict_getitem). An exception already raised stays aside and is restored.
   Reporting one while another is being reported would not end, and before
   the kernel is up - static types inherit through here while it boots - a
   failed lookup is only silence. */
static _Thread_local int reporting_unraisable;

static int
report_unraisable(void)
{
    return !reporting_unraisable && PyInterpreterState_Get()->specializing;
}

PyObject *
PyDict_GetItem(PyObject *op, PyObject *key)
{
    if (!PyDict_Check(op)) {
        return NULL;
    }
    /* the raw slot hash: PyObject_Hash goes through builtins.hash, whose own
       lookup lands here again (dict_getitem hashes with _PyObject_HashFast) */
    hashfunc hash_of = Py_TYPE(key)->tp_hash;
    Py_hash_t hash = hash_of != NULL ? hash_of(key) : -1;
    if (hash_of == NULL || (hash == -1 && PyErr_Occurred())) {
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_KeyError) && report_unraisable()) {
            reporting_unraisable = 1;
            PyErr_FormatUnraisable("Exception ignored in PyDict_GetItem(); consider "
                                   "using PyDict_GetItemRef() or PyDict_GetItemWithError()");
            reporting_unraisable = 0;
        }
        PyErr_Clear();
        return NULL;
    }
    PyObject *exc = PyErr_GetRaisedException();
    PyObject *v = PyDict_GetItemWithError(op, key);
    if (v == NULL && PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_KeyError) && report_unraisable()) {
        reporting_unraisable = 1;
        PyErr_FormatUnraisable("Exception ignored in PyDict_GetItem(); consider "
                               "using PyDict_GetItemRef() or PyDict_GetItemWithError()");
        reporting_unraisable = 0;
    }
    PyErr_Clear();
    PyErr_SetRaisedException(exc);
    return v;
}

PyObject *
PyDict_GetItemString(PyObject *op, const char *key)
{
    PyObject *k = PyUnicode_FromString(key);
    if (k == NULL) {
        PyErr_FormatUnraisable("Exception ignored in PyDict_GetItemString(); consider "
                               "using PyDict_GetItemStringRef()");
        PyErr_Clear();
        return NULL;
    }
    PyObject *v = PyDict_GetItem(op, k);
    Py_DECREF(k);
    return v;
}

PyObject *
PyDict_Copy(PyObject *op)
{
    if (dict_arg(op) == NULL) {
        return NULL;
    }
    return lu_dict_copy((PyDictObject *)op);    /* an exact dict, subclasses too */
}

PyObject *
PyDict_Keys(PyObject *op)
{
    return dict_arg(op) != NULL ? dict_own_list(op, "keys") : NULL;
}

PyObject *
PyDict_Values(PyObject *op)
{
    return dict_arg(op) != NULL ? dict_own_list(op, "values") : NULL;
}

PyObject *
PyDict_Items(PyObject *op)
{
    return dict_arg(op) != NULL ? dict_own_list(op, "items") : NULL;
}

int
PyDict_Next(PyObject *op, Py_ssize_t *pos, PyObject **key, PyObject **value)
{
    PyObject *k, *v;
    if (!PyDict_Check(op) || !lu_ns_next(op, pos, &k, &v)) {
        return 0;
    }
    if (key != NULL) {
        *key = k;
    }
    if (value != NULL) {
        *value = v;
    }
    return 1;
}

int
PyDict_Update(PyObject *a, PyObject *b)
{
    if (a == NULL || b == NULL || dict_arg(a) == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    /* a plain non-mapping has no keys to merge: the AttributeError is
       PyDict_Update's own contract (dict_update_common) */
    PyObject *keys = PyObject_GetAttrString(b, "keys");
    if (keys == NULL) {
        return -1;
    }
    Py_DECREF(keys);
    PyObject *r = call_method(a, "update", &b, 1);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PyDict_Merge(PyObject *a, PyObject *b, int override)
{
    if (a == NULL || b == NULL || !PyDict_Check(a)) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (override != 0) {
        return PyDict_Update(a, b);
    }
    PyObject *items = PyMapping_Items(b);
    if (items == NULL) {
        return -1;
    }
    int err = 0;
    for (Py_ssize_t i = 0; !err && i < PyList_GET_SIZE(items); i++) {
        PyObject *pair = PyList_GET_ITEM(items, i);
        PyObject *key = PyTuple_GET_ITEM(pair, 0), *value = PyTuple_GET_ITEM(pair, 1);
        int has = PyDict_Contains(a, key);
        if (has <= 0) {
            err = has < 0 ? -1 : PyDict_SetItem(a, key, value);
        }
    }
    Py_DECREF(items);
    return err;
}

/* Lists. */

static int
list_arg(PyObject *op)
{
    if (op == NULL || !PyList_Check(op)) {
        PyErr_BadInternalCall();
        return 0;
    }
    return 1;
}

Py_ssize_t
PyList_Size(PyObject *op)
{
    return list_arg(op) ? PyList_GET_SIZE(op) : -1;
}

int
PyList_Append(PyObject *op, PyObject *item)
{
    if (!list_arg(op) || item == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    int err;
    Py_BEGIN_CRITICAL_SECTION(op);
    err = lu_list_append(op, item);
    Py_END_CRITICAL_SECTION();
    return err;
}

PyObject *
PyList_GetItem(PyObject *op, Py_ssize_t i)
{
    if (!list_arg(op)) {
        return NULL;
    }
    if (i < 0 || i >= PyList_GET_SIZE(op)) {
        PyErr_SetString(PyExc_IndexError, "list index out of range");
        return NULL;
    }
    return PyList_GET_ITEM(op, i);
}

PyObject *
PyList_GetItemRef(PyObject *op, Py_ssize_t i)
{
    if (!PyList_Check(op)) {
        PyErr_SetString(PyExc_TypeError, "expected a list");
        return NULL;
    }
    PyObject *item = lu_list_item_ref((PyListObject *)op, i);
    if (item == NULL) {
        PyErr_SetString(PyExc_IndexError, "list index out of range");
    }
    return item;
}

int
PyList_SetItem(PyObject *op, Py_ssize_t i, PyObject *newitem)
{
    if (!list_arg(op)) {
        Py_XDECREF(newitem);
        return -1;
    }
    if (i < 0 || i >= PyList_GET_SIZE(op)) {
        Py_XDECREF(newitem);
        PyErr_SetString(PyExc_IndexError, "list assignment index out of range");
        return -1;
    }
    int r = PySequence_SetItem(op, i, newitem);
    Py_XDECREF(newitem);
    return r;
}

PyObject *
PyList_AsTuple(PyObject *op)
{
    return list_arg(op) ? lu_call_kernel(LU_K_tuple_of, &op, 1) : NULL;
}

PyObject *
PyList_GetSlice(PyObject *op, Py_ssize_t low, Py_ssize_t high)
{
    return list_arg(op) ? PySequence_GetSlice(op, low, high) : NULL;
}

int
PyList_Reverse(PyObject *op)
{
    PyObject *r = list_arg(op) ? call_method(op, "reverse", NULL, 0) : NULL;
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

int
PyList_Sort(PyObject *op)
{
    PyObject *r = list_arg(op) ? call_method(op, "sort", NULL, 0) : NULL;
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* Tuples. */

Py_ssize_t
PyTuple_Size(PyObject *op)
{
    if (op == NULL || !PyTuple_Check(op)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return PyTuple_GET_SIZE(op);
}

PyObject *
PyTuple_GetItem(PyObject *op, Py_ssize_t i)
{
    if (op == NULL || !PyTuple_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (i < 0 || i >= PyTuple_GET_SIZE(op)) {
        PyErr_SetString(PyExc_IndexError, "tuple index out of range");
        return NULL;
    }
    return PyTuple_GET_ITEM(op, i);
}

int
PyTuple_SetItem(PyObject *op, Py_ssize_t i, PyObject *newitem)
{
    PyObject *ob = op;
    int unique = op != NULL && PyTuple_Check(op)
        && _Py_IsOwnedByCurrentThread(ob) && ob->ob_ref_local == 1
        && _Py_atomic_load_ssize_relaxed(&ob->ob_ref_shared) == 0;
    if (!unique) {
        Py_XDECREF(newitem);
        PyErr_BadInternalCall();
        return -1;
    }
    if (i < 0 || i >= PyTuple_GET_SIZE(op)) {
        Py_XDECREF(newitem);
        PyErr_SetString(PyExc_IndexError, "tuple assignment index out of range");
        return -1;
    }
    PyObject **slot = &PyTuple_GET_ITEM(op, i);
    PyObject *old = *slot;
    *slot = newitem;
    Py_XDECREF(old);
    return 0;
}

PyObject *
PyTuple_GetSlice(PyObject *op, Py_ssize_t low, Py_ssize_t high)
{
    if (op == NULL || !PyTuple_Check(op)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    Py_ssize_t n = PyTuple_GET_SIZE(op);
    low = low < 0 ? 0 : low > n ? n : low;
    high = high < low ? low : high > n ? n : high;
    PyObject *r = PyTuple_New(high - low);
    for (Py_ssize_t i = low; r != NULL && i < high; i++) {
        PyTuple_SET_ITEM(r, i - low, Py_NewRef(PyTuple_GET_ITEM(op, i)));
    }
    return r;
}

PyObject *
_PyDict_GetItem_KnownHash(PyObject *op, PyObject *key, Py_hash_t hash)
{
    return PyDict_GetItemWithError(op, key);
}

LU_EXPORT void
_PyErr_SetKeyError(PyObject *arg)
{
    PyObject *tup = PyTuple_Pack(1, arg);
    if (tup != NULL) {
        PyErr_SetObject(PyExc_KeyError, tup);
        Py_DECREF(tup);
    }
}
