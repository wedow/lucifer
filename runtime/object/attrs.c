/* Instance attributes (DESIGN 4.1): the dict pointer of an instance, and
   the inline values of a plain class's instances.

   A class whose instances have a managed dict, no items and no builtin base
   but object gets Py_TPFLAGS_INLINE_VALUES. Its shared names (struct
   lu_shared_names in ht_cached_keys) list the attribute names its instances
   have stored, in the order the first of them did; a name keeps its index
   for the life of the class, and the table never moves. The table's room is
   fixed when the class is made: the names its methods and its bases' assign
   to self (__static_attributes__). An instance carries one word per name
   after tp_basicsize, so a site guarded by the type's version reads and
   writes a fixed offset.

   While an instance's LU_ATTRS_IN_DICT flag is clear, its attributes are
   those words and its dict pointer is NULL. Anything that needs a real dict
   (__dict__, vars, a name with no room left or stored out of insertion
   order, __class__ assignment, a C caller of _PyObject_GetDictPtr) moves the
   values into a new dict under the object's lock and sets the flag for good;
   the words stay NULL from then on. A name that shadows a class attribute
   changes the class's version when it joins the table, so a method cached
   for the class needs no check of the instance beyond the flag.

   Concurrency: writers hold the object's critical section, readers take a
   reference with a try-incref and fall back to the lock (as for __slots__
   members). A word read as NULL is only absence while the flag is still
   clear: the move publishes the dict and sets the flag before it clears the
   words.

   Unboxed floats and ints (DESIGN 4.5): each name has a second word.
   Compiled code stores a float (an int) it holds raw as that word, a
   binary64 (an int64), plus LU_UNBOXED (LU_UNBOXED_INT) in the value word,
   making no object, and reads it back without one. The first reader that
   needs the object makes it under the lock and installs it in the value
   word, so every later reader gets the same object; the raw word stays, so
   compiled readers keep reading it. An object stored is the value itself,
   as for any other value. */

#include "lucifer.h"

#include <string.h>

struct lu_shared_names {
    Py_ssize_t capacity;
    Py_ssize_t count;               /* names[0..count) are set and never change */
    PyObject *names[];
};

static struct lu_shared_names *
shared_names(PyTypeObject *tp)
{
    return (struct lu_shared_names *)((PyHeapTypeObject *)tp)->ht_cached_keys;
}

size_t
lu_inline_values_size(PyTypeObject *tp)
{
    return (tp->tp_flags & Py_TPFLAGS_INLINE_VALUES)
        ? (size_t)shared_names(tp)->capacity * LU_INLINE_STRIDE * sizeof(PyObject *) : 0;
}

/* Adds the names of a class's __static_attributes__ to `seen`. */
static int
add_static_attributes(PyObject *seen, PyTypeObject *tp)
{
    PyObject *attrs = (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) && tp->tp_dict != NULL
        ? lu_ns_lookup(tp->tp_dict, LU_ID(__static_attributes__)) : NULL;
    for (Py_ssize_t i = 0; attrs != NULL && PyTuple_Check(attrs) && i < PyTuple_GET_SIZE(attrs); i++) {
        PyObject *name = PyTuple_GET_ITEM(attrs, i);
        if (PyUnicode_CheckExact(name) && lu_ns_insert(seen, name, Py_None) < 0) {
            return -1;
        }
    }
    return 0;
}

int
lu_inline_values_init(PyTypeObject *tp)
{
    PyTypeObject *solid = tp;
    while (solid->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        solid = solid->tp_base;
    }
    if (!(tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) || tp->tp_itemsize != 0 || solid != &PyBaseObject_Type) {
        return 0;
    }
    PyObject *seen = PyDict_New();
    if (seen == NULL || add_static_attributes(seen, tp) < 0) {
        Py_XDECREF(seen);
        return -1;
    }
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(tp->tp_bases); i++) {
        PyObject *mro = ((PyTypeObject *)PyTuple_GET_ITEM(tp->tp_bases, i))->tp_mro;
        for (Py_ssize_t j = 0; mro != NULL && j < PyTuple_GET_SIZE(mro); j++) {
            if (add_static_attributes(seen, (PyTypeObject *)PyTuple_GET_ITEM(mro, j)) < 0) {
                Py_DECREF(seen);
                return -1;
            }
        }
    }
    Py_ssize_t capacity = PyDict_GET_SIZE(seen);
    Py_DECREF(seen);
    struct lu_shared_names *names = PyMem_Calloc(1, sizeof *names + (size_t)capacity * sizeof(PyObject *));
    if (names == NULL) {
        PyErr_NoMemory();
        return -1;
    }
    names->capacity = capacity;
    ((PyHeapTypeObject *)tp)->ht_cached_keys = (PyDictKeysObject *)names;
    tp->tp_flags |= Py_TPFLAGS_INLINE_VALUES;
    return 0;
}

void
lu_inline_values_fini(PyTypeObject *tp)
{
    struct lu_shared_names *names = shared_names(tp);
    if (!(tp->tp_flags & Py_TPFLAGS_INLINE_VALUES) || names == NULL) {
        return;
    }
    for (Py_ssize_t i = 0; i < names->count; i++) {
        Py_DECREF(names->names[i]);
    }
    PyMem_Free(names);
    ((PyHeapTypeObject *)tp)->ht_cached_keys = NULL;
}

Py_ssize_t
lu_inline_index(PyTypeObject *tp, PyObject *name)
{
    struct lu_shared_names *names = shared_names(tp);
    Py_ssize_t n = __atomic_load_n(&names->count, __ATOMIC_ACQUIRE);
    for (Py_ssize_t i = 0; i < n; i++) {
        if (names->names[i] == name) {
            return i;
        }
    }
    if (!PyUnicode_CHECK_INTERNED(name)) {
        for (Py_ssize_t i = 0; i < n; i++) {
            if (lu_str_eq(names->names[i], name)) {
                return i;
            }
        }
    }
    return -1;
}

/* The index of a name new to the class, or -1 when the table is full. */
static Py_ssize_t
add_name(PyTypeObject *tp, PyObject *name)
{
    struct lu_shared_names *names = shared_names(tp);
    PyMutex_Lock(&_PyRuntime.shared_names_lock);
    Py_ssize_t i = lu_inline_index(tp, name);
    int added = 0;
    if (i < 0 && names->count < names->capacity) {
        PyObject *interned = Py_NewRef(name);
        PyUnicode_InternInPlace(&interned);
        i = names->count;
        names->names[i] = interned;
        __atomic_store_n(&names->count, i + 1, __ATOMIC_RELEASE);
        added = 1;
    }
    PyMutex_Unlock(&_PyRuntime.shared_names_lock);
    if (added && _PyType_Lookup(tp, name) != NULL) {
        lu_type_modified(tp);           /* the name shadows a class attribute */
    }
    return i;
}

/* Whether a name new to the instance may take word i: no later word is in
   use, so that the words keep the instance's insertion order. */
static int
in_order(PyObject *obj, Py_ssize_t i)
{
    PyObject **values = lu_inline_values(obj);
    Py_ssize_t n = __atomic_load_n(&shared_names(Py_TYPE(obj))->count, __ATOMIC_ACQUIRE);
    for (Py_ssize_t j = i + 1; j < n; j++) {
        if (LU_INLINE_WORD(values, j) != NULL) {
            return 0;
        }
    }
    return 1;
}

/* The object a value word stands for, in the object's critical section:
   an unboxed float gets its box, installed for every later reader.
   Borrowed; NULL when absent, or with MemoryError. */
static PyObject *
boxed_locked(PyObject **word)
{
    PyObject *v = *word;
    if (lu_unboxed(v)) {
        uint64_t bits = *lu_inline_raw(word);
        double d;
        memcpy(&d, &bits, sizeof d);
        v = v == LU_UNBOXED ? PyFloat_FromDouble(d) : PyLong_FromLongLong((long long)bits);
        if (v != NULL) {
            __atomic_store_n(word, v, __ATOMIC_RELEASE);
        }
    }
    return v;
}

void
lu_inline_install(PyObject *obj, PyObject **word, PyObject *box)
{
    Py_BEGIN_CRITICAL_SECTION(obj);
    PyObject *w = *word;
    if (lu_inline_live(obj) && lu_unboxed(w) && PyFloat_CheckExact(box) == (w == LU_UNBOXED)) {
        uint64_t mine;
        if (w == LU_UNBOXED) {
            double d = PyFloat_AS_DOUBLE(box);
            memcpy(&mine, &d, sizeof d);
        }
        else {
            mine = (uint64_t)PyLong_AsLongLong(box);     /* made from an int64 */
        }
        if (mine == *lu_inline_raw(word)) {
            __atomic_store_n(word, Py_NewRef(box), __ATOMIC_RELEASE);
        }
    }
    Py_END_CRITICAL_SECTION();
}

/* A new reference to the object a value word holds, read without the lock:
   NULL when it holds none (*seen tells absent from unboxed), or when a try
   fails. The word is loaded once: a compiled store may make it a marker at
   any moment, and only the value tested is the one taken. */
static PyObject *
try_word_ref(PyObject **word, PyObject **seen)
{
    PyObject *w = __atomic_load_n(word, __ATOMIC_ACQUIRE);
    *seen = w;
    return w != NULL && !lu_unboxed(w) && lu_try_incref_compare(word, w) ? w : NULL;
}

PyObject *
lu_inline_get(PyObject *obj, PyObject **word)
{
    PyObject *seen, *v = try_word_ref(word, &seen);
    if (v != NULL || !lu_unboxed(seen)) {
        return v;
    }
    Py_BEGIN_CRITICAL_SECTION(obj);
    if (lu_inline_live(obj)) {
        v = boxed_locked(word);
        v = v != NULL ? lu_new_ref_with_lock(v) : NULL;
    }
    Py_END_CRITICAL_SECTION();
    if (v == NULL && PyErr_Occurred()) {
        PyErr_Clear();          /* the reader's slow path asks again */
    }
    return v;
}

/* Moves the inline values into a new dict, in the object's critical section. */
static int
move_to_dict(PyObject *obj)
{
    struct lu_shared_names *names = shared_names(Py_TYPE(obj));
    PyObject **values = lu_inline_values(obj);
    PyObject *dict = PyDict_New();
    if (dict == NULL) {
        return -1;
    }
    Py_ssize_t n = __atomic_load_n(&names->count, __ATOMIC_ACQUIRE);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject **word = &LU_INLINE_WORD(values, i);
        if (*word != NULL && (boxed_locked(word) == NULL || lu_ns_insert(dict, names->names[i], *word) < 0)) {
            Py_DECREF(dict);
            return -1;
        }
    }
    __atomic_store_n(&LU_MANAGED_DICT(obj), dict, __ATOMIC_RELEASE);
    __atomic_fetch_or(&obj->ob_flags, LU_ATTRS_IN_DICT, __ATOMIC_RELEASE);
    for (Py_ssize_t i = 0; i < n; i++) {
        /* the dict holds them */
        Py_XDECREF(__atomic_exchange_n(&LU_INLINE_WORD(values, i), NULL, __ATOMIC_RELEASE));
    }
    return 0;
}

static int
detach_inline(PyObject *obj)
{
    int err = 0;
    Py_BEGIN_CRITICAL_SECTION(obj);
    if (lu_inline_live(obj)) {
        err = move_to_dict(obj);
    }
    Py_END_CRITICAL_SECTION();
    return err;
}

/* The dict pointer, for objects that have one: managed or at tp_dictoffset.
   A static builtin type's dict is reached through lu_type_dict: its own
   tp_dict field stays NULL (FOUNDATIONS 2.1). */
PyObject **
lu_dict_ptr(PyObject *obj)
{
    PyTypeObject *tp = Py_TYPE(obj);
    if (PyType_Check(obj) && (((PyTypeObject *)obj)->tp_flags & _Py_TPFLAGS_STATIC_BUILTIN)) {
        return NULL;
    }
    if (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        return &LU_MANAGED_DICT(obj);
    }
    Py_ssize_t offset = tp->tp_dictoffset;
    if (offset < 0) {
        /* A negative offset counts back from the end of the object
           (_PyObject_ComputedDictPointer). */
        Py_ssize_t tsize = Py_SIZE(obj);
        if (tsize < 0) {
            tsize = -tsize;
        }
        offset += tp->tp_basicsize + tsize * tp->tp_itemsize;
    }
    return offset != 0 ? (PyObject **)((char *)obj + offset) : NULL;
}

PyObject *
lu_instance_dict(PyObject *obj, int create)
{
    PyObject **p = lu_dict_ptr(obj);
    if (p == NULL || (lu_inline_live(obj) && detach_inline(obj) < 0)) {
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

int
lu_instance_set_dict(PyObject *obj, PyObject *dict)
{
    PyObject **p = lu_dict_ptr(obj);
    if (p == NULL) {
        lu_err_format(PyExc_AttributeError, "This object has no __dict__");
        return -1;
    }
    if (lu_inline_live(obj) && detach_inline(obj) < 0) {
        return -1;
    }
    Py_XDECREF(__atomic_exchange_n(p, Py_XNewRef(dict), __ATOMIC_ACQ_REL));
    return 0;
}

int
lu_attr_lookup(PyObject *obj, PyObject *name, PyObject **v)
{
    *v = NULL;
    if (lu_inline_live(obj) && PyUnicode_CheckExact(name)) {
        Py_ssize_t i = lu_inline_index(Py_TYPE(obj), name);
        PyObject **slot = i >= 0 ? &LU_INLINE_WORD(lu_inline_values(obj), i) : NULL;
        PyObject *seen;
        if (slot != NULL && (*v = try_word_ref(slot, &seen)) != NULL) {
            return 1;
        }
        int live;
        Py_BEGIN_CRITICAL_SECTION(obj);
        live = lu_inline_live(obj);
        PyObject *boxed = live && slot != NULL ? boxed_locked(slot) : NULL;
        if (boxed != NULL) {
            *v = lu_new_ref_with_lock(boxed);
        }
        Py_END_CRITICAL_SECTION();
        if (*v == NULL && PyErr_Occurred()) {
            PyErr_Clear();      /* no memory for the box: the kernel asks again and raises */
            return -1;
        }
        if (live) {
            return *v != NULL;
        }
    }
    PyObject **p = lu_dict_ptr(obj);
    PyObject *dict = p != NULL ? __atomic_load_n(p, __ATOMIC_ACQUIRE) : NULL;
    if (dict == NULL) {
        return 0;
    }
    if (!PyDict_CheckExact(dict) || !PyUnicode_CheckExact(name)) {
        return -1;
    }
    *v = lu_ns_get(dict, name);
    return *v != NULL;
}

/* Stores v in word i, holding the object's lock: the old value in *old,
   or 0 when the values moved to the dict or v would break insertion order. */
/* Stores v (or a marker with the raw bits) in word i, holding the
   object's lock: the old value in *old (NULL for none or an unboxed
   value), or 0 when the values moved to the dict or v would break
   insertion order. */
static int
store_locked(PyObject *obj, Py_ssize_t i, PyObject *v, uint64_t bits, PyObject **old)
{
    PyObject **slot = &LU_INLINE_WORD(lu_inline_values(obj), i);
    if (!lu_inline_live(obj) || (*slot == NULL && !in_order(obj, i))) {
        return 0;
    }
    if (lu_unboxed(v)) {
        __atomic_store_n(lu_inline_raw(slot), bits, __ATOMIC_RELAXED);
    }
    else {
        Py_INCREF(v);
    }
    *old = __atomic_exchange_n(slot, v, __ATOMIC_RELEASE);
    if (lu_unboxed(*old)) {
        *old = NULL;
    }
    return 1;
}

static int
store_at(PyObject *obj, Py_ssize_t offset, PyObject *v, uint64_t bits)
{
    Py_ssize_t i = (offset - Py_TYPE(obj)->tp_basicsize) / (Py_ssize_t)(LU_INLINE_STRIDE * sizeof(PyObject *));
    PyObject *old = NULL;
    int done;
    uint8_t unlocked = _Py_UNLOCKED;
    if (__atomic_compare_exchange_n(&obj->ob_mutex._bits, &unlocked, _Py_LOCKED, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        done = store_locked(obj, i, v, bits, &old);     /* the lock alone, as CPython's LOCK_OBJECT */
        PyMutex_Unlock(&obj->ob_mutex);
    }
    else {
        Py_BEGIN_CRITICAL_SECTION(obj);
        done = store_locked(obj, i, v, bits, &old);
        Py_END_CRITICAL_SECTION();
    }
    Py_XDECREF(old);
    return done;
}

int
lu_inline_store(PyObject *obj, Py_ssize_t offset, PyObject *v)
{
    return store_at(obj, offset, v, 0);
}

int
lu_inline_store_f(PyObject *obj, Py_ssize_t offset, double v)
{
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    return store_at(obj, offset, LU_UNBOXED, bits);
}

int
lu_inline_store_i(PyObject *obj, Py_ssize_t offset, int64_t v)
{
    return store_at(obj, offset, LU_UNBOXED_INT, (uint64_t)v);
}

int
lu_attr_store(PyObject *obj, PyObject *name, PyObject *v)
{
    if (lu_inline_live(obj) && PyUnicode_CheckExact(name)) {
        PyTypeObject *tp = Py_TYPE(obj);
        Py_ssize_t i = lu_inline_index(tp, name);
        if (i < 0 && v != NULL) {
            i = add_name(tp, name);
        }
        PyObject *old = NULL;
        int done = 0, err = 0;
        Py_BEGIN_CRITICAL_SECTION(obj);
        if (v == NULL && lu_inline_live(obj)) {
            old = i >= 0 ? __atomic_exchange_n(&LU_INLINE_WORD(lu_inline_values(obj), i), NULL, __ATOMIC_RELEASE)
                         : NULL;
            done = old != NULL ? 1 : -1;
            if (lu_unboxed(old)) {
                old = NULL;
            }
        }
        else if (v != NULL && i >= 0 && store_locked(obj, i, v, 0, &old)) {
            done = 1;
        }
        else if (lu_inline_live(obj)) {
            err = move_to_dict(obj);
        }
        Py_END_CRITICAL_SECTION();
        Py_XDECREF(old);
        if (err < 0) {
            return -1;
        }
        if (done) {
            return done > 0;
        }
    }
    PyObject *dict = lu_instance_dict(obj, v != NULL);
    if (dict == NULL) {
        return PyErr_Occurred() ? -1 : 0;
    }
    if (v == NULL) {
        return lu_ns_delete(dict, name);
    }
    return lu_ns_store(dict, name, v) < 0 ? -1 : 1;
}

int
lu_inline_box_all(PyObject *obj)
{
    int err = 0;
    if (!lu_inline_live(obj)) {
        return 0;
    }
    Py_BEGIN_CRITICAL_SECTION(obj);
    if (lu_inline_live(obj)) {
        PyObject **values = lu_inline_values(obj);
        for (Py_ssize_t i = 0, n = shared_names(Py_TYPE(obj))->count; i < n && !err; i++) {
            if (lu_unboxed(LU_INLINE_WORD(values, i)) && boxed_locked(&LU_INLINE_WORD(values, i)) == NULL) {
                err = -1;
            }
        }
    }
    Py_END_CRITICAL_SECTION();
    return err;
}

/* An unboxed value holds no reference: the collector skips it. */
int
lu_inline_traverse(PyObject *obj, visitproc visit, void *arg)
{
    if (lu_inline_live(obj)) {
        PyObject **values = lu_inline_values(obj);
        for (Py_ssize_t i = 0, n = shared_names(Py_TYPE(obj))->count; i < n; i++) {
            if (!lu_unboxed(LU_INLINE_WORD(values, i))) {
                Py_VISIT(LU_INLINE_WORD(values, i));
            }
        }
    }
    return 0;
}

void
lu_inline_clear(PyObject *obj)
{
    if (lu_inline_live(obj)) {
        PyObject **values = lu_inline_values(obj);
        for (Py_ssize_t i = 0, n = shared_names(Py_TYPE(obj))->count; i < n; i++) {
            if (lu_unboxed(LU_INLINE_WORD(values, i))) {
                LU_INLINE_WORD(values, i) = NULL;
            }
            Py_CLEAR(LU_INLINE_WORD(values, i));
        }
    }
}
