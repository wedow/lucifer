/* The str API for C callers (Objects/unicodeobject.c) that needs no layout
   knowledge beyond the public accessors: encoding, comparison, search and
   the str methods, by calling the kernel's. */

#include "lucifer.h"

#include <string.h>

#define CHECK_STR(op) do { \
    if (!PyUnicode_Check(op)) { \
        PyErr_BadArgument(); \
        return -1; \
    } } while (0)

/* A str method called with a format-built argument list. */
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

PyObject *
PyUnicode_AsEncodedString(PyObject *unicode, const char *encoding, const char *errors)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return NULL;
    }
    PyObject *b = call_method(unicode, "encode", "(ss)", encoding != NULL ? encoding : "utf-8",
                              errors != NULL ? errors : "strict");
    if (b != NULL && !PyBytes_Check(b)) {
        PyErr_Format(PyExc_TypeError, "'%.400s' encoder returned '%.400s' instead of 'bytes'; "
                     "use codecs.encode() to encode to arbitrary types", encoding, Py_TYPE(b)->tp_name);
        Py_CLEAR(b);
    }
    return b;
}

PyObject *
PyUnicode_AsUTF8String(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "utf-8", NULL);
}

PyObject *
PyUnicode_AsASCIIString(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "ascii", NULL);
}

PyObject *
PyUnicode_AsLatin1String(PyObject *unicode)
{
    return PyUnicode_AsEncodedString(unicode, "latin-1", NULL);
}

PyObject *
PyUnicode_FromEncodedObject(PyObject *obj, const char *encoding, const char *errors)
{
    if (obj == NULL) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (PyUnicode_Check(obj)) {
        PyErr_SetString(PyExc_TypeError, "decoding str is not supported");
        return NULL;
    }
    if (!PyBytes_Check(obj) && !PyByteArray_Check(obj) && !PyObject_CheckBuffer(obj)) {
        PyErr_Format(PyExc_TypeError, "decoding to str: need a bytes-like object, %.80s found",
                     Py_TYPE(obj)->tp_name);
        return NULL;
    }
    PyObject *b = PyBytes_Check(obj) ? Py_NewRef(obj) : PyObject_CallOneArg((PyObject *)&PyBytes_Type, obj);
    PyObject *r = b != NULL ? call_method(b, "decode", "(ss)", encoding != NULL ? encoding : "utf-8",
                                          errors != NULL ? errors : "strict")
                            : NULL;
    Py_XDECREF(b);
    return r;
}

/* The filesystem codec, which interp/config.c settles as UTF-8 with
   surrogateescape. */


PyObject *
PyUnicode_FromObject(PyObject *obj)
{
    if (PyUnicode_CheckExact(obj)) {
        return Py_NewRef(obj);
    }
    if (PyUnicode_Check(obj)) {
        return PyUnicode_FromKindAndData(PyUnicode_KIND(obj), PyUnicode_DATA(obj), PyUnicode_GET_LENGTH(obj));
    }
    PyErr_Format(PyExc_TypeError, "Can't convert '%.100s' object to str implicitly", Py_TYPE(obj)->tp_name);
    return NULL;
}

PyObject *
PyUnicode_FromOrdinal(int ordinal)
{
    if (ordinal < 0 || ordinal > 0x10ffff) {
        PyErr_SetString(PyExc_ValueError, "chr() arg not in range(0x110000)");
        return NULL;
    }
    Py_UCS4 ch = (Py_UCS4)ordinal;
    return PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, &ch, 1);
}

Py_ssize_t
PyUnicode_GetLength(PyObject *unicode)
{
    CHECK_STR(unicode);
    return PyUnicode_GET_LENGTH(unicode);
}

Py_UCS4 *
PyUnicode_AsUCS4(PyObject *string, Py_UCS4 *target, Py_ssize_t targetsize, int copy_null)
{
    if (target == NULL || targetsize < 0) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (!PyUnicode_Check(string)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    Py_ssize_t len = PyUnicode_GET_LENGTH(string);
    if (targetsize < len + (copy_null != 0)) {
        PyErr_Format(PyExc_SystemError, "string is longer than the buffer");
        if (copy_null && 0 < targetsize) {
            target[0] = 0;
        }
        return NULL;
    }
    int kind = PyUnicode_KIND(string);
    const void *data = PyUnicode_DATA(string);
    for (Py_ssize_t i = 0; i < len; i++) {
        target[i] = PyUnicode_READ(kind, data, i);
    }
    if (copy_null) {
        target[len] = 0;
    }
    return target;
}

Py_UCS4 *
PyUnicode_AsUCS4Copy(PyObject *string)
{
    if (!PyUnicode_Check(string)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    Py_ssize_t len = PyUnicode_GET_LENGTH(string);
    Py_UCS4 *copy = PyMem_Malloc((len + 1) * sizeof(Py_UCS4));
    if (copy == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    return PyUnicode_AsUCS4(string, copy, len + 1, 1);
}

static const char *
kind_name(PyObject *unicode)
{
    return PyUnicode_IS_ASCII(unicode) ? "ascii" : PyUnicode_KIND(unicode) == PyUnicode_1BYTE_KIND ? "latin1"
         : PyUnicode_KIND(unicode) == PyUnicode_2BYTE_KIND ? "UCS2" : "UCS4";
}

static int check_modifiable(PyObject *unicode);
static Py_UCS4 max_char_value(PyObject *unicode);

/* Objects/unicodeobject.c: into a string nobody else holds yet, each
   character no wider than the target holds. */
Py_ssize_t
PyUnicode_CopyCharacters(PyObject *to, Py_ssize_t to_start, PyObject *from, Py_ssize_t from_start,
                         Py_ssize_t how_many)
{
    if (!PyUnicode_Check(from) || !PyUnicode_Check(to)) {
        PyErr_BadInternalCall();
        return -1;
    }
    if ((size_t)from_start > (size_t)PyUnicode_GET_LENGTH(from)
        || (size_t)to_start > (size_t)PyUnicode_GET_LENGTH(to)) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return -1;
    }
    if (how_many < 0) {
        PyErr_SetString(PyExc_SystemError, "how_many cannot be negative");
        return -1;
    }
    how_many = Py_MIN(PyUnicode_GET_LENGTH(from) - from_start, how_many);
    if (to_start + how_many > PyUnicode_GET_LENGTH(to)) {
        PyErr_Format(PyExc_SystemError, "Cannot write %zi characters at %zi in a string of %zi characters",
                     how_many, to_start, PyUnicode_GET_LENGTH(to));
        return -1;
    }
    if (how_many == 0) {
        return 0;
    }
    if (check_modifiable(to) < 0) {
        return -1;
    }
    int fk = PyUnicode_KIND(from), tk = PyUnicode_KIND(to);
    const void *fd = PyUnicode_DATA(from);
    void *td = PyUnicode_DATA(to);
    Py_UCS4 to_max = max_char_value(to);
    for (Py_ssize_t i = 0; i < how_many; i++) {
        Py_UCS4 ch = PyUnicode_READ(fk, fd, from_start + i);
        if (ch > to_max) {
            PyErr_Format(PyExc_SystemError, "Cannot copy %s characters into a string of %s characters",
                         kind_name(from), kind_name(to));
            return -1;
        }
        PyUnicode_WRITE(tk, td, to_start + i, ch);
    }
    return how_many;
}

PyObject *
PyUnicode_Substring(PyObject *self, Py_ssize_t start, Py_ssize_t end)
{
    if (!PyUnicode_Check(self)) {
        PyErr_BadArgument();
        return NULL;
    }
    Py_ssize_t length = PyUnicode_GET_LENGTH(self);
    if (start < 0 || end < 0) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return NULL;
    }
    end = Py_MIN(end, length);
    if (start >= end) {
        return PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, "", 0);
    }
    int kind = PyUnicode_KIND(self);
    return PyUnicode_FromKindAndData(kind, (const char *)PyUnicode_DATA(self) + start * kind, end - start);
}

int
PyUnicode_Resize(PyObject **p_unicode, Py_ssize_t length)
{
    if (p_unicode == NULL) {
        PyErr_BadInternalCall();
        return -1;
    }
    PyObject *old = *p_unicode;
    if (old == NULL || !PyUnicode_Check(old) || length < 0) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (PyUnicode_GET_LENGTH(old) == length) {
        return 0;
    }
    if (length == 0) {
        Py_SETREF(*p_unicode, PyUnicode_New(0, 0));
        return 0;
    }
    /* A copy of the new length, even of a string others hold
       (unicode_resize's resize_copy); the characters past the old end are
       the caller's to fill. */
    PyObject *copy = PyUnicode_New(length, PyUnicode_MAX_CHAR_VALUE(old));
    if (copy == NULL) {
        return -1;
    }
    PyUnicode_CopyCharacters(copy, 0, old, 0, Py_MIN(length, PyUnicode_GET_LENGTH(old)));
    *p_unicode = copy;
    Py_DECREF(old);
    return 0;
}

void
PyUnicode_InternInPlace(PyObject **p)
{
    lu_intern(PyInterpreterState_Get(), p);
}

/* Comparison. */

int
PyUnicode_Compare(PyObject *left, PyObject *right)
{
    if (!PyUnicode_Check(left) || !PyUnicode_Check(right)) {
        PyErr_Format(PyExc_TypeError, "Can't compare %.100s and %.100s", Py_TYPE(left)->tp_name,
                     Py_TYPE(right)->tp_name);
        return -1;
    }
    int lt = PyObject_RichCompareBool(left, right, Py_LT);
    if (lt != 0) {
        return lt < 0 ? -1 : -1;
    }
    int eq = PyObject_RichCompareBool(left, right, Py_EQ);
    return eq < 0 ? -1 : eq ? 0 : 1;
}

int
PyUnicode_CompareWithASCIIString(PyObject *uni, const char *str)
{
    PyObject *other = PyUnicode_FromString(str);
    if (other == NULL) {
        return -1;
    }
    int r = PyUnicode_Compare(uni, other);
    Py_DECREF(other);
    return r;
}

int
PyUnicode_Equal(PyObject *left, PyObject *right)
{
    if (!PyUnicode_Check(left)) {
        PyErr_Format(PyExc_TypeError, "first argument must be str, not %s", Py_TYPE(left)->tp_name);
        return -1;
    }
    if (!PyUnicode_Check(right)) {
        PyErr_Format(PyExc_TypeError, "second argument must be str, not %s", Py_TYPE(right)->tp_name);
        return -1;
    }
    return lu_str_eq(left, right);
}

/* The str operators and methods. */

PyObject *
PyUnicode_Concat(PyObject *left, PyObject *right)
{
    if (!PyUnicode_Check(left)) {
        PyErr_Format(PyExc_TypeError, "must be str, not %.100s", Py_TYPE(left)->tp_name);
        return NULL;
    }
    if (!PyUnicode_Check(right)) {
        PyErr_Format(PyExc_TypeError, "can only concatenate str (not \"%.200s\") to str",
                     Py_TYPE(right)->tp_name);
        return NULL;
    }
    return PyUnicode_FromFormat("%U%U", left, right);     /* native: runs no Python, so works with an error set */
}

int
PyUnicode_Contains(PyObject *str, PyObject *substr)
{
    if (!PyUnicode_Check(substr)) {
        PyErr_Format(PyExc_TypeError, "'in <string>' requires string as left operand, not %.100s",
                     Py_TYPE(substr)->tp_name);
        return -1;
    }
    if (!PyUnicode_Check(str)) {
        PyErr_BadArgument();
        return -1;
    }
    return PySequence_Contains(str, substr);
}

PyObject *
PyUnicode_Format(PyObject *format, PyObject *args)
{
    if (format == NULL || !PyUnicode_Check(format)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    return PyNumber_Remainder(format, args);
}

PyObject *
PyUnicode_Join(PyObject *separator, PyObject *seq)
{
    PyObject *sep = separator != NULL ? Py_NewRef(separator) : PyUnicode_FromString(" ");
    PyObject *r = sep != NULL ? call_method(sep, "join", "(O)", seq) : NULL;
    Py_XDECREF(sep);
    return r;
}

PyObject *
PyUnicode_Replace(PyObject *str, PyObject *substr, PyObject *replstr, Py_ssize_t maxcount)
{
    PyObject *args[3] = { str, substr, replstr };
    for (int i = 0; i < 3; i++) {
        if (!PyUnicode_Check(args[i])) {        /* ensure_unicode */
            PyErr_Format(PyExc_TypeError, "must be str, not %.100s", Py_TYPE(args[i])->tp_name);
            return NULL;
        }
    }
    PyObject *replace = PyObject_GetAttrString((PyObject *)&PyUnicode_Type, "replace");
    PyObject *r = replace != NULL ? PyObject_CallFunction(replace, "OOOn", str, substr, replstr, maxcount) : NULL;
    Py_XDECREF(replace);
    return r;
}

Py_ssize_t
PyUnicode_Tailmatch(PyObject *str, PyObject *substr, Py_ssize_t start, Py_ssize_t end, int direction)
{
    PyObject *both[2] = { str, substr };
    for (int i = 0; i < 2; i++) {
        if (!PyUnicode_Check(both[i])) {        /* ensure_unicode */
            PyErr_Format(PyExc_TypeError, "must be str, not %.100s", Py_TYPE(both[i])->tp_name);
            return -1;
        }
    }
    PyObject *m = PyObject_GetAttrString((PyObject *)&PyUnicode_Type, direction > 0 ? "endswith" : "startswith");
    PyObject *r = m != NULL ? PyObject_CallFunction(m, "OOnn", str, substr, start, end) : NULL;
    Py_XDECREF(m);
    if (r == NULL) {
        return -1;
    }
    int ok = r == Py_True;
    Py_DECREF(r);
    return ok;
}

Py_ssize_t
PyUnicode_FindChar(PyObject *str, Py_UCS4 ch, Py_ssize_t start, Py_ssize_t end, int direction)
{
    /* ADJUST_INDICES: negative indexes count from the end. */
    Py_ssize_t length = PyUnicode_GET_LENGTH(str);
    if (end > length) {
        end = length;
    }
    else if (end < 0) {
        end = end + length < 0 ? 0 : end + length;
    }
    if (start < 0) {
        start = start + length < 0 ? 0 : start + length;
    }
    if (end - start < 0) {
        return -1;
    }
    int kind = PyUnicode_KIND(str);
    const void *data = PyUnicode_DATA(str);
    if (direction > 0) {
        for (Py_ssize_t i = start; i < end; i++) {
            if (PyUnicode_READ(kind, data, i) == ch) {
                return i;
            }
        }
    }
    else {
        for (Py_ssize_t i = end - 1; i >= start; i--) {
            if (PyUnicode_READ(kind, data, i) == ch) {
                return i;
            }
        }
    }
    return -1;
}

/* PyUnicode_EqualToUTF8 (Objects/unicodeobject.c): an exact comparison
   against a UTF-8 C string, true only for a str it equals; it never
   raises (a str with lone surrogates equals no UTF-8 text). */

int
PyUnicode_EqualToUTF8AndSize(PyObject *str, const char *other, Py_ssize_t size)
{
    if (!PyUnicode_Check(str)) {
        return 0;
    }
    PyObject *pending = PyErr_GetRaisedException();
    Py_ssize_t len;
    const char *utf8 = PyUnicode_AsUTF8AndSize(str, &len);
    if (utf8 == NULL) {
        PyErr_Clear();
    }
    PyErr_SetRaisedException(pending);
    return utf8 != NULL && len == size && memcmp(utf8, other, size) == 0;
}

int
PyUnicode_EqualToUTF8(PyObject *str, const char *other)
{
    return PyUnicode_EqualToUTF8AndSize(str, other, (Py_ssize_t)strlen(other));
}

/* _Py_UTF8_Edit_Cost (Python/suggestions.c): the Levenshtein distance over
   the UTF-8 bytes, case flips at half the cost of any other change, bounded
   by max_cost (-1 for "as large as needed"). */

#define MOVE_COST 2
#define CASE_COST 1
#define MAX_STRING_SIZE 40
#define LEAST_FIVE_BITS(n) ((n) & 31)

static int
substitution_cost(char a, char b)
{
    if (LEAST_FIVE_BITS(a) != LEAST_FIVE_BITS(b)) {
        return MOVE_COST;
    }
    if (a == b) {
        return 0;
    }
    if ('A' <= a && a <= 'Z') {
        a += 'a' - 'A';
    }
    if ('A' <= b && b <= 'Z') {
        b += 'a' - 'A';
    }
    return a == b ? CASE_COST : MOVE_COST;
}

static Py_ssize_t
levenshtein_distance(const char *a, size_t a_size, const char *b, size_t b_size,
                     size_t max_cost, size_t *buffer)
{
    if (a == b) {
        return 0;
    }
    while (a_size && b_size && a[0] == b[0]) {
        a++; a_size--;
        b++; b_size--;
    }
    while (a_size && b_size && a[a_size-1] == b[b_size-1]) {
        a_size--;
        b_size--;
    }
    if (a_size == 0 || b_size == 0) {
        return (a_size + b_size) * MOVE_COST;
    }
    if (a_size > MAX_STRING_SIZE || b_size > MAX_STRING_SIZE) {
        return max_cost + 1;
    }
    if (b_size < a_size) {
        const char *t = a; a = b; b = t;
        size_t t_size = a_size; a_size = b_size; b_size = t_size;
    }
    if ((b_size - a_size) * MOVE_COST > max_cost) {
        return max_cost + 1;
    }
    size_t tmp = MOVE_COST;
    for (size_t i = 0; i < a_size; i++) {
        buffer[i] = tmp;
        tmp += MOVE_COST;
    }
    size_t result = 0;
    for (size_t b_index = 0; b_index < b_size; b_index++) {
        char code = b[b_index];
        size_t distance = result = b_index * MOVE_COST;
        size_t minimum = SIZE_MAX;
        for (size_t index = 0; index < a_size; index++) {
            size_t substitute = distance + substitution_cost(code, a[index]);
            distance = buffer[index];
            size_t insert_delete = Py_MIN(result, distance) + MOVE_COST;
            result = Py_MIN(insert_delete, substitute);
            buffer[index] = result;
            if (result < minimum) {
                minimum = result;
            }
        }
        if (minimum > max_cost) {
            return max_cost + 1;
        }
    }
    return result;
}

LU_EXPORT Py_ssize_t
_Py_UTF8_Edit_Cost(PyObject *a, PyObject *b, Py_ssize_t max_cost)
{
    Py_ssize_t size_a, size_b;
    const char *utf8_a = PyUnicode_AsUTF8AndSize(a, &size_a);
    if (utf8_a == NULL) {
        return -1;
    }
    const char *utf8_b = PyUnicode_AsUTF8AndSize(b, &size_b);
    if (utf8_b == NULL) {
        return -1;
    }
    if (max_cost == -1) {
        max_cost = MOVE_COST * Py_MAX(size_a, size_b);
    }
    size_t *buffer = PyMem_New(size_t, MAX_STRING_SIZE);
    if (buffer == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    Py_ssize_t res = levenshtein_distance(utf8_a, size_a, utf8_b, size_b, max_cost, buffer);
    PyMem_Free(buffer);
    return res;
}

/* _PyUnicode_FromId (Objects/unicodeobject.c): each identifier holds an
   index handed out once, and its interned str sits at that index. */
PyObject *
_PyUnicode_FromId(_Py_Identifier *id)
{
    __typeof__(_PyRuntime.identifiers) *ids = &_PyRuntime.identifiers;
    PyMutex_Lock(&ids->mutex);
    if (id->index < 0) {
        id->index = ids->next_index++;
    }
    Py_ssize_t index = id->index;
    PyObject *obj = NULL;
    if (index >= ids->size) {
        Py_ssize_t size = Py_MAX(index * 2, 16);
        PyObject **array = PyMem_Realloc(ids->array, size * sizeof *array);
        if (array == NULL) {
            PyErr_NoMemory();
            goto done;
        }
        memset(array + ids->size, 0, (size - ids->size) * sizeof *array);
        ids->array = array;
        ids->size = size;
    }
    obj = ids->array[index];
    if (obj == NULL) {
        obj = PyUnicode_DecodeUTF8(id->string, strlen(id->string), NULL);
        if (obj != NULL) {
            lu_intern(PyInterpreterState_Get(), &obj);
            ids->array[index] = obj;        /* the table owns it */
        }
    }
done:
    PyMutex_Unlock(&ids->mutex);
    return obj;      /* borrowed */
}

/* _PyObject_GetAttrId / _PyObject_CallMethodId: the Attr calls of the
   identifier API (Objects/object.c). */

PyObject *
_PyObject_GetAttrId(PyObject *o, _Py_Identifier *id)
{
    PyObject *n = _PyUnicode_FromId(id);
    if (n == NULL) {
        return NULL;
    }
    return PyObject_GetAttr(o, n);
}

int
_PyObject_HasAttrId(PyObject *o, _Py_Identifier *id)
{
    PyObject *n = _PyUnicode_FromId(id);
    if (n == NULL) {
        return -1;
    }
    return PyObject_HasAttr(o, n);
}

PyObject *
_PyObject_CallMethodId(PyObject *o, _Py_Identifier *id, const char *format, ...)
{
    PyObject *n = _PyUnicode_FromId(id);
    if (n == NULL) {
        return NULL;
    }
    PyObject *method = PyObject_GetAttr(o, n);
    if (method == NULL) {
        return NULL;
    }
    va_list ap;
    va_start(ap, format);
    PyObject *args = format != NULL && *format ? Py_VaBuildValue(format, ap) : PyTuple_New(0);
    va_end(ap);
    PyObject *result = NULL;
    if (args != NULL && PyTuple_Check(args)) {
        result = PyObject_Call(method, args, NULL);
        Py_DECREF(args);
    }
    else if (args != NULL) {
        PyObject *stack[1] = { args };
        result = lu_call(method, stack, 1, NULL);
        Py_DECREF(args);
    }
    Py_DECREF(method);
    return result;
}

/* PyUnicode_Splitlines: str.splitlines(keepends) (Objects/unicodeobject.c). */
PyObject *
PyUnicode_Splitlines(PyObject *string, int keepends)
{
    if (!PyUnicode_Check(string)) {
        PyErr_BadArgument();
        return NULL;
    }
    return PyObject_CallMethod(string, "splitlines", "i", keepends);
}

/* The str methods behind the C API's searches and splits, called on str
   itself so a subclass's override is not seen (Objects/unicodeobject.c). */

static int
ensure_unicode(PyObject *obj)
{
    if (!PyUnicode_Check(obj)) {
        PyErr_Format(PyExc_TypeError, "must be str, not %.100s", Py_TYPE(obj)->tp_name);
        return -1;
    }
    return 0;
}

static PyObject *
str_method(const char *name, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *f = PyObject_GetAttrString((PyObject *)&PyUnicode_Type, name);
    PyObject *r = f != NULL ? PyObject_Vectorcall(f, args, nargs, NULL) : NULL;
    Py_XDECREF(f);
    return r;
}

Py_ssize_t
PyUnicode_Count(PyObject *str, PyObject *substr, Py_ssize_t start, Py_ssize_t end)
{
    if (ensure_unicode(str) < 0 || ensure_unicode(substr) < 0) {
        return -1;
    }
    PyObject *a = PyLong_FromSsize_t(start), *b = PyLong_FromSsize_t(end);
    PyObject *args[4] = { str, substr, a, b };
    PyObject *r = a != NULL && b != NULL ? str_method("count", args, 4) : NULL;
    Py_XDECREF(a);
    Py_XDECREF(b);
    Py_ssize_t n = r != NULL ? PyLong_AsSsize_t(r) : -1;
    Py_XDECREF(r);
    return n;
}

Py_ssize_t
PyUnicode_Find(PyObject *str, PyObject *substr, Py_ssize_t start, Py_ssize_t end, int direction)
{
    if (ensure_unicode(str) < 0 || ensure_unicode(substr) < 0) {
        return -2;
    }
    PyObject *a = PyLong_FromSsize_t(start), *b = PyLong_FromSsize_t(end);
    PyObject *args[4] = { str, substr, a, b };
    PyObject *r = a != NULL && b != NULL ? str_method(direction > 0 ? "find" : "rfind", args, 4) : NULL;
    Py_XDECREF(a);
    Py_XDECREF(b);
    Py_ssize_t n = r != NULL ? PyLong_AsSsize_t(r) : -2;
    Py_XDECREF(r);
    return n;
}

static PyObject *
split_by(const char *name, PyObject *s, PyObject *sep, Py_ssize_t maxsplit)
{
    if (ensure_unicode(s) < 0 || (sep != NULL && ensure_unicode(sep) < 0)) {
        return NULL;
    }
    PyObject *n = PyLong_FromSsize_t(maxsplit < 0 ? -1 : maxsplit);
    PyObject *args[3] = { s, sep != NULL ? sep : Py_None, n };
    PyObject *r = n != NULL ? str_method(name, args, 3) : NULL;
    Py_XDECREF(n);
    return r;
}

PyObject *
PyUnicode_Split(PyObject *s, PyObject *sep, Py_ssize_t maxsplit)
{
    return split_by("split", s, sep, maxsplit);
}

PyObject *
PyUnicode_RSplit(PyObject *s, PyObject *sep, Py_ssize_t maxsplit)
{
    return split_by("rsplit", s, sep, maxsplit);
}

PyObject *
PyUnicode_Partition(PyObject *str, PyObject *sep)
{
    if (ensure_unicode(str) < 0 || ensure_unicode(sep) < 0) {
        return NULL;
    }
    PyObject *args[2] = { str, sep };
    return str_method("partition", args, 2);
}

PyObject *
PyUnicode_RPartition(PyObject *str, PyObject *sep)
{
    if (ensure_unicode(str) < 0 || ensure_unicode(sep) < 0) {
        return NULL;
    }
    PyObject *args[2] = { str, sep };
    return str_method("rpartition", args, 2);
}

PyObject *
PyUnicode_RichCompare(PyObject *left, PyObject *right, int op)
{
    if (!PyUnicode_Check(left) || !PyUnicode_Check(right)) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    if (op < Py_LT || op > Py_GE) {
        PyErr_BadArgument();
        return NULL;
    }
    int c = left == right ? 0 : PyUnicode_Compare(left, right);
    if (c == -1 && PyErr_Occurred()) {
        return NULL;
    }
    Py_RETURN_RICHCOMPARE(c, 0, op);
}

/* PyUnicode_Fill and PyUnicode_WriteChar change a string in place: only
   one nobody else holds, hashed or interned yet (unicode_modifiable). */
static int
check_modifiable(PyObject *unicode)
{
    if (Py_REFCNT(unicode) != 1 || ((PyASCIIObject *)unicode)->hash != -1
        || PyUnicode_CHECK_INTERNED(unicode) || !PyUnicode_CheckExact(unicode)) {
        PyErr_SetString(PyExc_SystemError, "Cannot modify a string currently used");
        return -1;
    }
    return 0;
}

static Py_UCS4
max_char_value(PyObject *unicode)
{
    return PyUnicode_IS_ASCII(unicode) ? 0x7f : PyUnicode_KIND(unicode) == PyUnicode_1BYTE_KIND ? 0xff
         : PyUnicode_KIND(unicode) == PyUnicode_2BYTE_KIND ? 0xffff : 0x10ffff;
}

Py_ssize_t
PyUnicode_Fill(PyObject *unicode, Py_ssize_t start, Py_ssize_t length, Py_UCS4 fill_char)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadInternalCall();
        return -1;
    }
    if (check_modifiable(unicode) < 0) {
        return -1;
    }
    if (start < 0) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return -1;
    }
    if (fill_char > max_char_value(unicode)) {
        PyErr_SetString(PyExc_ValueError, "fill character is bigger than the string maximum character");
        return -1;
    }
    Py_ssize_t maxlen = PyUnicode_GET_LENGTH(unicode) - start;
    length = Py_MIN(maxlen, length);
    if (length <= 0) {
        return 0;
    }
    int kind = PyUnicode_KIND(unicode);
    void *data = PyUnicode_DATA(unicode);
    for (Py_ssize_t i = start; i < start + length; i++) {
        PyUnicode_WRITE(kind, data, i, fill_char);
    }
    return length;
}

int
PyUnicode_WriteChar(PyObject *unicode, Py_ssize_t index, Py_UCS4 ch)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadArgument();
        return -1;
    }
    if (index < 0 || index >= PyUnicode_GET_LENGTH(unicode)) {
        PyErr_SetString(PyExc_IndexError, "string index out of range");
        return -1;
    }
    if (check_modifiable(unicode) < 0) {
        return -1;
    }
    if (ch > max_char_value(unicode)) {
        PyErr_SetString(PyExc_ValueError, "character out of range");
        return -1;
    }
    PyUnicode_WRITE(PyUnicode_KIND(unicode), PyUnicode_DATA(unicode), index, ch);
    return 0;
}

/* _PyUnicode_TranslateCharmap: each character through mapping[ord(ch)]; a
   LookupError keeps it, None is an error the handler named by `errors` gets
   (as a UnicodeTranslateError over the run of such characters), an int or a
   str is the replacement. */
static int
translate_lookup(PyObject *mapping, Py_UCS4 ch, PyObject **out)
{
    PyObject *key = PyLong_FromLong((long)ch);
    PyObject *x = key != NULL ? PyObject_GetItem(mapping, key) : NULL;
    Py_XDECREF(key);
    if (x == NULL) {
        if (PyErr_ExceptionMatches(PyExc_LookupError)) {
            PyErr_Clear();
            *out = NULL;            /* not mapped: kept */
            return 0;
        }
        return -1;
    }
    if (x == Py_None || PyUnicode_Check(x)) {
        *out = x;
        return 0;
    }
    if (PyLong_Check(x)) {
        long value = PyLong_AsLong(x);
        Py_DECREF(x);
        if (value == -1 && PyErr_Occurred()) {
            return -1;
        }
        if (value < 0 || value > 0x10ffff) {
            PyErr_SetString(PyExc_ValueError, "character mapping must be in range(0x110000)");
            return -1;
        }
        *out = PyUnicode_FromOrdinal((int)value);
        return *out != NULL ? 0 : -1;
    }
    Py_DECREF(x);
    PyErr_SetString(PyExc_TypeError, "character mapping must return integer, None or str");
    return -1;
}

PyObject *
PyUnicode_Translate(PyObject *str, PyObject *mapping, const char *errors)
{
    if (ensure_unicode(str) < 0) {
        return NULL;
    }
    if (mapping == NULL) {
        PyErr_BadArgument();
        return NULL;
    }
    Py_ssize_t size = PyUnicode_GET_LENGTH(str);
    PyUnicodeWriter *w = PyUnicodeWriter_Create(size);
    PyObject *handler = NULL;
    if (w == NULL) {
        return NULL;
    }
    for (Py_ssize_t i = 0; i < size;) {
        Py_UCS4 ch = PyUnicode_READ_CHAR(str, i);
        PyObject *x;
        if (translate_lookup(mapping, ch, &x) < 0) {
            goto fail;
        }
        if (x == NULL) {
            if (PyUnicodeWriter_WriteChar(w, ch) < 0) {
                goto fail;
            }
            i++;
            continue;
        }
        if (x != Py_None) {
            int r = PyUnicodeWriter_WriteStr(w, x);
            Py_DECREF(x);
            if (r < 0) {
                goto fail;
            }
            i++;
            continue;
        }
        Py_DECREF(x);
        /* The run of characters that map to None. */
        Py_ssize_t end = i + 1;
        for (; end < size; end++) {
            if (translate_lookup(mapping, PyUnicode_READ_CHAR(str, end), &x) < 0) {
                goto fail;
            }
            int undefined = x == Py_None;
            Py_XDECREF(x);
            if (!undefined) {
                break;
            }
        }
        if (handler == NULL && (handler = PyCodec_LookupError(errors)) == NULL) {
            goto fail;
        }
        PyObject *exc = PyObject_CallFunction(PyExc_UnicodeTranslateError, "Onns", str, i, end,
                                              "character maps to <undefined>");
        PyObject *res = exc != NULL ? PyObject_CallOneArg(handler, exc) : NULL;
        Py_XDECREF(exc);
        PyObject *repl;
        Py_ssize_t pos;
        if (res == NULL || !PyTuple_Check(res)
            || !PyArg_ParseTuple(res, "Un;translating error handler must return (str, int) tuple",
                                 &repl, &pos)) {
            if (res != NULL && !PyErr_Occurred()) {
                PyErr_SetString(PyExc_TypeError, "translating error handler must return (str, int) tuple");
            }
            Py_XDECREF(res);
            goto fail;
        }
        int r = PyUnicodeWriter_WriteStr(w, repl);
        Py_DECREF(res);
        if (r < 0) {
            goto fail;
        }
        if (pos < 0) {
            pos += size;
        }
        if (pos < 0 || pos > size) {
            PyErr_Format(PyExc_IndexError, "position %zd from error handler out of bounds", pos);
            goto fail;
        }
        i = pos;
    }
    Py_XDECREF(handler);
    return PyUnicodeWriter_Finish(w);
fail:
    Py_XDECREF(handler);
    PyUnicodeWriter_Discard(w);
    return NULL;
}

/* _PyUnicode_TransformDecimalAndSpaceToASCII (Objects/unicodeobject.c):
   whitespace to ' ', decimal digits to ASCII; at the first other non-ASCII
   character a '?' ends the result. */
LU_EXPORT PyObject *
_PyUnicode_TransformDecimalAndSpaceToASCII(PyObject *unicode)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (PyUnicode_IS_ASCII(unicode)) {
        return Py_NewRef(unicode);
    }
    Py_ssize_t len = PyUnicode_GET_LENGTH(unicode), n = 0;
    char *out = PyMem_Malloc(len);
    if (out == NULL) {
        return PyErr_NoMemory();
    }
    int kind = PyUnicode_KIND(unicode);
    const void *data = PyUnicode_DATA(unicode);
    for (; n < len; n++) {
        Py_UCS4 ch = PyUnicode_READ(kind, data, n);
        if (ch < 127) {
            out[n] = (char)ch;
        }
        else if (Py_UNICODE_ISSPACE(ch)) {
            out[n] = ' ';
        }
        else {
            int decimal = Py_UNICODE_TODECIMAL(ch);
            if (decimal < 0) {
                out[n++] = '?';
                break;
            }
            out[n] = (char)('0' + decimal);
        }
    }
    PyObject *result = PyUnicode_DecodeASCII(out, n, NULL);
    PyMem_Free(out);
    return result;
}
