/* Argument formats, int conversions, dict lookups, types from specs and
   single-phase initialization, as an unmodified extension module
   (tests/capi/capi_conv_run.py drives it). */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stddef.h>
#include <string.h>

/* parse(fmt, args): PyArg_ParseTuple(args, fmt) for a format of one unit, the converted value. */
static PyObject *
conv_parse(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    const char *fmt = PyUnicode_AsUTF8(args[0]);
    PyObject *a = args[1];
    if (fmt == NULL) {
        return NULL;
    }
    union { unsigned char b; short h; unsigned short H; int i; unsigned int I; long l;
            unsigned long k; long long L; unsigned long long K; Py_ssize_t n; double d; float f; } u;
    switch (fmt[0]) {
    case 'b': return PyArg_ParseTuple(a, fmt, &u.b) ? PyLong_FromLong(u.b) : NULL;
    case 'B': return PyArg_ParseTuple(a, fmt, &u.b) ? PyLong_FromLong(u.b) : NULL;
    case 'h': return PyArg_ParseTuple(a, fmt, &u.h) ? PyLong_FromLong(u.h) : NULL;
    case 'H': return PyArg_ParseTuple(a, fmt, &u.H) ? PyLong_FromLong(u.H) : NULL;
    case 'i': return PyArg_ParseTuple(a, fmt, &u.i) ? PyLong_FromLong(u.i) : NULL;
    case 'I': return PyArg_ParseTuple(a, fmt, &u.I) ? PyLong_FromUnsignedLong(u.I) : NULL;
    case 'l': return PyArg_ParseTuple(a, fmt, &u.l) ? PyLong_FromLong(u.l) : NULL;
    case 'k': return PyArg_ParseTuple(a, fmt, &u.k) ? PyLong_FromUnsignedLong(u.k) : NULL;
    case 'L': return PyArg_ParseTuple(a, fmt, &u.L) ? PyLong_FromLongLong(u.L) : NULL;
    case 'K': return PyArg_ParseTuple(a, fmt, &u.K) ? PyLong_FromUnsignedLongLong(u.K) : NULL;
    case 'n': return PyArg_ParseTuple(a, fmt, &u.n) ? PyLong_FromSsize_t(u.n) : NULL;
    case 'd': return PyArg_ParseTuple(a, fmt, &u.d) ? PyFloat_FromDouble(u.d) : NULL;
    case 'f': return PyArg_ParseTuple(a, fmt, &u.f) ? PyFloat_FromDouble(u.f) : NULL;
    default:
        PyErr_SetString(PyExc_ValueError, "unit");
        return NULL;
    }
}

/* to_c(kind, x): the C conversion `kind` of x, back as an int. */
static PyObject *
conv_to_c(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    const char *kind = PyUnicode_AsUTF8(args[0]);
    PyObject *x = args[1];
    if (kind == NULL) {
        return NULL;
    }
    if (!strcmp(kind, "ulonglong")) {
        unsigned long long v = PyLong_AsUnsignedLongLong(x);
        return v == (unsigned long long)-1 && PyErr_Occurred() ? NULL : PyLong_FromUnsignedLongLong(v);
    }
    if (!strcmp(kind, "size_t")) {
        size_t v = PyLong_AsSize_t(x);
        return v == (size_t)-1 && PyErr_Occurred() ? NULL : PyLong_FromSize_t(v);
    }
    if (!strcmp(kind, "uint32")) {
        uint32_t v;
        return PyLong_AsUInt32(x, &v) < 0 ? NULL : PyLong_FromUInt32(v);
    }
    if (!strcmp(kind, "double")) {
        double v = PyLong_AsDouble(x);
        return v == -1.0 && PyErr_Occurred() ? NULL : PyFloat_FromDouble(v);
    }
    PyErr_SetString(PyExc_ValueError, "kind");
    return NULL;
}

/* dict_get(d, key): PyDict_GetItem, PyDict_GetItemWithError and PyDict_GetItemRef,
   each as the value or None for a miss. A dict subclass's own lookup methods take no part. */
static PyObject *
conv_dict_get(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    PyObject *d = args[0], *key = args[1], *ref;
    PyObject *a = PyDict_GetItem(d, key);
    PyObject *b = PyDict_GetItemWithError(d, key);
    if (b == NULL && PyErr_Occurred()) {
        return NULL;
    }
    if (PyDict_GetItemRef(d, key, &ref) < 0) {
        return NULL;
    }
    PyObject *r = Py_BuildValue("(OOO)", a ? a : Py_None, b ? b : Py_None, ref ? ref : Py_None);
    Py_XDECREF(ref);
    return r;
}

/* Types from specifications. */

typedef struct {
    PyObject_HEAD
    int value;
} Box;

static PyMemberDef box_members[] = {
    { "value", Py_T_INT, offsetof(Box, value), 0, NULL },
    { NULL },
};

static PyObject *
box_peek(PyObject *self, PyObject *unused)
{
    return PyLong_FromLong(((Box *)self)->value);
}

static PyMethodDef box_methods[] = {
    { "peek", box_peek, METH_NOARGS, NULL },
    { NULL },
};

static PyType_Slot box_slots[] = {
    { Py_tp_doc, "Box(value)\n--\n\nA boxed int." },
    { Py_tp_members, box_members },
    { Py_tp_methods, box_methods },
    { 0, NULL },
};

static PyType_Spec box_spec = {
    "capi_conv.Box", sizeof(Box), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, box_slots,
};

static PyType_Slot sealed_slots[] = {
    { 0, NULL },
};

static PyType_Spec sealed_spec = {
    "capi_conv.Sealed", sizeof(PyObject), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
    sealed_slots,
};

/* A subclass of tuple that adds nothing: basicsize and itemsize 0 inherit the base's. */
static PyType_Slot pair_slots[] = {
    { Py_tp_base, NULL },               /* &PyTuple_Type, set at exec */
    { 0, NULL },
};

static PyType_Spec pair_spec = {
    "capi_conv.Pair", 0, 0, Py_TPFLAGS_DEFAULT, pair_slots,
};

/* tp_name(t): the C tp_name of a type. */
static PyObject *
conv_tp_name(PyObject *self, PyObject *t)
{
    return PyUnicode_FromString(((PyTypeObject *)t)->tp_name);
}

static PyMethodDef methods[] = {
    { "parse", (PyCFunction)(void (*)(void))conv_parse, METH_FASTCALL, NULL },
    { "to_c", (PyCFunction)(void (*)(void))conv_to_c, METH_FASTCALL, NULL },
    { "dict_get", (PyCFunction)(void (*)(void))conv_dict_get, METH_FASTCALL, NULL },
    { "tp_name", conv_tp_name, METH_O, NULL },
    { NULL, NULL, 0, NULL },
};

static int
add_type(PyObject *m, PyType_Spec *spec)
{
    PyObject *t = PyType_FromModuleAndSpec(m, spec, NULL);
    if (t == NULL) {
        return -1;
    }
    int r = PyModule_AddType(m, (PyTypeObject *)t);
    Py_DECREF(t);
    return r;
}

static int
conv_exec(PyObject *m)
{
    pair_slots[0].pfunc = &PyTuple_Type;
    return add_type(m, &box_spec) < 0 || add_type(m, &sealed_spec) < 0 || add_type(m, &pair_spec) < 0
        ? -1 : 0;
}

static PyModuleDef_Slot slots[] = {
    { Py_mod_exec, conv_exec },
    { Py_mod_gil, Py_MOD_GIL_NOT_USED },
    { 0, NULL },
};

static PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "capi_conv", NULL, 0, methods, slots, NULL, NULL, NULL,
};

PyMODINIT_FUNC
PyInit_capi_conv(void)
{
    return PyModuleDef_Init(&module);
}

/* Single-phase initialization: loaded as the submodule single of a package. */
static PyModuleDef single = {
    PyModuleDef_HEAD_INIT, "single", "made by PyModule_Create", -1, NULL, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC
PyInit_single(void)
{
    PyObject *m = PyModule_Create(&single);
    if (m != NULL) {
        PyUnstable_Module_SetGIL(m, Py_MOD_GIL_NOT_USED);
    }
    return m;
}
