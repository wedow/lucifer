/* Type, dict, function and code watchers through the public API, with the
   events and error messages of CPython 3.14 (Lib/test/test_capi/test_watchers.py).
   Preloaded into bin/lucifer, the constructor initializes the runtime, checks,
   prints "capi watchers ok" and exits. */

#include "Python.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "watchers.c:%d: check failed: %s\n", __LINE__, #cond); \
    failures++; } } while (0)

/* What the callbacks saw, one line per event. */
static char log_buf[4096];

static void
log_reset(void)
{
    log_buf[0] = 0;
}

static void
log_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
log_add(const char *fmt, ...)
{
    size_t n = strlen(log_buf);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_buf + n, sizeof log_buf - n, fmt, ap);
    va_end(ap);
}

#define CHECK_LOG(expected) do { \
    if (strcmp(log_buf, expected) != 0) { \
        fprintf(stderr, "watchers.c:%d: events [%s], expected [%s]\n", __LINE__, log_buf, expected); \
        failures++; } \
    log_reset(); } while (0)

/* A deferred-refcount object (CPython's free-threaded code and top-level
   functions) is destroyed when the collector gets to it, so its event may
   not have come. */
#define CHECK_LOG_OR_NONE(expected) do { \
    if (log_buf[0] != 0) { CHECK_LOG(expected); } } while (0)

#define CHECK_RAISES(call, exc, msg) do { \
    CHECK((call) < 0); \
    CHECK(PyErr_ExceptionMatches(exc)); \
    PyObject *e_ = PyErr_GetRaisedException(); \
    PyObject *args_ = e_ ? ((PyBaseExceptionObject *)e_)->args : NULL; \
    CHECK(args_ != NULL && PyTuple_GET_SIZE(args_) == 1 \
          && strcmp(PyUnicode_AsUTF8(PyTuple_GET_ITEM(args_, 0)), msg) == 0); \
    Py_XDECREF(e_); } while (0)

static PyObject *
new_class(const char *name, PyObject *base)
{
    PyObject *bases = PyTuple_Pack(1, base != NULL ? base : (PyObject *)&PyBaseObject_Type);
    PyObject *ns = PyDict_New();
    PyObject *n = PyUnicode_FromString(name);
    PyObject *cls = bases && ns && n
        ? PyObject_CallFunctionObjArgs((PyObject *)&PyType_Type, n, bases, ns, NULL) : NULL;
    Py_XDECREF(n);
    Py_XDECREF(ns);
    Py_XDECREF(bases);
    CHECK(cls != NULL);
    return cls;
}

static void
set_attr(PyObject *o, const char *name, PyObject *v)
{
    CHECK(PyObject_SetAttrString(o, name, v) == 0);
}

/* Type watchers. */

static PyObject *named[8];      /* the objects events name, by letter */
static const char *const names[8] = { "C", "D", "E", "F", "G", "H", "I", "J" };

static const char *
name_of(PyObject *o)
{
    if (o == NULL) {
        return "-";
    }
    for (int i = 0; i < 8; i++) {
        if (named[i] == o) {
            return names[i];
        }
    }
    return "?";
}

static int
type_cb(PyTypeObject *tp)
{
    log_add("type %s;", name_of((PyObject *)tp));
    return 0;
}

static int
type_cb_second(PyTypeObject *tp)
{
    log_add("second %s;", name_of((PyObject *)tp));
    return 0;
}

static void
check_type_watchers(void)
{
    PyObject *C = new_class("C", NULL), *D = new_class("D", C);
    named[0] = C;
    named[1] = D;
    int wid = PyType_AddWatcher(type_cb);
    CHECK(wid >= 1 && wid < 8);             /* id 0 is the interpreter's */

    /* A modification is announced once: the tag it clears is what armed it. */
    CHECK(PyType_Watch(wid, C) == 0);
    CHECK(((PyTypeObject *)C)->tp_version_tag != 0);
    PyObject *one = PyLong_FromLong(1);
    set_attr(C, "x", one);
    CHECK_LOG("type C;");
    CHECK(((PyTypeObject *)C)->tp_version_tag == 0);
    set_attr(C, "x", one);
    CHECK_LOG("");
    PyObject *got = PyObject_GetAttrString(C, "x");     /* a lookup assigns a tag */
    Py_XDECREF(got);
    CHECK(((PyTypeObject *)C)->tp_version_tag != 0);
    set_attr(C, "x", one);
    CHECK_LOG("type C;");

    /* A subclass is invalidated, and told, when its base changes. */
    CHECK(PyType_Watch(wid, D) == 0);
    unsigned int base_tag = ((PyTypeObject *)C)->tp_version_tag;
    CHECK(base_tag != 0 && ((PyTypeObject *)D)->tp_version_tag > base_tag);
    set_attr(C, "y", one);
    CHECK_LOG("type D;type C;");         /* subclasses first */
    CHECK(((PyTypeObject *)D)->tp_version_tag == 0 && ((PyTypeObject *)C)->tp_version_tag == 0);

    /* A change to the subclass leaves the base alone. */
    CHECK(PyUnstable_Type_AssignVersionTag((PyTypeObject *)D) == 1);
    CHECK(((PyTypeObject *)C)->tp_version_tag != 0);
    set_attr(D, "z", one);
    CHECK_LOG("type D;");
    CHECK(((PyTypeObject *)C)->tp_version_tag != 0);

    /* A second watcher, then one fewer. */
    int wid2 = PyType_AddWatcher(type_cb_second);
    CHECK(wid2 == wid + 1);
    CHECK(PyType_Watch(wid2, C) == 0 && PyType_Watch(wid, C) == 0);
    CHECK(PyUnstable_Type_AssignVersionTag((PyTypeObject *)D) == 1);
    set_attr(C, "w", one);
    CHECK_LOG("type D;type C;second C;");
    CHECK(PyUnstable_Type_AssignVersionTag((PyTypeObject *)D) == 1);
    CHECK(PyType_Unwatch(wid, C) == 0);
    set_attr(C, "v", one);
    CHECK_LOG("type D;second C;");
    CHECK(PyType_Unwatch(wid2, C) == 0);
    CHECK(PyType_Unwatch(wid, D) == 0);
    CHECK(PyType_ClearWatcher(wid2) == 0);
    CHECK(PyType_Watch(wid2, C) < 0);
    PyErr_Clear();

    /* Errors. */
    CHECK_RAISES(PyType_Watch(wid, one), PyExc_ValueError, "Cannot watch non-type");
    CHECK_RAISES(PyType_Watch(8, C), PyExc_ValueError, "Invalid type watcher ID 8");
    CHECK_RAISES(PyType_Watch(-1, C), PyExc_ValueError, "Invalid type watcher ID -1");
    CHECK_RAISES(PyType_Watch(0, C), PyExc_ValueError, "No type watcher set for ID 0");
    CHECK_RAISES(PyType_ClearWatcher(wid2), PyExc_ValueError, "No type watcher set for ID 2");
    CHECK_RAISES(PyType_Unwatch(9, C), PyExc_ValueError, "Invalid type watcher ID 9");

    /* The ids run out after seven. */
    int ids[8], n = 0;
    while ((ids[n] = PyType_AddWatcher(type_cb)) >= 0 && n < 8) {
        n++;
    }
    CHECK(n == 6);                          /* wid is still held */
    CHECK_RAISES(ids[n], PyExc_RuntimeError, "no more type watcher IDs available");
    for (int i = 0; i < n; i++) {
        CHECK(PyType_ClearWatcher(ids[i]) == 0);
    }
    CHECK(PyType_ClearWatcher(wid) == 0);
    named[0] = named[1] = NULL;
    Py_DECREF(one);
    Py_DECREF(D);
    Py_DECREF(C);
}

/* Version tags: a modified type gets a new tag from a lookup, a type has 1000
   in its life, and then none (MAX_VERSIONS_PER_CLASS). */
static void
check_version_tags(void)
{
    PyObject *C = new_class("C", NULL);
    PyTypeObject *tp = (PyTypeObject *)C;
    PyObject *one = PyLong_FromLong(1);
    int assigned = 0;
    unsigned int last = 0;
    for (int i = 0; i < 2000; i++) {
        PyType_Modified(tp);
        CHECK(tp->tp_version_tag == 0);
        if (PyUnstable_Type_AssignVersionTag(tp)) {
            CHECK(tp->tp_version_tag > last);
            last = tp->tp_version_tag;
            assigned++;
        }
        else {
            CHECK(tp->tp_version_tag == 0);
        }
    }
    CHECK(assigned > 900 && assigned <= 1000);
    CHECK(tp->tp_versions_used == 1000);
    /* A lookup cannot assign one either. */
    PyObject *got = PyObject_GetAttrString(C, "__doc__");
    Py_XDECREF(got);
    CHECK(tp->tp_version_tag == 0);
    set_attr(C, "x", one);                  /* still works, uncached */
    got = PyObject_GetAttrString(C, "x");
    CHECK(got == one);
    Py_XDECREF(got);
    Py_DECREF(one);
    Py_DECREF(C);
}

/* Dict watchers. */

static int
dict_cb(PyDict_WatchEvent event, PyObject *dict, PyObject *key, PyObject *value)
{
    static const char *kind[] = { "added", "modified", "deleted", "cloned", "cleared", "dealloc" };
    log_add("%s %s %s %s;", kind[event], name_of(dict), key && PyLong_Check(key) ? "k" : name_of(key),
            value && PyLong_Check(value) ? "v" : name_of(value));
    return 0;
}

static int
dict_cb_second(PyDict_WatchEvent event, PyObject *dict, PyObject *key, PyObject *value)
{
    log_add("second ");
    return dict_cb(event, dict, key, value);
}

static void
check_dict_watchers(void)
{
    int wid = PyDict_AddWatcher(dict_cb);
    CHECK(wid >= 2 && wid < 8);             /* ids 0 and 1 are the interpreter's */

    PyObject *d = PyDict_New(), *other = PyDict_New();
    PyObject *k = PyLong_FromLong(1), *v = PyLong_FromLong(2), *v2 = PyLong_FromLong(3);
    named[0] = d;
    named[1] = other;
    CHECK(PyDict_Watch(wid, d) == 0);
    CHECK(PyDict_SetItem(d, k, v) == 0);
    CHECK_LOG("added C k v;");
    CHECK(PyDict_SetItem(d, k, v2) == 0);
    CHECK_LOG("modified C k v;");
    CHECK(PyDict_SetItem(d, k, v2) == 0);           /* the same value: no change, no event */
    CHECK_LOG("");
    CHECK(PyDict_DelItem(d, k) == 0);
    CHECK_LOG("deleted C k -;");
    CHECK(PyDict_DelItem(d, k) < 0);
    PyErr_Clear();
    CHECK_LOG("");
    CHECK(PyDict_SetItem(d, k, v) == 0);
    log_reset();
    PyDict_Clear(d);
    CHECK_LOG("cleared C - -;");
    PyDict_Clear(d);                                 /* already empty */
    CHECK_LOG("");

    /* An empty watched dict updated from a clean dict takes its table whole. */
    CHECK(PyDict_SetItem(other, k, v) == 0);
    PyObject *update = PyObject_GetAttrString(d, "update");
    PyObject *r = update ? PyObject_CallOneArg(update, other) : NULL;
    CHECK(r != NULL);
    Py_XDECREF(r);
    Py_XDECREF(update);
    CHECK_LOG("cloned C D -;");

    /* Two watchers, in id order; an unwatched dict is silent. */
    int wid2 = PyDict_AddWatcher(dict_cb_second);
    CHECK(wid2 == wid + 1);
    CHECK(PyDict_Watch(wid2, d) == 0);
    CHECK(PyDict_SetItem(d, v, v2) == 0);
    CHECK_LOG("added C k v;second added C k v;");
    CHECK(PyDict_Unwatch(wid, d) == 0);
    CHECK(PyDict_SetItem(d, v2, v) == 0);
    CHECK_LOG("second added C k v;");
    CHECK(PyDict_Unwatch(wid2, d) == 0);
    CHECK(PyDict_SetItem(d, k, k) == 0);
    CHECK_LOG("");

    /* Death is an event. */
    CHECK(PyDict_Watch(wid, d) == 0);
    named[0] = d;
    Py_DECREF(d);
    CHECK_LOG("dealloc C - -;");

    /* Errors. */
    CHECK_RAISES(PyDict_Watch(wid, k), PyExc_ValueError, "Cannot watch non-dictionary");
    CHECK_RAISES(PyDict_Watch(8, other), PyExc_ValueError, "Invalid dict watcher ID 8");
    CHECK(PyDict_Watch(0, other) == 0);     /* the interpreter's own id is in use */
    CHECK(PyDict_Unwatch(0, other) == 0);
    CHECK_RAISES(PyDict_Watch(1, other), PyExc_ValueError, "No dict watcher set for ID 1");
    CHECK_RAISES(PyDict_Unwatch(-1, other), PyExc_ValueError, "Invalid dict watcher ID -1");
    CHECK(PyDict_ClearWatcher(wid2) == 0);
    CHECK_RAISES(PyDict_ClearWatcher(wid2), PyExc_ValueError, "No dict watcher set for ID 3");
    CHECK_RAISES(PyDict_ClearWatcher(99), PyExc_ValueError, "Invalid dict watcher ID 99");

    int ids[8], n = 0;
    while ((ids[n] = PyDict_AddWatcher(dict_cb)) >= 0 && n < 8) {
        n++;
    }
    CHECK(n == 5);                          /* ids 2..7, wid held */
    CHECK_RAISES(ids[n], PyExc_RuntimeError, "no more dict watcher IDs available");
    for (int i = 0; i < n; i++) {
        CHECK(PyDict_ClearWatcher(ids[i]) == 0);
    }
    CHECK(PyDict_ClearWatcher(wid) == 0);
    named[0] = named[1] = NULL;
    Py_DECREF(other);
    Py_DECREF(k);
    Py_DECREF(v);
    Py_DECREF(v2);
}

/* Code and function watchers. */

static int
code_cb(PyCodeEvent event, PyCodeObject *co)
{
    log_add("%s %s;", event == PY_CODE_EVENT_CREATE ? "create" : "destroy",
            PyUnicode_AsUTF8(co->co_name));
    return 0;
}

static void
check_code_watchers(void)
{
    int wid = PyCode_AddWatcher(code_cb);
    CHECK(wid == 0);                        /* code watchers start at 0 */
    PyCodeObject *co = PyCode_NewEmpty("f.py", "fn", 7);
    CHECK(co != NULL);
    CHECK_LOG("create fn;");
    CHECK(co->co_version >= 2);
    PyCodeObject *co2 = PyCode_NewEmpty("f.py", "gn", 8);
    CHECK(co2 != NULL && co2->co_version > co->co_version);
    log_reset();
    Py_DECREF(co);
    CHECK_LOG_OR_NONE("destroy fn;");
    CHECK(PyCode_ClearWatcher(wid) == 0);
    Py_DECREF(co2);
    CHECK_LOG("");                          /* no watcher left */
    CHECK_RAISES(PyCode_ClearWatcher(wid), PyExc_ValueError, "No code watcher set for ID 0");
    CHECK_RAISES(PyCode_ClearWatcher(8), PyExc_ValueError, "Invalid code watcher ID 8");
    int ids[9], n = 0;
    while ((ids[n] = PyCode_AddWatcher(code_cb)) >= 0 && n < 9) {
        n++;
    }
    CHECK(n == 8);
    CHECK_RAISES(ids[n], PyExc_RuntimeError, "no more code watcher IDs available");
    for (int i = 0; i < n; i++) {
        CHECK(PyCode_ClearWatcher(ids[i]) == 0);
    }
}

static int
func_cb(PyFunction_WatchEvent event, PyFunctionObject *func, PyObject *new_value)
{
    static const char *kind[] = { "create", "destroy", "code", "defaults", "kwdefaults" };
    const char *what = new_value == NULL ? "-" : PyTuple_Check(new_value) ? "tuple"
        : PyDict_Check(new_value) ? "dict" : PyCode_Check(new_value) ? "code" : "?";
    log_add("%s %s %s;", kind[event], PyUnicode_AsUTF8(func->func_name), what);
    return 0;
}

static void
check_func_watchers(void)
{
    int wid = PyFunction_AddWatcher(func_cb);
    CHECK(wid == 0);                        /* function watchers start at 0 */
    PyCodeObject *co = PyCode_NewEmpty("f.py", "fn", 7);
    PyCodeObject *co2 = PyCode_NewEmpty("f.py", "gn", 8);
    PyObject *globals = PyDict_New();
    PyObject *f = PyFunction_New((PyObject *)co, globals);
    CHECK(f != NULL);
    CHECK_LOG("create fn -;");
    CHECK(((PyFunctionObject *)f)->func_version == 0);       /* only MAKE_FUNCTION sets one */

    PyObject *defaults = PyTuple_Pack(1, Py_None);
    CHECK(PyFunction_SetDefaults(f, defaults) == 0);
    CHECK_LOG("defaults fn tuple;");
    CHECK(PyFunction_SetDefaults(f, Py_None) == 0);
    CHECK_LOG("defaults fn -;");
    set_attr(f, "__defaults__", defaults);
    CHECK_LOG("defaults fn tuple;");
    PyObject *kw = PyDict_New();
    CHECK(PyFunction_SetKwDefaults(f, kw) == 0);
    CHECK_LOG("kwdefaults fn dict;");
    set_attr(f, "__kwdefaults__", Py_None);
    CHECK_LOG("kwdefaults fn -;");
    set_attr(f, "__code__", (PyObject *)co2);
    CHECK_LOG("code fn code;");
    CHECK(PyFunction_GetCode(f) == (PyObject *)co2);

    /* The event comes before the change, so a refused change makes none. */
    CHECK(PyObject_SetAttrString(f, "__code__", Py_None) < 0);
    CHECK(PyErr_ExceptionMatches(PyExc_TypeError));
    PyErr_Clear();
    CHECK(PyObject_SetAttrString(f, "__defaults__", Py_True) < 0);
    CHECK(PyErr_ExceptionMatches(PyExc_TypeError));
    PyErr_Clear();
    CHECK(PyFunction_SetDefaults(f, kw) < 0);
    CHECK(PyErr_ExceptionMatches(PyExc_SystemError));
    PyErr_Clear();
    CHECK_LOG("");

    Py_DECREF(f);
    CHECK_LOG_OR_NONE("destroy fn -;");
    CHECK(PyFunction_ClearWatcher(wid) == 0);
    CHECK_RAISES(PyFunction_ClearWatcher(wid), PyExc_ValueError, "no func watcher set for ID 0");
    CHECK_RAISES(PyFunction_ClearWatcher(8), PyExc_ValueError, "invalid func watcher ID 8");
    int ids[9], n = 0;
    while ((ids[n] = PyFunction_AddWatcher(func_cb)) >= 0 && n < 9) {
        n++;
    }
    CHECK(n == 8);
    CHECK_RAISES(ids[n], PyExc_RuntimeError, "no more func watcher IDs available");
    for (int i = 0; i < n; i++) {
        CHECK(PyFunction_ClearWatcher(ids[i]) == 0);
    }
    Py_DECREF(defaults);
    Py_DECREF(kw);
    Py_DECREF(globals);
    Py_DECREF(co);
    Py_DECREF(co2);
}

static struct PyModuleDef watchers_module = {
    PyModuleDef_HEAD_INIT, .m_name = "watchers",
};

PyMODINIT_FUNC
PyInit_watchers(void)
{
    return PyModuleDef_Init(&watchers_module);
}

__attribute__((constructor)) static void
check(void)
{
    Py_InitializeEx(0);
    check_type_watchers();
    check_version_tags();
    check_dict_watchers();
    check_code_watchers();
    check_func_watchers();
    if (failures == 0) {
        puts("capi watchers ok");
    }
    fflush(stdout);
    _exit(failures != 0);
}
