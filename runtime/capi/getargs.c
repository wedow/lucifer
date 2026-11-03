/* The argument-clinic helpers of Python/getargs.c (adapted: see
   third_party/cpython/CHANGES.md): positional checks and keyword unpacking
   driven by a _PyArg_Parser. A parser whose arguments come from a format string
   (PyArg_ParseTupleAndKeywords's) is not supported yet. */

#include "lucifer.h"
#include "interp/frame.h"

LU_EXPORT void
_PyArg_BadArgument(const char *fname, const char *displayname, const char *expected, PyObject *arg)
{
    PyErr_Format(PyExc_TypeError, "%.200s() %.200s must be %.50s, not %.50s", fname, displayname,
                 expected, arg == Py_None ? "None" : Py_TYPE(arg)->tp_name);
}

LU_EXPORT int
_PyArg_CheckPositional(const char *name, Py_ssize_t nargs, Py_ssize_t min, Py_ssize_t max)
{
    if (nargs < min) {
        if (name != NULL) {
            PyErr_Format(PyExc_TypeError, "%.200s expected %s%zd argument%s, got %zd", name,
                         min == max ? "" : "at least ", min, min == 1 ? "" : "s", nargs);
        }
        else {
            PyErr_Format(PyExc_TypeError, "unpacked tuple should have %s%zd element%s, but has %zd",
                         min == max ? "" : "at least ", min, min == 1 ? "" : "s", nargs);
        }
        return 0;
    }
    if (nargs == 0) {
        return 1;
    }
    if (nargs > max) {
        if (name != NULL) {
            PyErr_Format(PyExc_TypeError, "%.200s expected %s%zd argument%s, got %zd", name,
                         min == max ? "" : "at most ", max, max == 1 ? "" : "s", nargs);
        }
        else {
            PyErr_Format(PyExc_TypeError, "unpacked tuple should have %s%zd element%s, but has %zd",
                         min == max ? "" : "at most ", max, max == 1 ? "" : "s", nargs);
        }
        return 0;
    }
    return 1;
}

LU_EXPORT int
_PyArg_NoKeywords(const char *funcname, PyObject *kwargs)
{
    if (kwargs == NULL || ((PyDictObject *)kwargs)->ma_used == 0) {
        return 1;
    }
    PyErr_Format(PyExc_TypeError, "%.200s() takes no keyword arguments", funcname);
    return 0;
}

LU_EXPORT int
_PyArg_NoPositional(const char *funcname, PyObject *args)
{
    if (args == NULL || PyTuple_GET_SIZE(args) == 0) {
        return 1;
    }
    PyErr_Format(PyExc_TypeError, "%.200s() takes no positional arguments", funcname);
    return 0;
}

/* The keywords' count, and how many lead as positional-only (empty names). */
static int
scan_keywords(const char *const *keywords, int *ptotal, int *pposonly)
{
    int i;
    for (i = 0; keywords[i] && !*keywords[i]; i++) {
    }
    *pposonly = i;
    for (; keywords[i]; i++) {
        if (!*keywords[i]) {
            PyErr_SetString(PyExc_SystemError, "Empty keyword parameter name");
            return -1;
        }
    }
    *ptotal = i;
    return 0;
}

/* The interned names of the keyword-capable parameters, built on first use. */
static int
parser_init(struct _PyArg_Parser *parser)
{
    if (__atomic_load_n(&parser->once.v, __ATOMIC_ACQUIRE) == 4) {     /* _Py_ONCE_INITIALIZED */
        return 0;
    }
    PyMutex_Lock(&_PyRuntime.getargs_lock);
    int err = 0;
    if (parser->once.v != 4) {
        int len, pos;
        if (parser->format != NULL) {
            PyErr_SetString(PyExc_SystemError, "format-string argument parsers are not supported yet");
            err = -1;
        }
        else if (scan_keywords(parser->keywords, &len, &pos) < 0) {
            err = -1;
        }
        else {
            PyObject *kwtuple = parser->kwtuple;
            if (kwtuple == NULL) {
                kwtuple = PyTuple_New(len - pos);
                for (int i = pos; kwtuple != NULL && i < len; i++) {
                    PyObject *name = PyUnicode_InternFromString(parser->keywords[i]);
                    if (name == NULL) {
                        Py_CLEAR(kwtuple);
                        break;
                    }
                    PyTuple_SET_ITEM(kwtuple, i - pos, name);
                }
                parser->is_kwtuple_owned = 1;
            }
            if (kwtuple == NULL) {
                err = -1;
            }
            else {
                parser->pos = pos;
                parser->kwtuple = kwtuple;
                __atomic_store_n(&parser->once.v, 4, __ATOMIC_RELEASE);
            }
        }
    }
    PyMutex_Unlock(&_PyRuntime.getargs_lock);
    return err;
}

/* The value of keyword `key` among kwnames, borrowed from kwstack, or NULL. */
static PyObject *
find_keyword(PyObject *kwnames, PyObject *const *kwstack, PyObject *key)
{
    Py_ssize_t n = PyTuple_GET_SIZE(kwnames);
    for (Py_ssize_t i = 0; i < n; i++) {
        if (PyTuple_GET_ITEM(kwnames, i) == key) {
            return kwstack[i];
        }
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        if (lu_str_eq(PyTuple_GET_ITEM(kwnames, i), key)) {
            return kwstack[i];
        }
    }
    return NULL;
}

/* The value of keyword `key` in kwargs or kwnames, borrowed, or NULL. */
static PyObject *
keyword_value(PyObject *kwargs, PyObject *kwnames, PyObject *const *kwstack, PyObject *key)
{
    return kwargs != NULL ? lu_ns_lookup(kwargs, key) : find_keyword(kwnames, kwstack, key);
}

static void
error_unexpected_keyword_arg(PyObject *kwargs, PyObject *kwnames, PyObject *kwtuple, const char *fname)
{
    const char *name = fname == NULL ? "this function" : fname, *parens = fname == NULL ? "" : "()";
    Py_ssize_t j = 0;
    for (;;) {
        PyObject *keyword;
        if (kwargs != NULL) {
            if (!lu_ns_next(kwargs, &j, &keyword, NULL)) {
                break;
            }
        }
        else {
            if (j >= PyTuple_GET_SIZE(kwnames)) {
                break;
            }
            keyword = PyTuple_GET_ITEM(kwnames, j++);
        }
        if (!PyUnicode_Check(keyword)) {
            PyErr_SetString(PyExc_TypeError, "keywords must be strings");
            return;
        }
        int known = 0;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(kwtuple) && !known; i++) {
            known = lu_str_eq(PyTuple_GET_ITEM(kwtuple, i), keyword);
        }
        if (!known) {
            PyObject *suggestion = lu_suggest_keyword(&PyTuple_GET_ITEM(kwtuple, 0),
                                                      PyTuple_GET_SIZE(kwtuple), keyword);
            if (suggestion != NULL) {
                PyErr_Format(PyExc_TypeError, "%.200s%s got an unexpected keyword argument '%S'."
                             " Did you mean '%S'?", name, parens, keyword, suggestion);
                Py_DECREF(suggestion);
            }
            else {
                PyErr_Format(PyExc_TypeError, "%.200s%s got an unexpected keyword argument '%S'",
                             name, parens, keyword);
            }
            return;
        }
    }
    PyErr_Format(PyExc_TypeError, "invalid keyword argument for %.200s%s", name, parens);
}

LU_EXPORT PyObject *const *
_PyArg_UnpackKeywords(PyObject *const *args, Py_ssize_t nargs, PyObject *kwargs, PyObject *kwnames,
                      struct _PyArg_Parser *parser, int minpos, int maxpos, int minkw, int varpos,
                      PyObject **buf)
{
    int reqlimit = minkw ? maxpos + minkw : minpos;
    PyObject *const *kwstack = NULL;
    if (parser == NULL || (kwnames != NULL && !PyTuple_Check(kwnames))) {
        PyErr_BadInternalCall();
        return NULL;
    }
    if (args == NULL && nargs == 0) {
        args = buf;
    }
    if (parser_init(parser) < 0) {
        return NULL;
    }
    PyObject *kwtuple = parser->kwtuple;
    int posonly = parser->pos;
    int minposonly = Py_MIN(posonly, minpos);
    int maxargs = posonly + (int)PyTuple_GET_SIZE(kwtuple);
    const char *fname = parser->fname == NULL ? "function" : parser->fname;
    const char *parens = parser->fname == NULL ? "" : "()";

    Py_ssize_t nkwargs = 0;
    if (kwargs != NULL) {
        nkwargs = ((PyDictObject *)kwargs)->ma_used;
    }
    else if (kwnames != NULL) {
        nkwargs = PyTuple_GET_SIZE(kwnames);
        kwstack = args + nargs;
    }
    if (nkwargs == 0 && minkw == 0 && minpos <= nargs && (varpos || nargs <= maxpos)) {
        return args;
    }
    if (!varpos && nargs + nkwargs > maxargs) {
        PyErr_Format(PyExc_TypeError, "%.200s%s takes at most %d %sargument%s (%zd given)", fname,
                     parens, maxargs, nargs == 0 ? "keyword " : "", maxargs == 1 ? "" : "s",
                     nargs + nkwargs);
        return NULL;
    }
    if (!varpos && nargs > maxpos) {
        if (maxpos == 0) {
            PyErr_Format(PyExc_TypeError, "%.200s%s takes no positional arguments", fname, parens);
        }
        else {
            PyErr_Format(PyExc_TypeError, "%.200s%s takes %s %d positional argument%s (%zd given)",
                         fname, parens, minpos < maxpos ? "at most" : "exactly", maxpos,
                         maxpos == 1 ? "" : "s", nargs);
        }
        return NULL;
    }
    if (nargs < minposonly) {
        PyErr_Format(PyExc_TypeError, "%.200s%s takes %s %d positional argument%s (%zd given)", fname,
                     parens, varpos || minposonly < maxpos ? "at least" : "exactly", minposonly,
                     minposonly == 1 ? "" : "s", nargs);
        return NULL;
    }
    if (varpos) {
        nargs = Py_MIN(maxpos, nargs);
    }
    for (int i = 0; i < nargs; i++) {
        buf[i] = args[i];
    }
    /* The keywords, driven by the parameter names. */
    for (int i = Py_MAX((int)nargs, posonly); i < maxargs; i++) {
        PyObject *current = NULL;
        if (nkwargs) {
            current = keyword_value(kwargs, kwnames, kwstack, PyTuple_GET_ITEM(kwtuple, i - posonly));
        }
        else if (i >= reqlimit) {
            break;
        }
        buf[i] = current;
        if (current != NULL) {
            --nkwargs;
        }
        else if (i < minpos || (maxpos <= i && i < reqlimit)) {
            PyErr_Format(PyExc_TypeError, "%.200s%s missing required argument '%U' (pos %d)", fname,
                         parens, PyTuple_GET_ITEM(kwtuple, i - posonly), i + 1);
            return NULL;
        }
    }
    if (nkwargs > 0) {
        for (int i = posonly; i < nargs; i++) {
            PyObject *keyword = PyTuple_GET_ITEM(kwtuple, i - posonly);
            if (keyword_value(kwargs, kwnames, kwstack, keyword) != NULL) {
                PyErr_Format(PyExc_TypeError, "argument for %.200s%s given by name ('%U') and "
                             "position (%d)", fname, parens, keyword, i + 1);
                return NULL;
            }
        }
        error_unexpected_keyword_arg(kwargs, kwnames, kwtuple, parser->fname);
        return NULL;
    }
    return buf;
}
