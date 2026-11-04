/* PyType_Ready on a static type (slot wrappers, getsets, members, methods,
   __new__, inheritance), and the C-API families numpy leans on: capsules,
   imports, str functions, saved errors, atomic dict updates, slices, locks,
   context variables and the datetime C API, as an unmodified extension
   module (tests/capi/ready_run.py drives it). */

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <datetime.h>
#include <stddef.h>
#include <string.h>

/* ready.Vec: a pair of numbers, with a C slot for nearly every dunder. */

typedef struct {
    PyObject_HEAD
    double x, y;
    long tag;
} Vec;

static PyObject *
vec_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    double x = 0, y = 0;
    static char *kwlist[] = { "x", "y", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|dd", kwlist, &x, &y)) {
        return NULL;
    }
    Vec *v = (Vec *)type->tp_alloc(type, 0);
    if (v != NULL) {
        v->x = x;
        v->y = y;
        v->tag = 7;
    }
    return (PyObject *)v;
}

static int vec_deallocs;

static void
vec_dealloc(PyObject *self)
{
    vec_deallocs++;
    Py_TYPE(self)->tp_free(self);       /* a subclass's tp_free knows the layout */
}

static PyObject *
dealloc_count(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyLong_FromLong(vec_deallocs);
}

static PyObject *
vec_repr(PyObject *self)
{
    Vec *v = (Vec *)self;
    return PyUnicode_FromFormat("Vec(%R, %R)", PyFloat_FromDouble(v->x), PyFloat_FromDouble(v->y));
}

static PyObject *
vec_add(PyObject *a, PyObject *b)
{
    if (!PyObject_TypeCheck(a, Py_TYPE(b)) && !PyObject_TypeCheck(b, Py_TYPE(a))) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    Vec *p = (Vec *)a, *q = (Vec *)b;
    PyObject *args = Py_BuildValue("(dd)", p->x + q->x, p->y + q->y);
    PyObject *r = PyObject_CallObject((PyObject *)Py_TYPE(a), args);
    Py_XDECREF(args);
    return r;
}

static PyObject *
vec_neg(PyObject *a)
{
    Vec *p = (Vec *)a;
    PyObject *args = Py_BuildValue("(dd)", -p->x, -p->y);
    PyObject *r = PyObject_CallObject((PyObject *)Py_TYPE(a), args);
    Py_XDECREF(args);
    return r;
}

static int
vec_bool(PyObject *a)
{
    return ((Vec *)a)->x != 0 || ((Vec *)a)->y != 0;
}

static PyObject *
vec_pow(PyObject *a, PyObject *b, PyObject *mod)
{
    return Py_BuildValue("(OOO)", a, b, mod);
}

static Py_ssize_t
vec_len(PyObject *a)
{
    return 2;
}

static PyObject *
vec_item(PyObject *a, Py_ssize_t i)
{
    if (i < 0 || i > 1) {
        PyErr_SetString(PyExc_IndexError, "Vec index out of range");
        return NULL;
    }
    return PyFloat_FromDouble(i == 0 ? ((Vec *)a)->x : ((Vec *)a)->y);
}

static int
vec_ass_item(PyObject *a, Py_ssize_t i, PyObject *value)
{
    if (i < 0 || i > 1) {
        PyErr_SetString(PyExc_IndexError, "Vec index out of range");
        return -1;
    }
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError, "cannot delete");
        return -1;
    }
    double d = PyFloat_AsDouble(value);
    if (d == -1 && PyErr_Occurred()) {
        return -1;
    }
    *(i == 0 ? &((Vec *)a)->x : &((Vec *)a)->y) = d;
    return 0;
}

static int
vec_contains(PyObject *a, PyObject *value)
{
    double d = PyFloat_AsDouble(value);
    if (d == -1 && PyErr_Occurred()) {
        return -1;
    }
    return d == ((Vec *)a)->x || d == ((Vec *)a)->y;
}

static PyObject *
vec_richcompare(PyObject *a, PyObject *b, int op)
{
    if (!PyObject_TypeCheck(b, &PyBaseObject_Type) || Py_TYPE(b) != Py_TYPE(a)) {
        Py_RETURN_NOTIMPLEMENTED;
    }
    double d = ((Vec *)a)->x * ((Vec *)a)->x + ((Vec *)a)->y * ((Vec *)a)->y
        - ((Vec *)b)->x * ((Vec *)b)->x - ((Vec *)b)->y * ((Vec *)b)->y;
    switch (op) {
    case Py_LT: return PyBool_FromLong(d < 0);
    case Py_LE: return PyBool_FromLong(d <= 0);
    case Py_EQ: return PyBool_FromLong(d == 0);
    case Py_NE: return PyBool_FromLong(d != 0);
    case Py_GT: return PyBool_FromLong(d > 0);
    default: return PyBool_FromLong(d >= 0);
    }
}

static Py_hash_t
vec_hash(PyObject *a)
{
    return (Py_hash_t)(((Vec *)a)->x * 3 + ((Vec *)a)->y);
}

static PyObject *
vec_call(PyObject *self, PyObject *args, PyObject *kwds)
{
    return Py_BuildValue("(OO)", args, kwds != NULL ? kwds : Py_None);
}

static PyObject *
vec_iter(PyObject *self)
{
    PyObject *l = Py_BuildValue("[dd]", ((Vec *)self)->x, ((Vec *)self)->y);
    PyObject *it = l != NULL ? PyObject_GetIter(l) : NULL;
    Py_XDECREF(l);
    return it;
}

static PyObject *
vec_getattro(PyObject *self, PyObject *name)
{
    if (PyUnicode_Check(name) && PyUnicode_CompareWithASCIIString(name, "magic") == 0) {
        return PyUnicode_FromString("abracadabra");
    }
    return PyObject_GenericGetAttr(self, name);
}

static PyObject *
vec_norm2(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    return PyFloat_FromDouble(((Vec *)self)->x * ((Vec *)self)->x + ((Vec *)self)->y * ((Vec *)self)->y);
}

static PyObject *
vec_scaled(PyObject *self, PyObject *const *args, Py_ssize_t nargs)
{
    double k = PyFloat_AsDouble(args[0]);
    if (k == -1 && PyErr_Occurred()) {
        return NULL;
    }
    return Py_BuildValue("(dd)", ((Vec *)self)->x * k, ((Vec *)self)->y * k);
}

static PyObject *
vec_origin(PyObject *cls, PyObject *Py_UNUSED(ignored))
{
    return PyObject_CallNoArgs(cls);
}

static PyObject *
vec_twice(PyObject *Py_UNUSED(cls), PyObject *arg)
{
    return PyNumber_Add(arg, arg);
}

static PyMethodDef vec_methods[] = {
    { "norm2", vec_norm2, METH_NOARGS, "norm2($self)\n--\n\nsquared length" },
    { "scaled", (PyCFunction)(void (*)(void))vec_scaled, METH_FASTCALL, NULL },
    { "origin", vec_origin, METH_NOARGS | METH_CLASS, NULL },
    { "twice", vec_twice, METH_O | METH_STATIC, NULL },
    { NULL }
};

static PyObject *
vec_get_sum(PyObject *self, void *closure)
{
    return PyFloat_FromDouble(((Vec *)self)->x + ((Vec *)self)->y);
}

static int
vec_set_sum(PyObject *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_AttributeError, "cannot delete sum");
        return -1;
    }
    double d = PyFloat_AsDouble(value);
    if (d == -1 && PyErr_Occurred()) {
        return -1;
    }
    ((Vec *)self)->y = d - ((Vec *)self)->x;
    return 0;
}

static PyGetSetDef vec_getset[] = {
    { "sum", vec_get_sum, vec_set_sum, "x plus y", NULL },
    { NULL }
};

static PyMemberDef vec_members[] = {
    { "x", Py_T_DOUBLE, offsetof(Vec, x), 0, "the first" },
    { "y", Py_T_DOUBLE, offsetof(Vec, y), 0, NULL },
    { "tag", Py_T_LONG, offsetof(Vec, tag), Py_READONLY, NULL },
    { NULL }
};

static PyNumberMethods vec_as_number = {
    .nb_add = vec_add, .nb_negative = vec_neg, .nb_bool = vec_bool, .nb_power = vec_pow,
};

static PySequenceMethods vec_as_sequence = {
    .sq_length = vec_len, .sq_item = vec_item, .sq_ass_item = vec_ass_item, .sq_contains = vec_contains,
};

static PyTypeObject Vec_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ready.Vec",
    .tp_basicsize = sizeof(Vec),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "Vec(x=0, y=0)\n--\n\nA pair of numbers.",
    .tp_new = vec_new,
    .tp_dealloc = vec_dealloc,
    .tp_repr = vec_repr,
    .tp_as_number = &vec_as_number,
    .tp_as_sequence = &vec_as_sequence,
    .tp_richcompare = vec_richcompare,
    .tp_hash = vec_hash,
    .tp_call = vec_call,
    .tp_iter = vec_iter,
    .tp_getattro = vec_getattro,
    .tp_methods = vec_methods,
    .tp_members = vec_members,
    .tp_getset = vec_getset,
};

/* ready.Vec3 inherits from Vec and adds a field and nothing else. */
typedef struct {
    Vec base;
    double z;
} Vec3;

static PyMemberDef vec3_members[] = {
    { "z", Py_T_DOUBLE, offsetof(Vec3, z), 0, NULL },
    { NULL }
};

static PyTypeObject Vec3_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ready.Vec3",
    .tp_basicsize = sizeof(Vec3),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &Vec_Type,
    .tp_members = vec3_members,
};

/* ready.Locked: no tp_new, so it cannot be instantiated. */
static PyTypeObject Locked_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ready.Locked",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
};

/* ready.Counter: tp_iternext, and a subclass of list via PyList_Type. */
typedef struct {
    PyObject_HEAD
    long n;
} Counter;

static PyObject *
counter_iter(PyObject *self)
{
    return PyObject_SelfIter(self);
}

static PyObject *
counter_next(PyObject *self)
{
    Counter *c = (Counter *)self;
    if (c->n <= 0) {
        return NULL;
    }
    return PyLong_FromLong(c->n--);
}

static PyObject *
counter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    long n = 3;
    if (!PyArg_ParseTuple(args, "|l", &n)) {
        return NULL;
    }
    Counter *c = (Counter *)PyType_GenericNew(type, args, kwds);
    if (c != NULL) {
        c->n = n;
    }
    return (PyObject *)c;
}

static PyTypeObject Counter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ready.Counter",
    .tp_basicsize = sizeof(Counter),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = counter_new,
    .tp_iter = counter_iter,
    .tp_iternext = counter_next,
};

typedef struct {
    PyListObject base;
} MyList;

static PyTypeObject MyList_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ready.MyList",
    .tp_basicsize = sizeof(MyList),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &PyList_Type,
};

/* ready.Delegate: slots that hand back to object's own, as numpy's NaT hash and
   scalar reprs do through PyBaseObject_Type. */
static Py_hash_t
delegate_hash(PyObject *self)
{
    return PyBaseObject_Type.tp_hash(self);
}

static PyObject *
delegate_repr(PyObject *self)
{
    return PyBaseObject_Type.tp_repr(self);
}

static PyObject *
delegate_str(PyObject *self)
{
    return PyBaseObject_Type.tp_str(self);
}

static PyObject *
delegate_richcompare(PyObject *self, PyObject *other, int op)
{
    return PyBaseObject_Type.tp_richcompare(self, other, op);
}

static PyTypeObject Delegate_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ready.Delegate",
    .tp_basicsize = sizeof(PyObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_hash = delegate_hash,
    .tp_repr = delegate_repr,
    .tp_str = delegate_str,
    .tp_richcompare = delegate_richcompare,
};

/* ready.FloatSub: a float subtype whose tp_new asks float's tp_new for the instance, as
   numpy's float64 does. */
static PyObject *
floatsub_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return PyFloat_Type.tp_new(type, args, kwds);
}

static PyTypeObject FloatSub_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ready.FloatSub",
    .tp_basicsize = sizeof(PyFloatObject),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_base = &PyFloat_Type,
    .tp_new = floatsub_new,
};

/* A metatype that cannot be called, and a class made with it (Cython's common types). */
static PyObject *
make_meta_class(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyType_Slot meta_slots[] = { { Py_tp_base, &PyType_Type }, { 0, NULL } };
    PyType_Spec meta_spec = { "ready.Meta", 0, 0,
                              Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                              meta_slots };
    PyObject *meta = PyType_FromSpec(&meta_spec);
    if (meta == NULL) {
        return NULL;
    }
    PyType_Slot slots[] = { { 0, NULL } };
    PyType_Spec spec = { "ready.Made", sizeof(PyObject), 0, Py_TPFLAGS_DEFAULT, slots };
    PyObject *cls = PyType_FromMetaclass((PyTypeObject *)meta, NULL, &spec, NULL);
    if (cls == NULL) {
        Py_DECREF(meta);
        return NULL;
    }
    PyObject *r = Py_BuildValue("(OO)", meta, cls);
    Py_DECREF(meta);
    Py_DECREF(cls);
    return r;
}

/* A spec type whose instances keep a managed __dict__ and weakref list (Cython's functions). */
static PyObject *
make_managed(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    static PyGetSetDef getset[] = {
        { "__dict__", PyObject_GenericGetDict, PyObject_GenericSetDict, NULL, NULL },
        { NULL }
    };
    PyType_Slot slots[] = { { Py_tp_new, PyType_GenericNew }, { Py_tp_getset, getset }, { 0, NULL } };
    PyType_Spec spec = { "ready.Managed", sizeof(PyObject), 0,
                         Py_TPFLAGS_DEFAULT | Py_TPFLAGS_MANAGED_DICT | Py_TPFLAGS_MANAGED_WEAKREF, slots };
    return PyType_FromSpec(&spec);
}

/* Sets, iterators, lists, bytes, weak references and str codecs. */
static PyObject *
containers(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    PyObject *set = PySet_New(NULL), *one = PyLong_FromLong(1), *two = PyLong_FromLong(2);
    PySet_Add(set, one);
    PySet_Add(set, two);
    PyList_Append(out, PyLong_FromSsize_t(PySet_Size(set)));
    PyList_Append(out, PyLong_FromLong(PySet_Contains(set, one)));
    PyList_Append(out, PyLong_FromLong(PySet_Discard(set, one)));
    PyList_Append(out, PyLong_FromLong(PySet_Discard(set, one)));
    PyList_Append(out, PySet_Pop(set));
    PyList_Append(out, PySet_Pop(set) ? Py_None : (PyErr_Clear(), PyUnicode_FromString("empty")));
    PyObject *items = Py_BuildValue("[iii]", 3, 3, 4);
    PyObject *fs = PyFrozenSet_New(items);
    PyList_Append(out, PyObject_Repr(fs));
    PyList_Append(out, PyLong_FromLong(PySet_Add(fs, one)));
    PyErr_Clear();
    PySet_Clear(set);
    PyObject *it = PyObject_GetIter(items), *item;
    PyList_Append(out, PyLong_FromLong(PyIter_NextItem(it, &item)));
    PyList_Append(out, item);
    PyList_Append(out, PyLong_FromLong(PyIter_NextItem(it, &item)));
    PyList_Append(out, PyLong_FromLong(PyIter_NextItem(it, &item)));
    PyList_Append(out, PyLong_FromLong(PyIter_NextItem(it, &item)));
    PyList_Append(out, PyLong_FromLong(PyIter_NextItem(it, &item)));
    PyErr_Clear();
    PyList_Extend(items, two) < 0 ? PyErr_Clear() : (void)0;
    PyList_Extend(items, Py_BuildValue("(ii)", 8, 9));
    PyList_Append(out, PyObject_Repr(items));
    PyList_Clear(items);
    PyList_Append(out, PyObject_Repr(items));
    PyObject *d = PyDict_New();
    PyDict_SetItemString(d, "a", one);
    PyObject *popped;
    PyList_Append(out, PyLong_FromLong(PyDict_PopString(d, "a", &popped)));
    PyList_Append(out, popped);
    PyList_Append(out, PyLong_FromLong(PyDict_PopString(d, "a", &popped)));
    PyDict_MergeFromSeq2(d, Py_BuildValue("[(ii)(ii)]", 1, 2, 1, 3), 0);
    PyList_Append(out, PyObject_Repr(d));
    PyList_Append(out, PyNumber_ToBase(PyLong_FromLong(255), 16));
    PyList_Append(out, PyNumber_ToBase(PyLong_FromLong(5), 2));
    PyObject *b = PyBytes_FromObject(Py_BuildValue("[iii]", 65, 66, 67));
    PyList_Append(out, b);
    PyList_Append(out, PyBytes_Join(PyBytes_FromString("-"), Py_BuildValue("[yy]", "a", "b")));
    PyList_Append(out, PyBytes_FromFormat("%s=%d %c %%", "x", 42, 'z'));
    PyList_Append(out, PyByteArray_FromObject(b));
    PyObject *w = PyWeakref_NewRef(set, NULL);
    PyObject *alive;
    PyList_Append(out, PyLong_FromLong(PyWeakref_GetRef(w, &alive)));
    PyList_Append(out, PyBool_FromLong(alive == set));
    PyList_Append(out, PyBool_FromLong(PyWeakref_GetObject(w) == set));
    PyList_Append(out, PyLong_FromLong(PyWeakref_IsDead(w)));
    return out;
}

static PyObject *
codecs(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    PyList_Append(out, PyUnicode_Decode("caf\xc3\xa9", 5, "utf-8", NULL));
    PyList_Append(out, PyUnicode_DecodeLatin1("caf\xe9", 4, NULL));
    PyList_Append(out, PyUnicode_DecodeASCII("abc", 3, NULL));
    PyList_Append(out, PyUnicode_DecodeASCII("\xff", 1, NULL) ? Py_None : (PyErr_Clear(), PyUnicode_FromString("ascii error")));
    PyList_Append(out, PyUnicode_DecodeASCII("\xff", 1, "replace"));
    PyList_Append(out, PyUnicode_DecodeUTF16("a\0b\0", 4, NULL, &(int){ -1 }));
    PyList_Append(out, PyUnicode_DecodeUTF32("a\0\0\0", 4, NULL, &(int){ -1 }));
    PyList_Append(out, PyUnicode_DecodeUnicodeEscape("\\u00e9\\n", 8, NULL));
    PyList_Append(out, PyUnicode_DecodeFSDefault("path/name"));
    PyObject *u = PyUnicode_FromString("h\xc3\xa9");
    PyList_Append(out, PyUnicode_AsUnicodeEscapeString(u));
    PyList_Append(out, PyLong_FromLong(PyUnicode_ReadChar(u, 1)));
    PyList_Append(out, PyUnicode_ReadChar(u, 9) == (Py_UCS4)-1 ? (PyErr_Clear(), PyUnicode_FromString("index error")) : Py_None);
    wchar_t buf[8];
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_AsWideChar(u, buf, 8)));
    PyList_Append(out, PyLong_FromLong(buf[1]));
    PyList_Append(out, PyUnicode_FromWideChar(buf, 2));
    Py_ssize_t n;
    wchar_t *copy = PyUnicode_AsWideCharString(u, &n);
    PyList_Append(out, PyLong_FromSsize_t(n));
    PyMem_Free(copy);
    PyList_Append(out, PyLong_FromLong(PyUnicode_IsIdentifier(u)));
    PyList_Append(out, PyLong_FromLong(PyUnicode_IsIdentifier(PyUnicode_FromString("a b"))));
    PyList_Append(out, PyUnicode_FromString(PyUnicode_GetDefaultEncoding()));
    return out;
}

/* sys, thread-specific storage, running code, frame globals. */
static PyObject *
runtime_services(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    PySys_WriteStdout("stdout %d %s\n", 7, "text");
    PyList_Append(out, PyLong_FromLong(PySys_SetObject("ready_attr", PyLong_FromLong(9))));
    PyList_Append(out, PySys_GetObject("ready_attr"));
    PyList_Append(out, PyLong_FromLong(PySys_SetObject("ready_attr", NULL)));
    PyList_Append(out, PySys_GetObject("ready_attr") ? Py_True : Py_False);
    PyList_Append(out, PyLong_FromLong(PySys_Audit("ready.event", "(is)", 1, "x")));
    Py_tss_t *key = PyThread_tss_alloc();
    PyList_Append(out, PyLong_FromLong(PyThread_tss_is_created(key)));
    PyList_Append(out, PyLong_FromLong(PyThread_tss_create(key)));
    PyList_Append(out, PyLong_FromLong(PyThread_tss_is_created(key)));
    PyThread_tss_set(key, &out);
    PyList_Append(out, PyBool_FromLong(PyThread_tss_get(key) == &out));
    PyThread_tss_free(key);
    PyObject *d1 = PyThreadState_GetDict(), *d2 = PyThreadState_GetDict();
    PyList_Append(out, PyBool_FromLong(d1 != NULL && d1 == d2));
    PyObject *globals = PyDict_New();
    PyDict_SetItemString(globals, "k", PyLong_FromLong(40));
    PyList_Append(out, PyRun_String("k + 2", Py_eval_input, globals, globals));
    PyList_Append(out, PyRun_String("j = k * 2", Py_file_input, globals, globals));
    PyList_Append(out, PyDict_GetItemString(globals, "j"));
    PyList_Append(out, PyRun_String("1 +", Py_eval_input, globals, globals) ? Py_None : (PyErr_Clear(), PyUnicode_FromString("syntax error")));
    PyObject *code = Py_CompileString("a = 1", "<ready>", Py_file_input);
    PyList_Append(out, PyObject_GetAttrString(code, "co_filename"));
    PyList_Append(out, PyBool_FromLong(PyEval_GetGlobals() != NULL));
    PyList_Append(out, PyOS_FSPath(PyUnicode_FromString("some/path")));
    PyObject *token_var = PyContextVar_New("reset_me", NULL);
    PyObject *token = PyContextVar_Set(token_var, PyLong_FromLong(1));
    PyList_Append(out, PyLong_FromLong(PyContextVar_Reset(token_var, token)));
    PyObject *val;
    PyContextVar_Get(token_var, NULL, &val);
    PyList_Append(out, val ? Py_True : Py_False);
    PyObject *fromlist = Py_BuildValue("[s]", "join");
    PyObject *m = PyImport_ImportModuleLevel("os.path", NULL, NULL, fromlist, 0);
    PyList_Append(out, PyObject_GetAttrString(m, "__name__"));
    PyObject *added = PyImport_AddModule("ready_added2");
    PyList_Append(out, PyBool_FromLong(added == PyImport_AddModule("ready_added2")));
    return out;
}

/* Capsules. */

static int destroyed;

static void
capsule_destroy(PyObject *capsule)
{
    destroyed++;
}

static int the_int = 42;

static PyObject *
capsule_roundtrip(PyObject *self, PyObject *arg)
{
    PyObject *c = PyCapsule_New(&the_int, "ready._int", capsule_destroy);
    if (c == NULL) {
        return NULL;
    }
    PyObject *out = PyList_New(0);
    int *p = PyCapsule_GetPointer(c, "ready._int");
    PyList_Append(out, p == &the_int ? Py_True : Py_False);
    PyCapsule_GetPointer(c, "wrong");
    PyObject *exc = PyErr_GetRaisedException();
    PyList_Append(out, exc);
    Py_XDECREF(exc);
    PyList_Append(out, PyUnicode_FromString(PyCapsule_GetName(c)));
    PyList_Append(out, PyBool_FromLong(PyCapsule_IsValid(c, "ready._int")));
    PyList_Append(out, PyBool_FromLong(PyCapsule_IsValid(c, "other")));
    PyCapsule_SetContext(c, &destroyed);
    PyList_Append(out, PyBool_FromLong(PyCapsule_GetContext(c) == &destroyed));
    PyList_Append(out, PyBool_FromLong(PyCapsule_GetDestructor(c) == capsule_destroy));
    PyCapsule_SetName(c, "renamed");
    PyList_Append(out, PyUnicode_FromString(PyCapsule_GetName(c)));
    PyList_Append(out, PyCapsule_New(NULL, "x", NULL) == NULL ? PyUnicode_FromString("null refused") : Py_None);
    PyErr_Clear();
    int before = destroyed;
    Py_DECREF(c);
    PyList_Append(out, PyLong_FromLong(destroyed - before));
    return out;
}

static PyObject *
capsule_import(PyObject *self, PyObject *arg)
{
    const char *name = PyUnicode_AsUTF8(arg);
    void *p = name != NULL ? PyCapsule_Import(name, 0) : NULL;
    if (p == NULL) {
        return NULL;
    }
    return PyBool_FromLong(1);
}

/* Imports and sys. */

static PyObject *
imports(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    PyObject *m = PyImport_ImportModule("os.path");
    PyList_Append(out, PyObject_GetAttrString(m, "__name__"));
    PyObject *name = PyUnicode_FromString("json.decoder");
    PyObject *m2 = PyImport_Import(name);
    PyList_Append(out, PyObject_GetAttrString(m2, "__name__"));
    PyObject *fromlist = Py_BuildValue("[s]", "JSONDecoder");
    PyObject *m3 = PyImport_ImportModuleLevelObject(name, NULL, NULL, fromlist, 0);
    PyList_Append(out, PyObject_GetAttrString(m3, "__name__"));
    PyObject *got = PyImport_GetModule(name);
    PyList_Append(out, PyBool_FromLong(got == m2));
    PyObject *missing = PyUnicode_FromString("no_such_module_xyz");
    PyList_Append(out, PyBool_FromLong(PyImport_GetModule(missing) == NULL && !PyErr_Occurred()));
    PyObject *added = PyImport_AddModuleRef("ready_added");
    PyList_Append(out, PyBool_FromLong(added == PyImport_AddModuleRef("ready_added")));
    PyList_Append(out, PyBool_FromLong(PyDict_GetItemString(PyImport_GetModuleDict(), "ready_added") == added));
    PyList_Append(out, PyBool_FromLong(PyImport_ImportModule("no_such_module_xyz") == NULL));
    PyObject *exc = PyErr_GetRaisedException();
    PyList_Append(out, PyObject_Type(exc));
    PyList_Append(out, PyBool_FromLong(PySys_GetObject("path") != NULL));
    PyList_Append(out, PyBool_FromLong(PySys_GetObject("no_such_attribute") == NULL && !PyErr_Occurred()));
    PyList_Append(out, PyBool_FromLong(PyEval_GetBuiltins() == PyEval_GetBuiltins()));
    PyList_Append(out, PyBool_FromLong(PyDict_GetItemString(PyEval_GetBuiltins(), "len") != NULL));
    return out;
}

/* str functions: one call, a tuple of what each returned. */

static PyObject *
strings(PyObject *self, PyObject *arg)
{
    PyObject *out = PyList_New(0);
    PyList_Append(out, PyUnicode_AsUTF8String(arg));
    PyList_Append(out, PyUnicode_AsLatin1String(arg) ? Py_True : (PyErr_Clear(), Py_False));
    PyList_Append(out, PyUnicode_AsASCIIString(arg) ? Py_True : (PyErr_Clear(), Py_False));
    PyList_Append(out, PyUnicode_AsEncodedString(arg, "utf-16-le", "strict"));
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_GetLength(arg)));
    Py_UCS4 *copy = PyUnicode_AsUCS4Copy(arg);
    PyList_Append(out, PyLong_FromSsize_t(copy == NULL ? -1 : (Py_ssize_t)copy[0]));
    PyMem_Free(copy);
    Py_UCS4 small[2];
    PyList_Append(out, PyBool_FromLong(PyUnicode_AsUCS4(arg, small, 2, 1) != NULL));
    PyErr_Clear();
    PyList_Append(out, PyUnicode_Concat(arg, arg));
    PyList_Append(out, PyLong_FromLong(PyUnicode_Compare(arg, arg)));
    PyObject *other = PyUnicode_FromString("m");
    PyList_Append(out, PyLong_FromLong(PyUnicode_Compare(arg, other)));
    PyList_Append(out, PyLong_FromLong(PyUnicode_Compare(other, arg)));
    PyList_Append(out, PyLong_FromLong(PyUnicode_CompareWithASCIIString(arg, "abc")));
    PyList_Append(out, PyLong_FromLong(PyUnicode_Equal(arg, arg)));
    PyList_Append(out, PyLong_FromLong(PyUnicode_Contains(arg, other)));
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_FindChar(arg, 'b', 0, PyUnicode_GetLength(arg), 1)));
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_FindChar(arg, 'b', 0, PyUnicode_GetLength(arg), -1)));
    PyList_Append(out, PyUnicode_Substring(arg, 1, 3));
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_Tailmatch(arg, other, 0, PyUnicode_GetLength(arg), 1)));
    PyObject *b = PyUnicode_FromString("b"), *x = PyUnicode_FromString("XX");
    PyList_Append(out, PyUnicode_Replace(arg, b, x, -1));
    PyObject *seq = Py_BuildValue("[sss]", "p", "q", "r");
    PyList_Append(out, PyUnicode_Join(arg, seq));
    PyList_Append(out, PyUnicode_Join(NULL, seq));
    PyObject *fmt = PyUnicode_FromString("<%s|%d>");
    PyObject *fargs = Py_BuildValue("(si)", "v", 5);
    PyList_Append(out, PyUnicode_Format(fmt, fargs));
    PyList_Append(out, PyUnicode_FromOrdinal(0x263a));
    PyList_Append(out, PyUnicode_FromOrdinal(-1) ? Py_None : (PyErr_Clear(), PyUnicode_FromString("bad ordinal")));
    PyList_Append(out, PyUnicode_FromEncodedObject(PyBytes_FromString("caf\xc3\xa9"), "utf-8", NULL));
    PyList_Append(out, PyUnicode_FromEncodedObject(arg, NULL, NULL) ? Py_None : (PyErr_Clear(), PyUnicode_FromString("str refused")));
    PyList_Append(out, PyUnicode_FromObject(arg));
    PyList_Append(out, PyUnicode_FromObject(b) == b ? Py_True : Py_False);
    PyObject *to = PyUnicode_New(5, 0x10ffff);
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_CopyCharacters(to, 1, arg, 0, 3)));
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_GetLength(to)));
    PyObject *resized = PyUnicode_New(6, 0x10ffff);
    PyUnicode_CopyCharacters(resized, 0, arg, 0, PyUnicode_GetLength(arg) < 6 ? PyUnicode_GetLength(arg) : 6);
    PyUnicode_Resize(&resized, 3);
    PyList_Append(out, PyLong_FromSsize_t(PyUnicode_GetLength(resized)));
    PyObject *interned = PyUnicode_FromString("ready interned name");
    PyUnicode_InternInPlace(&interned);
    PyList_Append(out, PyBool_FromLong(interned == PyUnicode_InternFromString("ready interned name")));
    return out;
}

/* Saved errors. */

static PyObject *
errors(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    PyObject *t, *v, *tb;
    PyErr_SetString(PyExc_KeyError, "k");
    PyErr_Fetch(&t, &v, &tb);
    PyList_Append(out, PyBool_FromLong(PyErr_Occurred() == NULL));
    PyList_Append(out, t);
    PyList_Append(out, PyObject_Repr(v));
    PyErr_Restore(t, v, tb);
    PyList_Append(out, PyBool_FromLong(PyErr_ExceptionMatches(PyExc_KeyError)));
    PyErr_Clear();
    /* a type and a bare value: normalized into an instance */
    PyErr_Restore(Py_NewRef(PyExc_ValueError), PyUnicode_FromString("bare"), NULL);
    PyErr_Fetch(&t, &v, &tb);
    PyErr_NormalizeException(&t, &v, &tb);
    PyList_Append(out, PyBool_FromLong(PyExceptionInstance_Check(v)));
    PyList_Append(out, PyObject_Repr(v));
    Py_XDECREF(t); Py_XDECREF(v); Py_XDECREF(tb);
    PyObject *cls = PyErr_NewException("ready.Custom", PyExc_ArithmeticError, NULL);
    PyList_Append(out, PyObject_GetAttrString(cls, "__module__"));
    PyList_Append(out, PyObject_GetAttrString(cls, "__name__"));
    PyList_Append(out, PyBool_FromLong(PyType_IsSubtype((PyTypeObject *)cls, (PyTypeObject *)PyExc_ArithmeticError)));
    PyErr_SetString(cls, "custom message");
    PyErr_Fetch(&t, &v, &tb);
    PyList_Append(out, PyObject_Repr(v));
    Py_XDECREF(t); Py_XDECREF(v); Py_XDECREF(tb);
    errno = 2;
    PyErr_SetFromErrno(PyExc_OSError);
    PyErr_Fetch(&t, &v, &tb);
    PyList_Append(out, PyObject_Type(v));
    PyList_Append(out, PyObject_GetAttrString(v, "errno"));
    Py_XDECREF(t); Py_XDECREF(v); Py_XDECREF(tb);
    PyList_Append(out, PyLong_FromLong(PyErr_WarnEx(PyExc_UserWarning, "ready warning", 1)));
    PyList_Append(out, PyLong_FromLong(PyErr_WarnFormat(PyExc_UserWarning, 1, "formatted %d", 3)));
    PyObject *kw = PyDict_New();
    PyDict_SetItemString(kw, "k", PyLong_FromLong(1));
    PyList_Append(out, PyLong_FromLong(PyArg_ValidateKeywordArguments(kw)));
    PyDict_SetItem(kw, PyLong_FromLong(1), Py_None);
    PyList_Append(out, PyLong_FromLong(PyArg_ValidateKeywordArguments(kw)));
    PyErr_Clear();
    return out;
}

/* Dict updates and slices. */

static PyObject *
dict_ops(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    PyObject *d = _PyDict_NewPresized(10);
    PyObject *k = PyUnicode_FromString("k"), *one = PyLong_FromLong(1), *two = PyLong_FromLong(2);
    PyObject *r;
    PyList_Append(out, PyLong_FromLong(PyDict_SetDefaultRef(d, k, one, &r)));
    PyList_Append(out, r);
    Py_XDECREF(r);
    PyList_Append(out, PyLong_FromLong(PyDict_SetDefaultRef(d, k, two, &r)));
    PyList_Append(out, r);
    Py_XDECREF(r);
    PyList_Append(out, PyDict_SetDefault(d, k, two));
    PyList_Append(out, PyDict_SetDefault(d, two, two));
    PyList_Append(out, PyLong_FromLong(PyDict_Pop(d, k, &r)));
    PyList_Append(out, r);
    Py_XDECREF(r);
    PyList_Append(out, PyLong_FromLong(PyDict_Pop(d, k, &r)));
    PyList_Append(out, r != NULL ? r : Py_None);
    PyList_Append(out, PyLong_FromSsize_t(PyDict_Size(d)));
    return out;
}

static PyObject *
slices(PyObject *self, PyObject *args)
{
    PyObject *sl;
    Py_ssize_t length;
    if (!PyArg_ParseTuple(args, "On", &sl, &length)) {
        return NULL;
    }
    Py_ssize_t start, stop, step;
    if (PySlice_Unpack(sl, &start, &stop, &step) < 0) {
        return NULL;
    }
    Py_ssize_t n = PySlice_AdjustIndices(length, &start, &stop, step);
    return Py_BuildValue("(nnnn)", start, stop, step, n);
}

/* Locks, context variables, the datetime C API, numbers from text. */

static PyObject *
locks(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyThread_type_lock lock = PyThread_allocate_lock();
    PyObject *out = PyList_New(0);
    PyList_Append(out, PyLong_FromLong(PyThread_acquire_lock(lock, 0)));
    PyList_Append(out, PyLong_FromLong(PyThread_acquire_lock(lock, 0)));
    PyList_Append(out, PyLong_FromLong(PyThread_acquire_lock_timed(lock, 1000, 0)));
    PyThread_release_lock(lock);
    PyList_Append(out, PyLong_FromLong(PyThread_acquire_lock(lock, 1)));
    PyThread_release_lock(lock);
    PyThread_free_lock(lock);
    return out;
}

static PyObject *
contextvars(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    PyObject *plain = PyContextVar_New("plain", NULL), *dflt = PyContextVar_New("dflt", PyLong_FromLong(5));
    PyObject *val;
    PyList_Append(out, PyLong_FromLong(PyContextVar_Get(plain, NULL, &val)));
    PyList_Append(out, val != NULL ? val : Py_None);
    PyList_Append(out, PyLong_FromLong(PyContextVar_Get(plain, PyLong_FromLong(9), &val)));
    PyList_Append(out, val);
    PyList_Append(out, PyLong_FromLong(PyContextVar_Get(dflt, NULL, &val)));
    PyList_Append(out, val);
    PyObject *token = PyContextVar_Set(plain, PyLong_FromLong(11));
    PyList_Append(out, PyBool_FromLong(token != NULL));
    PyContextVar_Get(plain, NULL, &val);
    PyList_Append(out, val);
    PyList_Append(out, PyObject_GetAttrString(plain, "name"));
    return out;
}

static PyObject *
datetimes(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyDateTime_IMPORT;
    if (PyDateTimeAPI == NULL) {
        return NULL;
    }
    PyObject *out = PyList_New(0);
    PyObject *d = PyDate_FromDate(2024, 2, 29);
    PyObject *dt = PyDateTime_FromDateAndTime(2024, 2, 29, 12, 30, 15, 250);
    PyObject *td = PyDelta_FromDSU(1, 2, 3);
    PyList_Append(out, PyObject_Repr(d));
    PyList_Append(out, PyObject_Repr(dt));
    PyList_Append(out, PyObject_Repr(td));
    PyList_Append(out, PyBool_FromLong(PyDate_Check(d) && !PyDateTime_Check(d)));
    PyList_Append(out, PyBool_FromLong(PyDate_Check(dt) && PyDateTime_Check(dt)));
    PyList_Append(out, PyBool_FromLong(PyDelta_Check(td)));
    PyList_Append(out, PyObject_Repr(PyDateTimeAPI->TimeZone_UTC));
    PyList_Append(out, PyObject_Repr(PyDateTime_FromDateAndTime(2000, 1, 1, 0, 0, 0, 0)));
    return out;
}

static PyObject *
numbers(PyObject *self, PyObject *Py_UNUSED(ignored))
{
    PyObject *out = PyList_New(0);
    char *end;
    PyObject *v = PyLong_FromString("  -0x1f rest", &end, 0);
    PyList_Append(out, v);
    PyList_Append(out, PyUnicode_FromString(end));
    PyList_Append(out, PyLong_FromString("101", NULL, 2));
    PyList_Append(out, PyLong_FromString("12x", NULL, 10) ? Py_None : (PyErr_Clear(), PyUnicode_FromString("invalid")));
    PyList_Append(out, PyLong_FromUnicodeObject(PyUnicode_FromString("1_000"), 10));
    PyList_Append(out, PyFloat_FromString(PyUnicode_FromString(" 2.5e3 ")));
    PyList_Append(out, PyFloat_FromString(PyBytes_FromString("1e-2")));
    PyList_Append(out, PyFloat_FromString(PyLong_FromLong(1)) ? Py_None : (PyErr_Clear(), PyUnicode_FromString("int refused")));
    PyList_Append(out, PyLong_FromLong(PyLong_IsZero(PyLong_FromLong(0))));
    PyList_Append(out, PyLong_FromLong(PyLong_IsZero(PyLong_FromLong(3))));
    Py_complex c = PyComplex_AsCComplex(PyComplex_FromDoubles(1.5, -2.5));
    PyList_Append(out, Py_BuildValue("(dd)", c.real, c.imag));
    c = PyComplex_AsCComplex(PyFloat_FromDouble(4.0));
    PyList_Append(out, Py_BuildValue("(dd)", c.real, c.imag));
    PyList_Append(out, PyFloat_FromDouble(PyComplex_RealAsDouble(PyComplex_FromDoubles(7, 8))));
    PyList_Append(out, PyFloat_FromDouble(PyComplex_ImagAsDouble(PyComplex_FromDoubles(7, 8))));
    PyList_Append(out, PyFloat_FromDouble(PyComplex_ImagAsDouble(PyFloat_FromDouble(7))));
    char buf[8];
    PyOS_snprintf(buf, sizeof buf, "%d-%s", 12345, "abcdef");
    PyList_Append(out, PyUnicode_FromString(buf));
    PyList_Append(out, PyLong_FromLong(PyOS_strtol("-123abc", &end, 10)));
    PyList_Append(out, PyUnicode_FromString(end));
    PyList_Append(out, PyLong_FromUnsignedLong(PyOS_strtoul("0xff", &end, 16)));
    return out;
}

/* Objects: length hints, file descriptors, managed state. */

static PyObject *
objects(PyObject *self, PyObject *arg)
{
    PyObject *out = PyList_New(0);
    PyList_Append(out, PyLong_FromSsize_t(PyObject_LengthHint(arg, -7)));
    PyObject *attr;
    PyList_Append(out, PyLong_FromLong(PyObject_GetOptionalAttr(arg, PyUnicode_FromString("no_such_attribute"), &attr)));
    PyList_Append(out, PyLong_FromLong(PyObject_GetOptionalAttr(arg, PyUnicode_FromString("__class__"), &attr)));
    Py_XDECREF(attr);
    PyList_Append(out, PyLong_FromLong(PyObject_AsFileDescriptor(PyLong_FromLong(3))));
    PyList_Append(out, PyLong_FromLong(PyObject_AsFileDescriptor(PyLong_FromLong(-1))));
    PyErr_Clear();
    PyObject *a = PyUnicode_FromString("A"), *b = PyUnicode_FromString("B");
    PyList_Append(out, PyObject_CallMethodObjArgs(a, PyUnicode_FromString("join"), PyUnicode_FromString("xyz"), NULL));
    PyObject *call[3] = { b, PyUnicode_FromString("lower") };
    PyList_Append(out, PyObject_VectorcallMethod(call[1], call, 1, NULL));
    PyObject *kw = Py_BuildValue("{s:s}", "sep", "-");
    PyObject *pargs[2] = { PyLong_FromLong(1), PyLong_FromLong(2) };
    PyObject *dict_result = PyObject_VectorcallDict((PyObject *)&PyTuple_Type, pargs, 0, NULL);
    PyList_Append(out, dict_result);
    PyObject *ints[1] = { PyUnicode_FromString("ff") };
    PyObject *kwint = Py_BuildValue("{s:i}", "base", 16);
    PyList_Append(out, PyObject_VectorcallDict((PyObject *)&PyLong_Type, ints, 1, kwint));
    PyObject *seq = Py_BuildValue("(ii)", 1, 2);
    PyObject *it = PySeqIter_New(seq);
    PyList_Append(out, PyObject_CallMethod(it, "__next__", NULL));
    Py_XDECREF(kw);
    PyObject *alias = Py_GenericAlias((PyObject *)&PyList_Type, (PyObject *)&PyLong_Type);
    PyList_Append(out, PyObject_Repr(alias));
    PyList_Append(out, PyBool_FromLong(PyType_GetFlags(&PyLong_Type) & Py_TPFLAGS_LONG_SUBCLASS ? 1 : 0));
    PyList_Append(out, PyBool_FromLong(PyUnstable_Object_IsUniquelyReferenced(PyLong_FromLong(1 << 20)) || 1));
    return out;
}

static PyMethodDef module_methods[] = {
    { "runtime_services", runtime_services, METH_NOARGS, NULL },
    { "dealloc_count", dealloc_count, METH_NOARGS, NULL },
    { "containers", containers, METH_NOARGS, NULL },
    { "codecs", codecs, METH_NOARGS, NULL },
    { "make_managed", make_managed, METH_NOARGS, NULL },
    { "make_meta_class", make_meta_class, METH_NOARGS, NULL },
    { "capsule_roundtrip", capsule_roundtrip, METH_NOARGS, NULL },
    { "capsule_import", capsule_import, METH_O, NULL },
    { "imports", imports, METH_NOARGS, NULL },
    { "strings", strings, METH_O, NULL },
    { "errors", errors, METH_NOARGS, NULL },
    { "dict_ops", dict_ops, METH_NOARGS, NULL },
    { "slices", slices, METH_VARARGS, NULL },
    { "locks", locks, METH_NOARGS, NULL },
    { "contextvars", contextvars, METH_NOARGS, NULL },
    { "datetimes", datetimes, METH_NOARGS, NULL },
    { "numbers", numbers, METH_NOARGS, NULL },
    { "objects", objects, METH_O, NULL },
    { NULL }
};

static PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "ready", "PyType_Ready and API families", -1, module_methods,
};

PyMODINIT_FUNC
PyInit_ready(void)
{
    if (PyType_Ready(&Vec_Type) < 0 || PyType_Ready(&Vec3_Type) < 0 || PyType_Ready(&Locked_Type) < 0
        || PyType_Ready(&Counter_Type) < 0 || PyType_Ready(&MyList_Type) < 0 || PyType_Ready(&FloatSub_Type) < 0
        || PyType_Ready(&Delegate_Type) < 0) {
        return NULL;
    }
    PyObject *m = PyModule_Create(&module);
    if (m == NULL
        || PyModule_AddObjectRef(m, "Vec", (PyObject *)&Vec_Type) < 0
        || PyModule_AddObjectRef(m, "Vec3", (PyObject *)&Vec3_Type) < 0
        || PyModule_AddObjectRef(m, "Locked", (PyObject *)&Locked_Type) < 0
        || PyModule_AddObjectRef(m, "Counter", (PyObject *)&Counter_Type) < 0
        || PyModule_AddObjectRef(m, "MyList", (PyObject *)&MyList_Type) < 0
        || PyModule_AddObjectRef(m, "FloatSub", (PyObject *)&FloatSub_Type) < 0
        || PyModule_AddObjectRef(m, "Delegate", (PyObject *)&Delegate_Type) < 0) {
        Py_XDECREF(m);
        return NULL;
    }
    PyUnstable_Module_SetGIL(m, Py_MOD_GIL_NOT_USED);
    return m;
}
