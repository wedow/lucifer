/* Format-string argument parsing (Python/getargs.c): PyArg_Parse,
   PyArg_ParseTuple, PyArg_ParseTupleAndKeywords and PyArg_UnpackTuple. The
   format units supported are those extension modules use: the integer,
   float, string, bytes, object and predicate codes, nested tuples, and the
   optional, keyword-only and name/message markers. */

#include "lucifer.h"

#include <string.h>

extern PyObject *_PyNumber_Index(PyObject *item);

struct parse {
    const char *fname;                  /* from ':name' */
    const char *message;                /* from ';message' */
    int unit;                           /* the argument being converted, from 1 */
};

static void
type_error(struct parse *p, const char *expected, PyObject *arg)
{
    if (p->message != NULL) {
        PyErr_SetString(PyExc_TypeError, p->message);
        return;
    }
    PyErr_Format(PyExc_TypeError, "%.200s%s argument %d must be %.50s, not %.50s",
                 p->fname != NULL ? p->fname : "function", p->fname != NULL ? "()" : "", p->unit,
                 expected, arg == Py_None ? "None" : Py_TYPE(arg)->tp_name);
}

/* A converted integer of C type `name`, range-checked to [min, max]. */
static int
small_int(struct parse *p, PyObject *arg, long min, long max, const char *what, long *out)
{
    long v = PyLong_AsLong(arg);
    if (v == -1 && PyErr_Occurred()) {
        if (PyErr_ExceptionMatches(PyExc_TypeError)) {
            PyErr_Clear();
            type_error(p, "int", arg);
        }
        return -1;
    }
    if (v < min) {
        PyErr_Format(PyExc_OverflowError, "%s is less than minimum", what);
        return -1;
    }
    if (v > max) {
        PyErr_Format(PyExc_OverflowError, "%s is greater than maximum", what);
        return -1;
    }
    *out = v;
    return 0;
}

static int convert_unit(struct parse *p, PyObject *arg, const char **fmt, va_list *va);
static int skip_unit(const char **fmt, va_list *va);

/* The unit after a '#' or '*' modifier, or a plain one: the format character
   with its modifier. */
static int
modifier(const char **fmt)
{
    int m = **fmt;
    if (m == '#' || m == '*' || m == '!' || m == '&') {
        (*fmt)++;
        return m;
    }
    return 0;
}

static int
convert_string(struct parse *p, PyObject *arg, int code, int mod, va_list *va)
{
    int none_ok = code == 'z';
    if (none_ok && arg == Py_None) {
        if (mod == '#') {
            *va_arg(*va, const char **) = NULL;
            *va_arg(*va, Py_ssize_t *) = 0;
        }
        else {
            *va_arg(*va, const char **) = NULL;
        }
        return 0;
    }
    if (code == 'y') {
        const char *data;
        Py_ssize_t size;
        if (PyBytes_Check(arg)) {
            data = PyBytes_AS_STRING(arg);
            size = PyBytes_GET_SIZE(arg);
        }
        else {
            type_error(p, "bytes-like object", arg);
            return -1;
        }
        if (mod != '#' && strlen(data) != (size_t)size) {
            PyErr_SetString(PyExc_ValueError, "embedded null byte");
            return -1;
        }
        *va_arg(*va, const char **) = data;
        if (mod == '#') {
            *va_arg(*va, Py_ssize_t *) = size;
        }
        return 0;
    }
    if (!PyUnicode_Check(arg)) {
        type_error(p, none_ok ? "str or None" : "str", arg);
        return -1;
    }
    Py_ssize_t size;
    const char *data = PyUnicode_AsUTF8AndSize(arg, &size);
    if (data == NULL) {
        return -1;
    }
    if (mod != '#' && strlen(data) != (size_t)size) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        return -1;
    }
    *va_arg(*va, const char **) = data;
    if (mod == '#') {
        *va_arg(*va, Py_ssize_t *) = size;
    }
    return 0;
}

static int
convert_tuple(struct parse *p, PyObject *arg, const char **fmt, va_list *va)
{
    int n = 0;
    const char *f = *fmt;
    for (int depth = 1; *f && depth > 0; f++) {
        if (*f == '(') {
            depth++;
        }
        else if (*f == ')') {
            depth--;
        }
        else if (depth == 1 && strchr("bBhHiIlkLKnfdDcCsSyYUOp", *f)) {
            n++;
        }
    }
    if (!PySequence_Check(arg) || PyBytes_Check(arg) || PyUnicode_Check(arg)) {
        type_error(p, "tuple", arg);
        return -1;
    }
    PyObject *seq = PySequence_Tuple(arg);
    if (seq == NULL) {
        return -1;
    }
    if (PyTuple_GET_SIZE(seq) != n) {
        PyErr_Format(PyExc_TypeError, "%s%s argument %d must be sequence of length %d, not %zd",
                     p->fname != NULL ? p->fname : "function", p->fname != NULL ? "()" : "", p->unit, n,
                     PyTuple_GET_SIZE(seq));
        Py_DECREF(seq);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (convert_unit(p, PyTuple_GET_ITEM(seq, i), fmt, va) < 0) {
            Py_DECREF(seq);
            return -1;
        }
    }
    Py_DECREF(seq);
    if (**fmt == ')') {
        (*fmt)++;
    }
    return 0;
}

/* Converts arg by the unit at *fmt, storing through the next va arguments. */
static int
convert_unit(struct parse *p, PyObject *arg, const char **fmt, va_list *va)
{
    int code = *(*fmt)++;
    long v;
    switch (code) {
    case 'b': case 'B': {
        if (small_int(p, arg, code == 'b' ? 0 : INT_MIN, code == 'b' ? UCHAR_MAX : INT_MAX,
                      code == 'b' ? "unsigned byte integer" : "byte", &v) < 0) {
            return -1;
        }
        *va_arg(*va, unsigned char *) = (unsigned char)v;
        return 0;
    }
    case 'h': case 'H': {
        if (small_int(p, arg, code == 'h' ? SHRT_MIN : INT_MIN, code == 'h' ? SHRT_MAX : INT_MAX,
                      "signed short integer", &v) < 0) {
            return -1;
        }
        *va_arg(*va, short *) = (short)v;
        return 0;
    }
    case 'i': case 'I': {
        int x = PyLong_AsInt(arg);
        if (x == -1 && PyErr_Occurred()) {
            if (PyErr_ExceptionMatches(PyExc_TypeError)) {
                PyErr_Clear();
                type_error(p, "int", arg);
            }
            return -1;
        }
        *va_arg(*va, int *) = x;
        return 0;
    }
    case 'l': {
        long x = PyLong_AsLong(arg);
        if (x == -1 && PyErr_Occurred()) {
            if (PyErr_ExceptionMatches(PyExc_TypeError)) {
                PyErr_Clear();
                type_error(p, "int", arg);
            }
            return -1;
        }
        *va_arg(*va, long *) = x;
        return 0;
    }
    case 'k': case 'K': {
        if (!PyLong_Check(arg)) {
            type_error(p, "int", arg);
            return -1;
        }
        if (code == 'k') {
            *va_arg(*va, unsigned long *) = PyLong_AsUnsignedLongMask(arg);
        }
        else {
            *va_arg(*va, unsigned long long *) = PyLong_AsUnsignedLongLongMask(arg);
        }
        return 0;
    }
    case 'L': {
        long long x = PyLong_AsLongLong(arg);
        if (x == -1 && PyErr_Occurred()) {
            if (PyErr_ExceptionMatches(PyExc_TypeError)) {
                PyErr_Clear();
                type_error(p, "int", arg);
            }
            return -1;
        }
        *va_arg(*va, long long *) = x;
        return 0;
    }
    case 'n': {
        PyObject *index = _PyNumber_Index(arg);
        if (index == NULL) {
            if (PyErr_ExceptionMatches(PyExc_TypeError)) {
                PyErr_Clear();
                type_error(p, "int", arg);
            }
            return -1;
        }
        Py_ssize_t x = PyLong_AsSsize_t(index);
        Py_DECREF(index);
        if (x == -1 && PyErr_Occurred()) {
            return -1;
        }
        *va_arg(*va, Py_ssize_t *) = x;
        return 0;
    }
    case 'f': case 'd': {
        double x = PyFloat_AsDouble(arg);
        if (x == -1.0 && PyErr_Occurred()) {
            if (PyErr_ExceptionMatches(PyExc_TypeError)) {
                PyErr_Clear();
                type_error(p, "float", arg);
            }
            return -1;
        }
        if (code == 'f') {
            *va_arg(*va, float *) = (float)x;
        }
        else {
            *va_arg(*va, double *) = x;
        }
        return 0;
    }
    case 'p': {
        int t = PyObject_IsTrue(arg);
        if (t < 0) {
            return -1;
        }
        *va_arg(*va, int *) = t;
        return 0;
    }
    case 'c': {
        if ((PyBytes_Check(arg) && PyBytes_GET_SIZE(arg) == 1)) {
            *va_arg(*va, char *) = PyBytes_AS_STRING(arg)[0];
            return 0;
        }
        if (PyByteArray_Check(arg) && PyByteArray_GET_SIZE(arg) == 1) {
            *va_arg(*va, char *) = PyByteArray_AS_STRING(arg)[0];
            return 0;
        }
        type_error(p, "a byte string of length 1", arg);
        return -1;
    }
    case 'C': {
        if (!PyUnicode_Check(arg) || PyUnicode_GET_LENGTH(arg) != 1) {
            type_error(p, "a unicode character", arg);
            return -1;
        }
        *va_arg(*va, int *) = (int)PyUnicode_READ_CHAR(arg, 0);
        return 0;
    }
    case 's': case 'z': case 'y': {
        int mod = modifier(fmt);
        if (mod == '*') {
            PyErr_SetString(PyExc_SystemError, "buffer formats ('*') are not supported yet");
            return -1;
        }
        return convert_string(p, arg, code, mod, va);
    }
    case 'S': case 'Y': case 'U': {
        int ok = code == 'S' ? PyBytes_Check(arg) : code == 'Y' ? PyByteArray_Check(arg) : PyUnicode_Check(arg);
        if (!ok) {
            type_error(p, code == 'S' ? "bytes" : code == 'Y' ? "bytearray" : "str", arg);
            return -1;
        }
        *va_arg(*va, PyObject **) = arg;
        return 0;
    }
    case 'O': {
        int mod = modifier(fmt);
        if (mod == '!') {
            PyTypeObject *type = va_arg(*va, PyTypeObject *);
            if (!PyType_IsSubtype(Py_TYPE(arg), type)) {
                type_error(p, type->tp_name, arg);
                return -1;
            }
            *va_arg(*va, PyObject **) = arg;
        }
        else if (mod == '&') {
            int (*convert)(PyObject *, void *) = va_arg(*va, int (*)(PyObject *, void *));
            void *addr = va_arg(*va, void *);
            if (!convert(arg, addr)) {
                if (!PyErr_Occurred()) {
                    type_error(p, "(unspecified)", arg);
                }
                return -1;
            }
        }
        else {
            *va_arg(*va, PyObject **) = arg;
        }
        return 0;
    }
    case '(':
        return convert_tuple(p, arg, fmt, va);
    default:
        PyErr_Format(PyExc_SystemError, "bad format char '%c' in the argument format", code);
        return -1;
    }
}

/* Passes over one unit, consuming the va arguments it would have stored through. */
static int
skip_unit(const char **fmt, va_list *va)
{
    int code = *(*fmt)++;
    switch (code) {
    case 'b': case 'B': va_arg(*va, unsigned char *); break;
    case 'h': case 'H': va_arg(*va, short *); break;
    case 'i': case 'I': case 'p': va_arg(*va, int *); break;
    case 'l': va_arg(*va, long *); break;
    case 'k': va_arg(*va, unsigned long *); break;
    case 'L': va_arg(*va, long long *); break;
    case 'K': va_arg(*va, unsigned long long *); break;
    case 'n': va_arg(*va, Py_ssize_t *); break;
    case 'f': va_arg(*va, float *); break;
    case 'd': va_arg(*va, double *); break;
    case 'c': va_arg(*va, char *); break;
    case 'C': va_arg(*va, int *); break;
    case 's': case 'z': case 'y': {
        int mod = modifier(fmt);
        va_arg(*va, const char **);
        if (mod == '#') {
            va_arg(*va, Py_ssize_t *);
        }
        break;
    }
    case 'S': case 'Y': case 'U': va_arg(*va, PyObject **); break;
    case 'O': {
        int mod = modifier(fmt);
        if (mod == '!') {
            va_arg(*va, PyTypeObject *);
            va_arg(*va, PyObject **);
        }
        else if (mod == '&') {
            va_arg(*va, void *);
            va_arg(*va, void *);
        }
        else {
            va_arg(*va, PyObject **);
        }
        break;
    }
    case '(':
        while (**fmt && **fmt != ')') {
            if (skip_unit(fmt, va) < 0) {
                return -1;
            }
        }
        if (**fmt == ')') {
            (*fmt)++;
        }
        break;
    default:
        PyErr_Format(PyExc_SystemError, "bad format char '%c' in the argument format", code);
        return -1;
    }
    return 0;
}

/* The number of argument units in the format, where the optional ones start
   and where the keyword-only ones do; fills the ':name' and ';message'. */
static int
scan_format(const char *format, struct parse *p, int *min, int *max, int *kwonly)
{
    int n = 0;
    *min = -1;
    *kwonly = -1;
    const char *f = format;
    for (; *f && *f != ':' && *f != ';'; f++) {
        switch (*f) {
        case '|':
            if (*min >= 0) {
                PyErr_SetString(PyExc_SystemError, "Invalid format string (| specified twice)");
                return -1;
            }
            *min = n;
            break;
        case '$':
            if (*kwonly >= 0) {
                PyErr_SetString(PyExc_SystemError, "Invalid format string ($ specified twice)");
                return -1;
            }
            *kwonly = n;
            if (*min < 0) {
                *min = n;
            }
            break;
        case '(': {
            int depth = 1;
            while (f[1] && depth > 0) {
                f++;
                depth += *f == '(' ? 1 : *f == ')' ? -1 : 0;
            }
            n++;
            break;
        }
        case '#': case '*': case '!': case '&':
            break;
        default:
            n++;
        }
    }
    if (*f == ':') {
        p->fname = f + 1;
    }
    else if (*f == ';') {
        p->message = f + 1;
    }
    if (*min < 0) {
        *min = n;
    }
    *max = n;
    return 0;
}

static int
parse_args(PyObject *args, PyObject *kwargs, const char *format, char *const *kwlist, va_list *va)
{
    struct parse p = { NULL, NULL, 0 };
    int min, max, kwonly;
    if (scan_format(format, &p, &min, &max, &kwonly) < 0) {
        return 0;
    }
    if (kwonly >= 0 && kwonly < max) {
        /* keyword-only units follow the positional ones */
    }
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t nkw = kwargs != NULL ? PyDict_Size(kwargs) : 0;
    int npos_max = kwonly >= 0 ? kwonly : max;
    const char *fname = p.fname != NULL ? p.fname : "function";
    const char *parens = p.fname != NULL ? "()" : "";
    if (nargs > npos_max) {
        PyErr_Format(PyExc_TypeError, "%.200s%s takes %s %d positional argument%s (%zd given)", fname,
                     parens, min == npos_max && nkw == 0 ? "exactly" : "at most", npos_max,
                     npos_max == 1 ? "" : "s", nargs);
        return 0;
    }
    if (kwlist == NULL && nkw > 0) {
        PyErr_Format(PyExc_TypeError, "%.200s%s takes no keyword arguments", fname, parens);
        return 0;
    }
    const char *f = format;
    Py_ssize_t used_kw = 0;
    for (int i = 0; i < max; i++) {
        while (*f == '|' || *f == '$') {
            f++;
        }
        p.unit = i + 1;
        PyObject *arg = i < nargs ? PyTuple_GET_ITEM(args, i) : NULL;
        if (arg == NULL && kwargs != NULL && kwlist != NULL && kwlist[i] != NULL && *kwlist[i]) {
            arg = PyDict_GetItemString(kwargs, kwlist[i]);
            if (arg != NULL) {
                used_kw++;
            }
        }
        else if (arg != NULL && kwargs != NULL && kwlist != NULL && kwlist[i] != NULL
                 && PyDict_GetItemString(kwargs, kwlist[i]) != NULL) {
            PyErr_Format(PyExc_TypeError, "argument for %.200s%s given by name ('%s') and position (%d)",
                         fname, parens, kwlist[i], i + 1);
            return 0;
        }
        if (arg == NULL) {
            if (i < min) {
                if (i < npos_max && nkw == 0 && kwlist == NULL) {
                    PyErr_Format(PyExc_TypeError, "%.200s%s takes %s %d argument%s (%zd given)", fname,
                                 parens, min == max ? "exactly" : "at least", min, min == 1 ? "" : "s",
                                 nargs);
                }
                else {
                    PyErr_Format(PyExc_TypeError, "%.200s%s missing required argument '%s' (pos %d)",
                                 fname, parens, kwlist != NULL && kwlist[i] ? kwlist[i] : "?", i + 1);
                }
                return 0;
            }
            if (skip_unit(&f, va) < 0) {
                return 0;
            }
            continue;
        }
        if (convert_unit(&p, arg, &f, va) < 0) {
            return 0;
        }
    }
    if (kwargs != NULL && used_kw < nkw) {
        PyObject *key, *value;
        Py_ssize_t pos = 0;
        while (PyDict_Next(kwargs, &pos, &key, &value)) {
            const char *name = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : NULL;
            int known = 0;
            for (int i = 0; name != NULL && kwlist != NULL && kwlist[i] != NULL; i++) {
                known |= strcmp(name, kwlist[i]) == 0;
            }
            if (!known) {
                PyErr_Format(PyExc_TypeError, "'%S' is an invalid keyword argument for %.200s%s", key,
                             p.fname != NULL ? p.fname : "this function", p.fname != NULL ? "()" : "");
                return 0;
            }
        }
    }
    return 1;
}

int
PyArg_VaParse(PyObject *args, const char *format, va_list va)
{
    va_list copy;
    va_copy(copy, va);
    int r = PyTuple_Check(args) ? parse_args(args, NULL, format, NULL, &copy) : 0;
    if (!PyTuple_Check(args)) {
        PyErr_SetString(PyExc_SystemError, "new style getargs format but argument is not a tuple");
    }
    va_end(copy);
    return r;
}

int
PyArg_ParseTuple(PyObject *args, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    int r = PyArg_VaParse(args, format, va);
    va_end(va);
    return r;
}

int
PyArg_VaParseTupleAndKeywords(PyObject *args, PyObject *kwargs, const char *format, char *const *kwlist,
                              va_list va)
{
    if (!PyTuple_Check(args) || (kwargs != NULL && !PyDict_Check(kwargs)) || format == NULL || kwlist == NULL) {
        PyErr_BadInternalCall();
        return 0;
    }
    va_list copy;
    va_copy(copy, va);
    int r = parse_args(args, kwargs, format, kwlist, &copy);
    va_end(copy);
    return r;
}

int
PyArg_ParseTupleAndKeywords(PyObject *args, PyObject *kwargs, const char *format, char *const *kwlist, ...)
{
    va_list va;
    va_start(va, kwlist);
    int r = PyArg_VaParseTupleAndKeywords(args, kwargs, format, kwlist, va);
    va_end(va);
    return r;
}

int
PyArg_Parse(PyObject *args, const char *format, ...)
{
    PyObject *tuple = PyTuple_Pack(1, args);
    if (tuple == NULL) {
        return 0;
    }
    va_list va;
    va_start(va, format);
    int r = PyArg_VaParse(tuple, format, va);
    va_end(va);
    Py_DECREF(tuple);
    return r;
}

int
PyArg_UnpackTuple(PyObject *args, const char *name, Py_ssize_t min, Py_ssize_t max, ...)
{
    if (!PyTuple_Check(args)) {
        PyErr_SetString(PyExc_SystemError, "PyArg_UnpackTuple() argument list is not a tuple");
        return 0;
    }
    Py_ssize_t n = PyTuple_GET_SIZE(args);
    if (n < min || n > max) {
        PyErr_Format(PyExc_TypeError, "%s expected %s%zd argument%s, got %zd", name != NULL ? name : "unpacked tuple",
                     min == max ? "" : n < min ? "at least " : "at most ", n < min ? min : max,
                     (n < min ? min : max) == 1 ? "" : "s", n);
        return 0;
    }
    va_list va;
    va_start(va, max);
    for (Py_ssize_t i = 0; i < max; i++) {
        PyObject **slot = va_arg(va, PyObject **);
        if (i < n) {
            *slot = PyTuple_GET_ITEM(args, i);
        }
    }
    va_end(va);
    return 1;
}
