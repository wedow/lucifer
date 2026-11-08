/* str primitives (DESIGN 4.4, storage): the loops under the kernel's str.
   Search, slicing, splitting, stripping, case mapping over the Unicode
   database, the character predicates, the utf-8, latin-1 and ascii codecs
   with the error handlers of Python/codecs.c, and repr/ascii escaping. The
   methods, argument parsing and messages are lib/kernel/str.py. */

#include "prim/prim.h"

#include <string.h>

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)
#define STR(i) do { if (!PyUnicode_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() argument %d must be a str, not '%.200s'", \
                      __func__ + 5, (i) + 1, Py_TYPE(args[i])->tp_name); \
        return NULL; } } while (0)
/* An int argument, saturated to the Py_ssize_t range. */
#define IDX(i, var) Py_ssize_t var; do { if (index_arg(args[i], &var) < 0) return NULL; } while (0)

static int
index_arg(PyObject *o, Py_ssize_t *out)
{
    long v;
    if (!PyLong_Check(o)) {
        lu_err_format(PyExc_TypeError, "an int is required, not '%.200s'", Py_TYPE(o)->tp_name);
        return -1;
    }
    if (lu_long_as_long(o, &v) < 0) {
        PyErr_Clear();
        v = (((PyLongObject *)o)->long_value.lv_tag & 3) == 2 ? PY_SSIZE_T_MIN : PY_SSIZE_T_MAX;
    }
    *out = (Py_ssize_t)v;
    return 0;
}

static PyObject *
bool_of(int v)
{
    return Py_NewRef(v ? Py_True : Py_False);
}

#define LEN(s) PyUnicode_GET_LENGTH(s)
#define KIND(s) PyUnicode_KIND(s)
#define DATA(s) PyUnicode_DATA(s)
#define CH(s, i) PyUnicode_READ(KIND(s), DATA(s), (i))

/* A growing buffer of code points, the result of the transformations. */
typedef struct {
    Py_UCS4 *p;
    Py_ssize_t n, cap;
} ubuf;

static int
ub_put(ubuf *b, Py_UCS4 c)
{
    if (b->n == b->cap) {
        Py_ssize_t cap = b->cap ? b->cap * 2 : 32;
        Py_UCS4 *p = PyMem_Realloc(b->p, cap * sizeof(Py_UCS4));
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        b->p = p;
        b->cap = cap;
    }
    b->p[b->n++] = c;
    return 0;
}

static int
ub_range(ubuf *b, PyObject *s, Py_ssize_t from, Py_ssize_t to)
{
    for (Py_ssize_t i = from; i < to; i++) {
        if (ub_put(b, CH(s, i)) < 0) {
            return -1;
        }
    }
    return 0;
}

static int
ub_ascii(ubuf *b, const char *s)
{
    for (; *s; s++) {
        if (ub_put(b, (unsigned char)*s) < 0) {
            return -1;
        }
    }
    return 0;
}

static PyObject *
ub_finish(ubuf *b)
{
    PyObject *r = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, b->p, b->n);
    PyMem_Free(b->p);
    b->p = NULL;
    return r;
}

/* s[start:end] as an exact str. */
static PyObject *
substring(PyObject *s, Py_ssize_t start, Py_ssize_t end)
{
    if (start == 0 && end == LEN(s) && PyUnicode_CheckExact(s)) {
        return Py_NewRef(s);
    }
    int kind = KIND(s);
    return PyUnicode_FromKindAndData(kind, (const char *)DATA(s) + start * kind, end - start);
}

/* ADJUST_INDICES of Objects/unicodeobject.c. */
static void
adjust(Py_ssize_t len, Py_ssize_t *start, Py_ssize_t *end)
{
    if (*end > len) {
        *end = len;
    }
    else if (*end < 0) {
        *end += len;
        if (*end < 0) *end = 0;
    }
    if (*start < 0) {
        *start += len;
        if (*start < 0) *start = 0;
    }
}

/* Whether sub occurs at s[i:]. */
static int
match_at(PyObject *s, PyObject *sub, Py_ssize_t i)
{
    Py_ssize_t n = LEN(sub);
    if (KIND(s) == KIND(sub)) {
        return memcmp((const char *)DATA(s) + i * KIND(s), DATA(sub), n * KIND(s)) == 0;
    }
    for (Py_ssize_t j = 0; j < n; j++) {
        if (CH(s, i + j) != CH(sub, j)) return 0;
    }
    return 1;
}

/* The first index of sub in s[start:end], or the last with `last`; -1 if none. */
static Py_ssize_t
find_in(PyObject *s, PyObject *sub, Py_ssize_t start, Py_ssize_t end, int last)
{
    Py_ssize_t n = LEN(sub);
    adjust(LEN(s), &start, &end);
    if (end - start < n || KIND(sub) > KIND(s)) {
        return -1;
    }
    if (!last && KIND(s) == 1 && KIND(sub) == 1) {
        const char *at = memmem((const char *)DATA(s) + start, end - start, DATA(sub), n);
        return at == NULL ? -1 : at - (const char *)DATA(s);
    }
    if (n == 0) {
        return last ? end : start;
    }
    Py_UCS4 first = CH(sub, 0);
    if (last) {
        for (Py_ssize_t i = end - n; i >= start; i--) {
            if (CH(s, i) == first && match_at(s, sub, i)) return i;
        }
        return -1;
    }
    for (Py_ssize_t i = start; i <= end - n; i++) {
        if (CH(s, i) == first && match_at(s, sub, i)) return i;
    }
    return -1;
}

/* str_find(s, sub, start, end, last) -> index or -1. */
static PyObject *
prim_str_find(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(5); STR(0); STR(1); IDX(2, start); IDX(3, end);
    return PyLong_FromLongLong(find_in(args[0], args[1], start, end, args[4] == Py_True));
}

/* str_count(s, sub, start, end): non-overlapping occurrences. */
static PyObject *
prim_str_count(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); STR(0); STR(1); IDX(2, start); IDX(3, end);
    PyObject *s = args[0], *sub = args[1];
    adjust(LEN(s), &start, &end);
    Py_ssize_t n = LEN(sub), count = 0;
    if (end - start < n) {
        return PyLong_FromLong(0);
    }
    if (n == 0) {
        return PyLong_FromLongLong(end - start + 1);
    }
    for (;;) {
        Py_ssize_t at = find_in(s, sub, start, end, 0);
        if (at < 0) break;
        count++;
        start = at + n;
    }
    return PyLong_FromLongLong(count);
}

/* str_replace(s, old, new, count): count < 0 replaces all. */
static PyObject *
prim_str_replace(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); STR(0); STR(1); STR(2); IDX(3, count);
    PyObject *s = args[0], *old = args[1], *new = args[2];
    Py_ssize_t len = LEN(s), n = LEN(old);
    if (count == 0) {
        return substring(s, 0, len);
    }
    ubuf b = { 0 };
    Py_ssize_t i = 0, done = 0;
    int err = 0;
    if (n == 0) {
        for (; i <= len && (count < 0 || done < count); done++) {
            err = ub_range(&b, new, 0, LEN(new));
            if (err == 0 && i < len) err = ub_put(&b, CH(s, i));
            if (err < 0) break;
            i++;
        }
        if (err == 0 && i <= len) err = ub_range(&b, s, i < len ? i : len, len);
    }
    else {
        for (;;) {
            Py_ssize_t at = (count < 0 || done < count) ? find_in(s, old, i, len, 0) : -1;
            if (at < 0) break;
            if (ub_range(&b, s, i, at) < 0 || ub_range(&b, new, 0, LEN(new)) < 0) { err = -1; break; }
            i = at + n;
            done++;
        }
        if (err == 0 && done == 0) {
            PyMem_Free(b.p);
            return substring(s, 0, len);
        }
        if (err == 0) err = ub_range(&b, s, i, len);
    }
    if (err < 0) {
        PyMem_Free(b.p);
        return NULL;
    }
    return ub_finish(&b);
}

/* str_slice(s, start, stop): s[start:stop] with both already within 0..len. */
static PyObject *
prim_str_slice(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); STR(0); IDX(1, start); IDX(2, stop);
    if (start < 0 || stop > LEN(args[0]) || start > stop) {
        return substring(args[0], 0, 0);
    }
    return substring(args[0], start, stop);
}

/* str_slice_step(s, start, step, count): count characters from start. */
static PyObject *
prim_str_slice_step(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); STR(0); IDX(1, start); IDX(2, step); IDX(3, count);
    ubuf b = { 0 };
    for (Py_ssize_t i = 0; i < count; i++) {
        if (ub_put(&b, CH(args[0], start + i * step)) < 0) {
            PyMem_Free(b.p);
            return NULL;
        }
    }
    return ub_finish(&b);
}

/* str_item(s, i): the one-character str at 0 <= i < len. */
static PyObject *
prim_str_item(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); IDX(1, i);
    Py_UCS4 c = CH(args[0], i);
    return PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, &c, 1);
}

static PyObject *
prim_str_ord(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); IDX(1, i);
    return PyLong_FromLong(CH(args[0], i));
}

static PyObject *
prim_str_chr(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); IDX(0, c);
    Py_UCS4 u = (Py_UCS4)c;
    return PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, &u, 1);
}

static PyObject *
prim_str_len(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); STR(0);
    return PyLong_FromLongLong(LEN(args[0]));
}

/* str_decimal(s, i): the decimal digit value of s[i], or -1. */
static PyObject *
prim_str_decimal(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); IDX(1, i);
    return PyLong_FromLong(_PyUnicode_ToDecimalDigit(CH(args[0], i)));
}

/* box_get(box, i) and box_set(box, i, value): a list used as a record of mutable fields
   until the list type has its own item access. */
static PyObject *
prim_box_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); IDX(1, i);
    if (!PyList_CheckExact(args[0]) || i < 0 || i >= Py_SIZE(args[0])) {
        lu_err_format(PyExc_SystemError, "box_get() needs a list and an index within it");
        return NULL;
    }
    return Py_NewRef(PyList_GET_ITEM(args[0], i));
}

static PyObject *
prim_box_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); IDX(1, i);
    if (!PyList_CheckExact(args[0]) || i < 0 || i >= Py_SIZE(args[0])) {
        lu_err_format(PyExc_SystemError, "box_set() needs a list and an index within it");
        return NULL;
    }
    Py_XSETREF(PyList_GET_ITEM(args[0], i), Py_NewRef(args[2]));
    return Py_NewRef(Py_None);
}

/* slice_parts(slice): (start, stop, step) as the slice holds them. */
static PyObject *
prim_slice_parts(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PySlice_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "slice_parts() needs a slice");
        return NULL;
    }
    PySliceObject *s = (PySliceObject *)args[0];
    return PyTuple_Pack(3, s->start, s->stop, s->step);
}

/* bytes_ord(b, i): the byte at 0 <= i < len as an int. */
static PyObject *
prim_bytes_ord(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); IDX(1, i);
    if (!PyBytes_Check(args[0]) || i < 0 || i >= PyBytes_GET_SIZE(args[0])) {
        lu_err_format(PyExc_TypeError, "bytes_ord() needs bytes and an index within them");
        return NULL;
    }
    return PyLong_FromLong((unsigned char)PyBytes_AS_STRING(args[0])[i]);
}

/* str_sizeof(s): unicode_sizeof_impl. */
static PyObject *
prim_str_sizeof(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); STR(0);
    PyObject *s = args[0];
    Py_ssize_t size;
    if (PyUnicode_IS_COMPACT_ASCII(s)) {
        size = sizeof(PyASCIIObject) + LEN(s) + 1;
    }
    else if (PyUnicode_IS_COMPACT(s)) {
        size = sizeof(PyCompactUnicodeObject) + (LEN(s) + 1) * KIND(s);
    }
    else {
        size = sizeof(PyUnicodeObject);
        if (((PyUnicodeObject *)s)->data.any != NULL) size += (LEN(s) + 1) * KIND(s);
    }
    if (!PyUnicode_IS_ASCII(s) && ((PyCompactUnicodeObject *)s)->utf8 != NULL) {
        size += ((PyCompactUnicodeObject *)s)->utf8_length + 1;
    }
    return PyLong_FromLongLong(size);
}

static PyObject *
prim_str_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); STR(0);
    return PyLong_FromLongLong(lu_str_hash(args[0]));
}

/* str_cmp(a, b): -1, 0 or 1 by code point. */
static PyObject *
prim_str_cmp(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); STR(1);
    PyObject *a = args[0], *b = args[1];
    Py_ssize_t la = LEN(a), lb = LEN(b), n = la < lb ? la : lb;
    int c = 0;
    if (KIND(a) == 1 && KIND(b) == 1) {
        c = memcmp(DATA(a), DATA(b), n);
    }
    else {
        for (Py_ssize_t i = 0; i < n && c == 0; i++) {
            Py_UCS4 x = CH(a, i), y = CH(b, i);
            c = x < y ? -1 : x > y;
        }
    }
    if (c == 0) {
        c = la < lb ? -1 : la > lb;
    }
    return PyLong_FromLong(c < 0 ? -1 : c > 0);
}

/* a + b for two strs. */
PyObject *
lu_str_concat(PyObject *a, PyObject *b)
{
    if (LEN(a) == 0 && PyUnicode_CheckExact(b)) return Py_NewRef(b);
    if (LEN(b) == 0 && PyUnicode_CheckExact(a)) return Py_NewRef(a);
    PyObject *parts = PyTuple_Pack(2, a, b);
    PyObject *r = NULL;
    if (parts != NULL) {
        r = lu_str_join_tuple((PyObject *)&_PyRuntime.singletons.empty_str, parts);
        Py_DECREF(parts);
    }
    return r;
}

static PyObject *
prim_str_concat(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); STR(1);
    return lu_str_concat(args[0], args[1]);
}

/* str_repeat(s, n): n >= 0. */
static PyObject *
prim_str_repeat(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); IDX(1, n);
    PyObject *s = args[0];
    Py_ssize_t len = LEN(s);
    if (n <= 0 || len == 0) {
        return substring(s, 0, 0);
    }
    if (n == 1) {
        return substring(s, 0, len);
    }
    if (len > PY_SSIZE_T_MAX / n) {
        lu_err_format(PyExc_OverflowError, "repeated string is too long");
        return NULL;
    }
    int kind = KIND(s);
    PyObject *r = PyUnicode_New(len * n, PyUnicode_MAX_CHAR_VALUE(s));
    if (r == NULL) {
        return NULL;
    }
    char *out = PyUnicode_DATA(r);
    for (Py_ssize_t i = 0; i < n; i++) {
        memcpy(out + i * len * kind, DATA(s), len * kind);
    }
    PyUnicode_WRITE(KIND(r), out, len * n, 0);
    return r;
}

/* str_contains_char etc. share the database; Py_UNICODE_ISSPACE. */
static int
is_space(Py_UCS4 c)
{
    return c < 128 ? (c == ' ' || (c >= 9 && c <= 13) || (c >= 28 && c <= 31)) : _PyUnicode_IsWhitespace(c);
}

/* Appends the pieces of a split to list: s[start:end] each, or none. */
static int
push_piece(PyObject *list, PyObject *s, Py_ssize_t start, Py_ssize_t end)
{
    PyObject *p = substring(s, start, end);
    if (p == NULL) return -1;
    int err = lu_list_append(list, p);
    Py_DECREF(p);
    return err;
}

/* str_split(s, sep or None, maxsplit, from_right) -> list of strs. */
static PyObject *
prim_str_split(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); STR(0); IDX(2, maxsplit);
    PyObject *s = args[0], *sep = args[1];
    int right = args[3] == Py_True;
    Py_ssize_t len = LEN(s);
    if (maxsplit < 0) maxsplit = PY_SSIZE_T_MAX;
    /* Boundaries of the pieces, left to right, as start/end pairs. */
    Py_ssize_t cap = 16, np = 0;
    Py_ssize_t *b = PyMem_Malloc(cap * 2 * sizeof(Py_ssize_t));
    if (b == NULL) return PyErr_NoMemory();
#define PAIR(a, z) do { \
        if (np == cap) { cap *= 2; Py_ssize_t *g = PyMem_Realloc(b, cap * 2 * sizeof(Py_ssize_t)); \
            if (g == NULL) { PyMem_Free(b); return PyErr_NoMemory(); } b = g; } \
        b[2 * np] = (a); b[2 * np + 1] = (z); np++; } while (0)
    if (sep == Py_None) {
        if (!right) {
            Py_ssize_t i = 0;
            for (;;) {
                while (i < len && is_space(CH(s, i))) i++;
                if (i == len) break;
                Py_ssize_t j = i;
                if (maxsplit-- == 0) { PAIR(i, len); break; }
                while (j < len && !is_space(CH(s, j))) j++;
                PAIR(i, j);
                i = j;
            }
        }
        else {
            Py_ssize_t i = len;
            for (;;) {
                while (i > 0 && is_space(CH(s, i - 1))) i--;
                if (i == 0) break;
                Py_ssize_t j = i;
                if (maxsplit-- == 0) { PAIR(0, i); break; }
                while (j > 0 && !is_space(CH(s, j - 1))) j--;
                PAIR(j, i);
                i = j;
            }
        }
    }
    else {
        Py_ssize_t n = LEN(sep);
        if (!PyUnicode_Check(sep) || n == 0) {
            PyMem_Free(b);
            lu_err_format(PyExc_ValueError, "empty separator");
            return NULL;
        }
        if (!right) {
            Py_ssize_t i = 0;
            while (maxsplit > 0) {
                Py_ssize_t at = find_in(s, sep, i, len, 0);
                if (at < 0) break;
                PAIR(i, at);
                i = at + n;
                maxsplit--;
            }
            PAIR(i, len);
        }
        else {
            Py_ssize_t i = len;
            while (maxsplit > 0) {
                Py_ssize_t at = find_in(s, sep, 0, i, 1);
                if (at < 0) break;
                PAIR(at + n, i);
                i = at;
                maxsplit--;
            }
            PAIR(0, i);
        }
    }
#undef PAIR
    PyObject *list = PyList_New(0);
    for (Py_ssize_t k = 0; list != NULL && k < np; k++) {
        Py_ssize_t at = right ? np - 1 - k : k;
        if (push_piece(list, s, b[2 * at], b[2 * at + 1]) < 0) {
            Py_CLEAR(list);
        }
    }
    PyMem_Free(b);
    return list;
}

/* str_splitlines(s, keepends) -> list of strs. */
static PyObject *
prim_str_splitlines(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0);
    PyObject *s = args[0];
    int keep = args[1] == Py_True;
    Py_ssize_t len = LEN(s), i = 0;
    PyObject *list = PyList_New(0);
    while (list != NULL && i < len) {
        Py_ssize_t j = i, eol;
        while (j < len && !_PyUnicode_IsLinebreak(CH(s, j))) j++;
        eol = j;
        if (j < len) {
            j += (CH(s, j) == '\r' && j + 1 < len && CH(s, j + 1) == '\n') ? 2 : 1;
            if (keep) eol = j;
        }
        if (push_piece(list, s, i, eol) < 0) {
            Py_CLEAR(list);
        }
        i = j;
    }
    return list;
}

static int
find_char(PyObject *set, Py_UCS4 c)
{
    for (Py_ssize_t i = 0; i < LEN(set); i++) {
        if (CH(set, i) == c) return 1;
    }
    return 0;
}

/* str_strip(s, chars or None, mode): mode 1 strips the left, 2 the right, 3 both. */
static PyObject *
prim_str_strip(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); STR(0); IDX(2, mode);
    PyObject *s = args[0], *chars = args[1];
    Py_ssize_t i = 0, j = LEN(s);
    if (chars != Py_None) {
        STR(1);
    }
#define STRIPPABLE(c) (chars == Py_None ? is_space(c) : find_char(chars, (c)))
    if (mode & 1) {
        while (i < j && STRIPPABLE(CH(s, i))) i++;
    }
    if (mode & 2) {
        while (j > i && STRIPPABLE(CH(s, j - 1))) j--;
    }
#undef STRIPPABLE
    return substring(s, i, j);
}

/* str_join(sep, items): items is a tuple of exact strs. */
PyObject *
lu_prim_str_join(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2 || !PyUnicode_Check(args[0]) || !PyTuple_CheckExact(args[1])) {
        lu_err_format(PyExc_TypeError, "str_join(sep, items) takes a str and a tuple");
        return NULL;
    }
    return lu_str_join_tuple(args[0], args[1]);
}

PyObject *
lu_str_join_tuple(PyObject *sep, PyObject *items)
{
    Py_ssize_t n = PyTuple_GET_SIZE(items);
    if (n == 1 && PyUnicode_CheckExact(PyTuple_GET_ITEM(items, 0))) {
        return Py_NewRef(PyTuple_GET_ITEM(items, 0));
    }
    Py_ssize_t length = n > 0 ? (n - 1) * PyUnicode_GET_LENGTH(sep) : 0;
    Py_UCS4 maxchar = n > 1 ? PyUnicode_MAX_CHAR_VALUE(sep) : 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *s = PyTuple_GET_ITEM(items, i);
        if (!PyUnicode_Check(s)) {
            lu_err_format(PyExc_TypeError, "sequence item %zd: expected str instance, %s found",
                          i, Py_TYPE(s)->tp_name);
            return NULL;
        }
        length += PyUnicode_GET_LENGTH(s);
        Py_UCS4 m = PyUnicode_MAX_CHAR_VALUE(s);
        maxchar = m > maxchar ? m : maxchar;
    }
    PyObject *result = PyUnicode_New(length, maxchar);
    if (result == NULL || length == 0) {
        return result;
    }
    int kind = PyUnicode_KIND(result);
    void *data = PyUnicode_DATA(result);
    Py_ssize_t at = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *parts[2] = { sep, PyTuple_GET_ITEM(items, i) };
        for (int p = i == 0; p < 2; p++) {
            PyObject *s = parts[p];
            int from = PyUnicode_KIND(s);
            const void *src = PyUnicode_DATA(s);
            Py_ssize_t len = PyUnicode_GET_LENGTH(s);
            if (from == kind) {
                memcpy((char *)data + at * kind, src, len * kind);
            }
            else {
                for (Py_ssize_t j = 0; j < len; j++) {
                    PyUnicode_WRITE(kind, data, at + j, PyUnicode_READ(from, src, j));
                }
            }
            at += len;
        }
    }
    PyUnicode_WRITE(kind, data, length, 0);
    return result;
}


/* Case mapping (Objects/unicodeobject.c, do_title and its siblings). */

static int
handle_capital_sigma(PyObject *s, Py_ssize_t i)
{
    Py_ssize_t j, length = LEN(s);
    Py_UCS4 c = 0;
    for (j = i - 1; j >= 0; j--) {
        c = CH(s, j);
        if (!_PyUnicode_IsCaseIgnorable(c)) break;
    }
    int final_sigma = j >= 0 && _PyUnicode_IsCased(c);
    if (final_sigma) {
        for (j = i + 1; j < length; j++) {
            c = CH(s, j);
            if (!_PyUnicode_IsCaseIgnorable(c)) break;
        }
        final_sigma = j == length || !_PyUnicode_IsCased(c);
    }
    return final_sigma ? 0x3C2 : 0x3C3;
}

static int
lower_ucs4(PyObject *s, Py_ssize_t i, Py_UCS4 c, Py_UCS4 *mapped)
{
    if (c == 0x3A3) {
        mapped[0] = handle_capital_sigma(s, i);
        return 1;
    }
    return _PyUnicode_ToLowerFull(c, mapped);
}

enum { CASE_LOWER, CASE_UPPER, CASE_CASEFOLD, CASE_TITLE, CASE_SWAPCASE, CASE_CAPITALIZE };

/* str_case(s, which). */
static PyObject *
prim_str_case(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); IDX(1, which);
    PyObject *s = args[0];
    ubuf b = { 0 };
    int previous_is_cased = 0;
    for (Py_ssize_t i = 0; i < LEN(s); i++) {
        Py_UCS4 c = CH(s, i), mapped[3];
        int n;
        switch (which) {
        case CASE_LOWER: n = lower_ucs4(s, i, c, mapped); break;
        case CASE_UPPER: n = _PyUnicode_ToUpperFull(c, mapped); break;
        case CASE_CASEFOLD: n = _PyUnicode_ToFoldedFull(c, mapped); break;
        case CASE_TITLE:
            n = previous_is_cased ? lower_ucs4(s, i, c, mapped) : _PyUnicode_ToTitleFull(c, mapped);
            previous_is_cased = _PyUnicode_IsCased(c);
            break;
        case CASE_SWAPCASE:
            if (_PyUnicode_IsUppercase(c)) n = lower_ucs4(s, i, c, mapped);
            else if (_PyUnicode_IsLowercase(c)) n = _PyUnicode_ToUpperFull(c, mapped);
            else { n = 1; mapped[0] = c; }
            break;
        default:
            n = i == 0 ? _PyUnicode_ToTitleFull(c, mapped) : lower_ucs4(s, i, c, mapped);
            break;
        }
        for (int j = 0; j < n; j++) {
            if (ub_put(&b, mapped[j]) < 0) {
                PyMem_Free(b.p);
                return NULL;
            }
        }
    }
    return ub_finish(&b);
}

/* The character predicates of str, by name. */
enum { IS_ALPHA, IS_ALNUM, IS_DECIMAL, IS_DIGIT, IS_NUMERIC, IS_SPACE, IS_LOWER, IS_UPPER,
       IS_TITLE, IS_PRINTABLE, IS_IDENTIFIER, IS_ASCII };

/* str_is(s, which) -> bool. */
static PyObject *
prim_str_is(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); IDX(1, which);
    PyObject *s = args[0];
    Py_ssize_t len = LEN(s);
    if (which == IS_ASCII) return bool_of(PyUnicode_IS_ASCII(s));
    if (which == IS_PRINTABLE && len == 0) return bool_of(1);
    if (len == 0) return bool_of(0);
    if (which == IS_LOWER || which == IS_UPPER || which == IS_TITLE) {
        int cased = 0, prev = 0;
        for (Py_ssize_t i = 0; i < len; i++) {
            Py_UCS4 c = CH(s, i);
            int up = _PyUnicode_IsUppercase(c), lo = _PyUnicode_IsLowercase(c), ti = _PyUnicode_IsTitlecase(c);
            if (which == IS_LOWER) {
                if (up || ti) return bool_of(0);
                if (!cased && lo) cased = 1;
            }
            else if (which == IS_UPPER) {
                if (lo || ti) return bool_of(0);
                if (!cased && up) cased = 1;
            }
            else if (up || ti) {
                if (prev) return bool_of(0);
                prev = cased = 1;
            }
            else if (lo) {
                if (!prev) return bool_of(0);
                prev = cased = 1;
            }
            else {
                prev = 0;
            }
        }
        return bool_of(cased);
    }
    for (Py_ssize_t i = 0; i < len; i++) {
        Py_UCS4 c = CH(s, i);
        int ok;
        switch (which) {
        case IS_ALPHA: ok = _PyUnicode_IsAlpha(c); break;
        case IS_ALNUM: ok = _PyUnicode_IsAlpha(c) || _PyUnicode_IsDecimalDigit(c)
                            || _PyUnicode_IsDigit(c) || _PyUnicode_IsNumeric(c); break;
        case IS_DECIMAL: ok = _PyUnicode_IsDecimalDigit(c); break;
        case IS_DIGIT: ok = _PyUnicode_IsDigit(c); break;
        case IS_NUMERIC: ok = _PyUnicode_IsNumeric(c); break;
        case IS_SPACE: ok = is_space(c); break;
        case IS_PRINTABLE: ok = c == ' ' || _PyUnicode_IsPrintable(c); break;
        default: ok = i == 0 ? (c == '_' || _PyUnicode_IsXidStart(c)) : _PyUnicode_IsXidContinue(c); break;
        }
        if (!ok) return bool_of(0);
    }
    return bool_of(1);
}

/* str_expandtabs(s, tabsize). */
static PyObject *
prim_str_expandtabs(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(0); IDX(1, tabsize);
    PyObject *s = args[0];
    ubuf b = { 0 };
    Py_ssize_t col = 0;
    for (Py_ssize_t i = 0; i < LEN(s); i++) {
        Py_UCS4 c = CH(s, i);
        int err = 0;
        if (c == '\t') {
            if (tabsize > 0) {
                Py_ssize_t fill = tabsize - col % tabsize;
                col += fill;
                while (fill-- > 0 && err == 0) err = ub_put(&b, ' ');
            }
        }
        else {
            col++;
            if (c == '\n' || c == '\r') col = 0;
            err = ub_put(&b, c);
        }
        if (err < 0) {
            PyMem_Free(b.p);
            return NULL;
        }
    }
    return ub_finish(&b);
}

/* repr and ascii. */

static int
repr_char(ubuf *b, Py_UCS4 c, Py_UCS4 quote, int ascii)
{
    char buf[16];
    if (c == quote || c == '\\') return ub_put(b, '\\') < 0 || ub_put(b, c) < 0 ? -1 : 0;
    if (c == '\t') return ub_ascii(b, "\\t");
    if (c == '\n') return ub_ascii(b, "\\n");
    if (c == '\r') return ub_ascii(b, "\\r");
    if (c >= 0x20 && c < 0x7f) return ub_put(b, c);
    if (c >= 0x7f && !ascii && _PyUnicode_IsPrintable(c)) return ub_put(b, c);
    snprintf(buf, sizeof buf, c < 0x100 ? "\\x%02x" : c < 0x10000 ? "\\u%04x" : "\\U%08x", (unsigned)c);
    return ub_ascii(b, buf);
}

/* text_repr(obj, limit): repr() of a str or bytes, cut to limit characters. */
PyObject *
lu_prim_text_repr(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    long limit;
    if (nargs != 2 || !(PyUnicode_Check(args[0]) || PyBytes_Check(args[0]))
        || !PyLong_CheckExact(args[1]) || lu_long_as_long(args[1], &limit) < 0) {
        lu_err_format(PyExc_TypeError, "text_repr(obj, limit) takes a str or bytes and an int");
        return NULL;
    }
    PyObject *s = args[0];
    int is_bytes = PyBytes_Check(s);
    Py_ssize_t len = is_bytes ? PyBytes_GET_SIZE(s) : LEN(s);
    int single = 0, dbl = 0;
    for (Py_ssize_t i = 0; i < len; i++) {
        Py_UCS4 c = is_bytes ? (unsigned char)PyBytes_AS_STRING(s)[i] : CH(s, i);
        single += c == '\'';
        dbl += c == '"';
    }
    Py_UCS4 quote = (single && !dbl) ? '"' : '\'';
    ubuf b = { 0 };
    int err = (is_bytes && ub_put(&b, 'b') < 0) || ub_put(&b, quote) < 0;
    for (Py_ssize_t i = 0; i < len && !err; i++) {
        Py_UCS4 c = is_bytes ? (unsigned char)PyBytes_AS_STRING(s)[i] : CH(s, i);
        err = repr_char(&b, c, quote, is_bytes);
    }
    if (err || ub_put(&b, quote) < 0) {
        PyMem_Free(b.p);
        return NULL;
    }
    if (limit >= 0 && b.n > limit) {
        b.n = limit;
    }
    return ub_finish(&b);
}

/* str_ascii(s): s with every non-ASCII character escaped, as ascii() does to a repr. */
static PyObject *
prim_str_ascii(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); STR(0);
    PyObject *s = args[0];
    /* an ASCII repr is returned as it is, subclass included (PyObject_ASCII) */
    if (PyUnicode_IS_ASCII(s)) return Py_NewRef(s);
    ubuf b = { 0 };
    for (Py_ssize_t i = 0; i < LEN(s); i++) {
        Py_UCS4 c = CH(s, i);
        char buf[16];
        int err;
        if (c < 128) {
            err = ub_put(&b, c);
        }
        else {
            snprintf(buf, sizeof buf, c < 0x100 ? "\\x%02x" : c < 0x10000 ? "\\u%04x" : "\\U%08x", (unsigned)c);
            err = ub_ascii(&b, buf);
        }
        if (err < 0) {
            PyMem_Free(b.p);
            return NULL;
        }
    }
    return ub_finish(&b);
}

/* Codecs: utf-8, latin-1 and ascii, with the error handlers of
   Python/codecs.c. A failure under `strict` (or an unknown handler) answers
   the tuple (start, end, reason) and the kernel raises the exception. */

enum { UTF8, LATIN1, ASCII };
enum { H_STRICT, H_IGNORE, H_REPLACE, H_BACKSLASH, H_XMLCHARREF, H_SURROGATEESCAPE, H_SURROGATEPASS };

static int
codec_of(PyObject *name)
{
    const char *n = PyUnicode_AsUTF8(name);
    return !strcmp(n, "utf-8") ? UTF8 : !strcmp(n, "latin-1") ? LATIN1 : ASCII;
}

static int
handler_of(PyObject *name)
{
    static const char names[7][20] = { "strict", "ignore", "replace", "backslashreplace",
                                       "xmlcharrefreplace", "surrogateescape", "surrogatepass" };
    const char *n = PyUnicode_AsUTF8(name);
    for (int i = 0; i < 7; i++) {
        if (!strcmp(n, names[i])) return i;
    }
    return H_STRICT;
}

static PyObject *
codec_error(Py_ssize_t start, Py_ssize_t end, const char *reason)
{
    PyObject *r = reason == NULL ? NULL : PyUnicode_FromString(reason);
    PyObject *a = PyLong_FromLongLong(start), *z = PyLong_FromLongLong(end);
    PyObject *t = (r && a && z) ? PyTuple_Pack(3, a, z, r) : NULL;
    Py_XDECREF(r); Py_XDECREF(a); Py_XDECREF(z);
    return t;
}

typedef struct {
    char *p;
    Py_ssize_t n, cap;
} bbuf;

static int
bb_put(bbuf *b, unsigned char c)
{
    if (b->n == b->cap) {
        Py_ssize_t cap = b->cap ? b->cap * 2 : 64;
        char *p = PyMem_Realloc(b->p, cap);
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        b->p = p;
        b->cap = cap;
    }
    b->p[b->n++] = (char)c;
    return 0;
}

static int
bb_str(bbuf *b, const char *s)
{
    for (; *s; s++) {
        if (bb_put(b, (unsigned char)*s) < 0) return -1;
    }
    return 0;
}

static int
put_utf8(bbuf *b, Py_UCS4 c)
{
    if (c < 0x80) return bb_put(b, c);
    if (c < 0x800) return bb_put(b, 0xc0 | (c >> 6)) | bb_put(b, 0x80 | (c & 0x3f));
    if (c < 0x10000) {
        return bb_put(b, 0xe0 | (c >> 12)) | bb_put(b, 0x80 | ((c >> 6) & 0x3f)) | bb_put(b, 0x80 | (c & 0x3f));
    }
    return bb_put(b, 0xf0 | (c >> 18)) | bb_put(b, 0x80 | ((c >> 12) & 0x3f))
         | bb_put(b, 0x80 | ((c >> 6) & 0x3f)) | bb_put(b, 0x80 | (c & 0x3f));
}

/* str_encode(s, codec, errors) -> bytes, or (start, end, reason). */
static PyObject *
prim_str_encode(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); STR(0); STR(1); STR(2);
    PyObject *s = args[0];
    int codec = codec_of(args[1]), handler = handler_of(args[2]);
    Py_UCS4 limit = codec == UTF8 ? 0x10ffff : codec == LATIN1 ? 0xff : 0x7f;
    const char *reason = codec == UTF8 ? "surrogates not allowed"
        : codec == LATIN1 ? "ordinal not in range(256)" : "ordinal not in range(128)";
    bbuf b = { 0 };
    Py_ssize_t len = LEN(s);
    int err = 0;
    for (Py_ssize_t i = 0; i < len && !err; i++) {
        Py_UCS4 c = CH(s, i);
        int surrogate = c >= 0xd800 && c <= 0xdfff;
        if (c <= limit && !(codec == UTF8 && surrogate)) {
            err = codec == UTF8 ? put_utf8(&b, c) : bb_put(&b, c);
            continue;
        }
        Py_ssize_t j = i + 1;
        while (j < len) {
            Py_UCS4 d = CH(s, j);
            if (codec == UTF8 ? !(d >= 0xd800 && d <= 0xdfff) : d <= limit) break;
            j++;
        }
        if (handler == H_STRICT || (handler == H_SURROGATEPASS && codec != UTF8)) {
            PyMem_Free(b.p);
            return codec_error(i, j, reason);
        }
        for (Py_ssize_t k = i; k < j && !err; k++) {
            Py_UCS4 d = CH(s, k);
            char buf[16];
            switch (handler) {
            case H_REPLACE: err = bb_put(&b, '?'); break;
            case H_BACKSLASH:
                snprintf(buf, sizeof buf, d < 0x100 ? "\\x%02x" : d < 0x10000 ? "\\u%04x" : "\\U%08x", (unsigned)d);
                err = bb_str(&b, buf);
                break;
            case H_XMLCHARREF:
                snprintf(buf, sizeof buf, "&#%u;", (unsigned)d);
                err = bb_str(&b, buf);
                break;
            case H_SURROGATEESCAPE:
                if (d < 0xdc80 || d > 0xdcff) {
                    PyMem_Free(b.p);
                    return codec_error(i, j, reason);
                }
                err = bb_put(&b, d - 0xdc00);
                break;
            case H_SURROGATEPASS: err = put_utf8(&b, d); break;
            default: break;     /* ignore */
            }
        }
        i = j - 1;
    }
    if (err) {
        PyMem_Free(b.p);
        return NULL;
    }
    PyObject *r = PyBytes_FromStringAndSize(b.p, b.n);
    PyMem_Free(b.p);
    return r;
}

#define CONT(c) ((c) >= 0x80 && (c) < 0xC0)

/* One utf-8 sequence at s[0..avail): 0 and its character, 1 invalid start byte,
   2-4 invalid continuation byte (the error covers 1 to 3 bytes), 5 truncated. */
static int
utf8_step(const unsigned char *s, Py_ssize_t avail, int surrogates, Py_UCS4 *out, int *len)
{
    Py_UCS4 ch = s[0], c2, c3, c4;
    if (ch < 0x80) { *out = ch; *len = 1; return 0; }
    if (ch < 0xE0) {
        if (ch < 0xC2) return 1;
        if (avail < 2) return 5;
        if (!CONT(s[1])) return 2;
        *out = (ch << 6) + s[1] - ((0xC0 << 6) + 0x80);
        *len = 2;
        return 0;
    }
    if (ch < 0xF0) {
        if (avail < 3) {
            if (avail < 2) return 5;
            c2 = s[1];
            if (!CONT(c2) || (c2 < 0xA0 ? ch == 0xE0 : (ch == 0xED && !surrogates))) return 2;
            return 5;
        }
        c2 = s[1]; c3 = s[2];
        if (!CONT(c2)) return 2;
        if (ch == 0xE0 ? c2 < 0xA0 : (ch == 0xED && c2 >= 0xA0 && !surrogates)) return 2;
        if (!CONT(c3)) return 3;
        *out = (ch << 12) + (c2 << 6) + c3 - ((0xE0 << 12) + (0x80 << 6) + 0x80);
        *len = 3;
        return 0;
    }
    if (ch < 0xF5) {
        if (avail < 4) {
            if (avail < 2) return 5;
            c2 = s[1];
            if (!CONT(c2) || (c2 < 0x90 ? ch == 0xF0 : ch == 0xF4)) return 2;
            if (avail < 3) return 5;
            if (!CONT(s[2])) return 3;
            return 5;
        }
        c2 = s[1]; c3 = s[2]; c4 = s[3];
        if (!CONT(c2)) return 2;
        if (ch == 0xF0 ? c2 < 0x90 : (ch == 0xF4 && c2 >= 0x90)) return 2;
        if (!CONT(c3)) return 3;
        if (!CONT(c4)) return 4;
        *out = (ch << 18) + (c2 << 12) + (c3 << 6) + c4
             - ((0xF0 << 18) + (0x80 << 12) + (0x80 << 6) + 0x80);
        *len = 4;
        return 0;
    }
    return 1;
}

/* str_decode(bytes, codec, errors) -> str, or (start, end, reason). */
static PyObject *
prim_str_decode(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); STR(1); STR(2);
    if (!PyBytes_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "str_decode() needs bytes");
        return NULL;
    }
    const unsigned char *s = (const unsigned char *)PyBytes_AS_STRING(args[0]);
    Py_ssize_t n = PyBytes_GET_SIZE(args[0]);
    int codec = codec_of(args[1]), handler = handler_of(args[2]);
    ubuf b = { 0 };
    int err = 0;
    for (Py_ssize_t i = 0; i < n && !err;) {
        Py_UCS4 c = s[i];
        int len = 1, bad = 0;
        const char *reason = NULL;
        Py_ssize_t end = i + 1;
        if (codec == UTF8) {
            bad = utf8_step(s + i, n - i, handler == H_SURROGATEPASS, &c, &len);
            if (bad) {
                reason = bad == 1 ? "invalid start byte" : bad == 5 ? "unexpected end of data"
                                                                   : "invalid continuation byte";
                end = bad == 5 ? n : i + (bad == 1 ? 1 : bad - 1);
            }
        }
        else if (codec == ASCII && c > 0x7f) {
            bad = 1;
            reason = "ordinal not in range(128)";
        }
        if (!bad) {
            err = ub_put(&b, c);
            i += len;
            continue;
        }
        if (handler == H_STRICT || handler == H_SURROGATEPASS || handler == H_XMLCHARREF) {
            PyMem_Free(b.p);
            return codec_error(i, end, reason);
        }
        for (Py_ssize_t k = i; k < end && !err; k++) {
            char buf[8];
            switch (handler) {
            case H_REPLACE: if (k == i) err = ub_put(&b, 0xfffd); break;
            case H_BACKSLASH:
                snprintf(buf, sizeof buf, "\\x%02x", s[k]);
                err = ub_ascii(&b, buf);
                break;
            case H_SURROGATEESCAPE:
                if (s[k] < 0x80) {
                    PyMem_Free(b.p);
                    return codec_error(i, end, reason);
                }
                err = ub_put(&b, 0xdc00 + s[k]);
                break;
            default: break;
            }
        }
        i = end;
    }
    if (err) {
        PyMem_Free(b.p);
        return NULL;
    }
    return ub_finish(&b);
}

/* str_iter(s) and its iterator, over the layout's code points. */

typedef struct {
    PyObject_HEAD
    Py_ssize_t index;
    PyObject *seq;
} striterobject;

static PyObject *
striter_next(PyObject *op)
{
    striterobject *it = (striterobject *)op;
    if (it->seq == NULL) return NULL;
    if (it->index < LEN(it->seq)) {
        Py_UCS4 c = CH(it->seq, it->index++);
        return PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, &c, 1);
    }
    Py_CLEAR(it->seq);
    return NULL;
}

static PyObject *
striter_self(PyObject *op)
{
    return Py_NewRef(op);
}

static void
striter_dealloc(PyObject *op)
{
    Py_XDECREF(((striterobject *)op)->seq);
    lu_object_free(op);
}

static int
striter_traverse(PyObject *op, visitproc visit, void *arg)
{
    Py_VISIT(((striterobject *)op)->seq);
    return 0;
}

void
lu_striter_init(void)
{
    PyUnicodeIter_Type.tp_basicsize = sizeof(striterobject);
    PyUnicodeIter_Type.tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    PyUnicodeIter_Type.tp_dealloc = striter_dealloc;
    PyUnicodeIter_Type.tp_traverse = striter_traverse;
    PyUnicodeIter_Type.tp_iter = striter_self;
    PyUnicodeIter_Type.tp_iternext = striter_next;
}

static PyObject *
prim_str_iter(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); STR(0);
    striterobject *it = (striterobject *)lu_object_new(&PyUnicodeIter_Type, sizeof(striterobject));
    if (it != NULL) {
        it->seq = Py_NewRef(args[0]);
    }
    return (PyObject *)it;
}

/* str_iter_next(it): the next character, or MISSING when exhausted. */
static PyObject *
prim_str_iter_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *v = striter_next(args[0]);
    return v != NULL ? v : Py_NewRef(LU_MISSING);
}

/* str_subtype(cls, s): an instance of the str subclass cls, with the
   non-compact layout PyUnicode_DATA reads through data.any. */
static PyObject *
prim_str_subtype(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); STR(1);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyUnicode_Type)) {
        lu_err_format(PyExc_TypeError, "str_subtype() needs a str subclass");
        return NULL;
    }
    PyObject *s = args[1];
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyUnicodeObject *r = (PyUnicodeObject *)tp->tp_alloc(tp, 0);
    if (r == NULL) {
        return NULL;
    }
    Py_ssize_t size = (LEN(s) + 1) * KIND(s);
    void *data = PyObject_Malloc(size);
    if (data == NULL) {
        Py_DECREF(r);
        return PyErr_NoMemory();
    }
    memcpy(data, DATA(s), size);
    PyASCIIObject *a = (PyASCIIObject *)r;
    a->length = LEN(s);
    a->hash = -1;
    a->state.kind = KIND(s);
    a->state.compact = 0;
    a->state.ascii = PyUnicode_IS_ASCII(s);
    r->data.any = data;
    return (PyObject *)r;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
#define PURE(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL | LU_METH_NOPY, NULL }
PyMethodDef lu_prim_str_methods[] = {
    PURE(str_find), PURE(str_count), FAST(str_replace), PURE(str_slice), PURE(str_slice_step),
    PURE(str_item), PURE(str_decimal), FAST(slice_parts), FAST(box_get), FAST(box_set), PURE(str_ord), PURE(str_chr), PURE(str_len), FAST(bytes_ord), FAST(str_sizeof), FAST(str_hash), PURE(str_cmp),
    PURE(str_concat), FAST(str_repeat), FAST(str_split), FAST(str_splitlines), FAST(str_strip),
    PURE(str_case), PURE(str_is), FAST(str_expandtabs), FAST(str_ascii), FAST(str_encode),
    FAST(str_decode), FAST(str_iter), FAST(str_iter_next), FAST(str_subtype),
    { NULL, NULL, 0, NULL },
};
