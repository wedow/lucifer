/* Dict primitives under the kernel's dict (DESIGN 4.4, dict family): the
   steps of Objects/dictobject.c that touch the keys table, over the object
   layer of runtime/object/dict.c. Python semantics (hashing, __eq__ on
   candidate keys, the hooks, views, errors) are lib/kernel/dict.py, which
   compares candidates between probes and commits through these.

   Every mutator takes the dict's critical section, as CPython's
   @critical_section dict methods do, so extension code and kernel code
   exclude each other. Probes and entry reads are lock-free: a replaced
   keys table goes to lu_qsbr_retire. A read of an entry that a concurrent
   mutation removed answers MISSING and the kernel probes again. */

#include "prim/prim.h"
#include "object/dict.h"

#define ARGS(n) do { if (nargs != (n)) { \
        lu_err_format(PyExc_TypeError, "%s() takes %d arguments", __func__ + 5, (n)); \
        return NULL; } } while (0)
#define DICT(i) do { if (!PyDict_Check(args[i])) { \
        lu_err_format(PyExc_TypeError, "%s() needs a dict", __func__ + 5); return NULL; } } while (0)
#define SSIZE(i, var) Py_ssize_t var; do { long v_; if (!PyLong_CheckExact(args[i]) \
        || lu_long_as_long(args[i], &v_) < 0) { \
        if (!PyErr_Occurred()) lu_err_format(PyExc_TypeError, "%s() needs an int", __func__ + 5); \
        return NULL; } var = v_; } while (0)

static PyObject *
int_of(Py_ssize_t n)
{
    return PyLong_FromLongLong(n);
}

static PyObject *
bool_of(int v)
{
    return Py_NewRef(v ? Py_True : Py_False);
}

/* The live entry ix of the dict: its key table, or NULL. */
static PyDictKeysObject *
live_entry(PyObject *d, Py_ssize_t ix)
{
    PyDictKeysObject *dk = __atomic_load_n(&((PyDictObject *)d)->ma_keys, __ATOMIC_ACQUIRE);
    return ix >= 0 && ix < dk->dk_nentries && lu_dk_value(dk, ix) != NULL ? dk : NULL;
}

/* dict_new(cls): an empty dict of the class cls, a subclass of dict. */
static PyObject *
prim_dict_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    if (!PyType_Check(args[0]) || !PyType_IsSubtype((PyTypeObject *)args[0], &PyDict_Type)) {
        lu_err_format(PyExc_TypeError, "dict_new() needs a dict class");
        return NULL;
    }
    return lu_dict_alloc((PyTypeObject *)args[0]);
}

/* dict_probe(d, key, hash, after) -> entry number, -1, or -3 - the entry
   number of a candidate to compare. after is -1 or the candidate to pass. */
static PyObject *
prim_dict_probe(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); DICT(0); SSIZE(2, hash); SSIZE(3, after);
    return int_of(lu_dict_probe((PyDictObject *)args[0], args[1], hash, after));
}

/* dict_key(d, ix), dict_value(d, ix): the live entry's key or value, else MISSING. */
static PyObject *
prim_dict_key(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(0); SSIZE(1, ix);
    PyDictKeysObject *dk = live_entry(args[0], ix);
    return Py_NewRef(dk != NULL ? lu_dk_key(dk, ix) : LU_MISSING);
}

static PyObject *
prim_dict_value(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(0); SSIZE(1, ix);
    PyDictKeysObject *dk = live_entry(args[0], ix);
    PyObject *value = dk != NULL ? lu_dk_value(dk, ix) : NULL;
    return Py_NewRef(value != NULL ? value : LU_MISSING);
}

/* dict_hash(d, ix): the hash stored for live entry ix. */
static PyObject *
prim_dict_hash(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(0); SSIZE(1, ix);
    PyDictKeysObject *dk = live_entry(args[0], ix);
    return dk != NULL ? int_of(lu_dk_hash(dk, ix)) : Py_NewRef(LU_MISSING);
}

/* dict_insert(d, key, hash, value): adds a key that is not in d. */
static PyObject *
prim_dict_insert(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(4); DICT(0); SSIZE(2, hash);
    int err;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    err = lu_dict_insert((PyDictObject *)args[0], args[1], hash, args[3]);
    Py_END_CRITICAL_SECTION();
    return err < 0 ? NULL : Py_NewRef(Py_None);
}

/* dict_replace(d, ix, value): sets the value of live entry ix; False when
   the entry is gone. */
static PyObject *
prim_dict_replace(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(3); DICT(0); SSIZE(1, ix);
    int live;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    live = live_entry(args[0], ix) != NULL;
    if (live) {
        lu_dict_replace((PyDictObject *)args[0], ix, args[2]);
    }
    Py_END_CRITICAL_SECTION();
    return bool_of(live);
}

/* dict_remove(d, ix): deletes live entry ix and returns its value, else MISSING. */
static PyObject *
prim_dict_remove(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(0); SSIZE(1, ix);
    PyObject *value = NULL;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    if (live_entry(args[0], ix) != NULL) {
        value = lu_dict_remove((PyDictObject *)args[0], ix);
    }
    Py_END_CRITICAL_SECTION();
    return value != NULL ? value : Py_NewRef(LU_MISSING);
}

/* dict_popitem(d): deletes the newest entry; (key, value), or MISSING when empty. */
static PyObject *
prim_dict_popitem(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); DICT(0);
    PyObject *pair;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    pair = ((PyDictObject *)args[0])->ma_used == 0 ? Py_NewRef(LU_MISSING)
         : lu_dict_popitem((PyDictObject *)args[0]);
    Py_END_CRITICAL_SECTION();
    return pair;
}

static PyObject *
prim_dict_clear(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); DICT(0);
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    lu_dict_clear((PyDictObject *)args[0]);
    Py_END_CRITICAL_SECTION();
    return Py_NewRef(Py_None);
}

/* dict_next(d, ix), dict_prev(d, ix): the live entry number at or after
   (before) ix, or -1. */
static PyObject *
prim_dict_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(0); SSIZE(1, ix);
    return int_of(lu_dict_next_entry((PyDictObject *)args[0], ix));
}

static PyObject *
prim_dict_prev(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(0); SSIZE(1, ix);
    return int_of(lu_dict_prev_entry((PyDictObject *)args[0], ix));
}

/* dict_keys_id(d): identifies d's current keys table, to see a replacement. */
static PyObject *
prim_dict_keys_id(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); DICT(0);
    return PyLong_FromVoidPtr(((PyDictObject *)args[0])->ma_keys);
}

/* dict_copy(d): PyDict_Copy, an exact dict. */
static PyObject *
prim_dict_copy(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); DICT(0);
    PyObject *copy;
    Py_BEGIN_CRITICAL_SECTION(args[0]);
    copy = lu_dict_copy((PyDictObject *)args[0]);
    Py_END_CRITICAL_SECTION();
    return copy;
}

/* dict_merge_prepare(d, other): True when updating d from other needs no
   more, False when the entries of other must be added. */
static PyObject *
prim_dict_merge_prepare(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(0); DICT(1);
    int done;
    Py_BEGIN_CRITICAL_SECTION2(args[0], args[1]);
    done = lu_dict_merge_prepare((PyDictObject *)args[0], (PyDictObject *)args[1]);
    Py_END_CRITICAL_SECTION2();
    return done < 0 ? NULL : bool_of(done);
}

static PyObject *
prim_dict_sizeof(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1); DICT(0);
    return int_of(lu_dict_sizeof((PyDictObject *)args[0]));
}

/* Iterators and views. */

/* dictiterobject and _PyDictViewObject. */
typedef struct {
    PyObject_HEAD
    PyDictObject *di_dict;      /* NULL once exhausted */
    Py_ssize_t di_used;
    Py_ssize_t di_pos;
    Py_ssize_t len;
} dictiterobject;

typedef struct {
    PyObject_HEAD
    PyDictObject *dv_dict;
} dictviewobject;

static void
dictiter_dealloc(PyObject *op)
{
    Py_XDECREF(((dictiterobject *)op)->di_dict);
    lu_object_free(op);
}

static void
dictview_dealloc(PyObject *op)
{
    Py_XDECREF(((dictviewobject *)op)->dv_dict);
    lu_object_free(op);
}

static int
is_reverse(PyTypeObject *tp)
{
    return tp == &PyDictRevIterKey_Type || tp == &PyDictRevIterValue_Type
        || tp == &PyDictRevIterItem_Type;
}

/* dictiter_new(type, d): an iterator of the given dict_*iterator type. */
static PyObject *
prim_dictiter_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(1);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    if (tp != &PyDictIterKey_Type && tp != &PyDictIterValue_Type && tp != &PyDictIterItem_Type
        && !is_reverse(tp)) {
        lu_err_format(PyExc_TypeError, "dictiter_new() needs a dict iterator type");
        return NULL;
    }
    dictiterobject *di = (dictiterobject *)lu_object_new(tp, sizeof(dictiterobject));
    if (di == NULL) {
        return NULL;
    }
    PyDictObject *d = (PyDictObject *)args[1];
    di->di_dict = (PyDictObject *)Py_NewRef(d);
    di->di_used = di->len = d->ma_used;
    di->di_pos = is_reverse(tp) ? d->ma_keys->dk_nentries - 1 : 0;
    return (PyObject *)di;
}

/* dictiter_next(it): the next key, value or (key, value) pair, MISSING when
   exhausted; RuntimeError when the dict changed size or its entries
   during the iteration. */
static PyObject *
prim_dictiter_next(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    dictiterobject *di = (dictiterobject *)args[0];
    PyTypeObject *tp = Py_TYPE(di);
    int reverse = is_reverse(tp);
    PyDictObject *d = di->di_dict;
    if (d == NULL) {
        return Py_NewRef(LU_MISSING);
    }
    PyObject *result = NULL;
    Py_BEGIN_CRITICAL_SECTION(d);
    if (di->di_used != d->ma_used) {
        PyErr_SetString(PyExc_RuntimeError, "dictionary changed size during iteration");
        di->di_used = -1;       /* sticky */
    }
    else {
        Py_ssize_t ix = di->di_pos < 0 ? -1
            : reverse ? lu_dict_prev_entry(d, di->di_pos) : lu_dict_next_entry(d, di->di_pos);
        if (ix >= 0 && di->len == 0) {
            PyErr_SetString(PyExc_RuntimeError, "dictionary keys changed during iteration");
            ix = -2;
        }
        if (ix >= 0) {
            PyDictKeysObject *dk = d->ma_keys;
            PyObject *key = lu_dk_key(dk, ix), *value = lu_dk_value(dk, ix);
            di->di_pos = reverse ? ix - 1 : ix + 1;
            di->len--;
            if (tp == &PyDictIterKey_Type || tp == &PyDictRevIterKey_Type) {
                result = Py_NewRef(key);
            }
            else if (tp == &PyDictIterValue_Type || tp == &PyDictRevIterValue_Type) {
                result = Py_NewRef(value);
            }
            else if ((result = PyTuple_New(2)) != NULL) {
                PyTuple_SET_ITEM(result, 0, Py_NewRef(key));
                PyTuple_SET_ITEM(result, 1, Py_NewRef(value));
            }
        }
        else {
            di->di_dict = NULL;
            Py_DECREF(d);
            result = ix == -1 ? Py_NewRef(LU_MISSING) : NULL;
        }
    }
    Py_END_CRITICAL_SECTION();
    return result;
}

/* dictiter_len(it): the entries left, 0 once the dict changed size. */
static PyObject *
prim_dictiter_len(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    dictiterobject *di = (dictiterobject *)args[0];
    Py_ssize_t len = 0;
    if (di->di_dict != NULL && di->di_used == di->di_dict->ma_used) {
        len = di->len;
    }
    return int_of(len);
}

/* dictiter_copy(it): an iterator at the same place. */
static PyObject *
prim_dictiter_copy(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    dictiterobject *di = (dictiterobject *)args[0];
    dictiterobject *copy = (dictiterobject *)lu_object_new(Py_TYPE(di), sizeof(dictiterobject));
    if (copy != NULL) {
        *copy = (dictiterobject){ .ob_base = copy->ob_base, .di_dict = (PyDictObject *)Py_XNewRef(
            (PyObject *)di->di_dict), .di_used = di->di_used, .di_pos = di->di_pos, .len = di->len };
    }
    return (PyObject *)copy;
}

/* dictview_new(type, d), dictview_dict(view). */
static PyObject *
prim_dictview_new(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(2); DICT(1);
    PyTypeObject *tp = (PyTypeObject *)args[0];
    if (tp != &PyDictKeys_Type && tp != &PyDictValues_Type && tp != &PyDictItems_Type) {
        lu_err_format(PyExc_TypeError, "dictview_new() needs a dict view type");
        return NULL;
    }
    dictviewobject *dv = (dictviewobject *)lu_object_new(tp, sizeof(dictviewobject));
    if (dv != NULL) {
        dv->dv_dict = (PyDictObject *)Py_NewRef(args[1]);
    }
    return (PyObject *)dv;
}

static PyObject *
prim_dictview_dict(PyObject *m, PyObject *const *args, Py_ssize_t nargs)
{
    ARGS(1);
    return Py_NewRef((PyObject *)((dictviewobject *)args[0])->dv_dict);
}

void
lu_dict_views_init(void)
{
    PyTypeObject *iters[] = { &PyDictIterKey_Type, &PyDictIterValue_Type, &PyDictIterItem_Type,
                              &PyDictRevIterKey_Type, &PyDictRevIterValue_Type,
                              &PyDictRevIterItem_Type };
    for (int i = 0; i < 6; i++) {
        iters[i]->tp_basicsize = sizeof(dictiterobject);
        iters[i]->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        iters[i]->tp_dealloc = dictiter_dealloc;
    }
    PyTypeObject *views[] = { &PyDictKeys_Type, &PyDictValues_Type, &PyDictItems_Type };
    for (int i = 0; i < 3; i++) {
        views[i]->tp_basicsize = sizeof(dictviewobject);
        views[i]->tp_flags |= Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
        views[i]->tp_dealloc = dictview_dealloc;
    }
}

#define FAST(name) { #name, (PyCFunction)(void (*)(void))prim_##name, METH_FASTCALL, NULL }
PyMethodDef lu_prim_dict_methods[] = {
    FAST(dict_new), FAST(dict_probe), FAST(dict_key), FAST(dict_value), FAST(dict_hash),
    FAST(dict_insert), FAST(dict_replace), FAST(dict_remove), FAST(dict_popitem),
    FAST(dict_clear), FAST(dict_next), FAST(dict_prev), FAST(dict_keys_id), FAST(dict_copy),
    FAST(dict_merge_prepare), FAST(dict_sizeof), FAST(dictiter_new), FAST(dictiter_next),
    FAST(dictiter_len), FAST(dictiter_copy), FAST(dictview_new), FAST(dictview_dict),
    { NULL, NULL, 0, NULL },
};
