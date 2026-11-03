/* str storage primitives. */

#include "prim/prim.h"

#include <string.h>

/* str_join(sep, items): items is a tuple of exact strs. */
PyObject *
lu_prim_str_join(PyObject *module, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2 || !PyUnicode_CheckExact(args[0]) || !PyTuple_CheckExact(args[1])) {
        lu_err_format(PyExc_TypeError, "str_join(sep, items) takes a str and a tuple");
        return NULL;
    }
    PyObject *sep = args[0], *items = args[1];
    Py_ssize_t n = PyTuple_GET_SIZE(items);
    if (n == 1 && PyUnicode_CheckExact(PyTuple_GET_ITEM(items, 0))) {
        return Py_NewRef(PyTuple_GET_ITEM(items, 0));
    }
    Py_ssize_t length = n > 0 ? (n - 1) * PyUnicode_GET_LENGTH(sep) : 0;
    Py_UCS4 maxchar = n > 1 ? PyUnicode_MAX_CHAR_VALUE(sep) : 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *s = PyTuple_GET_ITEM(items, i);
        if (!PyUnicode_CheckExact(s)) {
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

/* Whether repr leaves a non-ASCII character as it is. Without the Unicode
   database (K1e) this is the printable test of ranges that are certainly not:
   controls, spaces, format characters, surrogates, private use and
   noncharacters. */
static int
repr_printable(Py_UCS4 c)
{
    if (c < 0x20 || c == 0x7f) return 0;
    if (c < 0x7f) return 1;
    return !(c <= 0xa0 || c == 0xad || (c >= 0x2000 && c <= 0x200f) || (c >= 0x2028 && c <= 0x202f)
             || (c >= 0x205f && c <= 0x206f) || c == 0x1680 || c == 0x3000
             || (c >= 0xd800 && c <= 0xf8ff) || c == 0xfeff || (c >= 0xfff0 && c <= 0xffff)
             || (c >= 0xe0000));
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
    Py_ssize_t len = is_bytes ? PyBytes_GET_SIZE(s) : PyUnicode_GET_LENGTH(s);
    int kind = is_bytes ? 1 : PyUnicode_KIND(s);
    const void *data = is_bytes ? (const void *)PyBytes_AS_STRING(s) : PyUnicode_DATA(s);
    int single = 0, dbl = 0;
    for (Py_ssize_t i = 0; i < len; i++) {
        Py_UCS4 c = is_bytes ? ((const unsigned char *)data)[i] : PyUnicode_READ(kind, data, i);
        single += c == '\'';
        dbl += c == '"';
    }
    Py_UCS4 quote = (single && !dbl) ? '"' : '\'';
    Py_UCS4 *out = PyMem_Malloc(((size_t)len * 10 + 4) * sizeof(Py_UCS4));
    if (out == NULL) {
        return PyErr_NoMemory();
    }
    Py_ssize_t n = 0;
    if (is_bytes) out[n++] = 'b';
    out[n++] = quote;
    for (Py_ssize_t i = 0; i < len; i++) {
        Py_UCS4 c = is_bytes ? ((const unsigned char *)data)[i] : PyUnicode_READ(kind, data, i);
        if (c == quote || c == '\\') { out[n++] = '\\'; out[n++] = c; }
        else if (c == '\t') { out[n++] = '\\'; out[n++] = 't'; }
        else if (c == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
        else if (c == '\r') { out[n++] = '\\'; out[n++] = 'r'; }
        else if (c < 0x7f ? c >= 0x20 : (!is_bytes && repr_printable(c))) { out[n++] = c; }
        else {
            const char *fmt = c < 0x100 ? "\\x%02x" : c < 0x10000 ? "\\u%04x" : "\\U%08x";
            char buf[16];
            int w = snprintf(buf, sizeof buf, fmt, (unsigned)c);
            for (int j = 0; j < w; j++) out[n++] = (unsigned char)buf[j];
        }
    }
    out[n++] = quote;
    if (limit >= 0 && n > limit) {
        n = limit;
    }
    PyObject *r = PyUnicode_FromKindAndData(PyUnicode_4BYTE_KIND, out, n);
    PyMem_Free(out);
    return r;
}
