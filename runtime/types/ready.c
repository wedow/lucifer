/* PyType_Ready for static extension types (Objects/typeobject.c type_ready):
   base, metatype, bases, MRO and dict; the dict's descriptors, wrappers,
   __new__ and __doc__ (lu_type_fill_dict, shared with PyType_FromSpec);
   then the slots and flags inherited from the bases. Static builtin types
   are made ready at startup instead (runtime/prim/type.c). */

#include "lucifer.h"

#include <string.h>

/* The bound __new__ of a type with a tp_new (Objects/typeobject.c tp_new_wrapper):
   the first argument is the type to make an instance of. */
static PyObject *
tp_new_wrapper(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTypeObject *type = (PyTypeObject *)self;
    if (PyTuple_GET_SIZE(args) < 1) {
        PyErr_Format(PyExc_TypeError, "%s.__new__(): not enough arguments", type->tp_name);
        return NULL;
    }
    PyObject *arg0 = PyTuple_GET_ITEM(args, 0);
    if (!PyType_Check(arg0)) {
        PyErr_Format(PyExc_TypeError, "%s.__new__(X): X is not a type object (%s)", type->tp_name,
                     Py_TYPE(arg0)->tp_name);
        return NULL;
    }
    PyTypeObject *subtype = (PyTypeObject *)arg0;
    if (!PyType_IsSubtype(subtype, type)) {
        PyErr_Format(PyExc_TypeError, "%s.__new__(%s): %s is not a subtype of %s", type->tp_name,
                     subtype->tp_name, subtype->tp_name, type->tp_name);
        return NULL;
    }
    PyObject *rest = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args));
    if (rest == NULL) {
        return NULL;
    }
    PyObject *result = type->tp_new(subtype, rest, kwds);
    Py_DECREF(rest);
    return result;
}

static PyMethodDef tp_new_methoddef = {
    "__new__", (PyCFunction)(void (*)(void))tp_new_wrapper, METH_VARARGS | METH_KEYWORDS,
    "Create and return a new object.  See help(type) for accurate signature."
};

/* The doc after its introspection signature, "Name(...)\n--\n\n", if it starts
   with one (Objects/typeobject.c _PyType_DocWithoutSignature). */
static const char *
doc_without_signature(const char *name, const char *doc)
{
    const char *dot = strrchr(name, '.');
    name = dot != NULL ? dot + 1 : name;
    size_t n = strlen(name);
    if (strncmp(doc, name, n) != 0 || doc[n] != '(') {
        return doc;
    }
    for (const char *p = doc + n; *p; p++) {
        if (strncmp(p, ")\n--\n\n", 6) == 0) {
            return p + 6;
        }
        if (p[0] == '\n' && p[1] == '\n') {
            break;
        }
    }
    return doc;
}

int
lu_type_fill_dict(PyTypeObject *tp)
{
    PyObject *dict = lu_type_dict(tp);
    if (lu_type_add_operators(tp) < 0 || lu_type_add_descriptors(tp) < 0) {
        return -1;
    }
    for (PyMethodDef *ml = tp->tp_methods; ml != NULL && ml->ml_name != NULL; ml++) {
        PyObject *d = (ml->ml_flags & METH_CLASS) ? lu_descr_new_classmethod(tp, ml)
                    : (ml->ml_flags & METH_STATIC) ? PyCFunction_NewEx(ml, NULL, NULL)
                    : PyDescr_NewMethod(tp, ml);
        if (d == NULL || PyDict_SetItemString(dict, ml->ml_name, d) < 0) {
            Py_XDECREF(d);
            return -1;
        }
        Py_DECREF(d);
    }
    if (tp->tp_new != NULL && tp->tp_new != tp->tp_base->tp_new && !lu_tp_new_is_thunk(tp->tp_new)
        && PyDict_GetItemString(dict, "__new__") == NULL) {
        PyObject *new = PyCFunction_NewEx(&tp_new_methoddef, (PyObject *)tp, NULL);
        if (new == NULL || PyDict_SetItemString(dict, "__new__", new) < 0) {
            Py_XDECREF(new);
            return -1;
        }
        Py_DECREF(new);
    }
    if (tp->tp_doc != NULL || PyDict_GetItemString(dict, "__doc__") == NULL) {
        const char *text = tp->tp_doc != NULL ? doc_without_signature(tp->tp_name, tp->tp_doc) : "";
        PyObject *d = *text != '\0' ? PyUnicode_FromString(text) : Py_NewRef(Py_None);
        if (d == NULL || PyDict_SetItemString(dict, "__doc__", d) < 0) {
            Py_XDECREF(d);
            return -1;
        }
        Py_DECREF(d);
    }
    return 0;
}

/* The MRO of a type with the given bases: the base's own when there is one
   (the C3 merge of a single list), else the kernel's. */
static PyObject *
make_mro(PyTypeObject *tp)
{
    if (PyTuple_GET_SIZE(tp->tp_bases) != 1) {
        PyObject *args[1] = { (PyObject *)tp };
        PyObject *mro = lu_call(PyDict_GetItemString(lu_type_dict(&PyType_Type), "mro"), args, 1, NULL);
        if (mro == NULL || PyTuple_Check(mro)) {
            return mro;
        }
        PyObject *t = PySequence_Tuple(mro);
        Py_DECREF(mro);
        return t;
    }
    PyObject *base_mro = ((PyTypeObject *)PyTuple_GET_ITEM(tp->tp_bases, 0))->tp_mro;
    Py_ssize_t n = PyTuple_GET_SIZE(base_mro);
    PyObject *mro = PyTuple_New(n + 1);
    if (mro == NULL) {
        return NULL;
    }
    PyTuple_SET_ITEM(mro, 0, Py_NewRef(tp));
    for (Py_ssize_t i = 0; i < n; i++) {
        PyTuple_SET_ITEM(mro, i + 1, Py_NewRef(PyTuple_GET_ITEM(base_mro, i)));
    }
    return mro;
}

/* inherit_special: the flags and non-function slots a type takes from its base. */
#define INHERITED_FLAGS (Py_TPFLAGS_PREHEADER | Py_TPFLAGS_BASE_EXC_SUBCLASS \
    | Py_TPFLAGS_TYPE_SUBCLASS | Py_TPFLAGS_LONG_SUBCLASS | Py_TPFLAGS_BYTES_SUBCLASS \
    | Py_TPFLAGS_UNICODE_SUBCLASS | Py_TPFLAGS_TUPLE_SUBCLASS | Py_TPFLAGS_LIST_SUBCLASS \
    | Py_TPFLAGS_DICT_SUBCLASS | _Py_TPFLAGS_MATCH_SELF | Py_TPFLAGS_ITEMS_AT_END)

static void
inherit_special(PyTypeObject *tp, PyTypeObject *base)
{
    if (!(tp->tp_flags & Py_TPFLAGS_HAVE_GC) && (base->tp_flags & Py_TPFLAGS_HAVE_GC)
        && tp->tp_traverse == NULL && tp->tp_clear == NULL) {
        tp->tp_flags |= Py_TPFLAGS_HAVE_GC;
        tp->tp_traverse = base->tp_traverse;
        tp->tp_clear = base->tp_clear;
    }
    tp->tp_flags |= base->tp_flags & INHERITED_FLAGS;
    if (tp->tp_basicsize == 0) {
        tp->tp_basicsize = base->tp_basicsize;
    }
    if (tp->tp_itemsize == 0) {
        tp->tp_itemsize = base->tp_itemsize;
    }
    if (tp->tp_weaklistoffset == 0) {
        tp->tp_weaklistoffset = base->tp_weaklistoffset;
    }
    if (tp->tp_dictoffset == 0) {
        tp->tp_dictoffset = base->tp_dictoffset;
    }
}

/* A slot of base that base did not itself inherit. */
#define DEFINED(SLOT) (base->SLOT != 0 && (basebase == NULL || base->SLOT != basebase->SLOT))
#define COPY(SLOT) if (!tp->SLOT && DEFINED(SLOT)) tp->SLOT = base->SLOT
#define COPY_SUB(TABLE, SLOT) \
    if (tp->TABLE != NULL && base->TABLE != NULL) { \
        PyTypeObject *bb = basebase != NULL && basebase->TABLE != NULL ? basebase : NULL; \
        if (!tp->TABLE->SLOT && base->TABLE->SLOT && (bb == NULL || base->TABLE->SLOT != bb->TABLE->SLOT)) \
            tp->TABLE->SLOT = base->TABLE->SLOT; \
    }

static void
inherit_slots(PyTypeObject *tp, PyTypeObject *base)
{
    PyTypeObject *basebase = base->tp_base;
#define NUM(S) COPY_SUB(tp_as_number, S)
    NUM(nb_add); NUM(nb_subtract); NUM(nb_multiply); NUM(nb_remainder); NUM(nb_divmod);
    NUM(nb_power); NUM(nb_negative); NUM(nb_positive); NUM(nb_absolute); NUM(nb_bool);
    NUM(nb_invert); NUM(nb_lshift); NUM(nb_rshift); NUM(nb_and); NUM(nb_xor); NUM(nb_or);
    NUM(nb_int); NUM(nb_float); NUM(nb_inplace_add); NUM(nb_inplace_subtract);
    NUM(nb_inplace_multiply); NUM(nb_inplace_remainder); NUM(nb_inplace_power);
    NUM(nb_inplace_lshift); NUM(nb_inplace_rshift); NUM(nb_inplace_and); NUM(nb_inplace_xor);
    NUM(nb_inplace_or); NUM(nb_true_divide); NUM(nb_floor_divide); NUM(nb_inplace_true_divide);
    NUM(nb_inplace_floor_divide); NUM(nb_index); NUM(nb_matrix_multiply);
    NUM(nb_inplace_matrix_multiply);
#undef NUM
    COPY_SUB(tp_as_async, am_await); COPY_SUB(tp_as_async, am_aiter); COPY_SUB(tp_as_async, am_anext);
    COPY_SUB(tp_as_sequence, sq_length); COPY_SUB(tp_as_sequence, sq_concat);
    COPY_SUB(tp_as_sequence, sq_repeat); COPY_SUB(tp_as_sequence, sq_item);
    COPY_SUB(tp_as_sequence, sq_ass_item); COPY_SUB(tp_as_sequence, sq_contains);
    COPY_SUB(tp_as_sequence, sq_inplace_concat); COPY_SUB(tp_as_sequence, sq_inplace_repeat);
    COPY_SUB(tp_as_mapping, mp_length); COPY_SUB(tp_as_mapping, mp_subscript);
    COPY_SUB(tp_as_mapping, mp_ass_subscript);
    COPY_SUB(tp_as_buffer, bf_getbuffer); COPY_SUB(tp_as_buffer, bf_releasebuffer);

    COPY(tp_dealloc);
    if (tp->tp_getattr == NULL && tp->tp_getattro == NULL) {
        tp->tp_getattr = base->tp_getattr;
        tp->tp_getattro = base->tp_getattro;
    }
    if (tp->tp_setattr == NULL && tp->tp_setattro == NULL) {
        tp->tp_setattr = base->tp_setattr;
        tp->tp_setattro = base->tp_setattro;
    }
    COPY(tp_repr);
    COPY(tp_vectorcall_offset);
    if (!tp->tp_call && (base->tp_flags & Py_TPFLAGS_HAVE_VECTORCALL)) {
        tp->tp_flags |= Py_TPFLAGS_HAVE_VECTORCALL;
    }
    COPY(tp_call);
    COPY(tp_str);
    /* Comparison and hash go together unless the type overrides either. */
    if (tp->tp_richcompare == NULL && tp->tp_hash == NULL) {
        PyObject *dict = lu_type_dict(tp);
        if (PyDict_GetItemString(dict, "__eq__") == NULL && PyDict_GetItemString(dict, "__hash__") == NULL) {
            tp->tp_richcompare = base->tp_richcompare;
            tp->tp_hash = base->tp_hash;
        }
    }
    COPY(tp_iter);
    COPY(tp_iternext);
    COPY(tp_descr_get);
    if (base->tp_descr_get && tp->tp_descr_get == base->tp_descr_get
        && (base->tp_flags & Py_TPFLAGS_METHOD_DESCRIPTOR)) {
        tp->tp_flags |= Py_TPFLAGS_METHOD_DESCRIPTOR;
    }
    COPY(tp_descr_set);
    COPY(tp_init);
    COPY(tp_alloc);
    COPY(tp_is_gc);
    COPY(tp_finalize);
    if ((tp->tp_flags & Py_TPFLAGS_HAVE_GC) == (base->tp_flags & Py_TPFLAGS_HAVE_GC)) {
        COPY(tp_free);
    }
    else if ((tp->tp_flags & Py_TPFLAGS_HAVE_GC) && tp->tp_free == NULL && base->tp_free == PyObject_Free) {
        tp->tp_free = PyObject_GC_Del;
    }
}

static void
free_object(void *p)
{
    lu_object_free(p);
}

int
PyType_Ready(PyTypeObject *tp)
{
    if (tp->tp_flags & Py_TPFLAGS_READY) {
        return 0;
    }
    if (tp->tp_name == NULL) {
        PyErr_SetString(PyExc_SystemError, "Type does not define the tp_name field.");
        return -1;
    }
    if (tp->tp_base == NULL) {
        tp->tp_base = &PyBaseObject_Type;
    }
    PyTypeObject *base = tp->tp_base;
    if (!(base->tp_flags & Py_TPFLAGS_READY) && PyType_Ready(base) < 0) {
        return -1;
    }
    if (Py_TYPE(tp) == NULL) {
        Py_SET_TYPE(tp, Py_TYPE(base));
    }
    if (tp->tp_bases == NULL && (tp->tp_bases = PyTuple_Pack(1, (PyObject *)base)) == NULL) {
        return -1;
    }
    if (tp->tp_dict == NULL && (tp->tp_dict = PyDict_New()) == NULL) {
        return -1;
    }
    if (tp->tp_mro == NULL && (tp->tp_mro = make_mro(tp)) == NULL) {
        return -1;
    }
    if (!(tp->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        tp->tp_flags |= Py_TPFLAGS_IMMUTABLETYPE;
    }
    if (tp->tp_new == NULL && base == &PyBaseObject_Type && !(tp->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        tp->tp_flags |= Py_TPFLAGS_DISALLOW_INSTANTIATION;
    }
    if (lu_type_fill_dict(tp) < 0) {
        return -1;
    }
    inherit_special(tp, base);
    for (Py_ssize_t i = 1; i < PyTuple_GET_SIZE(tp->tp_mro); i++) {
        inherit_slots(tp, (PyTypeObject *)PyTuple_GET_ITEM(tp->tp_mro, i));
    }
    if (tp->tp_new == NULL && !(tp->tp_flags & Py_TPFLAGS_DISALLOW_INSTANTIATION)) {
        tp->tp_new = base->tp_new;
    }
    if (tp->tp_alloc == NULL) {
        tp->tp_alloc = PyType_GenericAlloc;
    }
    if (tp->tp_free == NULL) {
        tp->tp_free = free_object;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(tp->tp_bases); i++) {
        if (lu_type_add_subclass((PyTypeObject *)PyTuple_GET_ITEM(tp->tp_bases, i), tp) < 0) {
            return -1;
        }
    }
    tp->tp_flags |= Py_TPFLAGS_READY;
    lu_type_modified(tp);
    return 0;
}

PyObject *
PyType_GenericNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return type->tp_alloc(type, 0);
}
