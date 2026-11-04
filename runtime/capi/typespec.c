/* Types made from specifications (Objects/typeobject.c PyType_FromMetaclass
   and its callers, PyType_GetSlot, type data). The kernel's type() makes the
   heap type, its MRO and its dict; this adds what the specification
   describes: sizes and offsets, flags, the slot functions, members, getsets,
   methods and __new__. */

#include "lucifer.h"

#include <stddef.h>
#include <string.h>

/* Where a slot id (typeslots.h) is stored: in the PyTypeObject, or in the
   struct it points to (sub >= 0). */
struct slot_offset {
    short slot, sub;
};

#define TP(id, f)  [id] = { offsetof(PyTypeObject, f), -1 }
#define SUB(id, table, T, f)  [id] = { offsetof(PyTypeObject, table), offsetof(T, f) }
#define NB(id, f) SUB(id, tp_as_number, PyNumberMethods, f)
#define SQ(id, f) SUB(id, tp_as_sequence, PySequenceMethods, f)
#define MP(id, f) SUB(id, tp_as_mapping, PyMappingMethods, f)
#define AM(id, f) SUB(id, tp_as_async, PyAsyncMethods, f)
#define BF(id, f) SUB(id, tp_as_buffer, PyBufferProcs, f)

static const struct slot_offset slot_offsets[] = {
    BF(Py_bf_getbuffer, bf_getbuffer), BF(Py_bf_releasebuffer, bf_releasebuffer),
    MP(Py_mp_ass_subscript, mp_ass_subscript), MP(Py_mp_length, mp_length),
    MP(Py_mp_subscript, mp_subscript),
    NB(Py_nb_absolute, nb_absolute), NB(Py_nb_add, nb_add), NB(Py_nb_and, nb_and),
    NB(Py_nb_bool, nb_bool), NB(Py_nb_divmod, nb_divmod), NB(Py_nb_float, nb_float),
    NB(Py_nb_floor_divide, nb_floor_divide), NB(Py_nb_index, nb_index),
    NB(Py_nb_inplace_add, nb_inplace_add), NB(Py_nb_inplace_and, nb_inplace_and),
    NB(Py_nb_inplace_floor_divide, nb_inplace_floor_divide),
    NB(Py_nb_inplace_lshift, nb_inplace_lshift), NB(Py_nb_inplace_multiply, nb_inplace_multiply),
    NB(Py_nb_inplace_or, nb_inplace_or), NB(Py_nb_inplace_power, nb_inplace_power),
    NB(Py_nb_inplace_remainder, nb_inplace_remainder), NB(Py_nb_inplace_rshift, nb_inplace_rshift),
    NB(Py_nb_inplace_subtract, nb_inplace_subtract),
    NB(Py_nb_inplace_true_divide, nb_inplace_true_divide), NB(Py_nb_inplace_xor, nb_inplace_xor),
    NB(Py_nb_int, nb_int), NB(Py_nb_invert, nb_invert), NB(Py_nb_lshift, nb_lshift),
    NB(Py_nb_multiply, nb_multiply), NB(Py_nb_negative, nb_negative), NB(Py_nb_or, nb_or),
    NB(Py_nb_positive, nb_positive), NB(Py_nb_power, nb_power), NB(Py_nb_remainder, nb_remainder),
    NB(Py_nb_rshift, nb_rshift), NB(Py_nb_subtract, nb_subtract),
    NB(Py_nb_true_divide, nb_true_divide), NB(Py_nb_xor, nb_xor),
    SQ(Py_sq_ass_item, sq_ass_item), SQ(Py_sq_concat, sq_concat), SQ(Py_sq_contains, sq_contains),
    SQ(Py_sq_inplace_concat, sq_inplace_concat), SQ(Py_sq_inplace_repeat, sq_inplace_repeat),
    SQ(Py_sq_item, sq_item), SQ(Py_sq_length, sq_length), SQ(Py_sq_repeat, sq_repeat),
    TP(Py_tp_alloc, tp_alloc), TP(Py_tp_call, tp_call), TP(Py_tp_clear, tp_clear),
    TP(Py_tp_dealloc, tp_dealloc), TP(Py_tp_del, tp_del), TP(Py_tp_descr_get, tp_descr_get),
    TP(Py_tp_descr_set, tp_descr_set), TP(Py_tp_getattr, tp_getattr), TP(Py_tp_getattro, tp_getattro),
    TP(Py_tp_hash, tp_hash), TP(Py_tp_init, tp_init), TP(Py_tp_is_gc, tp_is_gc),
    TP(Py_tp_iter, tp_iter), TP(Py_tp_iternext, tp_iternext), TP(Py_tp_methods, tp_methods),
    TP(Py_tp_new, tp_new), TP(Py_tp_repr, tp_repr), TP(Py_tp_richcompare, tp_richcompare),
    TP(Py_tp_setattr, tp_setattr), TP(Py_tp_setattro, tp_setattro), TP(Py_tp_str, tp_str),
    TP(Py_tp_traverse, tp_traverse), TP(Py_tp_members, tp_members), TP(Py_tp_getset, tp_getset),
    TP(Py_tp_free, tp_free),
    NB(Py_nb_matrix_multiply, nb_matrix_multiply),
    NB(Py_nb_inplace_matrix_multiply, nb_inplace_matrix_multiply),
    AM(Py_am_await, am_await), AM(Py_am_aiter, am_aiter), AM(Py_am_anext, am_anext),
    TP(Py_tp_finalize, tp_finalize), AM(Py_am_send, am_send), TP(Py_tp_vectorcall, tp_vectorcall),
};

#define NSLOTS (sizeof slot_offsets / sizeof slot_offsets[0])

/* Slots stored elsewhere than a plain field have an offset of 0 in the table
   above only when the id is not in use; Py_tp_base, bases, doc and token are
   handled by name. */
static int
slot_known(int id)
{
    return id > 0 && (size_t)id < NSLOTS && (slot_offsets[id].slot != 0 || slot_offsets[id].sub != 0);
}

/* tp_free over the runtime's object free (a freefunc takes void *). */
void
lu_free_object(void *p)
{
    lu_object_free(p);
}

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

static Py_ssize_t
align_up(Py_ssize_t n)
{
    return (n + (Py_ssize_t)_Alignof(max_align_t) - 1) & ~((Py_ssize_t)_Alignof(max_align_t) - 1);
}

/* The offset a member named __dictoffset__ and the like gives, relative to
   the type's data when the member says so. */
static int
special_offset(const PyMemberDef *m, Py_ssize_t data_offset, Py_ssize_t *out)
{
    if (m == NULL) {
        return 0;
    }
    if (m->type != Py_T_PYSSIZET || !(m->flags & Py_READONLY)) {
        PyErr_Format(PyExc_SystemError, "%s must be a read-only Py_ssize_t member", m->name);
        return -1;
    }
    *out = m->offset + ((m->flags & Py_RELATIVE_OFFSET) ? data_offset : 0);
    return 0;
}

static PyObject *
bases_tuple(PyObject *bases_in, PyType_Spec *spec)
{
    if (bases_in != NULL) {
        if (PyTuple_Check(bases_in)) {
            return Py_NewRef(bases_in);
        }
        if (PyType_Check(bases_in)) {
            return PyTuple_Pack(1, bases_in);
        }
        PyErr_SetString(PyExc_SystemError, "Py_tp_bases is not a tuple");
        return NULL;
    }
    PyObject *from_slots = NULL;
    for (PyType_Slot *s = spec->slots; s->slot; s++) {
        if (s->slot == Py_tp_bases) {
            from_slots = s->pfunc;
        }
        else if (s->slot == Py_tp_base && from_slots == NULL) {
            from_slots = PyTuple_Pack(1, (PyObject *)s->pfunc);
            if (from_slots == NULL) {
                return NULL;
            }
            return from_slots;
        }
    }
    if (from_slots != NULL) {
        return bases_tuple(from_slots, spec);
    }
    return PyTuple_Pack(1, (PyObject *)&PyBaseObject_Type);
}

/* The doc after its introspection signature, "Name(...)\n--\n\n", if it starts
   with one (Objects/typeobject.c _PyType_DocWithoutSignature). */
const char *
lu_doc_without_signature(const char *name, const char *doc)
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

/* tp_methods into a type's dict (Objects/typeobject.c type_add_methods). */
int
lu_type_add_methods(PyTypeObject *tp, PyObject *dict)
{
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
    return 0;
}

/* The bound __new__ of a type with a tp_new (add_tp_new_wrapper): the first
   argument names the type to make an instance of. The insert is native: the
   kernel's dict functions are not up when static types are readied. */
int
lu_type_add_new_wrapper(PyTypeObject *tp, PyObject *dict)
{
    PyObject *new = PyCFunction_NewEx(&tp_new_methoddef, (PyObject *)tp, NULL);
    int err = new == NULL;
    if (!err) {
        PyObject *name = PyUnicode_InternFromString("__new__");
        err = name == NULL || lu_ns_insert(dict, name, new) < 0;
        Py_XDECREF(name);
    }
    Py_XDECREF(new);
    return err ? -1 : 0;
}

#define REPLACED_FLAGS (Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE \
    | Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_HAVE_VECTORCALL | Py_TPFLAGS_ITEMS_AT_END \
    | Py_TPFLAGS_METHOD_DESCRIPTOR | Py_TPFLAGS_SEQUENCE | Py_TPFLAGS_MAPPING \
    | _Py_TPFLAGS_MATCH_SELF | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HAVE_FINALIZE)

PyObject *
PyType_FromMetaclass(PyTypeObject *metaclass, PyObject *module, PyType_Spec *spec, PyObject *bases_in)
{
    if (spec->name == NULL) {
        PyErr_SetString(PyExc_SystemError, "Type spec does not define the name field.");
        return NULL;
    }
    const PyMemberDef *members = NULL, *weaklist = NULL, *dict_member = NULL, *vectorcall = NULL;
    const char *doc = NULL;
    Py_ssize_t nmembers = 0;
    for (PyType_Slot *s = spec->slots; s->slot; s++) {
        int special = s->slot == Py_tp_base || s->slot == Py_tp_bases || s->slot == Py_tp_doc
            || s->slot == Py_tp_token;
        if (s->slot < 0 || (!special && !slot_known(s->slot))) {
            PyErr_SetString(PyExc_RuntimeError, "invalid slot offset");
            return NULL;
        }
        if (s->slot == Py_tp_members) {
            if (members != NULL) {
                PyErr_SetString(PyExc_SystemError, "Multiple Py_tp_members slots are not supported.");
                return NULL;
            }
            members = s->pfunc;
            for (const PyMemberDef *m = members; m->name != NULL; m++, nmembers++) {
                if ((m->flags & Py_RELATIVE_OFFSET)
                    && (spec->basicsize > 0 || m->offset < 0 || m->offset >= -spec->basicsize)) {
                    PyErr_SetString(PyExc_SystemError, spec->basicsize > 0
                                    ? "With Py_RELATIVE_OFFSET, basicsize must be negative."
                                    : "Member offset out of range (0..-basicsize)");
                    return NULL;
                }
                if (!strcmp(m->name, "__weaklistoffset__")) {
                    weaklist = m;
                }
                else if (!strcmp(m->name, "__dictoffset__")) {
                    dict_member = m;
                }
                else if (!strcmp(m->name, "__vectorcalloffset__")) {
                    vectorcall = m;
                }
            }
        }
        else if (s->slot == Py_tp_doc) {
            doc = s->pfunc;
        }
    }
    const char *dot = strrchr(spec->name, '.');
    PyObject *bases = bases_tuple(bases_in, spec);
    PyObject *name = PyUnicode_FromString(dot != NULL ? dot + 1 : spec->name);
    PyObject *ns = PyDict_New();
    PyObject *slots = PyTuple_New(0);
    /* A type with managed dict or weakref storage is a class without __slots__; any other has no dict. */
    int managed = (spec->flags & (Py_TPFLAGS_MANAGED_DICT | Py_TPFLAGS_MANAGED_WEAKREF)) != 0;
    PyObject *modname = dot != NULL ? PyUnicode_FromStringAndSize(spec->name, dot - spec->name) : NULL;
    PyObject *cls = NULL;
    if (bases == NULL || name == NULL || ns == NULL || slots == NULL
        || (dot != NULL && modname == NULL)
        || (!managed && PyDict_SetItemString(ns, "__slots__", slots) < 0)
        || (modname != NULL && PyDict_SetItemString(ns, "__module__", modname) < 0)) {
        goto done;
    }
    /* type.__new__ itself: the metaclass may forbid instantiation (Cython's does),
       which only stops code calling it. */
    PyObject *type_new = PyObject_GetAttr((PyObject *)&PyType_Type, LU_ID(__new__));
    PyObject *args[4] = { metaclass != NULL ? (PyObject *)metaclass : (PyObject *)&PyType_Type, name, bases, ns };
    cls = type_new != NULL ? lu_call(type_new, args, 4, NULL) : NULL;
    Py_XDECREF(type_new);
    if (cls == NULL) {
        goto done;
    }

    PyTypeObject *tp = (PyTypeObject *)cls, *base = tp->tp_base;
    PyHeapTypeObject *et = (PyHeapTypeObject *)cls;
    Py_ssize_t basicsize = spec->basicsize, data_offset = spec->basicsize;
    if (basicsize == 0) {
        basicsize = base->tp_basicsize;
    }
    else if (basicsize < 0) {
        data_offset = align_up(base->tp_basicsize);
        basicsize = data_offset + align_up(-spec->basicsize);
        if (base->tp_itemsize && !((base->tp_flags | spec->flags) & Py_TPFLAGS_ITEMS_AT_END)) {
            PyErr_SetString(PyExc_SystemError,
                            "Cannot extend variable-size class without Py_TPFLAGS_ITEMS_AT_END.");
            goto fail;
        }
    }
    Py_ssize_t weaklistoffset = 0, dictoffset = 0, vectorcalloffset = 0;
    if (special_offset(weaklist, data_offset, &weaklistoffset) < 0
        || special_offset(dict_member, data_offset, &dictoffset) < 0
        || special_offset(vectorcall, data_offset, &vectorcalloffset) < 0) {
        goto fail;
    }
    tp->tp_flags = (tp->tp_flags & ~(unsigned long)REPLACED_FLAGS) | (spec->flags & REPLACED_FLAGS)
        | (base->tp_flags & Py_TPFLAGS_HAVE_GC);
    tp->tp_basicsize = basicsize;
    if (spec->itemsize != 0) {
        tp->tp_itemsize = spec->itemsize;        /* 0 inherits the base's */
    }
    tp->tp_vectorcall_offset = vectorcalloffset;
    if (weaklistoffset) {
        tp->tp_weaklistoffset = weaklistoffset;
    }
    if (dictoffset) {
        tp->tp_dictoffset = dictoffset;
    }
    Py_XSETREF(et->ht_module, Py_XNewRef(module));
    et->_ht_tpname = PyMem_Malloc(strlen(spec->name) + 1);   /* tp_name is the full dotted name */
    if (et->_ht_tpname == NULL) {
        PyErr_NoMemory();
        goto fail;
    }
    strcpy(et->_ht_tpname, spec->name);
    tp->tp_name = et->_ht_tpname;

    PyMemberDef *own_members = NULL;
    for (PyType_Slot *s = spec->slots; s->slot; s++) {
        switch (s->slot) {
        case Py_tp_base:
        case Py_tp_bases:
        case Py_tp_doc:
            break;
        case Py_tp_token:
            et->ht_token = s->pfunc == Py_TP_USE_SPEC ? spec : s->pfunc;
            break;
        case Py_tp_members:
            own_members = PyMem_Calloc(nmembers + 1, sizeof(PyMemberDef));
            if (own_members == NULL) {
                PyErr_NoMemory();
                goto fail;
            }
            memcpy(own_members, members, nmembers * sizeof(PyMemberDef));
            for (PyMemberDef *m = own_members; m->name != NULL; m++) {
                if (m->flags & Py_RELATIVE_OFFSET) {
                    m->flags &= ~Py_RELATIVE_OFFSET;
                    m->offset += data_offset;
                }
            }
            tp->tp_members = own_members;
            break;
        default: {
            const struct slot_offset *o = &slot_offsets[s->slot];
            char *table = (char *)tp + o->slot;
            if (o->sub >= 0) {
                table = *(char **)table;
                *(void **)(table + o->sub) = s->pfunc;
            }
            else {
                *(void **)table = s->pfunc;
            }
        }
        }
    }
    if (spec->flags & Py_TPFLAGS_DISALLOW_INSTANTIATION) {
        tp->tp_new = NULL;
    }
    if (tp->tp_free == NULL) {
        tp->tp_free = lu_free_object;
    }
    if (doc != NULL) {
        char *copy = PyMem_Malloc(strlen(doc) + 1);
        if (copy == NULL) {
            PyErr_NoMemory();
            goto fail;
        }
        strcpy(copy, doc);
        tp->tp_doc = copy;
    }
    PyObject *dict = lu_type_dict(tp);
    if (!managed && PyDict_DelItemString(dict, "__slots__") < 0) {   /* only there to keep the instances free of a __dict__ */
        goto fail;
    }
    if (lu_type_add_operators(tp) < 0
        || lu_type_add_descriptors(tp) < 0
        || lu_type_add_methods(tp, dict) < 0
        || (tp->tp_new != NULL && tp->tp_new != base->tp_new && !lu_tp_new_is_thunk(tp->tp_new)
            && lu_type_add_new_wrapper(tp, dict) < 0)) {
        goto fail;
    }
    if (doc != NULL) {
        const char *text = lu_doc_without_signature(spec->name, doc);
        PyObject *d = *text != '\0' ? PyUnicode_FromString(text) : Py_NewRef(Py_None);
        if (d == NULL || PyDict_SetItemString(dict, "__doc__", d) < 0) {
            Py_XDECREF(d);
            goto fail;
        }
        Py_DECREF(d);
    }
    for (int i = 0; i < 3; i++) {
        const PyMemberDef *special = i == 0 ? weaklist : i == 1 ? dict_member : vectorcall;
        if (special != NULL && PyDict_DelItemString(dict, special->name) < 0) {
            goto fail;
        }
    }
    lu_type_modified(tp);
    goto done;
fail:
    Py_CLEAR(cls);
done:
    Py_XDECREF(bases);
    Py_XDECREF(name);
    Py_XDECREF(ns);
    Py_XDECREF(slots);
    Py_XDECREF(modname);
    return cls;
}

PyObject *
PyType_FromModuleAndSpec(PyObject *module, PyType_Spec *spec, PyObject *bases)
{
    return PyType_FromMetaclass(NULL, module, spec, bases);
}

PyObject *
PyType_FromSpecWithBases(PyType_Spec *spec, PyObject *bases)
{
    return PyType_FromMetaclass(NULL, NULL, spec, bases);
}

PyObject *
PyType_FromSpec(PyType_Spec *spec)
{
    return PyType_FromMetaclass(NULL, NULL, spec, NULL);
}

void *
PyType_GetSlot(PyTypeObject *type, int id)
{
    if (id <= 0 || (size_t)id >= NSLOTS || !slot_known(id)) {
        if (id == Py_tp_base) {
            return type->tp_base;
        }
        if (id == Py_tp_bases) {
            return type->tp_bases;
        }
        if (id == Py_tp_doc) {
            return (void *)type->tp_doc;
        }
        PyErr_BadInternalCall();
        return NULL;
    }
    const struct slot_offset *o = &slot_offsets[id];
    char *table = (char *)type + o->slot;
    if (o->sub >= 0) {
        table = *(char **)table;
        if (table == NULL) {
            return NULL;
        }
        return *(void **)(table + o->sub);
    }
    return *(void **)table;
}

PyObject *
PyType_GetModule(PyTypeObject *type)
{
    if (!(type->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_TypeError, "PyType_GetModule: Type '%s' is not a heap type", type->tp_name);
        return NULL;
    }
    PyObject *m = ((PyHeapTypeObject *)type)->ht_module;
    if (m == NULL) {
        PyErr_Format(PyExc_TypeError, "PyType_GetModule: Type '%s' has no associated module",
                     type->tp_name);
    }
    return m;
}

void *
PyType_GetModuleState(PyTypeObject *type)
{
    PyObject *m = PyType_GetModule(type);
    return m != NULL ? PyModule_GetState(m) : NULL;
}

void *
PyObject_GetTypeData(PyObject *obj, PyTypeObject *cls)
{
    if (!PyObject_TypeCheck(obj, cls)) {
        PyErr_Format(PyExc_TypeError, "descriptor requires a '%s' object but received '%s'",
                     cls->tp_name, Py_TYPE(obj)->tp_name);
        return NULL;
    }
    if (cls->tp_base == NULL || !(cls->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        PyErr_Format(PyExc_TypeError, "type '%s' has no type data", cls->tp_name);
        return NULL;
    }
    return (char *)obj + align_up(cls->tp_base->tp_basicsize);
}

Py_ssize_t
PyType_GetTypeDataSize(PyTypeObject *cls)
{
    if (cls->tp_base == NULL || !(cls->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        return 0;
    }
    Py_ssize_t size = cls->tp_basicsize - align_up(cls->tp_base->tp_basicsize);
    return size > 0 ? size : 0;
}

int
PyModule_AddType(PyObject *module, PyTypeObject *type)
{
    const char *name = strrchr(type->tp_name, '.');
    name = name != NULL ? name + 1 : type->tp_name;
    return PyModule_AddObjectRef(module, name, (PyObject *)type);
}
