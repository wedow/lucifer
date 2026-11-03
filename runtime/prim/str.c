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
