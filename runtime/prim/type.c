/* Types (Objects/typeobject.c, layout halves): static builtin types made
   ready natively, the type dict of a static type in interpreter state,
   _PyType_Lookup over tp_mro, heap type and instance layouts, and the type
   and object primitives kernel Python builds class semantics on (DESIGN 4.4).

   A heap type's instances carry the two-word preheader of
   Py_TPFLAGS_MANAGED_DICT | Py_TPFLAGS_MANAGED_WEAKREF: the dict pointer
   at -1 word and the weakref list at -2. Attribute values stay in that dict;
   inline values behind shared keys are not built yet.

   Version tags follow 3.14: 0 means none. A static builtin type gets its tag
   at startup from the runtime-wide range below _Py_TYPE_BASE_VERSION_TAG; any
   other type gets one when something needs it (a lookup, PyType_Watch, the
   specializer), from its interpreter's counter above that range, after its
   bases. lu_type_modified tells the type's watchers, then clears its tag and
   its subclasses'; the next lookup assigns a new one. */

#include "interp/frame.h"
#include "interp/code.h"
#include "interp/exceptions.h"
#include "prim/prim.h"

#include <string.h>

#define MANAGED_WEAKREF_OFFSET (-2 * (Py_ssize_t)sizeof(PyObject *))
#define PREHEADER_SIZE (2 * sizeof(PyObject *))
#define VERSION_TAG_BASE (2u << 16)     /* _Py_TYPE_BASE_VERSION_TAG */

extern PyTypeObject *const lu_static_types[];

static size_t
preheader_size(PyTypeObject *tp)
{
    return (tp->tp_flags & Py_TPFLAGS_PREHEADER) ? PREHEADER_SIZE : 0;
}

static int
is_static_builtin(PyTypeObject *tp)
{
    return (tp->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN) != 0;
}

static struct lu_static_type_state *
static_state(PyTypeObject *tp)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    return &interp->static_types[(size_t)tp->tp_subclasses - 1];
}

PyObject *
lu_type_dict(PyTypeObject *tp)
{
    return is_static_builtin(tp) ? static_state(tp)->tp_dict : tp->tp_dict;
}

static PyObject **
subclasses_ref(PyTypeObject *tp)
{
    return is_static_builtin(tp) ? &static_state(tp)->tp_subclasses : (PyObject **)&tp->tp_subclasses;
}

/* A type's subclasses are weak references: a class nothing else holds dies
   (CPython's tp_subclasses is a dict of weakrefs). The list keeps creation
   order, which __subclasses__ shows. */

/* The subclass a reference in the list names, or NULL once it is dead. */
static PyTypeObject *
subclass_of(PyObject *ref)
{
    PyObject *ob = ((PyWeakReference *)ref)->wr_object;
    return ob == Py_None ? NULL : (PyTypeObject *)ob;
}

static int
add_subclass(PyTypeObject *base, PyTypeObject *tp)
{
    PyObject **list = subclasses_ref(base);
    if (*list == NULL && (*list = PyList_New(0)) == NULL) {
        return -1;
    }
    PyObject *ref = lu_weakref_new(&_PyWeakref_RefType, (PyObject *)tp, NULL);
    if (ref == NULL) {
        return -1;
    }
    int err = lu_list_append(*list, ref);
    Py_DECREF(ref);
    return err;
}

/* Drops tp from base's subclasses, and every entry already dead. */
static void
remove_subclass(PyTypeObject *base, PyTypeObject *tp)
{
    PyListObject *list = (PyListObject *)*subclasses_ref(base);
    for (Py_ssize_t i = list != NULL ? Py_SIZE(list) : 0; --i >= 0; ) {
        PyTypeObject *sub = subclass_of(list->ob_item[i]);
        if (sub == NULL || sub == tp) {
            PyObject *ref = list->ob_item[i];
            memmove(&list->ob_item[i], &list->ob_item[i + 1],
                    (Py_SIZE(list) - i - 1) * sizeof(PyObject *));
            Py_SET_SIZE(list, Py_SIZE(list) - 1);
            Py_DECREF(ref);
        }
    }
}

/* A type gets at most this many tags (MAX_VERSIONS_PER_CLASS); _Py_ATTR_CACHE_UNUSED
   marks one that may never be cached again. */
#define MAX_VERSIONS_PER_CLASS 1000
#define MCACHE_MAX_ATTR_SIZE 100

static void
set_version(PyTypeObject *tp, unsigned int version)
{
    if (version != 0) {
        tp->tp_versions_used++;
    }
    __atomic_store_n(&tp->tp_version_tag, version, __ATOMIC_RELEASE);
}

/* next_global_version_tag: the runtime-wide range, shared by all interpreters. */
static unsigned int
next_global_version_tag(void)
{
    unsigned int old = __atomic_load_n(&_PyRuntime.next_static_version_tag, __ATOMIC_RELAXED);
    do {
        if (old >= LU_VERSION_TAG_STATIC_MAX) {
            return 0;
        }
    } while (!__atomic_compare_exchange_n(&_PyRuntime.next_static_version_tag, &old, old + 1, 0,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return old + 1;
}

int
lu_type_assign_version(PyTypeObject *tp)
{
    if (tp->tp_version_tag != 0) {
        return 1;
    }
    if (!(tp->tp_flags & Py_TPFLAGS_READY) || tp->tp_versions_used >= MAX_VERSIONS_PER_CLASS) {
        return 0;
    }
    PyObject *bases = tp->tp_bases;
    for (Py_ssize_t i = 0; bases != NULL && i < PyTuple_GET_SIZE(bases); i++) {
        if (!lu_type_assign_version((PyTypeObject *)PyTuple_GET_ITEM(bases, i))) {
            return 0;
        }
    }
    unsigned int tag;
    if (tp->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) {
        tag = next_global_version_tag();
    }
    else {
        tag = PyInterpreterState_Get()->next_version_tag;
        if (tag != 0) {
            PyInterpreterState_Get()->next_version_tag = tag + 1;      /* wraps to 0 when spent */
        }
    }
    if (tag == 0) {
        return 0;
    }
    set_version(tp, tag);
    return 1;
}

int
PyUnstable_Type_AssignVersionTag(PyTypeObject *tp)
{
    return lu_type_assign_version(tp);
}

static void
notify_type_watchers(PyTypeObject *tp)
{
    PyInterpreterState *interp = PyInterpreterState_Get();
    for (int i = 0, bits = tp->tp_watched; bits != 0; i++, bits >>= 1) {
        PyType_WatchCallback cb = (PyType_WatchCallback)interp->type_watchers[i];
        if ((bits & 1) && cb != NULL && cb(tp) < 0) {
            char message[64];
            snprintf(message, sizeof message, "Exception ignored in type watcher callback #%d for", i);
            lu_err_unraisable_text(message, (PyObject *)tp);
        }
    }
}

void
lu_type_modified(PyTypeObject *tp)
{
    if (tp->tp_version_tag == 0) {
        return;
    }
    PyListObject *list = (PyListObject *)*subclasses_ref(tp);
    for (Py_ssize_t i = 0; list != NULL && i < Py_SIZE(list); i++) {
        PyTypeObject *sub = subclass_of(list->ob_item[i]);
        if (sub != NULL) {
            lu_type_modified(sub);
        }
    }
    notify_type_watchers(tp);
    set_version(tp, 0);
}

void
PyType_Modified(PyTypeObject *tp)
{
    lu_type_modified(tp);
}

/* The watcher API (typeobject.c). Id 0 is CPython's own, so ids start at 1. */

static lu_watcher *
type_watchers(void)
{
    return PyInterpreterState_Get()->type_watchers;
}

int
PyType_AddWatcher(PyType_WatchCallback callback)
{
    return lu_watcher_add(type_watchers(), 1, (lu_watcher)callback, "type");
}

static int
check_type_watcher(int id)
{
    return lu_watcher_check(type_watchers(), id, "Invalid type watcher ID %d",
                            "No type watcher set for ID %d");
}

int
PyType_ClearWatcher(int id)
{
    if (check_type_watcher(id) < 0) {
        return -1;
    }
    type_watchers()[id] = NULL;
    return 0;
}

int
PyType_Watch(int id, PyObject *obj)
{
    if (!PyType_Check(obj)) {
        lu_err_format(PyExc_ValueError, "Cannot watch non-type");
        return -1;
    }
    if (check_type_watcher(id) < 0) {
        return -1;
    }
    /* A tag is what makes the next change deliver an event. */
    lu_type_assign_version((PyTypeObject *)obj);
    ((PyTypeObject *)obj)->tp_watched |= (unsigned char)(1 << id);
    return 0;
}

int
PyType_Unwatch(int id, PyObject *obj)
{
    if (!PyType_Check(obj)) {
        lu_err_format(PyExc_ValueError, "Cannot watch non-type");
        return -1;
    }
    if (check_type_watcher(id) < 0) {
        return -1;
    }
    ((PyTypeObject *)obj)->tp_watched &= (unsigned char)~(1 << id);
    return 0;
}

PyObject *
_PyType_Lookup(PyTypeObject *tp, PyObject *name)
{
    /* The method cache's assignment: a lookup is what gives a type its tag. */
    if (tp->tp_version_tag == 0 && tp->tp_versions_used < MAX_VERSIONS_PER_CLASS
        && PyUnicode_CheckExact(name) && PyUnicode_GET_LENGTH(name) <= MCACHE_MAX_ATTR_SIZE) {
        lu_type_assign_version(tp);
    }
    PyObject *mro = tp->tp_mro;
    if (mro == NULL) {
        /* During creation, before the MRO exists: the type, then its bases. */
        for (; tp != NULL; tp = tp->tp_base) {
            PyObject *v = lu_ns_lookup(lu_type_dict(tp), name);
            if (v != NULL) {
                return v;
            }
        }
        return NULL;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); i++) {
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);
        PyObject *dict = lu_type_dict(base);
        PyObject *v = dict != NULL ? lu_ns_lookup(dict, name) : NULL;
        if (v != NULL) {
            return v;
        }
    }
    return NULL;
}

PyObject *
_PyType_LookupRef(PyTypeObject *tp, PyObject *name)
{
    return Py_XNewRef(_PyType_Lookup(tp, name));
}

int
PyType_IsSubtype(PyTypeObject *a, PyTypeObject *b)
{
    PyObject *mro = a->tp_mro;
    if (mro == NULL) {
        for (; a != NULL; a = a->tp_base) {
            if (a == b) {
                return 1;
            }
        }
        return b == &PyBaseObject_Type;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); i++) {
        if (PyTuple_GET_ITEM(mro, i) == (PyObject *)b) {
            return 1;
        }
    }
    return 0;
}

PyObject *
lu_type_name(PyTypeObject *tp)
{
    if (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        return Py_NewRef(((PyHeapTypeObject *)tp)->ht_name);
    }
    const char *dot = strrchr(tp->tp_name, '.');
    return PyUnicode_FromString(dot != NULL ? dot + 1 : tp->tp_name);
}

/* Instance dicts. */

/* A static builtin type's dict is reached through lu_type_dict: its own
   tp_dict field stays NULL (FOUNDATIONS 2.1). */
static PyObject **
dict_ptr(PyObject *obj)
{
    PyTypeObject *tp = Py_TYPE(obj);
    if (PyType_Check(obj) && is_static_builtin((PyTypeObject *)obj)) {
        return NULL;
    }
    if (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        return (PyObject **)((char *)obj + LU_MANAGED_DICT_OFFSET);
    }
    return tp->tp_dictoffset > 0 ? (PyObject **)((char *)obj + tp->tp_dictoffset) : NULL;
}

PyObject **
_PyObject_GetDictPtr(PyObject *obj)
{
    return dict_ptr(obj);
}

/* The instance dict, created on demand when `create`; NULL when the object
   has none (borrowed). */
static PyObject *
instance_dict(PyObject *obj, int create)
{
    PyObject **p = dict_ptr(obj);
    if (p == NULL) {
        return NULL;
    }
    PyObject *d = __atomic_load_n(p, __ATOMIC_ACQUIRE);
    if (d == NULL && create && (d = PyDict_New()) != NULL) {
        /* Threads that make it at once keep the first one. */
        PyObject *first = NULL;
        if (!__atomic_compare_exchange_n(p, &first, d, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            Py_DECREF(d);
            d = first;
        }
    }
    return d;
}

PyObject *
PyObject_GenericGetDict(PyObject *obj, void *closure)
{
    PyObject *d = instance_dict(obj, 1);
    if (d == NULL && !PyErr_Occurred()) {
        lu_err_format(PyExc_TypeError, "This object has no __dict__");
    }
    return Py_XNewRef(d);
}

/* Stores or (value NULL) deletes the instance dict; a deleted one is recreated empty on the next use. */
static int
set_dict(PyObject *obj, PyObject *value)
{
    PyObject **p = dict_ptr(obj);
    if (p == NULL) {
        lu_err_format(PyExc_AttributeError, "This object has no __dict__");
        return -1;
    }
    if (value != NULL && !PyDict_Check(value)) {
        lu_err_format(PyExc_TypeError, "__dict__ must be set to a dictionary, not a '%.200s'",
                      Py_TYPE(value)->tp_name);
        return -1;
    }
    Py_XSETREF(*p, Py_XNewRef(value));
    return 0;
}

int
PyObject_GenericSetDict(PyObject *obj, PyObject *value, void *closure)
{
    if (value == NULL) {
        lu_err_format(PyExc_TypeError, "cannot delete __dict__");
        return -1;
    }
    return set_dict(obj, value);
}

/* A class instance's __dict__ (subtype_setdict) may be deleted too. */
static int
subtype_setdict(PyObject *obj, PyObject *value, void *closure)
{
    return set_dict(obj, value);
}

static PyObject *
subtype_getweakref(PyObject *obj, void *closure)
{
    PyObject *list = *(PyObject **)((char *)obj + MANAGED_WEAKREF_OFFSET);
    return Py_NewRef(list != NULL ? list : Py_None);
}

static PyGetSetDef subtype_getsets_full[] = {
    { "__dict__", PyObject_GenericGetDict, subtype_setdict,
      "dictionary for instance variables", NULL },
    { "__weakref__", subtype_getweakref, NULL,
      "list of weak references to the object", NULL },
    { NULL },
};

/* Allocation (PyType_GenericAlloc, subtype_dealloc). */

PyObject *
PyType_GenericAlloc(PyTypeObject *tp, Py_ssize_t nitems)
{
    size_t size = (size_t)tp->tp_basicsize + (size_t)(nitems + 1) * (size_t)tp->tp_itemsize;
    size_t presize = preheader_size(tp);
    int gc = (tp->tp_flags & Py_TPFLAGS_HAVE_GC) != 0;
    char *mem = gc ? lu_gc_alloc(tp, size, presize) : NULL;
    if (!gc) {
        mem = PyObject_Malloc(presize + size);
        mem = mem != NULL ? mem + presize : NULL;
    }
    if (mem == NULL) {
        return PyErr_NoMemory();
    }
    memset(mem - presize, 0, presize + size);
    PyObject *op = (PyObject *)mem;
    PyObject_Init(op, tp);
    if (tp->tp_itemsize != 0) {
        Py_SET_SIZE(op, nitems);
    }
    if (gc) {
        op->ob_gc_bits |= LU_GC_TRACKED;
    }
    return op;
}

static void
subtype_dealloc(PyObject *self)
{
    if (Py_TYPE(self)->tp_finalize != NULL && PyObject_CallFinalizerFromDealloc(self) < 0) {
        return;                         /* resurrected */
    }
    PyTypeObject *tp = Py_TYPE(self), *base = tp;     /* __del__ may have assigned __class__ */
    lu_clear_weakrefs(self);
    /* Clear the slots of every heap type in the chain, then the dict. */
    for (; base->tp_flags & Py_TPFLAGS_HEAPTYPE; base = base->tp_base) {
        PyMemberDef *mp = base->tp_members;
        Py_ssize_t n = Py_SIZE(base);
        for (Py_ssize_t i = 0; mp != NULL && i < n; i++, mp++) {
            Py_CLEAR(*(PyObject **)((char *)self + mp->offset));
        }
    }
    if (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        Py_CLEAR(*(PyObject **)((char *)self + LU_MANAGED_DICT_OFFSET));
    }
    if (base->tp_dealloc != NULL && base != &PyBaseObject_Type) {
        base->tp_dealloc(self);
    }
    else {
        lu_object_free(self);
    }
    Py_DECREF(tp);
}

/* subtype_traverse and subtype_clear: the slots of every heap type in the
   chain, the managed dict, and the instance's reference to its type, then
   the first base that is not a heap type's. */
static int
subtype_traverse(PyObject *self, visitproc visit, void *arg)
{
    PyTypeObject *type = Py_TYPE(self), *base = type;
    traverseproc basetraverse;
    while ((basetraverse = base->tp_traverse) == subtype_traverse) {
        PyMemberDef *mp = base->tp_members;
        for (Py_ssize_t i = 0; mp != NULL && i < Py_SIZE(base); i++, mp++) {
            Py_VISIT(*(PyObject **)((char *)self + mp->offset));
        }
        base = base->tp_base;
    }
    if (type->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        Py_VISIT(LU_MANAGED_DICT(self));
    }
    /* An instance counts as a reference to its heap type, unless the base's
       traverse visits the type. */
    if ((type->tp_flags & Py_TPFLAGS_HEAPTYPE)
        && (basetraverse == NULL || !(base->tp_flags & Py_TPFLAGS_HEAPTYPE))) {
        Py_VISIT(type);
    }
    return basetraverse != NULL ? basetraverse(self, visit, arg) : 0;
}

static int
subtype_clear(PyObject *self)
{
    PyTypeObject *type = Py_TYPE(self), *base = type;
    inquiry baseclear;
    while ((baseclear = base->tp_clear) == subtype_clear) {
        PyMemberDef *mp = base->tp_members;
        for (Py_ssize_t i = 0; mp != NULL && i < Py_SIZE(base); i++, mp++) {
            Py_CLEAR(*(PyObject **)((char *)self + mp->offset));
        }
        base = base->tp_base;
    }
    /* The instance dict goes too: it breaks cycles through __dict__ alone. */
    if (type->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        Py_CLEAR(LU_MANAGED_DICT(self));
    }
    return baseclear != NULL ? baseclear(self) : 0;
}

/* The collector holds only heap types; static ones are immortal. */
static int
type_is_gc(PyObject *self)
{
    return (((PyTypeObject *)self)->tp_flags & Py_TPFLAGS_HEAPTYPE) != 0;
}

static int
type_traverse(PyObject *self, visitproc visit, void *arg)
{
    PyTypeObject *tp = (PyTypeObject *)self;
    Py_VISIT(tp->tp_dict);
    Py_VISIT(tp->tp_mro);
    Py_VISIT(tp->tp_bases);
    Py_VISIT(tp->tp_base);
    return 0;
}

/* The method cache is invalidated before the dict is cleared, so that other
   objects of the cycle never call into a destroyed method. */
static int
type_clear(PyObject *self)
{
    PyTypeObject *tp = (PyTypeObject *)self;
    lu_type_modified(tp);
    if (tp->tp_dict != NULL) {
        PyDict_Clear(tp->tp_dict);
    }
    Py_CLEAR(tp->tp_mro);
    return 0;
}

/* type_dealloc: a heap type's references. A class is normally kept alive
   by the cycle through its MRO; this runs when creating one fails. */
static void
type_dealloc(PyObject *self)
{
    PyTypeObject *tp = (PyTypeObject *)self;
    PyHeapTypeObject *et = (PyHeapTypeObject *)self;
    for (Py_ssize_t i = 0; tp->tp_bases != NULL && i < PyTuple_GET_SIZE(tp->tp_bases); i++) {
        remove_subclass((PyTypeObject *)PyTuple_GET_ITEM(tp->tp_bases, i), tp);
    }
    lu_clear_weakrefs(self);
    Py_CLEAR(tp->tp_dict);
    Py_CLEAR(tp->tp_bases);
    Py_CLEAR(tp->tp_mro);
    Py_CLEAR(tp->tp_base);
    Py_CLEAR(tp->tp_subclasses);
    Py_CLEAR(et->ht_name);
    Py_CLEAR(et->ht_qualname);
    Py_CLEAR(et->ht_slots);
    Py_CLEAR(et->ht_module);
    PyMem_Free((void *)tp->tp_doc);
    PyMem_Free(et->_ht_tpname);
    lu_object_free(self);       /* a heap metatype's subtype_dealloc releases it */
}

/* Static types (type_ready for _Py_TPFLAGS_STATIC_BUILTIN types). */

int
lu_type_add_descriptors(PyTypeObject *tp)
{
    PyObject *dict = lu_type_dict(tp);
    for (PyMemberDef *m = tp->tp_members; m != NULL && m->name != NULL; m++) {
        PyObject *d = lu_descr_new_member(tp, m);
        PyObject *name = d != NULL ? PyUnicode_InternFromString(m->name) : NULL;
        int err = name == NULL || lu_ns_insert(dict, name, d) < 0;
        Py_XDECREF(name);
        Py_XDECREF(d);
        if (err) {
            return -1;
        }
    }
    for (PyGetSetDef *g = tp->tp_getset; g != NULL && g->name != NULL; g++) {
        PyObject *d = lu_descr_new_getset(tp, g);
        PyObject *name = d != NULL ? PyUnicode_InternFromString(g->name) : NULL;
        int err = name == NULL || lu_ns_insert(dict, name, d) < 0;
        Py_XDECREF(name);
        Py_XDECREF(d);
        if (err) {
            return -1;
        }
    }
    return 0;
}

/* tp_free of a class: what a base's dealloc calls last, which must know the preheader. */
static void
heap_free(void *self)
{
    lu_object_free(self);
}

static void
object_dealloc(PyObject *self)
{
    lu_object_free(self);
}

/* The process-wide half, once: index, flags, bases, MRO and version tag. */
static int
ready_static(PyTypeObject *tp, size_t index, unsigned int tag)
{
    if (index >= LU_MAX_STATIC_TYPES) {
        lu_err_format(PyExc_SystemError, "more than %d static builtin types", LU_MAX_STATIC_TYPES);
        return -1;
    }
    tp->tp_subclasses = (PyObject *)(index + 1);
    tp->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE | _Py_TPFLAGS_STATIC_BUILTIN;
    Py_ssize_t depth = 0;
    for (PyTypeObject *b = tp; b != NULL; b = b->tp_base) {
        depth++;
    }
    PyObject *mro = PyTuple_New(depth);
    PyObject *bases = tp->tp_base != NULL ? PyTuple_New(1) : PyTuple_New(0);
    if (mro == NULL || bases == NULL) {
        return -1;
    }
    Py_ssize_t i = 0;
    for (PyTypeObject *b = tp; b != NULL; b = b->tp_base) {
        PyTuple_SET_ITEM(mro, i++, (PyObject *)b);
    }
    if (tp->tp_base != NULL) {
        PyTuple_SET_ITEM(bases, 0, (PyObject *)tp->tp_base);
        if (tp->tp_basicsize == 0) {
            tp->tp_basicsize = tp->tp_base->tp_basicsize;
        }
    }
    lu_set_immortal(mro);
    lu_set_immortal(bases);
    tp->tp_mro = mro;
    tp->tp_bases = bases;
    set_version(tp, tag);
    tp->tp_flags |= Py_TPFLAGS_READY;
    return 0;
}

int
lu_types_init(PyInterpreterState *interp)
{
    PyBaseObject_Type.tp_basicsize = sizeof(PyObject);
    PyBaseObject_Type.tp_dealloc = object_dealloc;
    PyType_Type.tp_basicsize = sizeof(PyHeapTypeObject);
    PyType_Type.tp_itemsize = sizeof(PyMemberDef);
    PyType_Type.tp_alloc = PyType_GenericAlloc;
    PyType_Type.tp_flags |= Py_TPFLAGS_HAVE_GC;
    PyType_Type.tp_dealloc = type_dealloc;
    PyType_Type.tp_traverse = type_traverse;
    PyType_Type.tp_clear = type_clear;
    PyType_Type.tp_is_gc = type_is_gc;
    PyType_Type.tp_dictoffset = offsetof(PyTypeObject, tp_dict);
    PyType_Type.tp_weaklistoffset = offsetof(PyTypeObject, tp_weaklist);
    size_t index = 0;
    for (PyTypeObject *const *t = lu_static_types; *t != NULL; t++) {
        PyTypeObject *tp = *t;
        if (tp->tp_subclasses != NULL && tp->tp_mro != NULL) {
            continue;                   /* listed twice, or another interpreter readied it */
        }
        if (ready_static(tp, index++, next_global_version_tag()) < 0) {
            return -1;
        }
    }
    for (PyTypeObject *const *t = lu_static_types; *t != NULL; t++) {
        struct lu_static_type_state *state = &interp->static_types[(size_t)(*t)->tp_subclasses - 1];
        if (state->tp_dict != NULL) {
            continue;
        }
        if ((state->tp_dict = PyDict_New()) == NULL
            || lu_ns_insert(state->tp_dict, LU_ID(__doc__), Py_None) < 0
            || lu_type_add_descriptors(*t) < 0) {
            return -1;
        }
    }
    interp->next_version_tag = VERSION_TAG_BASE;
    lu_watchers_init(interp);
    return 0;
}

/* Heap types (type_new_alloc and its helpers, best_base, solid_base). */

static int
shape_differs(PyTypeObject *t1, PyTypeObject *t2)
{
    return t1->tp_basicsize != t2->tp_basicsize || t1->tp_itemsize != t2->tp_itemsize;
}

static PyTypeObject *
solid_base(PyTypeObject *tp)
{
    PyTypeObject *base = tp->tp_base != NULL ? solid_base(tp->tp_base) : &PyBaseObject_Type;
    return shape_differs(tp, base) ? tp : base;
}

static PyTypeObject *
best_base(PyObject *bases)
{
    PyTypeObject *base = NULL, *winner = NULL;
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(bases); i++) {
        PyObject *b = PyTuple_GET_ITEM(bases, i);
        if (!PyType_Check(b)) {
            lu_err_format(PyExc_TypeError, "bases must be types");
            return NULL;
        }
        PyTypeObject *base_i = (PyTypeObject *)b;
        if (!(base_i->tp_flags & Py_TPFLAGS_BASETYPE)) {
            lu_err_format(PyExc_TypeError, "type '%.100s' is not an acceptable base type",
                          base_i->tp_name);
            return NULL;
        }
        PyTypeObject *candidate = solid_base(base_i);
        if (winner == NULL) {
            winner = candidate;
            base = base_i;
        }
        else if (PyType_IsSubtype(winner, candidate)) {
        }
        else if (PyType_IsSubtype(candidate, winner)) {
            winner = candidate;
            base = base_i;
        }
        else {
            lu_err_format(PyExc_TypeError, "multiple bases have instance lay-out conflict");
            return NULL;
        }
    }
    return base;
}

/* The slots C copies from a base where the type has none (inherit_slots),
   and tp_new, except from object to a static type (type_ready_set_new: such
   a type cannot be instantiated). */
#define LU_COPY(field) if (tp->field == NULL) tp->field = base->field
#define LU_COPY_SUB(sub, field) \
    if (base->sub != NULL && tp->sub != NULL && tp->sub->field == NULL) tp->sub->field = base->sub->field
static void
inherit_slots(PyTypeObject *tp, PyTypeObject *base)
{
    LU_COPY(tp_getattro); LU_COPY(tp_setattro); LU_COPY(tp_repr); LU_COPY(tp_str);
    LU_COPY(tp_hash); LU_COPY(tp_call); LU_COPY(tp_richcompare); LU_COPY(tp_iter);
    LU_COPY(tp_iternext); LU_COPY(tp_descr_get); LU_COPY(tp_descr_set); LU_COPY(tp_init);
    LU_COPY(tp_finalize);
    if (!(base == &PyBaseObject_Type && !(tp->tp_flags & Py_TPFLAGS_HEAPTYPE))) {
        LU_COPY(tp_new);
    }
    LU_COPY_SUB(tp_as_number, nb_add); LU_COPY_SUB(tp_as_number, nb_subtract);
    LU_COPY_SUB(tp_as_number, nb_multiply); LU_COPY_SUB(tp_as_number, nb_remainder);
    LU_COPY_SUB(tp_as_number, nb_divmod); LU_COPY_SUB(tp_as_number, nb_power);
    LU_COPY_SUB(tp_as_number, nb_negative); LU_COPY_SUB(tp_as_number, nb_positive);
    LU_COPY_SUB(tp_as_number, nb_absolute); LU_COPY_SUB(tp_as_number, nb_bool);
    LU_COPY_SUB(tp_as_number, nb_invert); LU_COPY_SUB(tp_as_number, nb_lshift);
    LU_COPY_SUB(tp_as_number, nb_rshift); LU_COPY_SUB(tp_as_number, nb_and);
    LU_COPY_SUB(tp_as_number, nb_xor); LU_COPY_SUB(tp_as_number, nb_or);
    LU_COPY_SUB(tp_as_number, nb_int); LU_COPY_SUB(tp_as_number, nb_float);
    LU_COPY_SUB(tp_as_number, nb_index); LU_COPY_SUB(tp_as_number, nb_floor_divide);
    LU_COPY_SUB(tp_as_number, nb_true_divide); LU_COPY_SUB(tp_as_number, nb_matrix_multiply);
    LU_COPY_SUB(tp_as_mapping, mp_length); LU_COPY_SUB(tp_as_mapping, mp_subscript);
    LU_COPY_SUB(tp_as_mapping, mp_ass_subscript);
    LU_COPY_SUB(tp_as_sequence, sq_length); LU_COPY_SUB(tp_as_sequence, sq_item);
    LU_COPY_SUB(tp_as_sequence, sq_ass_item); LU_COPY_SUB(tp_as_sequence, sq_contains);
    LU_COPY_SUB(tp_as_sequence, sq_concat); LU_COPY_SUB(tp_as_sequence, sq_repeat);
    LU_COPY_SUB(tp_as_async, am_await); LU_COPY_SUB(tp_as_async, am_aiter);
    LU_COPY_SUB(tp_as_async, am_anext);
}
#undef LU_COPY
#undef LU_COPY_SUB

/* Fills the static types' NULL slots from their bases, bases first, run after
   every kernel module: kernel classes install slots as they define dunders.
   A native tp_new not shared with the base is callable as __new__
   (add_tp_new_wrapper) where the kernel class defined no __new__ of its own. */
int
lu_static_types_inherit(void)
{
    for (PyTypeObject *const *t = lu_static_types; *t != NULL; t++) {
        PyObject *mro = (*t)->tp_mro;
        for (Py_ssize_t i = PyTuple_GET_SIZE(mro) - 1; i > 0; i--) {
            inherit_slots((PyTypeObject *)PyTuple_GET_ITEM(mro, i - 1),
                          (PyTypeObject *)PyTuple_GET_ITEM(mro, i));
        }
        PyTypeObject *base = (*t)->tp_base;
        if ((*t)->tp_new != NULL && base != NULL && (*t)->tp_new != base->tp_new
            && PyDict_GetItemString(lu_type_dict(*t), "__new__") == NULL
            && lu_type_add_new_wrapper(*t, lu_type_dict(*t)) < 0) {
            return -1;
        }
    }
    return 0;
}

/* PyType_Ready: a static type defined outside the runtime (an extension's),
   readied where its module initializes. The sequence is CPython's type_ready
   (Objects/typeobject.c) on this runtime's model: the registration
   lu_types_init gives the runtime's own types (an index into the
   interpreter's static-type state, carried in tp_subclasses so every
   interpreter finds the same one), sizes, offsets and flags from the dominant
   base (inherit_special), the slots the type leaves NULL from every base,
   then its dict from tp_doc, tp_members, tp_getset and tp_methods.

   type_ready_lock makes the whole sequence process-wide, as CPython's type
   lock does: two interpreters importing one extension ready the same shared
   struct exactly once. */

/* The interpreter state array slot for a foreign static type; the index is
   claimed once, process-wide (type_ready_lock held). */
static int
claim_static_slot(PyTypeObject *tp, size_t *slot)
{
    if (tp->tp_subclasses != NULL) {
        *slot = (size_t)tp->tp_subclasses - 1;      /* readied somewhere else */
        return 0;
    }
    if (_PyRuntime.next_static_type == 0) {
        for (PyTypeObject *const *t = lu_static_types; *t != NULL; t++) {
            _PyRuntime.next_static_type++;
        }
    }
    if (_PyRuntime.next_static_type >= LU_MAX_STATIC_TYPES) {
        lu_err_format(PyExc_SystemError, "more than %d static builtin types", LU_MAX_STATIC_TYPES);
        return -1;
    }
    *slot = _PyRuntime.next_static_type++;
    tp->tp_subclasses = (PyObject *)(*slot + 1);
    return 0;
}

static int ready_type(PyTypeObject *tp, PyTypeObject *base, struct lu_static_type_state *state);

static int
ready_foreign(PyTypeObject *tp)
{
    int heap = (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) != 0;
    if (tp->tp_name == NULL) {
        lu_err_format(PyExc_SystemError, "Type does not define the tp_name field.");
        return -1;
    }
    tp->tp_flags |= Py_TPFLAGS_DEFAULT;
    if (heap) {
        PyTypeObject *base = tp->tp_base;
        if (base == NULL) {
            tp->tp_base = base = (PyTypeObject *)Py_NewRef((PyObject *)&PyBaseObject_Type);
        }
        if (!(base->tp_flags & Py_TPFLAGS_READY) && ready_foreign(base) < 0) {
            return -1;
        }
        if (Py_IS_TYPE(tp, NULL)) {
            Py_SET_TYPE(tp, Py_TYPE(base));
        }
        return ready_type(tp, base, NULL);
    }
    tp->tp_flags |= Py_TPFLAGS_IMMUTABLETYPE | _Py_TPFLAGS_STATIC_BUILTIN;
    if (tp->tp_base == NULL) {
        tp->tp_base = &PyBaseObject_Type;
    }
    PyTypeObject *base = tp->tp_base;
    if (base->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        lu_err_format(PyExc_TypeError, "type '%.100s' is not dynamically allocated but "
                      "its base type '%.100s' is dynamically allocated",
                      tp->tp_name, base->tp_name);
        return -1;
    }
    if (!(base->tp_flags & Py_TPFLAGS_READY) && ready_foreign(base) < 0) {
        return -1;
    }
    if (Py_IS_TYPE(tp, NULL)) {
        Py_SET_TYPE(tp, Py_TYPE(base));
    }
    lu_set_immortal((PyObject *)tp);        /* the dict that holds it never counts */
    size_t slot;
    if (claim_static_slot(tp, &slot) < 0) {
        return -1;
    }
    return ready_type(tp, base, &PyInterpreterState_Get()->static_types[slot]);
}

/* What type_ready does after the registration differs by kind: a static
   foreign type keeps its dict and MRO in interpreter state and immortal
   tuples, a heap one readied by hand (gh-128923) in the object itself.
   state NULL: heap. */
static int
ready_type(PyTypeObject *tp, PyTypeObject *base, struct lu_static_type_state *state)
{
    /* Sizes, offsets and GC from the dominant base (inherit_special); a type
       that stands on a GC base without its own traverse and clear adopts it. */
    if (!(tp->tp_flags & Py_TPFLAGS_HAVE_GC) && (base->tp_flags & Py_TPFLAGS_HAVE_GC)
        && tp->tp_traverse == NULL && tp->tp_clear == NULL) {
        tp->tp_flags |= Py_TPFLAGS_HAVE_GC;
        tp->tp_traverse = base->tp_traverse;
        tp->tp_clear = base->tp_clear;
    }
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
    tp->tp_flags |= base->tp_flags & (_Py_TPFLAGS_MATCH_SELF | Py_TPFLAGS_ITEMS_AT_END);
    /* the fast subclass flag of the dominant base, first match winning */
    struct { unsigned long flag; PyTypeObject *type; } families[] = {
        { Py_TPFLAGS_BASE_EXC_SUBCLASS, (PyTypeObject *)PyExc_BaseException },
        { Py_TPFLAGS_TYPE_SUBCLASS, &PyType_Type },
        { Py_TPFLAGS_LONG_SUBCLASS, &PyLong_Type },
        { Py_TPFLAGS_BYTES_SUBCLASS, &PyBytes_Type },
        { Py_TPFLAGS_UNICODE_SUBCLASS, &PyUnicode_Type },
        { Py_TPFLAGS_TUPLE_SUBCLASS, &PyTuple_Type },
        { Py_TPFLAGS_LIST_SUBCLASS, &PyList_Type },
        { Py_TPFLAGS_DICT_SUBCLASS, &PyDict_Type },
    };
    for (size_t i = 0; i < sizeof families / sizeof families[0]; i++) {
        if (PyType_IsSubtype(base, families[i].type)) {
            tp->tp_flags |= families[i].flag;
            break;
        }
    }

    /* MRO and bases (type_ready_mro): the base's MRO behind the type, or, for a
       type that names several bases (numpy.float64 stands on numpy.floating and float),
       the kernel's C3 merge of them. */
    PyObject *bases = tp->tp_bases != NULL && PyTuple_GET_SIZE(tp->tp_bases) > 1 ? Py_NewRef(tp->tp_bases)
                                                                                : PyTuple_Pack(1, (PyObject *)base);
    PyObject *mro = NULL;
    if (bases != NULL && PyTuple_GET_SIZE(bases) > 1) {
        tp->tp_bases = bases;
        mro = lu_call(PyDict_GetItemString(lu_type_dict(&PyType_Type), "mro"), (PyObject *[]){ (PyObject *)tp }, 1, NULL);
        if (mro != NULL && !PyTuple_CheckExact(mro)) {
            PyObject *t = PySequence_Tuple(mro);
            Py_DECREF(mro);
            mro = t;
        }
    }
    else if (bases != NULL) {
        Py_ssize_t depth = 0;
        for (PyTypeObject *b = tp; b != NULL; b = b->tp_base) {
            depth++;
        }
        mro = PyTuple_New(depth);
        Py_ssize_t i = 0;
        for (PyTypeObject *b = tp; mro != NULL && b != NULL; b = b->tp_base) {
            PyTuple_SET_ITEM(mro, i++, Py_NewRef(b));      /* a no-op on immortals */
        }
    }
    if (mro == NULL || bases == NULL) {
        Py_XDECREF(mro);
        Py_XDECREF(bases);
        return -1;
    }
    if (state != NULL) {
        lu_set_immortal(mro);
        lu_set_immortal(bases);
    }
    tp->tp_mro = mro;
    tp->tp_bases = bases;

    /* The dict, once per interpreter for a static type, in the object for a
       heap one: __doc__ from tp_doc (type_dict_set_doc), the descriptors and
       the methods (type_ready_fill_dict). */
    PyObject **dict_slot = state != NULL ? &state->tp_dict : &tp->tp_dict;
    if (*dict_slot == NULL) {
        PyObject *dict = PyDict_New();
        PyObject *doc = tp->tp_doc != NULL
            ? PyUnicode_FromString(lu_doc_without_signature(tp->tp_name, tp->tp_doc))
            : Py_NewRef(Py_None);
        int err = dict == NULL || doc == NULL || PyDict_SetItemString(dict, "__doc__", doc) < 0;
        Py_XDECREF(doc);
        if (err) {
            Py_XDECREF(dict);
            return -1;
        }
        *dict_slot = dict;
    }
    PyObject *dict = *dict_slot;
    if (lu_type_add_operators(tp) < 0 || lu_type_add_descriptors(tp) < 0 || lu_type_add_methods(tp, dict) < 0) {
        return -1;
    }

    /* The slots the type leaves NULL, from every base, bases first; a type
       whose dict defines __eq__ or __hash__ inherits neither of the pair
       (inherit_slots, overrides_hash). */
    int hash_pair = tp->tp_hash == NULL && tp->tp_richcompare == NULL;
    for (Py_ssize_t j = PyTuple_GET_SIZE(mro) - 1; j > 0; j--) {
        inherit_slots((PyTypeObject *)PyTuple_GET_ITEM(mro, j - 1),
                      (PyTypeObject *)PyTuple_GET_ITEM(mro, j));
    }
    if (hash_pair
        && (PyDict_GetItemString(dict, "__eq__") != NULL || PyDict_GetItemString(dict, "__hash__") != NULL)) {
        tp->tp_hash = NULL;
        tp->tp_richcompare = NULL;
    }
    /* A NULL tp_as_* struct shares the base's, as in CPython
       (type_ready_inherit_as_structs), and the slots no dunder feeds come
       from the base or the runtime's defaults (inherit_slots' rest). */
    if (tp->tp_as_async == NULL) {
        tp->tp_as_async = base->tp_as_async;
    }
    if (tp->tp_as_number == NULL) {
        tp->tp_as_number = base->tp_as_number;
    }
    if (tp->tp_as_sequence == NULL) {
        tp->tp_as_sequence = base->tp_as_sequence;
    }
    if (tp->tp_as_mapping == NULL) {
        tp->tp_as_mapping = base->tp_as_mapping;
    }
    if (tp->tp_as_buffer == NULL) {
        tp->tp_as_buffer = base->tp_as_buffer;
    }
    if (tp->tp_dealloc == NULL) {
        tp->tp_dealloc = base->tp_dealloc;
    }
    if (tp->tp_alloc == NULL) {
        tp->tp_alloc = PyType_GenericAlloc;
    }
    if (tp->tp_free == NULL) {
        tp->tp_free = lu_free_object;
    }
    if (tp->tp_is_gc == NULL) {
        tp->tp_is_gc = base->tp_is_gc;
    }
    if (tp->tp_vectorcall_offset == 0) {
        tp->tp_vectorcall_offset = base->tp_vectorcall_offset;
    }
    if (tp->tp_call == NULL && (base->tp_flags & Py_TPFLAGS_HAVE_VECTORCALL)) {
        tp->tp_flags |= Py_TPFLAGS_HAVE_VECTORCALL;
    }

    /* tp_new: a static type with no tp_new on object cannot be instantiated
       (type_ready_set_new); any other came with the slots above. */
    if (tp->tp_new == NULL) {
        if (state != NULL && base == &PyBaseObject_Type) {
            tp->tp_flags |= Py_TPFLAGS_DISALLOW_INSTANTIATION;
        }
        else if (base->tp_new != NULL) {
            tp->tp_new = base->tp_new;
        }
    }
    else if (tp->tp_new != base->tp_new && !lu_tp_new_is_thunk(tp->tp_new)
             && lu_type_add_new_wrapper(tp, dict) < 0) {
        return -1;
    }
    /* Still no tp_hash after all that: the type is unhashable
       (type_ready_set_hash). */
    if (tp->tp_hash == NULL && PyDict_GetItemString(dict, "__hash__") == NULL) {
        tp->tp_hash = PyObject_HashNotImplemented;
        PyObject *none = Py_NewRef(Py_None);
        int err = none == NULL || PyDict_SetItemString(dict, "__hash__", none) < 0;
        Py_XDECREF(none);
        if (err) {
            return -1;
        }
    }
    set_version(tp, next_global_version_tag());
    tp->tp_flags |= Py_TPFLAGS_READY;
    for (Py_ssize_t b = 0; b < PyTuple_GET_SIZE(tp->tp_bases); b++) {
        if (add_subclass((PyTypeObject *)PyTuple_GET_ITEM(tp->tp_bases, b), tp) < 0) {
            return -1;
        }
    }
    return 0;
}

PyObject *
PyType_GenericNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return type->tp_alloc(type, 0);
}

int
PyType_Ready(PyTypeObject *tp)
{
    if (tp->tp_flags & Py_TPFLAGS_READY) {
        return 0;
    }
    PyMutex_Lock(&_PyRuntime.type_ready_lock);
    int result = tp->tp_flags & Py_TPFLAGS_READY ? 0 : ready_foreign(tp);
    PyMutex_Unlock(&_PyRuntime.type_ready_lock);
    return result;
}

static int
is_identifier(PyObject *s)
{
    Py_ssize_t n = PyUnicode_GET_LENGTH(s);
    int kind = PyUnicode_KIND(s);
    const void *data = PyUnicode_DATA(s);
    if (n == 0) {
        return 0;
    }
    for (Py_ssize_t i = 0; i < n; i++) {
        Py_UCS4 c = PyUnicode_READ(kind, data, i);
        int ok = c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128
            || (i > 0 && c >= '0' && c <= '9');
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

/* _Py_Mangle: "__x" in class "C" is "_C__x". */
static PyObject *
mangle(PyObject *classname, PyObject *name)
{
    const char *n = PyUnicode_AsUTF8(name);
    size_t len = strlen(n);
    if (n[0] != '_' || n[1] != '_' || (len >= 4 && n[len - 1] == '_' && n[len - 2] == '_')
        || strchr(n, '.') != NULL) {
        return Py_NewRef(name);
    }
    const char *c = PyUnicode_AsUTF8(classname);
    while (*c == '_') {
        c++;
    }
    if (*c == '\0') {
        return Py_NewRef(name);
    }
    size_t clen = strlen(c);
    char *buf = PyMem_Malloc(1 + clen + len + 1);
    if (buf == NULL) {
        return PyErr_NoMemory();
    }
    buf[0] = '_';
    memcpy(buf + 1, c, clen);
    memcpy(buf + 1 + clen, n, len + 1);
    PyObject *result = PyUnicode_FromString(buf);
    PyMem_Free(buf);
    return result;
}

static int
str_compare(PyObject *a, PyObject *b)
{
    Py_ssize_t na = PyUnicode_GET_LENGTH(a), nb = PyUnicode_GET_LENGTH(b);
    for (Py_ssize_t i = 0; i < na && i < nb; i++) {
        Py_UCS4 x = PyUnicode_READ_CHAR(a, i), y = PyUnicode_READ_CHAR(b, i);
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return na < nb ? -1 : na > nb;
}

/* type_new_slots and type_new_copy_slots: the __slots__ names, checked,
   mangled and sorted, without __dict__ and __weakref__ (a new tuple), and
   whether those two are added. */
static PyObject *
process_slots(PyObject *name, PyObject *slots, PyObject *dict, PyTypeObject *base,
              PyObject *bases, int *add_dict, int *add_weak)
{
    int may_add_dict = base->tp_dictoffset == 0;
    int may_add_weak = base->tp_weaklistoffset == 0 && base->tp_itemsize == 0;
    *add_dict = *add_weak = 0;
    if (slots == NULL) {
        *add_dict = may_add_dict;
        *add_weak = may_add_weak;
        return PyTuple_New(0);
    }
    PyObject *tuple;
    if (PyUnicode_Check(slots)) {
        tuple = PyTuple_New(1);
        if (tuple != NULL) {
            PyTuple_SET_ITEM(tuple, 0, Py_NewRef(slots));
        }
    }
    else {
        tuple = lu_call_kernel(LU_K_tuple_of, &slots, 1);      /* PySequence_Tuple */
    }
    if (tuple == NULL) {
        return NULL;
    }
    Py_ssize_t n = PyTuple_GET_SIZE(tuple);
    if (n > 0 && base->tp_itemsize != 0) {
        lu_err_format(PyExc_TypeError, "nonempty __slots__ not supported for subtype of '%s'",
                      base->tp_name);
        goto fail;
    }
    PyObject *dict_name = PyUnicode_InternFromString("__dict__");
    PyObject *weak_name = PyUnicode_InternFromString("__weakref__");
    PyObject *result = PyList_New(0);
    for (Py_ssize_t i = 0; result != NULL && i < n; i++) {
        PyObject *s = PyTuple_GET_ITEM(tuple, i);
        if (!PyUnicode_Check(s)) {
            lu_err_format(PyExc_TypeError, "__slots__ items must be strings, not '%.200s'",
                          Py_TYPE(s)->tp_name);
            goto fail_list;
        }
        if (!is_identifier(s)) {
            lu_err_format(PyExc_TypeError, "__slots__ must be identifiers");
            goto fail_list;
        }
        if (lu_str_eq(s, dict_name) || lu_str_eq(s, weak_name)) {
            int is_dict = lu_str_eq(s, dict_name);
            int *add = is_dict ? add_dict : add_weak;
            if (!(is_dict ? may_add_dict : may_add_weak) || *add) {
                lu_err_format(PyExc_TypeError, "%s slot disallowed: we already got one",
                              is_dict ? "__dict__" : "__weakref__");
                goto fail_list;
            }
            *add = 1;
            continue;
        }
        PyObject *m = mangle(name, s);
        if (m == NULL) {
            goto fail_list;
        }
        if (lu_ns_lookup(dict, m) != NULL) {
            const char *u = PyUnicode_AsUTF8(m);
            if (strcmp(u, "__qualname__") && strcmp(u, "__classcell__")
                && strcmp(u, "__classdictcell__")) {
                lu_err_format(PyExc_ValueError, "'%s' in __slots__ conflicts with class variable", u);
                Py_DECREF(m);
                goto fail_list;
            }
        }
        /* Insertion sort: __slots__ are few. */
        int err = lu_list_append(result, m);
        Py_DECREF(m);
        if (err < 0) {
            goto fail_list;
        }
        PyObject **items = ((PyListObject *)result)->ob_item;
        for (Py_ssize_t j = PyList_GET_SIZE(result) - 1; j > 0 && str_compare(items[j - 1], items[j]) > 0; j--) {
            PyObject *t = items[j];
            items[j] = items[j - 1];
            items[j - 1] = t;
        }
    }
    if (result == NULL) {
        goto fail;
    }
    /* Secondary bases may provide the dict or the weakref list. */
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(bases); i++) {
        PyTypeObject *b = (PyTypeObject *)PyTuple_GET_ITEM(bases, i);
        if (b == base) {
            continue;
        }
        if (may_add_dict && !*add_dict && b->tp_dictoffset != 0) {
            *add_dict = 1;
        }
        if (may_add_weak && !*add_weak && b->tp_weaklistoffset != 0) {
            *add_weak = 1;
        }
    }
    Py_DECREF(tuple);
    PyObject *sorted = lu_args_as_tuple(Py_None, result);
    Py_DECREF(result);
    return sorted;
fail_list:
    Py_XDECREF(result);
fail:
    Py_DECREF(tuple);
    return NULL;
}

/* type_new_init through type_new_set_attrs: a heap type of `meta` with
   tp_dict a copy of `ns`, its layout, name, qualname, doc and slots; the
   MRO is the caller's (kernel type.__new__). */
static PyObject *
type_new_layout(PyTypeObject *meta, PyObject *name, PyObject *bases, PyObject *ns)
{
    PyTypeObject *base;
    PyObject *dict = NULL, *slots = NULL;
    if (PyTuple_GET_SIZE(bases) == 0) {
        base = &PyBaseObject_Type;
        bases = PyTuple_New(1);
        if (bases == NULL) {
            return NULL;
        }
        PyTuple_SET_ITEM(bases, 0, (PyObject *)base);
    }
    else {
        base = best_base(bases);
        if (base == NULL) {
            return NULL;
        }
        Py_INCREF(bases);
    }
    /* PyDict_Copy over the namespace. */
    dict = PyDict_New();
    Py_ssize_t pos = 0;
    PyObject *key, *value;
    while (dict != NULL && lu_ns_next(ns, &pos, &key, &value)) {
        if (lu_ns_insert(dict, key, value) < 0) {
            Py_CLEAR(dict);
        }
    }
    if (dict == NULL) {
        Py_DECREF(bases);
        return NULL;
    }
    int add_dict, add_weak;
    PyObject *slots_name = PyUnicode_InternFromString("__slots__");
    slots = process_slots(name, lu_ns_lookup(dict, slots_name), dict, base, bases,
                          &add_dict, &add_weak);
    Py_DECREF(slots_name);
    if (slots == NULL) {
        goto fail;
    }
    Py_ssize_t nslot = PyTuple_GET_SIZE(slots);
    PyHeapTypeObject *et = (PyHeapTypeObject *)PyType_GenericAlloc(meta, nslot);
    if (et == NULL) {
        goto fail;
    }
    PyTypeObject *tp = &et->ht_type;
    tp->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC
        | (base->tp_flags & (Py_TPFLAGS_LONG_SUBCLASS | Py_TPFLAGS_LIST_SUBCLASS
                             | Py_TPFLAGS_TUPLE_SUBCLASS | Py_TPFLAGS_BYTES_SUBCLASS
                             | Py_TPFLAGS_UNICODE_SUBCLASS | Py_TPFLAGS_DICT_SUBCLASS
                             | Py_TPFLAGS_BASE_EXC_SUBCLASS | Py_TPFLAGS_TYPE_SUBCLASS
                             | Py_TPFLAGS_MANAGED_DICT | Py_TPFLAGS_MANAGED_WEAKREF));
    tp->tp_as_async = &et->as_async;
    tp->tp_as_number = &et->as_number;
    tp->tp_as_sequence = &et->as_sequence;
    tp->tp_as_mapping = &et->as_mapping;
    tp->tp_as_buffer = &et->as_buffer;
    tp->tp_bases = bases;
    bases = NULL;
    tp->tp_base = (PyTypeObject *)Py_NewRef(base);
    tp->tp_dealloc = subtype_dealloc;
    tp->tp_traverse = subtype_traverse;
    tp->tp_clear = subtype_clear;
    tp->tp_alloc = PyType_GenericAlloc;
    tp->tp_free = heap_free;
    tp->tp_dictoffset = base->tp_dictoffset;
    tp->tp_weaklistoffset = base->tp_weaklistoffset;
    tp->tp_dict = dict;
    dict = NULL;
    et->ht_name = Py_NewRef(name);
    et->ht_slots = slots;
    slots = NULL;
    Py_ssize_t name_size;
    tp->tp_name = PyUnicode_AsUTF8AndSize(name, &name_size);
    if (tp->tp_name == NULL || strlen(tp->tp_name) != (size_t)name_size) {
        lu_err_format(PyExc_ValueError, "type name must not contain null characters");
        goto fail_type;
    }
    /* __module__ from the calling frame's globals. */
    PyObject *module_name = PyUnicode_InternFromString("__module__");
    if (lu_ns_lookup(tp->tp_dict, module_name) == NULL) {
        PyObject *globals = lu_caller_globals(PyThreadState_Get());
        PyObject *m = globals != NULL ? lu_ns_lookup(globals, LU_ID(__name__)) : NULL;
        if (m != NULL && lu_ns_insert(tp->tp_dict, module_name, m) < 0) {
            Py_DECREF(module_name);
            goto fail_type;
        }
    }
    Py_DECREF(module_name);
    /* __qualname__ */
    PyObject *qualname_name = PyUnicode_InternFromString("__qualname__");
    PyObject *qualname = lu_ns_lookup(tp->tp_dict, qualname_name);
    if (qualname != NULL) {
        if (!PyUnicode_Check(qualname)) {
            lu_err_format(PyExc_TypeError, "type __qualname__ must be a str, not %s",
                          Py_TYPE(qualname)->tp_name);
            Py_DECREF(qualname_name);
            goto fail_type;
        }
        et->ht_qualname = Py_NewRef(qualname);
        lu_ns_delete(tp->tp_dict, qualname_name);
    }
    else {
        et->ht_qualname = Py_NewRef(name);
    }
    Py_DECREF(qualname_name);
    /* tp_doc, a copy of a str __doc__ */
    PyObject *doc = lu_ns_lookup(tp->tp_dict, LU_ID(__doc__));
    if (doc != NULL && PyUnicode_Check(doc)) {
        const char *s = PyUnicode_AsUTF8(doc);
        char *copy = s != NULL ? PyMem_Malloc(strlen(s) + 1) : NULL;
        if (copy == NULL) {
            goto fail_type;
        }
        strcpy(copy, s);
        tp->tp_doc = copy;
    }
    /* type_new_descriptors: members for __slots__, then the dict and weakref. */
    Py_ssize_t offset = base->tp_basicsize;
    PyMemberDef *mp = (PyMemberDef *)((char *)et + Py_TYPE(et)->tp_basicsize);
    for (Py_ssize_t i = 0; i < nslot; i++, mp++) {
        mp->name = PyUnicode_AsUTF8(PyTuple_GET_ITEM(et->ht_slots, i));
        mp->type = Py_T_OBJECT_EX;
        mp->offset = offset;
        offset += sizeof(PyObject *);
    }
    if (add_weak) {
        tp->tp_flags |= Py_TPFLAGS_MANAGED_WEAKREF;
        tp->tp_weaklistoffset = MANAGED_WEAKREF_OFFSET;
    }
    if (add_dict) {
        tp->tp_flags |= Py_TPFLAGS_MANAGED_DICT;
        tp->tp_dictoffset = -1;
    }
    tp->tp_basicsize = offset;
    tp->tp_itemsize = base->tp_itemsize;
    tp->tp_members = nslot ? (PyMemberDef *)((char *)et + Py_TYPE(et)->tp_basicsize) : NULL;
    if (lu_type_add_descriptors(tp) < 0) {
        goto fail_type;
    }
    if (add_dict || add_weak) {
        /* subtype_getsets_full, as far as this type adds them */
        for (PyGetSetDef *g = subtype_getsets_full; g->name != NULL; g++) {
            if ((g == &subtype_getsets_full[0] && !add_dict)
                || (g == &subtype_getsets_full[1] && !add_weak)) {
                continue;
            }
            PyObject *d = lu_descr_new_getset(tp, g);
            PyObject *n = d != NULL ? PyUnicode_InternFromString(g->name) : NULL;
            int err = n == NULL || lu_ns_insert(tp->tp_dict, n, d) < 0;
            Py_XDECREF(n);
            Py_XDECREF(d);
            if (err) {
                goto fail_type;
            }
        }
    }
    inherit_slots(tp, base);
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(tp->tp_bases); i++) {
        if (add_subclass((PyTypeObject *)PyTuple_GET_ITEM(tp->tp_bases, i), tp) < 0) {
            goto fail_type;
        }
    }
    tp->tp_flags |= Py_TPFLAGS_READY;
    return (PyObject *)tp;
fail_type:
    Py_DECREF(et);
    return NULL;
fail:
    Py_XDECREF(bases);
    Py_XDECREF(dict);
    Py_XDECREF(slots);
    return NULL;
}

/* compatible_for_assignment (Objects/typeobject.c). */
static int
compatible_with_tp_base(PyTypeObject *child)
{
    PyTypeObject *parent = child->tp_base;
    return parent != NULL
        && child->tp_basicsize == parent->tp_basicsize
        && child->tp_itemsize == parent->tp_itemsize
        && child->tp_dictoffset == parent->tp_dictoffset
        && child->tp_weaklistoffset == parent->tp_weaklistoffset
        && (child->tp_flags & Py_TPFLAGS_HAVE_GC) == (parent->tp_flags & Py_TPFLAGS_HAVE_GC)
        && (child->tp_dealloc == subtype_dealloc || child->tp_dealloc == parent->tp_dealloc);
}

static int
same_slots_added(PyTypeObject *a, PyTypeObject *b)
{
    PyTypeObject *base = a->tp_base;
    Py_ssize_t size = base->tp_basicsize;
    if (!(a->tp_flags & Py_TPFLAGS_HEAPTYPE) || !(b->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        return 0;
    }
    PyObject *sa = ((PyHeapTypeObject *)a)->ht_slots, *sb = ((PyHeapTypeObject *)b)->ht_slots;
    if (sa != NULL && sb != NULL) {
        int r = lu_eq(sa, sb);
        if (r != 1) {
            PyErr_Clear();
            return 0;
        }
        size += sizeof(PyObject *) * PyTuple_GET_SIZE(sa);
    }
    return size == a->tp_basicsize && size == b->tp_basicsize;
}

static int
compatible_for_assignment(PyTypeObject *oldto, PyTypeObject *newto, const char *attr)
{
    PyTypeObject *newbase = newto, *oldbase = oldto;
    while (compatible_with_tp_base(newbase)) {
        newbase = newbase->tp_base;
    }
    while (compatible_with_tp_base(oldbase)) {
        oldbase = oldbase->tp_base;
    }
    if ((newbase == oldbase
         || (newbase->tp_base == oldbase->tp_base && same_slots_added(newbase, oldbase)))
        && (oldto->tp_flags & Py_TPFLAGS_PREHEADER) == (newto->tp_flags & Py_TPFLAGS_PREHEADER)) {
        return 1;
    }
    lu_err_format(PyExc_TypeError, "%s assignment: '%s' object layout differs from '%s'",
                  attr, newto->tp_name, oldto->tp_name);
    return 0;
}

/* PyEval_GetGlobals: the globals of the nearest frame that is not
   kernel-hidden. */
PyObject *
lu_caller_globals(PyThreadState *ts)
{
    struct lu_frame *f = lu_frame_user((struct lu_frame *)ts->current_frame, 0);
    return f != NULL ? f->globals : NULL;
}

/* The _lucifer primitives over types and objects. Arguments are checked:
   kernel Python passes what users pass. */

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments (%zd given)", __func__ + 5, (n), nargs); \
        return NULL; } } while (0)
#define TYPE_ARG(i) do { if (!PyType_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() argument %d must be a type, not '%.200s'", \
                      __func__ + 5, (i) + 1, Py_TYPE(args[i])->tp_name); \
        return NULL; } } while (0)
#define STR_ARG(i) do { if (!PyUnicode_CheckExact(args[i])) { \
        lu_err_format(PyExc_TypeError, "attribute name must be string, not '%.200s'", \
                      Py_TYPE(args[i])->tp_name); \
        return NULL; } } while (0)
#define DICT_ARG(i) do { if (!PyDict_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() argument %d must be a dict, not '%.200s'", \
                      __func__ + 5, (i) + 1, Py_TYPE(args[i])->tp_name); \
        return NULL; } } while (0)

static PyObject *
or_missing(PyObject *v)
{
    return Py_NewRef(v != NULL ? v : LU_MISSING);
}

static PyObject *
bool_of(int v)
{
    return Py_NewRef(v ? Py_True : Py_False);
}

/* type_lookup(t, name): _PyType_Lookup, MISSING when absent. */
static PyObject *
prim_type_lookup(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0); STR_ARG(1);
    return or_missing(_PyType_Lookup((PyTypeObject *)args[0], args[1]));
}

/* type_dict(t): the type's own dict (interpreter state for static types). */
static PyObject *
prim_type_dict(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    return Py_NewRef(lu_type_dict((PyTypeObject *)args[0]));
}

/* type_mro(t), type_bases(t), type_base(t), type_name(t), type_qualname(t):
   tp_mro (None while unset), tp_bases, tp_base (None for object), __name__
   and __qualname__. */
static PyObject *
prim_type_mro(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    PyObject *mro = ((PyTypeObject *)args[0])->tp_mro;
    return Py_NewRef(mro != NULL ? mro : Py_None);
}

static PyObject *
prim_type_bases(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    return Py_NewRef(((PyTypeObject *)args[0])->tp_bases);
}

static PyObject *
prim_type_base(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    PyTypeObject *base = ((PyTypeObject *)args[0])->tp_base;
    return Py_NewRef(base != NULL ? (PyObject *)base : Py_None);
}

static PyObject *
prim_type_name(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    return lu_type_name((PyTypeObject *)args[0]);
}

static PyObject *
prim_type_qualname(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    if (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        return Py_NewRef(((PyHeapTypeObject *)tp)->ht_qualname);
    }
    return lu_type_name(tp);
}

/* type_set_name(t, which, s): which 0 sets __name__, 1 __qualname__. */
static PyObject *
prim_type_set_name(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); TYPE_ARG(0);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyHeapTypeObject *et = (PyHeapTypeObject *)tp;
    if (!(tp->tp_flags & Py_TPFLAGS_HEAPTYPE) || !PyUnicode_Check(args[2])) {
        lu_err_format(PyExc_SystemError, "type_set_name: a heap type and a str");
        return NULL;
    }
    if (args[1] == lu_small_int(0)) {
        const char *s = PyUnicode_AsUTF8(args[2]);
        if (s == NULL) {
            return NULL;
        }
        Py_SETREF(et->ht_name, Py_NewRef(args[2]));
        tp->tp_name = s;
    }
    else {
        Py_SETREF(et->ht_qualname, Py_NewRef(args[2]));
    }
    return Py_NewRef(Py_None);
}

/* type_layout(t): (tp_flags, tp_basicsize, tp_itemsize, tp_dictoffset, tp_weaklistoffset) */
static PyObject *
prim_type_layout(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyObject *t = PyTuple_New(5);
    long long v[5] = { (long long)tp->tp_flags, tp->tp_basicsize, tp->tp_itemsize, tp->tp_dictoffset,
                       tp->tp_weaklistoffset };
    for (int i = 0; t != NULL && i < 5; i++) {
        PyObject *n = PyLong_FromLongLong(v[i]);
        if (n == NULL) {
            Py_CLEAR(t);
            break;
        }
        PyTuple_SET_ITEM(t, i, n);
    }
    return t;
}

/* object_size(obj): the allocation's size, as object.__sizeof__ answers. */
static PyObject *
prim_object_size(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyTypeObject *tp = Py_TYPE(args[0]);
    Py_ssize_t n = tp->tp_basicsize;
    if (tp->tp_itemsize != 0) {
        Py_ssize_t count = ((PyVarObject *)args[0])->ob_size;   /* not Py_SIZE: ints are sized by lv_tag */
        n += (count < 0 ? -count : count) * tp->tp_itemsize;
    }
    return PyLong_FromLongLong(n);
}

/* type_flag(t, flag): whether every bit of flag is set in tp_flags. */
static PyObject *
prim_type_flag(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0);
    long flag;
    if (!PyLong_CheckExact(args[1]) || lu_long_as_long(args[1], &flag) < 0) {
        lu_err_format(PyExc_TypeError, "type_flag(t, flag): flag is an int");
        return NULL;
    }
    return bool_of((((PyTypeObject *)args[0])->tp_flags & (unsigned long)flag) == (unsigned long)flag);
}

/* type_set_flag(t, flag, on): sets or clears bits of tp_flags: only
   Py_TPFLAGS_IS_ABSTRACT, which __abstractmethods__ assignment toggles. */
static PyObject *
prim_type_set_flag(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); TYPE_ARG(0);
    long flag;
    if (!PyLong_CheckExact(args[1]) || lu_long_as_long(args[1], &flag) < 0
        || (unsigned long)flag != Py_TPFLAGS_IS_ABSTRACT) {
        lu_err_format(PyExc_TypeError, "type_set_flag(t, flag, on): flag is Py_TPFLAGS_IS_ABSTRACT");
        return NULL;
    }
    PyTypeObject *tp = (PyTypeObject *)args[0];
    if (args[2] == Py_True) {
        tp->tp_flags |= (unsigned long)flag;
    }
    else {
        tp->tp_flags &= ~(unsigned long)flag;
    }
    return Py_NewRef(Py_None);
}

/* type_can_new(t): tp_new is set (else "cannot create instances"). */
static PyObject *
prim_type_can_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    return bool_of(((PyTypeObject *)args[0])->tp_new != NULL);
}

/* type_new_layout(meta, name, bases, ns) */
static PyObject *
prim_type_new_layout(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); TYPE_ARG(0);
    if (!PyUnicode_Check(args[1]) || !PyTuple_Check(args[2]) || !PyDict_CheckExact(args[3])) {
        lu_err_format(PyExc_SystemError, "type_new_layout(meta, str, tuple, dict)");
        return NULL;
    }
    if (!PyType_IsSubtype((PyTypeObject *)args[0], &PyType_Type)) {
        lu_err_format(PyExc_TypeError, "type.__new__(%s): %s is not a subtype of type",
                      ((PyTypeObject *)args[0])->tp_name, ((PyTypeObject *)args[0])->tp_name);
        return NULL;
    }
    return type_new_layout((PyTypeObject *)args[0], args[1], args[2], args[3]);
}

/* has_custom_mro: type(tp).mro is not type.mro. */
static int
has_custom_mro(PyTypeObject *tp)
{
    return _PyType_Lookup(Py_TYPE(tp), LU_ID(mro)) != _PyType_Lookup(&PyType_Type, LU_ID(mro));
}

/* type_mro_modified: a type whose MRO the interpreter cannot trust to be the
   linearization of its bases is never cached again. That is a custom mro()
   on a metaclass, or a MRO that hides one of the bases. */
static void
mro_modified(PyTypeObject *tp, PyObject *bases)
{
    int clear = Py_TYPE(tp) != &PyType_Type && has_custom_mro(tp);
    for (Py_ssize_t i = 0; !clear && i < PyTuple_GET_SIZE(bases); i++) {
        PyTypeObject *base = (PyTypeObject *)PyTuple_GET_ITEM(bases, i);
        clear = base->tp_versions_used >= _Py_ATTR_CACHE_UNUSED
            || !PyType_IsSubtype(tp, base);
    }
    if (clear) {
        set_version(tp, 0);
        tp->tp_versions_used = _Py_ATTR_CACHE_UNUSED;
    }
}

/* type_set_mro(t, mro): mro a tuple of types starting with t. */
static PyObject *
prim_type_set_mro(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyObject *mro = args[1];
    if (!PyTuple_CheckExact(mro) || PyTuple_GET_SIZE(mro) == 0) {
        lu_err_format(PyExc_TypeError, "mro() returned a non-tuple or an empty tuple");
        return NULL;
    }
    PyTypeObject *solid = solid_base(tp);       /* mro_check: the type's own solid base, not the type */
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(mro); i++) {
        PyObject *b = PyTuple_GET_ITEM(mro, i);
        if (!PyType_Check(b)) {
            lu_err_format(PyExc_TypeError, "mro() returned a non-class ('%.500s')",
                          Py_TYPE(b)->tp_name);
            return NULL;
        }
        if (!PyType_IsSubtype(solid, solid_base((PyTypeObject *)b))) {
            lu_err_format(PyExc_TypeError, "mro() returned base with unsuitable layout ('%.500s')",
                          ((PyTypeObject *)b)->tp_name);
            return NULL;
        }
    }
    if (tp->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN) {
        lu_set_immortal(mro);
    }
    Py_XSETREF(tp->tp_mro, Py_NewRef(mro));
    if (!(tp->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN)) {
        mro_modified(tp, mro);
        mro_modified(tp, tp->tp_bases);
        lu_type_modified(tp);
    }
    return Py_NewRef(Py_None);
}

/* type_set_bases(t, bases): a static type's bases from its kernel class
   (the first is tp_base), or __bases__ assignment on a heap type. */
static PyObject *
prim_type_set_bases(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    PyObject *bases = args[1];
    if (!PyTuple_CheckExact(bases) || PyTuple_GET_SIZE(bases) == 0) {
        lu_err_format(PyExc_TypeError, "can only assign non-empty tuple to %s.__bases__, not %s",
                      tp->tp_name, Py_TYPE(bases)->tp_name);
        return NULL;
    }
    PyTypeObject *base = best_base(bases);
    if (base == NULL) {
        return NULL;
    }
    if ((tp->tp_flags & Py_TPFLAGS_HEAPTYPE) && !compatible_for_assignment(tp->tp_base, base, "__bases__")) {
        return NULL;
    }
    PyObject *old = tp->tp_bases;
    for (Py_ssize_t i = 0; old != NULL && (tp->tp_flags & Py_TPFLAGS_HEAPTYPE)
                           && i < PyTuple_GET_SIZE(old); i++) {
        remove_subclass((PyTypeObject *)PyTuple_GET_ITEM(old, i), tp);
    }
    if (tp->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN) {
        lu_set_immortal(bases);
        tp->tp_base = base;
    }
    else {
        Py_INCREF(base);
        Py_SETREF(tp->tp_base, base);
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(bases); i++) {
            if (add_subclass((PyTypeObject *)PyTuple_GET_ITEM(bases, i), tp) < 0) {
                return NULL;
            }
        }
    }
    tp->tp_bases = Py_NewRef(bases);
    if (old != NULL && !(tp->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN)) {
        Py_DECREF(old);
    }
    return Py_NewRef(Py_None);
}

/* type_subclasses(t): a new list of the type's subclasses. */
static PyObject *
prim_type_subclasses(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    PyObject *list = *subclasses_ref((PyTypeObject *)args[0]);
    PyObject *copy = PyList_New(0);
    for (Py_ssize_t i = 0; copy != NULL && list != NULL && i < PyList_GET_SIZE(list); i++) {
        PyTypeObject *sub = subclass_of(PyList_GET_ITEM(list, i));
        if (sub != NULL && lu_list_append(copy, (PyObject *)sub) < 0) {
            Py_CLEAR(copy);
        }
    }
    return copy;
}

/* type_modified(t): PyType_Modified, after a change to the type's dict. */
static PyObject *
prim_type_modified(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    lu_type_modified((PyTypeObject *)args[0]);
    return Py_NewRef(Py_None);
}

/* is_subtype(a, b): PyType_IsSubtype. */
static PyObject *
prim_is_subtype(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0); TYPE_ARG(1);
    return bool_of(PyType_IsSubtype((PyTypeObject *)args[0], (PyTypeObject *)args[1]));
}

/* tp_new_wrapper's checks for owner.__new__(cls): cls is a subtype of owner
   and owner is its static base, or an extension type inherited owner's tp_new. */
/* One of the runtime's own static types, not an extension's. */
static int
is_runtime_type(PyTypeObject *tp)
{
    for (PyTypeObject *const *t = lu_static_types; *t != NULL; t++) {
        if (*t == tp) {
            return 1;
        }
    }
    return 0;
}

/* Set by C calling a builtin's tp_new directly (slot_tp_new_builtin): there is
   no tp_new_wrapper in that call to check, and the first check made on behalf
   of the call is the one it stands for. */
static __thread int new_check_skipped;

void
lu_new_check_skip(int on)
{
    new_check_skipped = on;
}

static int
check_new(PyTypeObject *tp, PyTypeObject *owner)
{
    if (new_check_skipped) {
        new_check_skipped = 0;
        return 0;
    }
    PyTypeObject *staticbase = tp;
    if (!PyType_IsSubtype(tp, owner)) {
        lu_err_format(PyExc_TypeError, "%s.__new__(%s): %s is not a subtype of %s",
                      owner->tp_name, tp->tp_name, tp->tp_name, owner->tp_name);
        return -1;
    }
    while (staticbase != NULL && (staticbase->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        staticbase = staticbase->tp_base;
    }
    /* Exception types share one allocation rule: a static exception class
       builds its instances with its base exception's __new__. */
    int inherited = !is_runtime_type(staticbase) && staticbase->tp_new == owner->tp_new;
    if (staticbase != owner && !inherited
        && !(PyExceptionClass_Check(staticbase) && PyExceptionClass_Check(owner))) {
        lu_err_format(PyExc_TypeError, "%s.__new__(%s) is not safe, use %s.__new__()",
                      owner->tp_name, tp->tp_name, staticbase->tp_name);
        return -1;
    }
    return 0;
}

/* alloc(cls, owner): a new instance of cls for owner.__new__, refusing a
   cls whose static base is not owner. */
static PyObject *
prim_alloc(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0); TYPE_ARG(1);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    if (check_new(tp, (PyTypeObject *)args[1]) < 0) {
        return NULL;
    }
    return (tp->tp_alloc != NULL ? tp->tp_alloc : PyType_GenericAlloc)(tp, 0);
}

/* new_check(cls, owner): the same refusals, for a __new__ that allocates itself. */
static PyObject *
prim_new_check(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0); TYPE_ARG(1);
    return check_new((PyTypeObject *)args[0], (PyTypeObject *)args[1]) < 0 ? NULL : Py_NewRef(Py_None);
}

/* type_tp_name(t): tp_name, as error messages name a type. */
static PyObject *
prim_type_tp_name(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    return PyUnicode_FromString(((PyTypeObject *)args[0])->tp_name);
}

/* type_static_module(t): a static type's __module__, the part of tp_name
   before its last dot, else "builtins". */
static PyObject *
prim_type_static_module(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); TYPE_ARG(0);
    const char *name = ((PyTypeObject *)args[0])->tp_name;
    const char *dot = strrchr(name, '.');
    return dot != NULL ? PyUnicode_FromStringAndSize(name, dot - name) : Py_NewRef(LU_ID(builtins));
}

/* size(x): the length of an exact tuple, list or dict. */
static PyObject *
prim_size(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    PyObject *x = args[0];
    if (PyTuple_Check(x) || PyList_Check(x)) {
        return PyLong_FromLongLong(Py_SIZE(x));
    }
    if (PyDict_Check(x)) {
        return PyLong_FromLongLong(((PyDictObject *)x)->ma_used);
    }
    if (PyAnySet_Check(x)) {
        return PyLong_FromLongLong(((PySetObject *)x)->used);
    }
    if (PyUnicode_Check(x)) {
        return PyLong_FromLongLong(PyUnicode_GET_LENGTH(x));
    }
    if (PyBytes_Check(x)) {
        return PyLong_FromLongLong(PyBytes_GET_SIZE(x));
    }
    lu_err_format(PyExc_TypeError, "size() of a '%.200s'", Py_TYPE(x)->tp_name);
    return NULL;
}

/* truth_exact(x): True or False where truth is the layout's (None, bool, int,
   str, tuple, list, dict, objects without __bool__ or __len__), else MISSING. */
static PyObject *
prim_truth_exact(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    int t = lu_truth_exact(args[0]);
    return t < 0 ? Py_NewRef(LU_MISSING) : bool_of(t);
}

/* instance_dict(obj, create): the instance's dict, or None. */
static PyObject *
prim_instance_dict(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyObject *d = instance_dict(args[0], args[1] == Py_True);
    if (d == NULL && PyErr_Occurred()) {
        return NULL;
    }
    return Py_NewRef(d != NULL ? d : Py_None);
}

/* set_class(obj, cls): __class__ assignment after the kernel's checks. */
static PyObject *
prim_set_class(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(1);
    PyObject *self = args[0];
    PyTypeObject *oldto = Py_TYPE(self), *newto = (PyTypeObject *)args[1];
    if (!(PyType_IsSubtype(newto, &PyModule_Type) && PyType_IsSubtype(oldto, &PyModule_Type))
        && ((newto->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) || (oldto->tp_flags & Py_TPFLAGS_IMMUTABLETYPE))) {
        lu_err_format(PyExc_TypeError,
                      "__class__ assignment only supported for mutable types or ModuleType subclasses");
        return NULL;
    }
    if (!compatible_for_assignment(oldto, newto, "__class__")) {
        return NULL;
    }
    if (newto->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        Py_INCREF(newto);
    }
    Py_SET_TYPE(self, newto);
    if (oldto->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        Py_DECREF(oldto);
    }
    return Py_NewRef(Py_None);
}

/* caller_globals(): the calling frame's globals, or None. */
static PyObject *
prim_caller_globals(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    PyObject *g = lu_caller_globals(PyThreadState_Get());
    return Py_NewRef(g != NULL ? g : Py_None);
}

/* super_args(): (__class__, first argument) of the calling frame, for
   zero-argument super() (super_init_without_args). */
static PyObject *
prim_super_args(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(0);
    struct lu_frame *f = lu_frame_user((struct lu_frame *)PyThreadState_Get()->current_frame, 0);
    if (f == NULL) {
        lu_err_format(PyExc_RuntimeError, "super(): no current frame");
        return NULL;
    }
    PyCodeObject *co = f->code;
    if (co->co_argcount == 0) {
        lu_err_format(PyExc_RuntimeError, "super(): no arguments");
        return NULL;
    }
    PyObject *first = f->localsplus[0];
    const char *kinds = PyBytes_AS_STRING(co->co_localspluskinds);
    if (first != NULL && (kinds[0] & CO_FAST_CELL) && PyCell_Check(first)) {
        first = ((PyCellObject *)first)->ob_ref;
    }
    if (first == NULL) {
        lu_err_format(PyExc_RuntimeError, "super(): arg[0] deleted");
        return NULL;
    }
    for (int i = co->co_nlocalsplus - co->co_nfreevars; i < co->co_nlocalsplus; i++) {
        PyObject *name = PyTuple_GET_ITEM(co->co_localsplusnames, i);
        if (!lu_str_eq(name, LU_ID(__class__))) {
            continue;
        }
        PyObject *cell = f->localsplus[i];
        PyObject *cls = cell != NULL ? ((PyCellObject *)cell)->ob_ref : NULL;
        if (cls == NULL) {
            lu_err_format(PyExc_RuntimeError, "super(): empty __class__ cell");
            return NULL;
        }
        if (!PyType_Check(cls)) {
            lu_err_format(PyExc_RuntimeError, "super(): __class__ is not a type (%s)",
                          Py_TYPE(cls)->tp_name);
            return NULL;
        }
        PyObject *pair = PyTuple_New(2);
        if (pair != NULL) {
            PyTuple_SET_ITEM(pair, 0, Py_NewRef(cls));
            PyTuple_SET_ITEM(pair, 1, Py_NewRef(first));
        }
        return pair;
    }
    lu_err_format(PyExc_RuntimeError, "super(): __class__ cell not found");
    return NULL;
}

/* error(type, message): raises. Kernel Python's raise until K1k. */
static PyObject *
prim_error(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); TYPE_ARG(0);
    PyErr_SetObject(args[0], args[1]);
    return NULL;
}

/* Namespace dicts: ns_get(d, key, default), ns_set(d, key, value),
   ns_del(d, key) -> whether it was there, ns_items(d) -> list of pairs. */
static PyObject *
prim_ns_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); DICT_ARG(0); STR_ARG(1);
    PyObject *v = lu_ns_get(args[0], args[1]);
    return v != NULL ? v : Py_NewRef(args[2]);
}

static PyObject *
prim_ns_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); DICT_ARG(0); STR_ARG(1);
    return lu_ns_store(args[0], args[1], args[2]) < 0 ? NULL : Py_NewRef(Py_None);
}

static PyObject *
prim_ns_del(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT_ARG(0); STR_ARG(1);
    return bool_of(lu_ns_delete(args[0], args[1]));
}

static PyObject *
prim_ns_items(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); DICT_ARG(0);
    PyObject *list = PyList_New(0);
    Py_ssize_t pos = 0;
    PyObject *key, *value;
    while (list != NULL && lu_ns_next(args[0], &pos, &key, &value)) {
        PyObject *pair = PyTuple_New(2);
        if (pair == NULL) {
            Py_CLEAR(list);
            break;
        }
        PyTuple_SET_ITEM(pair, 0, Py_NewRef(key));
        PyTuple_SET_ITEM(pair, 1, Py_NewRef(value));
        int err = lu_list_append(list, pair);
        Py_DECREF(pair);
        if (err < 0) {
            Py_CLEAR(list);
        }
    }
    return list;
}

/* is_str(x), is_type(x), is_dict(x), is_tuple(x), is_cell(x),
   is_function(x), is_kernel(f): exact layout checks. */
static PyObject *
prim_kind(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    PyObject *x = args[0];
    const char *kind = PyUnicode_Check(args[1]) ? PyUnicode_AsUTF8(args[1]) : "";
    if (!strcmp(kind, "str")) return bool_of(PyUnicode_Check(x));
    if (!strcmp(kind, "type")) return bool_of(PyType_Check(x));
    if (!strcmp(kind, "dict")) return bool_of(PyDict_Check(x));
    if (!strcmp(kind, "int")) return bool_of(PyLong_Check(x));
    if (!strcmp(kind, "float")) return bool_of(PyFloat_Check(x));
    if (!strcmp(kind, "complex")) return bool_of(PyComplex_Check(x));
    if (!strcmp(kind, "bool")) return bool_of(PyBool_Check(x));
    if (!strcmp(kind, "bytes")) return bool_of(PyBytes_Check(x));
    if (!strcmp(kind, "bytearray")) return bool_of(PyByteArray_Check(x));
    if (!strcmp(kind, "memoryview")) return bool_of(PyMemoryView_Check(x));
    if (!strcmp(kind, "tuple")) return bool_of(PyTuple_Check(x));
    if (!strcmp(kind, "list")) return bool_of(PyList_Check(x));
    if (!strcmp(kind, "set")) return bool_of(PyAnySet_Check(x));
    if (!strcmp(kind, "frozenset")) return bool_of(PyFrozenSet_Check(x));
    if (!strcmp(kind, "slice")) return bool_of(PySlice_Check(x));
    if (!strcmp(kind, "cell")) return bool_of(PyCell_Check(x));
    if (!strcmp(kind, "function")) return bool_of(PyFunction_Check(x));
    if (!strcmp(kind, "kernel")) {
        return bool_of(PyFunction_Check(x)
                       && (((PyCodeObject *)((PyFunctionObject *)x)->func_code)->co_flags & LU_CO_KERNEL));
    }
    lu_err_format(PyExc_SystemError, "kind(x, name): unknown kind '%s'", kind);
    return NULL;
}

/* cell_get(cell) -> value or MISSING; cell_set(cell, value or MISSING to empty it). */
static PyObject *
prim_cell_get(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyCell_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "cell_get() needs a cell");
        return NULL;
    }
    return or_missing(((PyCellObject *)args[0])->ob_ref);
}

static PyObject *
prim_cell_set(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2);
    if (!PyCell_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "cell_set() needs a cell");
        return NULL;
    }
    Py_XSETREF(((PyCellObject *)args[0])->ob_ref, args[1] == LU_MISSING ? NULL : Py_NewRef(args[1]));
    return Py_NewRef(Py_None);
}

/* address(obj): "0x..." for reprs; object_hash(obj): Py_HashPointer. */
static PyObject *
prim_address(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    char buf[32];
    snprintf(buf, sizeof buf, "%p", (void *)args[0]);
    return PyUnicode_FromString(buf);
}

static PyObject *
prim_object_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return PyLong_FromLongLong(Py_HashPointer(args[0]));
}

/* call_with_locals(func, ns): runs a class body with ns as its locals. A
   frame primitive: the interpreter pushes the frame it returns. */
static struct lu_frame *
prim_call_with_locals(PyThreadState *ts, PyObject *const *args, Py_ssize_t nargs)
{
    if (nargs != 2 || !PyFunction_Check(args[0])) {
        lu_err_format(PyExc_TypeError, "call_with_locals(func, ns)");
        return NULL;
    }
    struct lu_frame *f = lu_frame_push(ts, (PyFunctionObject *)args[0], args[1]);
    if (f == NULL) {
        return NULL;
    }
    PyCodeObject *co = (PyCodeObject *)((PyFunctionObject *)args[0])->func_code;
    if (co->co_argcount != 0 || co->co_kwonlyargcount != 0) {
        lu_frame_pop(ts, f);
        lu_err_format(PyExc_TypeError, "a class body takes no arguments");
        return NULL;
    }
    return f;
}

/* static_type(name): the static builtin type whose tp_name is name. */
static PyObject *
prim_static_type(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    const char *name = PyUnicode_Check(args[0]) ? PyUnicode_AsUTF8(args[0]) : "";
    for (PyTypeObject *const *t = lu_static_types; *t != NULL; t++) {
        if (!strcmp((*t)->tp_name, name)) {
            return Py_NewRef((PyObject *)*t);
        }
    }
    lu_err_format(PyExc_SystemError, "static_type: no type '%s'", name);
    return NULL;
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_type_methods[] = {
    FAST(static_type),
    FAST(type_lookup), FAST(type_dict), FAST(type_mro), FAST(type_bases), FAST(type_base),
    FAST(type_name), FAST(type_qualname), FAST(type_set_name), FAST(type_flag), FAST(type_set_flag), FAST(type_layout), FAST(object_size),
    FAST(type_can_new), FAST(type_new_layout), FAST(type_set_mro), FAST(type_set_bases),
    FAST(type_subclasses), FAST(type_modified), FAST(is_subtype), FAST(alloc), FAST(new_check),
    FAST(type_tp_name), FAST(type_static_module), FAST(size),
    FAST(truth_exact), FAST(instance_dict), FAST(set_class), FAST(caller_globals), FAST(super_args),
    FAST(error), FAST(ns_get), FAST(ns_set), FAST(ns_del), FAST(ns_items),
    { "kind", (PyCFunction)(void (*)(void))prim_kind, METH_FASTCALL, NULL },
    FAST(cell_get), FAST(cell_set), FAST(address), FAST(object_hash),
    { "call_with_locals", (PyCFunction)(void (*)(void))prim_call_with_locals,
      METH_FASTCALL | LU_METH_FRAME, NULL },
    { NULL, NULL, 0, NULL },
};
