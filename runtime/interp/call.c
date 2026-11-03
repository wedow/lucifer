/* The calling convention (Python/ceval.c initialize_locals and its error
   reporters, Python/suggestions.c, Python/ceval.c _PyEval_FormatKwargsError,
   Objects/object.c _PyObject_FunctionStr): binding positional, keyword,
   default, keyword-only, *args and **kwargs arguments to a pushed frame, with
   CPython's TypeError texts, and unpacking CALL_FUNCTION_EX operands. */

#include "interp/frame.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* error texts */

struct text {
    char buf[512];
    size_t n;
};

static void
text_add(struct text *t, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void
text_add(struct text *t, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(t->buf + t->n, sizeof t->buf - t->n, format, ap);
    va_end(ap);
    t->n = t->n + n < sizeof t->buf ? t->n + n : sizeof t->buf - 1;
}

static const char *
utf8(PyObject *s)
{
    return PyUnicode_AsUTF8(s);
}

/* _PyObject_FunctionStr: "module.qualname()", without the module for builtins. */
const char *
lu_function_str(PyObject *func, char *buf, size_t size)
{
    PyObject *qualname = NULL, *module = NULL;
    if (PyFunction_Check(func)) {
        qualname = ((PyFunctionObject *)func)->func_qualname;
        module = ((PyFunctionObject *)func)->func_module;
    }
    else if (PyCFunction_Check(func)) {
        PyCFunctionObject *c = (PyCFunctionObject *)func;
        snprintf(buf, size, "%s()", c->m_ml->ml_name);
        return buf;
    }
    if (qualname == NULL) {
        snprintf(buf, size, "<%s object>", Py_TYPE(func)->tp_name);
    }
    else if (module != NULL && PyUnicode_CheckExact(module)
             && strcmp(utf8(module), "builtins") != 0) {
        snprintf(buf, size, "%s.%s()", utf8(module), utf8(qualname));
    }
    else {
        snprintf(buf, size, "%s()", utf8(qualname));
    }
    return buf;
}

/* "'a'", "'a' and 'b'", "'a', 'b', and 'c'" over the names flagged in the
   frame's locals (format_missing, missing_arguments). */
static void
missing_arguments(PyFunctionObject *func, struct lu_frame *f, Py_ssize_t count, Py_ssize_t defcount)
{
    PyCodeObject *co = f->code;
    int positional = defcount != -1;
    Py_ssize_t start = positional ? 0 : co->co_argcount;
    Py_ssize_t end = positional ? co->co_argcount - defcount : start + co->co_kwonlyargcount;
    struct text names = { .n = 0 };
    Py_ssize_t seen = 0;
    for (Py_ssize_t i = start; i < end; i++) {
        if (f->localsplus[i] != NULL) {
            continue;
        }
        seen++;
        const char *sep = seen == 1 ? "" : count == 2 ? " and " : seen == count ? ", and " : ", ";
        text_add(&names, "%s'%s'", sep, utf8(PyTuple_GET_ITEM(co->co_localsplusnames, i)));
    }
    lu_err_format(PyExc_TypeError, "%s() missing %zd required %s argument%s: %s",
                  utf8(func->func_qualname), count, positional ? "positional" : "keyword-only",
                  count == 1 ? "" : "s", names.buf);
}

static void
too_many_positional(PyFunctionObject *func, struct lu_frame *f, Py_ssize_t given)
{
    PyCodeObject *co = f->code;
    Py_ssize_t kwonly_given = 0;
    for (Py_ssize_t i = co->co_argcount; i < co->co_argcount + co->co_kwonlyargcount; i++) {
        kwonly_given += f->localsplus[i] != NULL;
    }
    Py_ssize_t defcount = func->func_defaults == NULL ? 0 : PyTuple_GET_SIZE(func->func_defaults);
    char sig[64];
    int plural;
    if (defcount) {
        plural = 1;
        snprintf(sig, sizeof sig, "from %zd to %d", co->co_argcount - defcount, co->co_argcount);
    }
    else {
        plural = co->co_argcount != 1;
        snprintf(sig, sizeof sig, "%d", co->co_argcount);
    }
    char kwonly[96] = "";
    if (kwonly_given) {
        snprintf(kwonly, sizeof kwonly, " positional argument%s (and %zd keyword-only argument%s)",
                 given != 1 ? "s" : "", kwonly_given, kwonly_given != 1 ? "s" : "");
    }
    lu_err_format(PyExc_TypeError, "%s() takes %s positional argument%s but %zd%s %s given",
                  utf8(func->func_qualname), sig, plural ? "s" : "", given, kwonly,
                  given == 1 && !kwonly_given ? "was" : "were");
}

/* Returns 1 with the TypeError set when a keyword names a positional-only parameter. */
static int
positional_only_passed_as_keyword(PyFunctionObject *func, PyCodeObject *co, PyObject *kwnames)
{
    struct text names = { .n = 0 };
    int conflicts = 0;
    for (int k = 0; k < co->co_posonlyargcount; k++) {
        PyObject *posonly = PyTuple_GET_ITEM(co->co_localsplusnames, k);
        for (Py_ssize_t k2 = 0; k2 < PyTuple_GET_SIZE(kwnames); k2++) {
            PyObject *kwname = PyTuple_GET_ITEM(kwnames, k2);
            if (kwname == posonly || lu_str_eq(kwname, posonly)) {
                text_add(&names, conflicts++ ? ", %s" : "%s", utf8(kwname));
            }
        }
    }
    if (conflicts) {
        lu_err_format(PyExc_TypeError,
                      "%s() got some positional-only arguments passed as keyword arguments: '%s'",
                      utf8(func->func_qualname), names.buf);
    }
    return conflicts;
}

/* _Py_CalculateSuggestions: the Levenshtein-closest parameter name, or NULL. */

#define MAX_CANDIDATE_ITEMS 750
#define MAX_STRING_SIZE 40
#define MOVE_COST 2
#define CASE_COST 1

static size_t
substitution_cost(char a, char b)
{
    if ((a & 31) != (b & 31)) {
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

static size_t
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
    while (a_size && b_size && a[a_size - 1] == b[b_size - 1]) {
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
        size_t ts = a_size; a_size = b_size; b_size = ts;
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
            size_t insert_delete = (result < distance ? result : distance) + MOVE_COST;
            result = insert_delete < substitute ? insert_delete : substitute;
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

static PyObject *
suggest_keyword(PyObject *const *candidates, Py_ssize_t count, PyObject *name)
{
    if (count >= MAX_CANDIDATE_ITEMS) {
        return NULL;
    }
    size_t buffer[MAX_STRING_SIZE];
    const char *name_str = utf8(name);
    size_t name_size = strlen(name_str);
    PyObject *suggestion = NULL;
    size_t suggestion_distance = SIZE_MAX;
    for (Py_ssize_t i = 0; i < count; i++) {
        if (lu_str_eq(name, candidates[i])) {
            continue;
        }
        const char *item = utf8(candidates[i]);
        size_t item_size = strlen(item);
        size_t max_distance = (name_size + item_size + 3) * MOVE_COST / 6;
        if (suggestion_distance != SIZE_MAX && max_distance > suggestion_distance - 1) {
            max_distance = suggestion_distance - 1;
        }
        size_t d = levenshtein_distance(name_str, name_size, item, item_size, max_distance, buffer);
        if (d <= max_distance && (suggestion == NULL || d < suggestion_distance)) {
            suggestion = candidates[i];
            suggestion_distance = d;
        }
    }
    return suggestion;
}

static void
unexpected_keyword(PyFunctionObject *func, PyCodeObject *co, PyObject *keyword)
{
    int total_args = co->co_argcount + co->co_kwonlyargcount;
    PyObject *const *names = &PyTuple_GET_ITEM(co->co_localsplusnames, 0);
    PyObject *suggestion = suggest_keyword(names + co->co_posonlyargcount,
                                           total_args - co->co_posonlyargcount, keyword);
    if (suggestion != NULL) {
        lu_err_format(PyExc_TypeError,
                      "%s() got an unexpected keyword argument '%s'. Did you mean '%s'?",
                      utf8(func->func_qualname), utf8(keyword), utf8(suggestion));
    }
    else {
        lu_err_format(PyExc_TypeError, "%s() got an unexpected keyword argument '%s'",
                      utf8(func->func_qualname), utf8(keyword));
    }
}

/* initialize_locals */

static int
bind(struct lu_frame *f, PyObject **args, Py_ssize_t argcount, PyObject *kwnames)
{
    PyFunctionObject *func = (PyFunctionObject *)f->func;
    PyCodeObject *co = f->code;
    PyObject **locals = f->localsplus;
    const Py_ssize_t total_args = co->co_argcount + co->co_kwonlyargcount;
    Py_ssize_t kwcount = kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0;
    PyObject *kwdict = NULL;
    Py_ssize_t i, j;

    if (co->co_flags & CO_VARKEYWORDS) {
        kwdict = PyDict_New();
        if (kwdict == NULL) {
            for (j = 0; j < argcount + kwcount; j++) {
                Py_DECREF(args[j]);
            }
            return -1;
        }
        locals[total_args + ((co->co_flags & CO_VARARGS) != 0)] = kwdict;
    }

    Py_ssize_t n = argcount > co->co_argcount ? co->co_argcount : argcount;
    for (j = 0; j < n; j++) {
        locals[j] = args[j];
    }

    if (co->co_flags & CO_VARARGS) {
        PyObject *rest = PyTuple_New(argcount - n);
        if (rest == NULL) {
            for (j = n; j < argcount + kwcount; j++) {
                Py_DECREF(args[j]);
            }
            return -1;
        }
        for (j = n; j < argcount; j++) {
            PyTuple_SET_ITEM(rest, j - n, args[j]);
        }
        locals[total_args] = rest;
    }
    else {
        for (j = n; j < argcount; j++) {
            Py_DECREF(args[j]);
        }
    }

    for (i = 0; i < kwcount; i++) {
        PyObject *keyword = PyTuple_GET_ITEM(kwnames, i);
        PyObject *value = args[i + argcount];
        PyObject **varnames = &PyTuple_GET_ITEM(co->co_localsplusnames, 0);

        if (!PyUnicode_Check(keyword)) {
            lu_err_format(PyExc_TypeError, "%s() keywords must be strings",
                          utf8(func->func_qualname));
            goto kw_fail;
        }
        for (j = co->co_posonlyargcount; j < total_args; j++) {
            if (varnames[j] == keyword || lu_str_eq(varnames[j], keyword)) {
                break;
            }
        }
        if (j < total_args) {
            if (locals[j] != NULL) {
                lu_err_format(PyExc_TypeError, "%s() got multiple values for argument '%s'",
                              utf8(func->func_qualname), utf8(keyword));
                goto kw_fail;
            }
            locals[j] = value;
            continue;
        }
        if (kwdict == NULL) {
            if (co->co_posonlyargcount
                && positional_only_passed_as_keyword(func, co, kwnames)) {
                goto kw_fail;
            }
            unexpected_keyword(func, co, keyword);
            goto kw_fail;
        }
        if (lu_ns_insert(kwdict, keyword, value) < 0) {
            goto kw_fail;
        }
        Py_DECREF(value);
        continue;

    kw_fail:
        for (; i < kwcount; i++) {
            Py_DECREF(args[i + argcount]);
        }
        return -1;
    }

    if (argcount > co->co_argcount && !(co->co_flags & CO_VARARGS)) {
        too_many_positional(func, f, argcount);
        return -1;
    }

    if (argcount < co->co_argcount) {
        Py_ssize_t defcount = func->func_defaults == NULL ? 0
                                                          : PyTuple_GET_SIZE(func->func_defaults);
        Py_ssize_t m = co->co_argcount - defcount;
        Py_ssize_t missing = 0;
        for (i = argcount; i < m; i++) {
            missing += locals[i] == NULL;
        }
        if (missing) {
            missing_arguments(func, f, missing, defcount);
            return -1;
        }
        for (i = n > m ? n - m : 0; i < defcount; i++) {
            if (locals[m + i] == NULL) {
                locals[m + i] = Py_NewRef(PyTuple_GET_ITEM(func->func_defaults, i));
            }
        }
    }

    if (co->co_kwonlyargcount > 0) {
        Py_ssize_t missing = 0;
        for (i = co->co_argcount; i < total_args; i++) {
            if (locals[i] != NULL) {
                continue;
            }
            PyObject *def = func->func_kwdefaults != NULL
                ? lu_ns_lookup(func->func_kwdefaults, PyTuple_GET_ITEM(co->co_localsplusnames, i))
                : NULL;
            if (def != NULL) {
                locals[i] = Py_NewRef(def);
            }
            else {
                missing++;
            }
        }
        if (missing) {
            missing_arguments(func, f, missing, -1);
            return -1;
        }
    }
    return 0;
}

struct lu_frame *
lu_frame_call(PyThreadState *ts, PyFunctionObject *func, PyObject **args, Py_ssize_t nargs,
              PyObject *kwnames)
{
    struct lu_frame *f = lu_frame_push(ts, func, NULL);
    if (f == NULL) {
        for (Py_ssize_t i = 0; i < nargs + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0); i++) {
            Py_DECREF(args[i]);
        }
        return NULL;
    }
    if (bind(f, args, nargs, kwnames) < 0) {
        lu_frame_pop(ts, f);
        return NULL;
    }
    return f;
}

/* CALL_FUNCTION_EX */

/* The operands of f(*callargs, **kwargs) as a vectorcall array of owned
   references: positional items, then one value per keyword. *kwnames is a
   new tuple of the keys, or NULL. The array is PyMem memory with a spare
   word before it, where a call can prepend self; the caller frees it with
   PyMem_Free(array - 1) after handing the references on. */
PyObject **
lu_unpack_call(PyObject *callargs, PyObject *kwargs, Py_ssize_t *nargs, PyObject **kwnames)
{
    Py_ssize_t npos = PyTuple_GET_SIZE(callargs);
    Py_ssize_t nkw = kwargs != NULL ? ((PyDictObject *)kwargs)->ma_used : 0;
    PyObject **array = PyMem_Malloc((npos + nkw + 1) * sizeof(PyObject *));
    *kwnames = nkw ? PyTuple_New(nkw) : NULL;
    if (array == NULL || (nkw && *kwnames == NULL)) {
        PyMem_Free(array);
        Py_XDECREF(*kwnames);
        PyErr_NoMemory();
        return NULL;
    }
    array[0] = NULL;
    array++;
    for (Py_ssize_t i = 0; i < npos; i++) {
        array[i] = Py_NewRef(PyTuple_GET_ITEM(callargs, i));
    }
    Py_ssize_t pos = 0, i = 0;
    PyObject *key, *value;
    while (nkw && lu_ns_next(kwargs, &pos, &key, &value)) {
        PyTuple_SET_ITEM(*kwnames, i, Py_NewRef(key));
        array[npos + i++] = Py_NewRef(value);
    }
    *nargs = npos;
    return array;
}

/* _Py_Check_ArgsIterable and PySequence_Tuple, over tuples and lists until
   iteration exists (K1h). Returns a new tuple. */
PyObject *
lu_args_as_tuple(PyObject *func, PyObject *args)
{
    if (PyTuple_CheckExact(args)) {
        return Py_NewRef(args);
    }
    if (PyList_Check(args)) {
        Py_ssize_t n = PyList_GET_SIZE(args);
        PyObject *t = PyTuple_New(n);
        for (Py_ssize_t i = 0; t != NULL && i < n; i++) {
            PyTuple_SET_ITEM(t, i, Py_NewRef(PyList_GET_ITEM(args, i)));
        }
        return t;
    }
    char buf[256];
    lu_err_format(PyExc_TypeError, "%s argument after * must be an iterable, not %.200s",
                  lu_function_str(func, buf, sizeof buf), Py_TYPE(args)->tp_name);
    return NULL;
}

/* DICT_MERGE: update into dict, rejecting duplicate keys (_PyDict_MergeEx with
   override 2), with _PyEval_FormatKwargsError's texts. */
int
lu_dict_merge(PyObject *func, PyObject *dict, PyObject *update)
{
    char buf[256];
    if (!PyDict_CheckExact(update)) {
        lu_err_format(PyExc_TypeError, "%s argument after ** must be a mapping, not %.200s",
                      lu_function_str(func, buf, sizeof buf), Py_TYPE(update)->tp_name);
        return -1;
    }
    Py_ssize_t pos = 0;
    PyObject *key, *value;
    while (lu_ns_next(update, &pos, &key, &value)) {
        if (lu_ns_lookup(dict, key) != NULL) {
            lu_err_format(PyExc_TypeError, "%s got multiple values for keyword argument '%s'",
                          lu_function_str(func, buf, sizeof buf), utf8(key));
            return -1;
        }
        if (lu_ns_insert(dict, key, value) < 0) {
            return -1;
        }
    }
    return 0;
}
