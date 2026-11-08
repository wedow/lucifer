/* The eval loop (DESIGN 6.1): computed-goto dispatch over CPython 3.14
   bytecode, semantics from Python/bytecodes.c written by hand. One
   activation runs all of a task's Python frames: a Python call pushes a frame
   and dispatches into it, a return pops it, and only C callers start a new
   activation (lu_eval). The loop polls the carrier's word at calls and
   back-edges, and RESUME compares the code's monitoring version.

   Stack entries are strong references. An error joins the exception's
   traceback, then looks the raising instruction up in the frame's exception
   table: a handler gets the stack cut to its depth, the raising offset when
   it asks for it, and the exception; no handler pops the frame and the
   caller's loop takes up the error (zero-cost exceptions, 3.11+).

   Attribute access, calls of kernel functions and class bodies stay in the
   loop too: where a type's slot is the kernel's (a thunk), the loop pushes
   the kernel Python function's frame instead of calling the slot, and the
   frame's `ret` says what the caller does with its value (DESIGN 5.2). */

#include "interp/frame.h"
#include "interp/code.h"
#include "interp/exceptions.h"
#include "interp/gen.h"
#include "interp/instrument.h"
#include "interp/specialize.h"
#include "jit/jit.h"
#include "object/dict.h"
#include "object/set.h"

#include "opcode.h"
#include "opcode_ids.h"
#include "prim/prim.h"
#include "prim/bytes.h"

/* Include/internal/pycore_opcode_utils.h */
#define RESUME_AFTER_YIELD_FROM 2
#define RESUME_OPARG_LOCATION_MASK 0x3

#define NAME_ERROR_MSG "name '%.200s' is not defined"
#define UNBOUNDLOCAL_ERROR_MSG \
    "cannot access local variable '%s' where it is not associated with a value"

#define INTRINSIC_UNARY_POSITIVE 5  /* pycore_intrinsics.h */
#define INTRINSIC_PREP_RERAISE_STAR 1   /* pycore_intrinsics.h */
#define INTRINSIC_LIST_TO_TUPLE 6
#define INTRINSIC_IMPORT_STAR 2
#define INTRINSIC_STOPITERATION_ERROR 3
#define INTRINSIC_PRINT 1
#define INTRINSIC_ASYNC_GEN_WRAP 4
#define INTRINSIC_TYPEVAR 7             /* the PEP 695 intrinsics, run by the kernel */
#define INTRINSIC_TYPEALIAS 11
#define INTRINSIC_TYPEVAR_WITH_BOUND 2  /* INTRINSIC_2 */
#define INTRINSIC_SET_TYPEPARAM_DEFAULT 5

#define CANNOT_CATCH_MSG \
    "catching classes that do not inherit from BaseException is not allowed"

/* _PyEval_CheckExceptTypeValid. */
static int
check_except_type(PyObject *right)
{
    if (PyTuple_Check(right)) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(right); i++) {
            if (!PyExceptionClass_Check(PyTuple_GET_ITEM(right, i))) {
                lu_err_format(PyExc_TypeError, CANNOT_CATCH_MSG);
                return -1;
            }
        }
    }
    else if (!PyExceptionClass_Check(right)) {
        lu_err_format(PyExc_TypeError, CANNOT_CATCH_MSG);
        return -1;
    }
    return 0;
}

int
lu_exc_match(PyObject *exc, PyObject *type)
{
    return check_except_type(type) < 0 ? -1 : PyErr_GivenExceptionMatches(exc, type);
}

#define UNBOUNDFREE_ERROR_MSG \
    "cannot access free variable '%s' where it is not associated with a value" \
    " in enclosing scope"

/* _PyEval_FormatExcUnbound: the local at `index` has no value. */
static void
format_unbound(PyCodeObject *co, int index)
{
    const char *name = PyUnicode_AsUTF8(PyTuple_GET_ITEM(co->co_localsplusnames, index));
    if (index < PyUnstable_Code_GetFirstFree(co)) {
        lu_err_format(PyExc_UnboundLocalError, UNBOUNDLOCAL_ERROR_MSG, name);
    }
    else {
        lu_err_format(PyExc_NameError, UNBOUNDFREE_ERROR_MSG, name);
    }
}

/* PyObject_IsTrue where truth is a layout: 1 or 0, or -1 when the type's
   __bool__ or __len__ must run (kernel truth). */
int
lu_truth_exact(PyObject *v)
{
    if (v == Py_True || v == Py_False) {
        return v == Py_True;
    }
    if (v == Py_None) {
        return 0;
    }
    if (PyLong_CheckExact(v)) {
        return ((PyLongObject *)v)->long_value.lv_tag >= (1 << _PyLong_NON_SIZE_BITS);
    }
    if (PyUnicode_CheckExact(v)) {
        return PyUnicode_GET_LENGTH(v) != 0;
    }
    if (PyTuple_CheckExact(v) || PyList_CheckExact(v)) {
        return Py_SIZE(v) != 0;
    }
    if (PyDict_CheckExact(v)) {
        return ((PyDictObject *)v)->ma_used != 0;
    }
    PyTypeObject *tp = Py_TYPE(v);
    if ((tp->tp_as_number == NULL || tp->tp_as_number->nb_bool == NULL)
        && (tp->tp_as_mapping == NULL || tp->tp_as_mapping->mp_length == NULL)
        && (tp->tp_as_sequence == NULL || tp->tp_as_sequence->sq_length == NULL)) {
        return 1;
    }
    return -1;
}

/* PyObject_IsTrue for C callers: the kernel runs __bool__ and __len__. */
int
lu_is_true(PyObject *v)
{
    int t = lu_truth_exact(v);
    if (t >= 0) {
        return t;
    }
    PyObject *r = lu_call_kernel(LU_K_truth, &v, 1);
    if (r == NULL) {
        return -1;
    }
    t = r == Py_True;
    Py_DECREF(r);
    return t;
}

static void
name_error(PyObject *name)
{
    lu_err_format(PyExc_NameError, NAME_ERROR_MSG, PyUnicode_AsUTF8(name));
    PyObject *exc = PyErr_GetRaisedException();
    if (exc != NULL && PyObject_SetAttrString(exc, "name", name) < 0) {
        PyErr_Clear();
    }
    PyErr_SetRaisedException(exc);
}

/* LOAD_NAME and LOAD_GLOBAL over namespace dicts, a new reference; mappings
   that are not exact dicts arrive with the kernel's mapping protocol (K1c). */
static PyObject *
ns_get(PyObject *ns, PyObject *name)
{
    if (!PyDict_CheckExact(ns)) {
        lu_err_format(PyExc_SystemError, "namespace is a %s, not a dict", Py_TYPE(ns)->tp_name);
        return NULL;
    }
    return lu_ns_get(ns, name);
}

/* The lookup in a frame's locals, which a metaclass's __prepare__ can make any
   mapping: a new reference, or NULL without an error for a miss. */
static PyObject *
locals_get(PyObject *ns, PyObject *name)
{
    if (PyDict_CheckExact(ns)) {
        return lu_ns_get(ns, name);
    }
    PyObject *args[2] = { ns, name };
    PyObject *v = lu_call_kernel(LU_K_getitem, args, 2);
    if (v == NULL && PyErr_ExceptionMatches(PyExc_KeyError)) {
        PyErr_Clear();
    }
    return v;
}

/* LOAD_GLOBAL's lookup: the globals, then the builtins, either of which may be
   a dict subclass or other mapping (annotationlib runs __annotate__ over one).
   A new reference, or NULL without an error for a miss. */
static PyObject *
global_get(struct lu_frame *frame, PyObject *name)
{
    PyObject *v = locals_get(frame->globals, name);
    if (v == NULL && !PyErr_Occurred()) {
        v = locals_get(frame->builtins, name);
    }
    return v;
}

/* IMPORT_NAME: builtins.__import__(name, globals, locals, fromlist, level).
   Until builtins.py has defined it, while the kernel boots, a name is its
   sys.modules entry. */
static PyObject *
import_name(PyThreadState *ts, struct lu_frame *frame, PyObject *name, PyObject *fromlist,
            PyObject *level)
{
    PyObject *import_func = lu_ns_get(ts->interp->builtins, LU_ID(__import__));
    if (import_func == NULL) {
        PyObject *mod = lu_ns_get(ts->interp->modules, name);
        if (mod == NULL) {
            lu_err_format(PyExc_ModuleNotFoundError, "No module named '%s'", PyUnicode_AsUTF8(name));
        }
        return mod;
    }
    PyObject *args[5] = { name, frame->globals, frame->locals != NULL ? frame->locals : Py_None,
                          fromlist, level };
    PyObject *r = lu_call(import_func, args, 5, NULL);
    Py_DECREF(import_func);
    return r;
}

/* IMPORT_FROM: the attribute of an exact module's dict, else the kernel's
   import_from, which falls back to sys.modules and words the error. */
static PyObject *
import_from(PyObject *module, PyObject *name)
{
    PyObject *v = PyModule_CheckExact(module)
        ? lu_ns_get(((PyModuleObject *)module)->md_dict, name) : NULL;
    if (v != NULL) {
        return v;
    }
    PyObject *args[2] = { module, name };
    return lu_call_kernel(LU_K_import_from, args, 2);
}

/* GET_ITER of the exact types the loop iterates itself; NULL without an
   error for every other iterable, which the kernel's get_iter handles. */
PyObject *
lu_get_iter_exact(PyObject *v)
{
    PyTypeObject *tp = Py_TYPE(v);
    if (tp == &PyTuple_Type || tp == &PyList_Type) {
        seqiterobject *it = (seqiterobject *)lu_object_new(
            tp == &PyTuple_Type ? &PyTupleIter_Type : &PyListIter_Type, sizeof(seqiterobject));
        if (it != NULL) {
            it->it_seq = Py_NewRef(v);
            return (PyObject *)it;
        }
        return NULL;
    }
    if (tp == &PyRange_Type) {
        return lu_range_iter(v);
    }
    if (tp == &PyEnum_Type || tp == &PyZip_Type || tp == &PyMap_Type || tp == &PyFilter_Type
        || tp == &PyReversed_Type || tp == &PyListIter_Type || tp == &PyTupleIter_Type
        || tp == &PyRangeIter_Type || tp == &PyDictIterKey_Type || tp == &PyDictIterValue_Type
        || tp == &PyDictIterItem_Type) {
        return Py_NewRef(v);        /* an iterator: its __iter__ returns itself */
    }
    return lu_dict_iter(v);
}

/* UNPACK_SEQUENCE of an exact tuple or list of n items: the items, last
   first. Other sizes and iterables go to the kernel for its messages. */
static int
unpack_exact(PyObject *seq, int n, PyObject **out)
{
    if ((!PyTuple_CheckExact(seq) && !PyList_CheckExact(seq)) || Py_SIZE(seq) != n) {
        return 0;
    }
    if (PyTuple_CheckExact(seq)) {
        for (int i = 0; i < n; i++) {
            out[n - 1 - i] = Py_NewRef(PyTuple_GET_ITEM(seq, i));
        }
        return 1;
    }
    int done = 0;
    Py_BEGIN_CRITICAL_SECTION(seq);
    if (Py_SIZE(seq) == n) {
        for (int i = 0; i < n; i++) {
            out[n - 1 - i] = Py_NewRef(((PyListObject *)seq)->ob_item[i]);
        }
        done = 1;
    }
    Py_END_CRITICAL_SECTION();
    return done;
}

/* The oparg of the instruction at `at`, with its EXTENDED_ARG prefixes, which
   instrumentation may hide behind INSTRUMENTED_LINE or INSTRUMENTED_INSTRUCTION. */
static int
instr_oparg(PyCodeObject *code, lu_codeunit *at)
{
    int arg = at->op.arg, shift = 8;
    for (lu_codeunit *p = at - 1; lu_base_opcode(code, p - lu_code_start(code)) == EXTENDED_ARG;
         p--, shift += 8) {
        arg |= p->op.arg << shift;
    }
    return arg;
}

/* The compact non-negative int index of sub, or -1. */
static Py_ssize_t
small_index(PyObject *sub)
{
    return PyLong_CheckExact(sub) && _PyLong_IsCompact((PyLongObject *)sub)
        && _PyLong_CompactValue((PyLongObject *)sub) >= 0
        ? (Py_ssize_t)_PyLong_CompactValue((PyLongObject *)sub) : -1;
}

/* BINARY_OP NB_SUBSCR on an exact list or tuple with a compact int index in
   range, negative ones counted from the end: the item (a new reference), or NULL. */
static PyObject *
subscr_item(PyObject *container, PyObject *sub)
{
    if (!PyLong_CheckExact(sub) || !_PyLong_IsCompact((PyLongObject *)sub)) {
        return NULL;
    }
    Py_ssize_t i = _PyLong_CompactValue((PyLongObject *)sub);
    PyObject *item = NULL;
    if (PyList_CheckExact(container)) {
        i += i < 0 ? Py_SIZE(container) : 0;
        item = i >= 0 ? lu_list_item_ref((PyListObject *)container, i) : NULL;
    }
    else if (PyTuple_CheckExact(container)) {
        i += i < 0 ? Py_SIZE(container) : 0;
        item = i >= 0 && i < Py_SIZE(container) ? Py_NewRef(((PyTupleObject *)container)->ob_item[i]) : NULL;
    }
    return item;
}

/* A slice bound that is None (*v stays) or an exact compact int (*v is set). */
static int
slice_bound(PyObject *bound, Py_ssize_t *v)
{
    if (bound == NULL || bound == Py_None) {
        return 1;
    }
    if (!PyLong_CheckExact(bound) || !_PyLong_IsCompact((PyLongObject *)bound)) {
        return 0;
    }
    *v = _PyLong_CompactValue((PyLongObject *)bound);
    return 1;
}

/* PySlice_Unpack and PySlice_AdjustIndices for bounds of None or compact
   ints over a sequence of `length`: 1 with the first position, the step and
   the count, or 0 when the kernel must decide (another bound type, a zero step). */
static int
slice_unpack(PyObject *start_o, PyObject *stop_o, PyObject *step_o, Py_ssize_t length,
             Py_ssize_t *start, Py_ssize_t *step, Py_ssize_t *count)
{
    Py_ssize_t stop;
    *step = 1;
    if (!slice_bound(step_o, step) || *step == 0) {
        return 0;
    }
    int down = *step < 0;
    *start = down ? length - 1 : 0;
    stop = down ? -1 : length;
    if (!slice_bound(start_o, start) || !slice_bound(stop_o, &stop)) {
        return 0;
    }
    if (start_o != NULL && start_o != Py_None) {
        *start += *start < 0 ? length : 0;
        *start = *start < 0 ? (down ? -1 : 0) : *start >= length ? (down ? length - 1 : length) : *start;
    }
    if (stop_o != NULL && stop_o != Py_None) {
        stop += stop < 0 ? length : 0;
        stop = stop < 0 ? (down ? -1 : 0) : stop >= length ? (down ? length - 1 : length) : stop;
    }
    *count = down ? (stop < *start ? (*start - stop - 1) / -*step + 1 : 0)
                  : (*start < stop ? (stop - *start - 1) / *step + 1 : 0);
    return 1;
}

/* container[start:stop:step] for an exact list, tuple, str or bytes and bounds
   that slice_unpack takes, where the step of a str or bytes is 1: a new
   reference. NULL without an error when the kernel must slice. */
PyObject *
lu_slice_native(PyObject *container, PyObject *start_o, PyObject *stop_o, PyObject *step_o)
{
    PyTypeObject *tp = Py_TYPE(container);
    Py_ssize_t length, start, step, count;
    if (tp == &PyList_Type || tp == &PyTuple_Type) {
        length = Py_SIZE(container);
    }
    else if (tp == &PyUnicode_Type) {
        length = PyUnicode_GET_LENGTH(container);
    }
    else if (tp == &PyBytes_Type) {
        length = PyBytes_GET_SIZE(container);
    }
    else {
        return NULL;
    }
    if (!slice_unpack(start_o, stop_o, step_o, length, &start, &step, &count)
        || (tp != &PyList_Type && tp != &PyTuple_Type && step != 1)) {
        return NULL;
    }
    if (tp != &PyList_Type && start == 0 && count == length) {
        return Py_NewRef(container);
    }
    if (tp == &PyUnicode_Type) {
        return PyUnicode_Substring(container, start, start + count);
    }
    if (tp == &PyBytes_Type) {
        return PyBytes_FromStringAndSize(PyBytes_AS_STRING(container) + start, count);
    }
    return lu_seq_slice(container, start, step, count, tp == &PyTuple_Type);
}

PyObject *
lu_subscr_native(PyObject *container, PyObject *sub)
{
    PyObject *item = subscr_item(container, sub);
    if (item != NULL) {
        return item;
    }
    if (PyBytes_CheckExact(container) && PyLong_CheckExact(sub) && _PyLong_IsCompact((PyLongObject *)sub)
        && _PyLong_CompactValue((PyLongObject *)sub) >= 0
        && _PyLong_CompactValue((PyLongObject *)sub) < PyBytes_GET_SIZE(container)) {
        return lu_small_int((unsigned char)PyBytes_AS_STRING(container)[_PyLong_CompactValue((PyLongObject *)sub)]);
    }
    int found = PyDict_CheckExact(container) ? lu_dict_find_native(container, sub, &item) : -1;
    if (found > 0) {
        return item;
    }
    if (found == 0) {
        /* An exact dict has no __missing__: its __getitem__ raises KeyError(sub). */
        PyObject *args = PyTuple_Pack(1, sub);
        if (args != NULL) {
            PyErr_SetObject(PyExc_KeyError, args);
            Py_DECREF(args);
        }
        return NULL;
    }
    if (Py_IS_TYPE(sub, &PySlice_Type)) {
        PySliceObject *slc = (PySliceObject *)sub;
        return lu_slice_native(container, slc->start, slc->stop, slc->step);
    }
    return NULL;
}

/* STORE_SUBSCR into an exact list at a compact int index in range: the
   replaced item (a new reference to release), or NULL. */
static PyObject *
store_subscr_list(PyObject *container, PyObject *sub, PyObject *value)
{
    Py_ssize_t i = small_index(sub);
    PyObject *old = NULL;
    if (i >= 0 && PyList_CheckExact(container)) {
        Py_BEGIN_CRITICAL_SECTION(container);
        if (i < Py_SIZE(container)) {
            old = ((PyListObject *)container)->ob_item[i];
            _Py_atomic_store_ptr_release(&((PyListObject *)container)->ob_item[i], value);
        }
        Py_END_CRITICAL_SECTION();
    }
    return old;
}

/* Whether one of n items is item, as lu_native_eq decides: 1, 0, or -1. */
static int
items_contain(PyObject *const *items, Py_ssize_t n, PyObject *item)
{
    for (Py_ssize_t i = 0; i < n; i++) {
        int eq = items[i] == item ? 1 : lu_native_eq(items[i], item);
        if (eq != 0) {
            return eq;
        }
    }
    return 0;
}

/* `item in container` for an exact set, frozenset, dict, tuple or list, as
   lu_dict_find_native decides it: 1, 0, or -1 for the kernel. A list is
   scanned in its critical section, which its mutators hold: a resize
   replaces the array and the size apart. */
static int
contains_native(PyObject *container, PyObject *item)
{
    PyObject *value;
    int found = -1;
    if (PyAnySet_CheckExact(container)) {
        PyTypeObject *kt = Py_TYPE(item);
        PyObject *candidate = NULL;
        if (kt != &PyUnicode_Type && kt != &PyLong_Type) {
            return -1;
        }
        int verdict = lu_set_probe((PySetObject *)container, item, lu_hash(item), NULL, &candidate);
        Py_XDECREF(candidate);
        return verdict == LU_SET_FOUND ? 1 : verdict == LU_SET_ABSENT ? 0 : -1;
    }
    if (PyDict_CheckExact(container)) {
        int hit = lu_dict_find_native(container, item, &value);
        if (hit > 0) {
            Py_DECREF(value);
        }
        return hit;
    }
    if (PyTuple_CheckExact(container)) {
        return items_contain(((PyTupleObject *)container)->ob_item, Py_SIZE(container), item);
    }
    if (PyList_CheckExact(container)) {
        Py_BEGIN_CRITICAL_SECTION(container);
        found = items_contain(((PyListObject *)container)->ob_item, Py_SIZE(container), item);
        Py_END_CRITICAL_SECTION();
    }
    return found;
}

/* Whether instances of `tp` use object's attribute hook `id` (__getattribute__
   or __setattr__), so that a data descriptor of the type decides the access. */
static int
object_attr_hook(PyTypeObject *tp, PyObject *id)
{
    return _PyType_Lookup(tp, id) == _PyType_Lookup(&PyBaseObject_Type, id);
}

/* LOAD_ATTR answers that need no Python: obj.__class__ for an object whose
   type keeps object's __class__, and a plain class attribute (no __get__, or
   a function, which a class returns unbound) of a class whose metaclass keeps
   type's __getattribute__ and does not have the name. A new reference, or
   NULL. */
static PyObject *
attr_shortcut(PyObject *owner, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(owner);
    if (name == LU_ID(__class__)) {
        return _PyType_Lookup(tp, name) == _PyType_Lookup(&PyBaseObject_Type, name)
            && object_attr_hook(tp, LU_ID(__getattribute__)) ? Py_NewRef((PyObject *)tp) : NULL;
    }
    if (!PyType_Check(owner) || !PyUnicode_CheckExact(name)
        || _PyType_Lookup(tp, LU_ID(__getattribute__)) != _PyType_Lookup(&PyType_Type, LU_ID(__getattribute__))) {
        return NULL;
    }
    PyObject *meta_attr = _PyType_LookupRef(tp, name);
    int data = meta_attr != NULL && Py_TYPE(meta_attr)->tp_descr_set != NULL;
    Py_XDECREF(meta_attr);
    if (data) {
        return NULL;            /* a data descriptor of the metaclass wins */
    }
    PyObject *v = _PyType_LookupRef((PyTypeObject *)owner, name);
    if (v != NULL && Py_TYPE(v)->tp_descr_get != NULL && !PyFunction_Check(v)) {
        Py_CLEAR(v);
    }
    return v;
}

PyObject *
lu_getattr_native(PyObject *owner, PyObject *name)
{
    PyTypeObject *tp = Py_TYPE(owner);
    PyObject *v;
    if (!PyUnicode_CheckExact(name)) {
        return NULL;
    }
    if (PyModule_CheckExact(owner)) {
        return lu_ns_get(((PyModuleObject *)owner)->md_dict, name);        /* as LOAD_ATTR reads it */
    }
    if ((v = attr_shortcut(owner, name)) != NULL) {
        return v;
    }
    if (!object_attr_hook(tp, LU_ID(__getattribute__))) {
        return NULL;
    }
    PyObject *descr = _PyType_LookupRef(tp, name);
    if (descr != NULL && Py_TYPE(descr)->tp_descr_set != NULL) {
        v = lu_descr_get_native(descr, owner, &v) > 0 ? v : NULL;
        Py_DECREF(descr);
        return v;
    }
    if (tp->tp_flags & Py_TPFLAGS_MANAGED_DICT) {
        int found = lu_attr_lookup(owner, name, &v);
        if (found != 0) {
            Py_XDECREF(descr);
            return v;           /* NULL when only the kernel can answer */
        }
    }
    else if (tp->tp_dictoffset != 0) {
        Py_XDECREF(descr);
        return NULL;            /* a dict of another layout */
    }
    if (descr == NULL) {
        return NULL;
    }
    if (Py_TYPE(descr)->tp_descr_get == NULL) {
        return descr;
    }
    v = PyFunction_Check(descr) ? PyMethod_New(descr, owner) : lu_staticmethod_get(descr);
    Py_DECREF(descr);
    return v;
}

/* Whether tp compares for equality as `base` does: the same __eq__ and __ne__. */
static int
eq_as(PyTypeObject *tp, PyTypeObject *base)
{
    return _PyType_Lookup(tp, LU_ID(__eq__)) == _PyType_Lookup(base, LU_ID(__eq__))
        && _PyType_Lookup(tp, LU_ID(__ne__)) == _PyType_Lookup(base, LU_ID(__ne__));
}

/* a == b as PyObject_RichCompare answers it when no Python code decides:
   builtin scalars by value, objects with object's equality by identity,
   tuples item by item (an identical pair of items is equal, as
   PyObject_RichCompareBool has it). 1, 0, or -1 when the kernel must compare. */
static int
eq_native(PyObject *a, PyObject *b, int depth)
{
    if (a == b && PyFloat_CheckExact(a)) {
        return -1;              /* lu_native_eq treats a key as equal to itself: not a nan */
    }
    int r = lu_native_eq(a, b);
    if (r >= 0) {
        return r;
    }
    PyTypeObject *ta = Py_TYPE(a), *tb = Py_TYPE(b);
    if (eq_as(ta, &PyBaseObject_Type) && eq_as(tb, &PyBaseObject_Type)) {
        return a == b;
    }
    if (PyTuple_Check(a) && PyTuple_Check(b) && depth < 16 && eq_as(ta, &PyTuple_Type)
        && eq_as(tb, &PyTuple_Type)) {
        Py_ssize_t n = Py_MIN(Py_SIZE(a), Py_SIZE(b));
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject *x = PyTuple_GET_ITEM(a, i), *y = PyTuple_GET_ITEM(b, i);
            if (x != y && (r = eq_native(x, y, depth + 1)) != 1) {
                return r;
            }
        }
        return Py_SIZE(a) == Py_SIZE(b);
    }
    return -1;
}

int
lu_compare_native(PyObject *a, PyObject *b, int oparg)
{
    int op = oparg >> 5, eq;
    if ((PyLong_CheckExact(a) || PyBool_Check(a)) && (PyLong_CheckExact(b) || PyBool_Check(b))) {
        int c = lu_long_compare(a, b);
        return op == Py_LT ? c < 0 : op == Py_LE ? c <= 0 : op == Py_EQ ? c == 0
            : op == Py_NE ? c != 0 : op == Py_GT ? c > 0 : c >= 0;
    }
    if ((op == Py_EQ || op == Py_NE) && (eq = eq_native(a, b, 0)) >= 0) {
        return eq == (op == Py_EQ);
    }
    return -1;
}

/* How `cls(...)` with `total` arguments runs when type.__call__ has nothing to
   decide, for a class whose metaclass is type: 2 when it only allocates (its
   __new__ and __init__ are object's, and no arguments), 1 when it also calls
   the Python function *func (its __new__ is object's), 3 when *func is its
   __new__, called with the class (its __init__ is object's, so that only an
   instance of a subclass may need one), 0 for the kernel. */
static int
plan_for(PyTypeObject *cls, Py_ssize_t total, PyObject *new_, PyObject *init, PyObject **func)
{
    PyTypeObject *object = &PyBaseObject_Type;
    PyObject *object_init = _PyType_Lookup(object, LU_ID(__init__));
    if (cls->tp_new == NULL || new_ == NULL) {
        return 0;
    }
    if (new_ != _PyType_Lookup(object, LU_ID(__new__))) {
        return init == object_init && lu_new_function(new_, func) ? 3 : 0;
    }
    if ((cls->tp_flags & Py_TPFLAGS_IS_ABSTRACT)) {
        return 0;
    }
    if (init == object_init) {
        return total == 0 ? 2 : 0;
    }
    *func = init;
    return init != NULL && PyFunction_Check(init);
}

/* plan_for over the class's __new__ and __init__, which another thread may
   replace: *func, when the plan is 1 or 3, is a new reference. */
static int
instance_plan(PyTypeObject *cls, Py_ssize_t total, PyObject **func)
{
    PyObject *new_ = _PyType_LookupRef(cls, LU_ID(__new__)), *init = _PyType_LookupRef(cls, LU_ID(__init__));
    PyObject *f = NULL;
    int plan = plan_for(cls, total, new_, init, &f);
    *func = plan == 1 || plan == 3 ? Py_NewRef(f) : NULL;
    Py_XDECREF(new_);
    Py_XDECREF(init);
    return plan;
}

/* The rest of type.__call__ once the class's __new__ returned obj (a
   reference this takes): when obj is an instance of cls, its type's __init__
   with the call's positional args, unless that is object's and has nothing to
   refuse. obj, or NULL with an error. */
static PyObject *
init_new_instance(PyThreadState *ts, PyTypeObject *cls, PyObject *obj, PyObject *args)
{
    PyTypeObject *tp = Py_TYPE(obj), *object = &PyBaseObject_Type;
    PyObject *init = PyType_IsSubtype(tp, cls) ? _PyType_Lookup(tp, LU_ID(__init__)) : NULL;
    Py_ssize_t n = PyTuple_GET_SIZE(args);
    if (init == NULL
        || (init == _PyType_Lookup(object, LU_ID(__init__))
            && (n == 0 || _PyType_Lookup(tp, LU_ID(__new__)) != _PyType_Lookup(object, LU_ID(__new__))))) {
        return obj;
    }
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    PyObject *r = lu_call_special(obj, LU_ID(__init__), &PyTuple_GET_ITEM(args, 0), n);
    t->pin--;
    if (r != Py_None) {
        if (r != NULL) {
            lu_err_format(PyExc_TypeError, "__init__() should return None, not '%.200s'",
                          Py_TYPE(r)->tp_name);
            Py_DECREF(r);
        }
        Py_DECREF(obj);
        return NULL;
    }
    Py_DECREF(r);
    return obj;
}

/* isinstance(obj, cls) for an exact class, or a tuple of them, as
   object_isinstance has it: a negative answer asks obj.__class__, which is
   the type unless the type overrides __class__ or __getattribute__. 1, 0, or
   -1 for the kernel. */
static int
isinstance_native(PyObject *obj, PyObject *cls, int depth)
{
    PyTypeObject *t = Py_TYPE(obj);
    if (PyType_CheckExact(cls)) {
        if (PyType_IsSubtype(t, (PyTypeObject *)cls)) {
            return 1;
        }
        return object_attr_hook(t, LU_ID(__class__)) && object_attr_hook(t, LU_ID(__getattribute__)) ? 0 : -1;
    }
    if (PyTuple_CheckExact(cls) && depth < 16) {
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(cls); i++) {
            int is = isinstance_native(obj, PyTuple_GET_ITEM(cls, i), depth + 1);
            if (is != 0) {
                return is;
            }
        }
        return 0;
    }
    return -1;
}

/* list(seq) and tuple(seq) of an exact list or tuple, or of nothing (seq
   NULL): a new reference; NULL for any other argument, which is the kernel's. */
static PyObject *
seq_copy_native(int as_tuple, PyObject *seq)
{
    if (seq == NULL) {
        return as_tuple ? PyTuple_New(0) : PyList_New(0);
    }
    if (as_tuple && PyTuple_CheckExact(seq)) {
        return Py_NewRef(seq);
    }
    if (PyTuple_CheckExact(seq) || PyList_CheckExact(seq)) {
        return lu_seq_slice(seq, 0, 1, Py_SIZE(seq), as_tuple);
    }
    return NULL;
}

PyObject *
lu_native_call(PyThreadState *ts, PyObject *target, PyObject *const *args, Py_ssize_t npos)
{
    PyObject *const *kernel = ts->interp->kernel;
    if (target == kernel[LU_K_dict_get]) {
        PyObject *found;
        if ((npos == 2 || npos == 3) && PyDict_CheckExact(args[0])) {
            int hit = lu_dict_find_native(args[0], args[1], &found);
            if (hit >= 0) {
                return hit ? found : Py_NewRef(npos == 3 ? args[2] : Py_None);
            }
        }
    }
    else if (target == kernel[LU_K_builtin_getattr]) {
        if (npos == 2 || npos == 3) {
            return lu_getattr_native(args[0], args[1]);
        }
    }
    else if (target == kernel[LU_K_builtin_hasattr]) {
        PyObject *v = npos == 2 ? lu_getattr_native(args[0], args[1]) : NULL;
        if (v != NULL) {
            Py_DECREF(v);
            return Py_NewRef(Py_True);
        }
    }
    else if (target == kernel[LU_K_builtin_setattr]) {
        if (npos == 3 && lu_setattr_native(args[0], args[1], args[2]) > 0) {
            return Py_NewRef(Py_None);
        }
    }
    else if (target == kernel[LU_K_index]) {
        if (npos == 1 && PyLong_CheckExact(args[0])) {
            return Py_NewRef(args[0]);
        }
    }
    else if (target == kernel[LU_K_bytearray_append]) {
        if (npos == 2 && PyByteArray_CheckExact(args[0]) && PyLong_CheckExact(args[1])
            && _PyLong_IsCompact((PyLongObject *)args[1])
            && (size_t)_PyLong_CompactValue((PyLongObject *)args[1]) < 256) {
            return lu_bytearray_append(args[0], (int)_PyLong_CompactValue((PyLongObject *)args[1])) < 0
                ? NULL : Py_NewRef(Py_None);
        }
    }
    else if (target == kernel[LU_K_list_append]) {
        if (npos == 2 && PyList_CheckExact(args[0])) {
            int err;
            Py_BEGIN_CRITICAL_SECTION(args[0]);
            err = lu_list_append(args[0], args[1]);
            Py_END_CRITICAL_SECTION();
            return err < 0 ? NULL : Py_NewRef(Py_None);
        }
    }
    else if (target == kernel[LU_K_dict_items]) {
        if (npos == 1 && PyDict_Check(args[0])) {
            return lu_dict_view(args[0], &PyDictItems_Type);
        }
    }
    else if (target == kernel[LU_K_len]) {
        PyTypeObject *t = npos == 1 ? Py_TYPE(args[0]) : NULL;
        if (t == &PyList_Type || t == &PyTuple_Type) {
            return PyLong_FromSsize_t(Py_SIZE(args[0]));
        }
        if (t == &PyDict_Type) {
            return PyLong_FromSsize_t(((PyDictObject *)args[0])->ma_used);
        }
        if (t == &PyUnicode_Type) {
            return PyLong_FromSsize_t(PyUnicode_GET_LENGTH(args[0]));
        }
    }
    else if (target == kernel[LU_K_isinstance]) {
        int is = npos == 2 ? isinstance_native(args[0], args[1], 0) : -1;
        if (is >= 0) {
            return Py_NewRef(is ? Py_True : Py_False);
        }
    }
    return NULL;
}

/* SAVE_FRAME as an expression, for a condition that may raise. */
static inline int
save_frame_then(struct lu_frame *f, lu_codeunit *instr, PyObject **sp, lu_codeunit *last)
{
    f->instr = instr;
    f->stackpointer = sp;
    f->last = last;
    return 1;
}

/* How eval_frame starts: by running `frame`, by raising the exception in
   flight at its current instruction (a generator's throw), or by resuming
   the task at its top frame (lu_resume). */
enum { EVAL_RUN, EVAL_THROW, EVAL_RESUME, EVAL_CONTINUE, EVAL_CONTINUE_ERROR };

static struct lu_frame *
base_entry(struct lu_task *t)
{
    if (t->base_entry == NULL) {
        t->base_entry = PyMem_RawCalloc(1, sizeof(struct lu_frame));
        if (t->base_entry == NULL) {
            Py_FatalError("lucifer: out of memory");
        }
    }
    return t->base_entry;
}

/* Runs a frame in a new activation. */
static PyObject *
eval_frame(PyThreadState *ts, struct lu_frame *frame, int mode)
{
    /* Ranges first, then the opcodes that override them. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
    static void *const targets[256] = {
        [0 ... 255] = &&unknown_opcode,
        [MIN_INSTRUMENTED_OPCODE ... 255] = &&instrumented_opcode,
        [CACHE] = &&op_CACHE,
        [NOP] = &&op_NOP,
        [EXTENDED_ARG] = &&op_EXTENDED_ARG,
        [RESUME] = &&op_RESUME,
        [LOAD_CONST] = &&op_LOAD_CONST,
        [LOAD_SMALL_INT] = &&op_LOAD_SMALL_INT,
        [LOAD_NAME] = &&op_LOAD_NAME,
        [STORE_NAME] = &&op_STORE_NAME,
        [LOAD_GLOBAL] = &&op_LOAD_GLOBAL,
        [LOAD_GLOBAL_MODULE] = &&op_LOAD_GLOBAL_MODULE,
        [LOAD_GLOBAL_BUILTIN] = &&op_LOAD_GLOBAL_BUILTIN,
        [LOAD_FAST] = &&op_LOAD_FAST,
        [LOAD_FAST_BORROW] = &&op_LOAD_FAST,
        [LOAD_FAST_CHECK] = &&op_LOAD_FAST_CHECK,
        [LOAD_FAST_LOAD_FAST] = &&op_LOAD_FAST_LOAD_FAST,
        [LOAD_FAST_BORROW_LOAD_FAST_BORROW] = &&op_LOAD_FAST_LOAD_FAST,
        [STORE_FAST] = &&op_STORE_FAST,
        [STORE_FAST_LOAD_FAST] = &&op_STORE_FAST_LOAD_FAST,
        [STORE_FAST_STORE_FAST] = &&op_STORE_FAST_STORE_FAST,
        [PUSH_NULL] = &&op_PUSH_NULL,
        [POP_TOP] = &&op_POP_TOP,
        [CALL] = &&op_CALL,
        [CALL_KW] = &&op_CALL_KW,
        [CALL_FUNCTION_EX] = &&op_CALL_FUNCTION_EX,
        [MAKE_CELL] = &&op_MAKE_CELL,
        [COPY_FREE_VARS] = &&op_COPY_FREE_VARS,
        [LOAD_DEREF] = &&op_LOAD_DEREF,
        [STORE_DEREF] = &&op_STORE_DEREF,
        [DELETE_DEREF] = &&op_DELETE_DEREF,
        [LOAD_FROM_DICT_OR_DEREF] = &&op_LOAD_FROM_DICT_OR_DEREF,
        [DELETE_FAST] = &&op_DELETE_FAST,
        [LOAD_FAST_AND_CLEAR] = &&op_LOAD_FAST_AND_CLEAR,
        [STORE_GLOBAL] = &&op_STORE_GLOBAL,
        [DELETE_GLOBAL] = &&op_DELETE_GLOBAL,
        [DELETE_NAME] = &&op_DELETE_NAME,
        [COPY] = &&op_COPY,
        [SWAP] = &&op_SWAP,
        [NOT_TAKEN] = &&op_NOP,
        [TO_BOOL] = &&op_TO_BOOL,
        [UNARY_NOT] = &&op_UNARY_NOT,
        [IS_OP] = &&op_IS_OP,
        [POP_JUMP_IF_FALSE] = &&op_POP_JUMP_IF_FALSE,
        [POP_JUMP_IF_TRUE] = &&op_POP_JUMP_IF_TRUE,
        [POP_JUMP_IF_NONE] = &&op_POP_JUMP_IF_NONE,
        [POP_JUMP_IF_NOT_NONE] = &&op_POP_JUMP_IF_NOT_NONE,
        [JUMP_FORWARD] = &&op_JUMP_FORWARD,
        [JUMP_BACKWARD_NO_INTERRUPT] = &&op_JUMP_BACKWARD_NO_INTERRUPT,
        [BUILD_TUPLE] = &&op_BUILD_TUPLE,
        [BUILD_LIST] = &&op_BUILD_LIST,
        [LIST_APPEND] = &&op_LIST_APPEND,
        [LIST_EXTEND] = &&op_LIST_EXTEND,
        [DICT_MERGE] = &&op_DICT_MERGE,
        [DICT_UPDATE] = &&op_DICT_UPDATE,
        [CALL_INTRINSIC_1] = &&op_CALL_INTRINSIC_1,
        [RETURN_VALUE] = &&op_RETURN_VALUE,
        [MAKE_FUNCTION] = &&op_MAKE_FUNCTION,
        [SET_FUNCTION_ATTRIBUTE] = &&op_SET_FUNCTION_ATTRIBUTE,
        [BUILD_MAP] = &&op_BUILD_MAP,
        [BUILD_SET] = &&op_BUILD_SET,
        [SET_ADD] = &&op_SET_ADD,
        [SET_UPDATE] = &&op_SET_UPDATE,
        [MAP_ADD] = &&op_MAP_ADD,
        [IMPORT_NAME] = &&op_IMPORT_NAME,
        [IMPORT_FROM] = &&op_IMPORT_FROM,
        [JUMP_BACKWARD] = &&op_JUMP_BACKWARD,
        [LOAD_ATTR] = &&op_LOAD_ATTR,
        [LOAD_ATTR_MODULE] = &&op_LOAD_ATTR_MODULE,
        [LOAD_ATTR_CLASS] = &&op_LOAD_ATTR_CLASS,
        [LOAD_ATTR_CLASS_WITH_METACLASS_CHECK] = &&op_LOAD_ATTR_CLASS,
        [LOAD_ATTR_WITH_HINT] = &&op_LOAD_ATTR_WITH_HINT,
        [LOAD_ATTR_METHOD_NO_DICT] = &&op_LOAD_ATTR_METHOD,
        [LOAD_ATTR_METHOD_WITH_VALUES] = &&op_LOAD_ATTR_METHOD,
        [LOAD_ATTR_NONDESCRIPTOR_NO_DICT] = &&op_LOAD_ATTR_METHOD,
        [LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES] = &&op_LOAD_ATTR_METHOD,
        [LOAD_ATTR_SLOT] = &&op_LOAD_ATTR_SLOT,
        [STORE_ATTR] = &&op_STORE_ATTR,
        [STORE_ATTR_SLOT] = &&op_STORE_ATTR_FAST,
        [STORE_ATTR_WITH_HINT] = &&op_STORE_ATTR_FAST,
        [STORE_ATTR_INSTANCE_VALUE] = &&op_STORE_ATTR_FAST,
        [LOAD_ATTR_INSTANCE_VALUE] = &&op_LOAD_ATTR_SLOT,
        [DELETE_ATTR] = &&op_DELETE_ATTR,
        [LOAD_SUPER_ATTR] = &&op_LOAD_SUPER_ATTR,
        [LOAD_BUILD_CLASS] = &&op_LOAD_BUILD_CLASS,
        [LOAD_LOCALS] = &&op_LOAD_LOCALS,
        [LOAD_FROM_DICT_OR_GLOBALS] = &&op_LOAD_FROM_DICT_OR_GLOBALS,
        [SETUP_ANNOTATIONS] = &&op_SETUP_ANNOTATIONS,
        [GET_ITER] = &&op_GET_ITER,
        [FOR_ITER] = &&op_FOR_ITER,
        [END_FOR] = &&op_POP_TOP,
        [POP_ITER] = &&op_POP_TOP,
        [UNPACK_SEQUENCE] = &&op_UNPACK_SEQUENCE,
        [COMPARE_OP] = &&op_COMPARE_OP,
        [BINARY_OP] = &&op_BINARY_OP,
        [UNARY_NEGATIVE] = &&op_UNARY_NEGATIVE,
        [UNARY_INVERT] = &&op_UNARY_INVERT,
        [CONTAINS_OP] = &&op_CONTAINS_OP,
        [RAISE_VARARGS] = &&op_RAISE_VARARGS,
        [RERAISE] = &&op_RERAISE,
        [PUSH_EXC_INFO] = &&op_PUSH_EXC_INFO,
        [POP_EXCEPT] = &&op_POP_EXCEPT,
        [CHECK_EXC_MATCH] = &&op_CHECK_EXC_MATCH,
        [CHECK_EG_MATCH] = &&op_CHECK_EG_MATCH,
        [WITH_EXCEPT_START] = &&op_WITH_EXCEPT_START,
        [LOAD_SPECIAL] = &&op_LOAD_SPECIAL,
        [LOAD_COMMON_CONSTANT] = &&op_LOAD_COMMON_CONSTANT,
        [UNPACK_EX] = &&op_UNPACK_EX,
        [STORE_SUBSCR] = &&op_STORE_SUBSCR,
        [DELETE_SUBSCR] = &&op_DELETE_SUBSCR,
        [BUILD_SLICE] = &&op_BUILD_SLICE,
        [BINARY_SLICE] = &&op_BINARY_SLICE,
        [STORE_SLICE] = &&op_STORE_SLICE,
        [GET_LEN] = &&op_GET_LEN,
        [CALL_INTRINSIC_2] = &&op_CALL_INTRINSIC_2,
        [FORMAT_SIMPLE] = &&op_FORMAT_SIMPLE,
        [FORMAT_WITH_SPEC] = &&op_FORMAT_WITH_SPEC,
        [CONVERT_VALUE] = &&op_CONVERT_VALUE,
        [BUILD_STRING] = &&op_BUILD_STRING,
        [BUILD_TEMPLATE] = &&op_BUILD_TEMPLATE,
        [MATCH_CLASS] = &&op_MATCH_CLASS,
        [MATCH_KEYS] = &&op_MATCH_KEYS,
        [MATCH_MAPPING] = &&op_MATCH_MAPPING,
        [MATCH_SEQUENCE] = &&op_MATCH_SEQUENCE,
        [EXIT_INIT_CHECK] = &&op_EXIT_INIT_CHECK,
        [BUILD_INTERPOLATION] = &&op_BUILD_INTERPOLATION,
        [RETURN_GENERATOR] = &&op_RETURN_GENERATOR,
        [YIELD_VALUE] = &&op_YIELD_VALUE,
        [SEND] = &&op_SEND,
        [END_SEND] = &&op_END_SEND,
        [GET_YIELD_FROM_ITER] = &&op_GET_YIELD_FROM_ITER,
        [GET_AWAITABLE] = &&op_GET_AWAITABLE,
        [GET_AITER] = &&op_GET_AITER,
        [GET_ANEXT] = &&op_GET_ANEXT,
        [END_ASYNC_FOR] = &&op_END_ASYNC_FOR,
        [CLEANUP_THROW] = &&op_CLEANUP_THROW,
        [INSTRUMENTED_RESUME] = &&op_INSTRUMENTED_RESUME,
        [INSTRUMENTED_RETURN_VALUE] = &&op_INSTRUMENTED_RETURN_VALUE,
        [INSTRUMENTED_YIELD_VALUE] = &&op_INSTRUMENTED_YIELD_VALUE,
        [INSTRUMENTED_CALL] = &&op_INSTRUMENTED_CALL,
        [INSTRUMENTED_CALL_KW] = &&op_INSTRUMENTED_CALL_KW,
        [INSTRUMENTED_CALL_FUNCTION_EX] = &&op_INSTRUMENTED_CALL_FUNCTION_EX,
        [INSTRUMENTED_LOAD_SUPER_ATTR] = &&op_INSTRUMENTED_LOAD_SUPER_ATTR,
        [INSTRUMENTED_JUMP_FORWARD] = &&op_INSTRUMENTED_JUMP_FORWARD,
        [INSTRUMENTED_JUMP_BACKWARD] = &&op_INSTRUMENTED_JUMP_BACKWARD,
        [INSTRUMENTED_POP_JUMP_IF_FALSE] = &&op_INSTRUMENTED_POP_JUMP_IF_FALSE,
        [INSTRUMENTED_POP_JUMP_IF_TRUE] = &&op_INSTRUMENTED_POP_JUMP_IF_TRUE,
        [INSTRUMENTED_POP_JUMP_IF_NONE] = &&op_INSTRUMENTED_POP_JUMP_IF_NONE,
        [INSTRUMENTED_POP_JUMP_IF_NOT_NONE] = &&op_INSTRUMENTED_POP_JUMP_IF_NOT_NONE,
        [INSTRUMENTED_NOT_TAKEN] = &&op_INSTRUMENTED_NOT_TAKEN,
        [INSTRUMENTED_FOR_ITER] = &&op_FOR_ITER,
        [INSTRUMENTED_END_FOR] = &&op_INSTRUMENTED_END_FOR,
        [INSTRUMENTED_POP_ITER] = &&op_INSTRUMENTED_POP_ITER,
        [INSTRUMENTED_END_SEND] = &&op_INSTRUMENTED_END_SEND,
        [INSTRUMENTED_END_ASYNC_FOR] = &&op_INSTRUMENTED_END_ASYNC_FOR,
        [INSTRUMENTED_LINE] = &&op_INSTRUMENTED_LINE,
        [INSTRUMENTED_INSTRUCTION] = &&op_INSTRUMENTED_INSTRUCTION,
    };
#pragma GCC diagnostic pop
    /* An activation never changes carrier: an unpinned park returns from it. */
    struct lu_carrier *const carrier = lu_carrier;
    const uintptr_t *poll = &carrier->poll;
    struct lu_task *const task = (struct lu_task *)ts;
    /* An activation that finds no frame running is the task's outermost, the
       only one that can park: every other has C below it. Its C-entry marker
       is in the task, not on the C stack, so that it outlives the activation
       when the task resumes on another carrier. */
    const int outermost = mode == EVAL_RESUME || ts->current_frame == NULL;
    const int migratable = outermost && __atomic_load_n(&_PyRuntime.sched.active, __ATOMIC_RELAXED);
    struct lu_frame local_entry, *entry = &local_entry;
    if (mode >= EVAL_RESUME) {
        frame = (struct lu_frame *)ts->current_frame;
    }
    else {
        if (outermost) {
            entry = base_entry(task);
            task->home = carrier;
        }
        *entry = (struct lu_frame){ .owner = LU_FRAME_C_ENTRY };
        entry->previous = (struct lu_frame *)ts->current_frame;
        frame->previous = entry;
        ts->current_frame = (struct _PyInterpreterFrame *)frame;
    }

    PyCodeObject *code;
    PyObject **localsplus, **sp;
    lu_codeunit *next_instr;
    int opcode, oparg;
    Py_ssize_t lasti;           /* the raising instruction, in code units, for the unwinder */
    PyObject *result;
    PyObject *retval;           /* what a returning or yielding frame hands its caller */
    /* The call tail's operands: the callable and the owned argument values,
       positional first, then the keywords named by kwnames. */
    PyObject *callable, *kwnames, **args, **base, **to_free;
    Py_ssize_t total, npos;
    int free_self;              /* args[-1] may take a prepended self */
    lu_codeunit *monitored = NULL;      /* the INSTRUMENTED_CALL making the call being made */
    /* The start of the instruction being executed and of the one before it,
       for the events that name a source (LINE, NOT_TAKEN, POP_ITER). */
    lu_codeunit *cur_instr, *prev_instr;
    /* Where a RERAISE puts the frame for the tools: the instruction that first raised. */
    lu_codeunit *reraise_at = NULL;
    struct lu_frame *callee;
    /* A kernel call's operands: kernel[kid] on kargs[0..nk), owned, its
       value handled per kret. kret is also the value's fate for the callee
       push_callee enters. */
    PyObject *kargs[4];
    PyObject *exit_args[5];     /* WITH_EXCEPT_START's call: a free slot, self, 3 arguments */
    int kid, nk;
    uint8_t kret;

#define LOAD_FRAME() do { \
        code = frame->code; \
        localsplus = frame->localsplus; \
        sp = frame->stackpointer; \
        next_instr = frame->instr; \
        cur_instr = frame->last; \
    } while (0)
#define SAVE_FRAME() do { frame->instr = next_instr; frame->stackpointer = sp; frame->last = cur_instr; } while (0)
#define DISPATCH() do { \
        prev_instr = cur_instr; \
        cur_instr = next_instr; \
        opcode = next_instr->op.code; \
        oparg = next_instr->op.arg; \
        next_instr++; \
        goto *targets[opcode]; \
    } while (0)
#define SKIP_CACHES() (next_instr += lu_opcode_caches[opcode])
/* The start of the instruction `next_instr` follows, caches included. */
#define INSTR_START() lu_instr_before(code, next_instr)
/* The same for the instructions that resume a linked frame: they have one cache word. */
#define SEND_START() (next_instr - 1 - lu_opcode_caches[SEND])
#define PUSH(v) (*sp++ = (v))
#define POP() (*--sp)
#define TOP() (sp[-1])
#define GETLOCAL(i) (localsplus[i])
#define NAME(i) PyTuple_GET_ITEM(code->co_names, i)
#define ERROR_IF(cond) do { if (cond) goto error; } while (0)
/* A returned value, per the callee's enum lu_frame_return. */
#define PUSH_RETURNED(ret, v) do { \
        if ((ret) == LU_RETURN_DISCARD) { \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_PAIR) { \
            PUSH(Py_NewRef(PyTuple_GET_ITEM(v, 0))); \
            PUSH(Py_NewRef(PyTuple_GET_ITEM(v, 1))); \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_UNPACK) { \
            for (Py_ssize_t i_ = PyTuple_GET_SIZE(v); --i_ >= 0; ) { \
                PUSH(Py_NewRef(PyTuple_GET_ITEM(v, i_))); \
            } \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_SENT) { \
            PUSH(Py_NewRef(PyTuple_GET_ITEM(v, 0))); \
            if (PyTuple_GET_ITEM(v, 1) == Py_True) { \
                next_instr += instr_oparg(code, SEND_START()); \
            } \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_INIT) { \
            if ((v) != Py_None) { \
                lu_err_format(PyExc_TypeError, "__init__() should return None, not '%.200s'", \
                              Py_TYPE(v)->tp_name); \
                Py_DECREF(v); \
                goto error; \
            } \
            Py_DECREF(v); \
        } \
        else if ((ret) == LU_RETURN_NEW) { \
            PyObject *args_ = POP(), *cls_ = POP(); \
            SAVE_FRAME(); \
            PyObject *obj_ = init_new_instance(ts, (PyTypeObject *)cls_, (v), args_); \
            Py_DECREF(args_); \
            Py_DECREF(cls_); \
            ERROR_IF(obj_ == NULL); \
            PUSH(obj_); \
        } \
        else if ((ret) == LU_RETURN_FOR_ITER) { \
            if ((v) == LU_MISSING) { \
                next_instr += instr_oparg(code, SEND_START()) + 1; \
            } \
            else { \
                PUSH(v); \
                if (lu_instrumented_opcode_at(code, SEND_START()) == INSTRUMENTED_FOR_ITER) { \
                    lu_codeunit *this_instr = SEND_START(); \
                    INSTRUMENTED_JUMP(this_instr, next_instr, PY_MONITORING_EVENT_BRANCH_LEFT); \
                } \
            } \
        } \
        else { \
            PUSH(v); \
            if ((ret) == LU_RETURN_METHOD) { \
                PUSH(NULL); \
            } \
        } \
    } while (0)
/* A poll's exception belongs to the instruction that polled, whose end is
   `after`: a back-edge has already moved next_instr to its target, which
   would look the handler up at the wrong offset. A park resumes at next_instr. */
#define POLL_AFTER(after) do { \
        lu_qsbr_quiescent(carrier); \
        if (__atomic_load_n(poll, __ATOMIC_RELAXED) & LU_POLL_EVENTS_MASK) { \
            SAVE_FRAME(); \
            if (lu_poll_slow(ts) < 0) { \
                next_instr = (after); \
                goto error; \
            } \
        } \
        if (migratable && lu_sched_safepoint(task)) { \
            SAVE_FRAME(); \
            return LU_EVAL_PARKED; \
        } \
    } while (0)
#define POLL() POLL_AFTER(next_instr)

    LOAD_FRAME();
    if (mode == EVAL_THROW) {
        /* The throw skipped RESUME, which would have brought the code up to date. */
        cur_instr = next_instr;         /* the RESUME it skipped: no line event is hidden behind it */
        SAVE_FRAME();
        PyObject *thrown = PyErr_GetRaisedException();
        if (lu_instrument(code, ts->interp) < 0) {
            Py_DECREF(thrown);
            goto error;
        }
        PyErr_SetRaisedException(thrown);
        lu_monitor_throw(ts, frame, INSTR_START());
        goto error;
    }
    if (mode == EVAL_CONTINUE_ERROR) {
        goto error;
    }
    DISPATCH();

op_CACHE:
unknown_opcode:
    SAVE_FRAME();
    lu_err_format(PyExc_SystemError, "%s:%d: unknown opcode %d at unit %d",
                  PyUnicode_AsUTF8(code->co_filename), code->co_firstlineno, opcode,
                  (int)(next_instr - 1 - lu_code_start(code)));
    goto error;

instrumented_opcode:
    SAVE_FRAME();
    lu_err_format(PyExc_SystemError, "instrumented opcode %d without monitoring", opcode);
    goto error;

/* The jump an instrumented instruction makes, once the tools have seen it:
   `src` names the branch, `dest` is where it goes. A callback may have moved
   the frame (f_lineno), which decides where the loop goes instead. */
#define INSTRUMENTED_JUMP(src, dest, event) do { \
        lu_codeunit *dest_ = (dest); \
        if (ts->tracing) { \
            next_instr = dest_; \
        } \
        else { \
            SAVE_FRAME(); \
            lu_codeunit *to_ = lu_fire_jump(this_instr, ts, event, frame, src, dest_); \
            if (to_ == NULL) { \
                next_instr = dest_ + 1; \
                goto error; \
            } \
            next_instr = to_; \
            sp = frame->stackpointer; \
        } \
    } while (0)

op_EXTENDED_ARG:
    opcode = next_instr->op.code;
    oparg = oparg << 8 | next_instr->op.arg;
    next_instr++;
    goto *targets[opcode];

op_NOP:
    DISPATCH();

op_RESUME: {
    uintptr_t version = __atomic_load_n(poll, __ATOMIC_RELAXED) & ~LU_POLL_EVENTS_MASK;
    if (__atomic_load_n(&code->_co_instrumentation_version, __ATOMIC_ACQUIRE) != version) {
        /* Instrument the code, then run this instruction again: it may be INSTRUMENTED_RESUME now. */
        SAVE_FRAME();
        ERROR_IF(lu_instrument(code, ts->interp) < 0);
        next_instr--;
        DISPATCH();
    }
    if ((oparg & RESUME_OPARG_LOCATION_MASK) < RESUME_AFTER_YIELD_FROM) {
        POLL();
    }
    /* A call, or a generator going on after a yield. */
    if ((oparg == 0 || (code->co_flags & LU_CO_GENERATORS)) && lu_jit_on() && lu_jit_call_hot(code)) {
        goto jit_enter;
    }
    DISPATCH();
}

op_INSTRUMENTED_RESUME: {
    lu_codeunit *this_instr = next_instr - 1;
    uintptr_t version = __atomic_load_n(poll, __ATOMIC_RELAXED) & ~LU_POLL_EVENTS_MASK;
    SAVE_FRAME();
    if (__atomic_load_n(&code->_co_instrumentation_version, __ATOMIC_ACQUIRE) != version) {
        ERROR_IF(lu_instrument(code, ts->interp) < 0);
        next_instr--;
        DISPATCH();
    }
    /* The event first: a park at the poll resumes after this instruction. */
    ERROR_IF(lu_fire(ts, oparg > 0, frame, this_instr) < 0);
    if (frame->instr != next_instr) {
        next_instr = frame->instr;      /* the tool moved the frame */
        sp = frame->stackpointer;
        DISPATCH();
    }
    if ((oparg & RESUME_OPARG_LOCATION_MASK) < RESUME_AFTER_YIELD_FROM) {
        POLL();
    }
    DISPATCH();
}

op_INSTRUMENTED_LINE: {
    lu_codeunit *this_instr = next_instr - 1;
    int original;
    if (ts->tracing) {
        original = lu_line_original_opcode(code, this_instr);
    }
    else {
        SAVE_FRAME();
        original = lu_fire_line(ts, frame, this_instr, prev_instr);
        if (original < 0) {
            goto error;
        }
        if (frame->instr != next_instr) {
            next_instr = frame->instr;      /* the tool moved the frame */
            sp = frame->stackpointer;
            DISPATCH();
        }
    }
    opcode = original;
    goto *targets[opcode];
}

op_INSTRUMENTED_INSTRUCTION: {
    lu_codeunit *this_instr = next_instr - 1;
    SAVE_FRAME();
    int next_opcode = lu_fire_instruction(ts, frame, this_instr);
    if (next_opcode < 0) {
        goto error;
    }
    opcode = next_opcode;
    goto *targets[opcode];
}

op_LOAD_CONST:
    PUSH(Py_NewRef(PyTuple_GET_ITEM(code->co_consts, oparg)));
    DISPATCH();

op_LOAD_SMALL_INT:
    PUSH(lu_small_int(oparg));
    DISPATCH();

op_LOAD_NAME: {
    PyObject *name = NAME(oparg), *v;
    if (frame->locals == NULL) {
        SAVE_FRAME();
        lu_err_format(PyExc_SystemError, "no locals found");
        goto error;
    }
    v = locals_get(frame->locals, name);
    if (v == NULL && !PyErr_Occurred() && (v = global_get(frame, name)) == NULL && !PyErr_Occurred()) {
        name_error(name);
    }
    ERROR_IF(v == NULL);
    PUSH(v);
    DISPATCH();
}

op_STORE_NAME: {
    PyObject *name = NAME(oparg);
    PyObject *v = POP();
    SAVE_FRAME();
    int err;
    if (frame->locals == NULL) {
        lu_err_format(PyExc_SystemError, "no locals found when storing '%s'",
                      PyUnicode_AsUTF8(name));
        err = -1;
    }
    else {
        if (PyDict_CheckExact(frame->locals)) {
            PyObject *old = ns_get(frame->locals, name);
            err = old == NULL && PyErr_Occurred() ? -1 : lu_ns_store(frame->locals, name, v);
            Py_XDECREF(old);
        }
        else {
            PyObject *args[3] = { frame->locals, name, v };
            PyObject *r = lu_call_kernel(LU_K_setitem, args, 3);
            err = r == NULL ? -1 : 0;
            Py_XDECREF(r);
        }
    }
    Py_DECREF(v);
    ERROR_IF(err < 0);
    DISPATCH();
}

/* A specialized instruction whose guard fails runs the generic one, which
   counts the miss; a spent counter makes it try again. */
#define ADAPT(specialize_call) do { \
        lu_codeunit *counter_ = next_instr; \
        if (LU_COUNTER_SPENT(counter_->cache)) { \
            specialize_call; \
        } \
        else { \
            lu_cache_store(counter_, counter_->cache - LU_COUNTER_STEP); \
        } \
    } while (0)

op_LOAD_GLOBAL: {
    PyObject *name = NAME(oparg >> 1), *v;
    ADAPT(lu_specialize_load_global(frame->globals, frame->builtins, next_instr - 1, name));
    SKIP_CACHES();
    if ((v = global_get(frame, name)) == NULL && !PyErr_Occurred()) {
        name_error(name);
    }
    ERROR_IF(v == NULL);
    PUSH(v);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

/* The value at the cached entry of a keys table that still has the cached
   version, or NULL: a different table, or the entry is gone. */
op_LOAD_GLOBAL_MODULE:
op_LOAD_GLOBAL_BUILTIN: {
    lu_codeunit *site = next_instr - 1;
    uint16_t module_version = lu_cache_load(next_instr + LG_MODULE_KEYS_VERSION);
    uint16_t builtin_version = lu_cache_load(next_instr + LG_BUILTIN_KEYS_VERSION);
    Py_ssize_t index = lu_cache_load(next_instr + LG_INDEX);
    int from_builtins = opcode == LOAD_GLOBAL_BUILTIN;
    PyObject *v = NULL;
    if (module_version != 0 && PyDict_CheckExact(frame->globals)
        && (!from_builtins || (builtin_version != 0 && PyDict_CheckExact(frame->builtins)))) {
        PyDictKeysObject *gk = __atomic_load_n(&((PyDictObject *)frame->globals)->ma_keys, __ATOMIC_ACQUIRE);
        PyDictKeysObject *dk = from_builtins
            ? __atomic_load_n(&((PyDictObject *)frame->builtins)->ma_keys, __ATOMIC_ACQUIRE) : gk;
        if (gk->dk_version == module_version && dk->dk_version == (from_builtins ? builtin_version : module_version)
            && lu_dk_is_unicode(dk) && index < dk->dk_nentries) {
            v = lu_try_xget_ref(lu_dk_value_slot(dk, index));
            if (v != NULL && dk != __atomic_load_n(&((PyDictObject *)(from_builtins ? frame->builtins : frame->globals))->ma_keys,
                                                   __ATOMIC_ACQUIRE)) {
                Py_CLEAR(v);
            }
        }
    }
    if (v == NULL || !lu_site_unchanged(site, opcode)
        || lu_cache_load(next_instr + LG_MODULE_KEYS_VERSION) != module_version) {
        Py_XDECREF(v);
        goto op_LOAD_GLOBAL;
    }
    SKIP_CACHES();
    PUSH(v);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_LOAD_FAST:
    PUSH(Py_NewRef(GETLOCAL(oparg)));
    DISPATCH();

op_LOAD_FAST_CHECK: {
    PyObject *v = GETLOCAL(oparg);
    if (v == NULL) {
        SAVE_FRAME();
        format_unbound(code, oparg);
        goto error;
    }
    PUSH(Py_NewRef(v));
    DISPATCH();
}

op_LOAD_FAST_LOAD_FAST:
    PUSH(Py_NewRef(GETLOCAL(oparg >> 4)));
    PUSH(Py_NewRef(GETLOCAL(oparg & 15)));
    DISPATCH();

op_STORE_FAST:
    Py_XSETREF(GETLOCAL(oparg), POP());
    DISPATCH();

op_STORE_FAST_LOAD_FAST:
    Py_XSETREF(GETLOCAL(oparg >> 4), POP());
    PUSH(Py_NewRef(GETLOCAL(oparg & 15)));
    DISPATCH();

op_STORE_FAST_STORE_FAST:
    Py_XSETREF(GETLOCAL(oparg >> 4), POP());
    Py_XSETREF(GETLOCAL(oparg & 15), POP());
    DISPATCH();

op_PUSH_NULL:
    PUSH(NULL);
    DISPATCH();

op_POP_TOP:
    Py_XDECREF(POP());
    DISPATCH();

op_INSTRUMENTED_CALL_KW:
    monitored = next_instr - 1;
op_CALL_KW:
    /* As CALL's, below the keyword names: a keyword call has an argument. */
    lu_jit_observe(next_instr, sp[-1 - oparg], oparg >= 2 ? sp[-oparg] : sp[-1 - oparg]);
    SKIP_CACHES();
    kwnames = POP();
    goto call_stack_args;

op_INSTRUMENTED_CALL:
    monitored = next_instr - 1;
op_CALL:
    {
        /* The arguments' kinds; for a call without any, the callable's, so
           that the site shows it ran. */
        PyObject *first = oparg >= 1 ? sp[-oparg] : sp[-2];
        lu_jit_observe(next_instr, first, oparg >= 2 ? sp[-oparg + 1] : first);
    }
    SKIP_CACHES();
    kwnames = NULL;
call_stack_args:
    /* callable, self_or_null, args[oparg] */
    base = sp - oparg - 2;
    args = sp - oparg;
    callable = args[-2];
    total = oparg;
    if (args[-1] != NULL) {
        args--;
        total++;
    }
    npos = total - (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    free_self = args[-1] == NULL;
    to_free = NULL;

call_tail: {
    /* Consumes callable, args and kwnames; the stack is reset to base. A
       bound method, a kernel function and an object whose __call__ is
       Python run in this loop; everything else is foreign C. An
       INSTRUMENTED_CALL tells the tools of the call, and of the return of
       anything that is not a Python function: kernel functions and the type
       fast path then run as C does, under lu_call. */
    lu_codeunit *const mon = monitored;
    monitored = NULL;
    SAVE_FRAME();
    if (Py_IS_TYPE(callable, &PyMethod_Type) && free_self) {
        PyMethodObject *m = (PyMethodObject *)callable;
        *--args = Py_NewRef(m->im_self);
        npos++;
        total++;
        free_self = 0;
        PyObject *func = Py_NewRef(m->im_func);
        Py_SETREF(callable, func);
    }
    PyObject *arg0 = total > 0 ? args[0] : &lu_instrumentation_missing;
    if (mon) {
        if (lu_fire_2args(ts, PY_MONITORING_EVENT_CALL, frame, mon, callable, arg0) < 0) {
            result = NULL;
            goto call_done;
        }
        SAVE_FRAME();
    }
    if (!mon && callable == (PyObject *)&PyType_Type && total == 1 && npos == 1) {
        result = Py_NewRef((PyObject *)Py_TYPE(args[0]));       /* type(x) */
        goto call_done;
    }
    if (callable == (PyObject *)&PyBool_Type && total == 1 && npos == 1) {
        int truth = lu_truth_exact(args[0]);
        if (truth >= 0) {
            result = Py_NewRef(truth ? Py_True : Py_False);
            goto call_done;
        }
    }
    if (!mon && kwnames == NULL && total <= 1 && (callable == (PyObject *)&PyList_Type || callable == (PyObject *)&PyTuple_Type)
        && (result = seq_copy_native(callable == (PyObject *)&PyTuple_Type, total == 1 ? args[0] : NULL)) != NULL) {
        goto call_done;
    }
    PyObject *target = NULL, *self = NULL;
    PyObject *init = NULL, *held = NULL;      /* references that keep target alive */
    int plan = free_self && !mon && Py_IS_TYPE(callable, &PyType_Type) && callable != (PyObject *)&PyType_Type
        && ((PyTypeObject *)callable)->tp_vectorcall == NULL
        ? instance_plan((PyTypeObject *)callable, total, &init) : 0;
    if (plan == 3 && kwnames != NULL) {
        Py_CLEAR(init);         /* the kernel runs __new__ with keywords */
    }
    if (plan == 3 && kwnames == NULL) {
        /* The class's __new__ (in init) run in this loop, the class and the
           arguments kept below it for the __init__ of what it returns. */
        PyObject *kept = PyTuple_New(npos);
        if (kept == NULL) {
            Py_DECREF(init);
            result = NULL;
            goto call_done;
        }
        for (Py_ssize_t i = 0; i < npos; i++) {
            PyTuple_SET_ITEM(kept, i, Py_NewRef(args[i]));
        }
        *--args = Py_NewRef(callable);
        callee = lu_frame_call(ts, (PyFunctionObject *)init, args, npos + 1, NULL);
        Py_DECREF(init);
        PyMem_Free(to_free);
        sp = base;
        if (callee == NULL) {
            Py_DECREF(kept);
            Py_DECREF(callable);
            frame->stackpointer = sp;
            goto error;
        }
        PUSH(callable);
        PUSH(kept);
        frame->stackpointer = sp;
        callee->ret = kret = LU_RETURN_NEW;
        goto push_callee;
    }
    if (plan == 1 || plan == 2) {
        /* A class instance: allocated here, its __init__ run in this loop. */
        PyObject *obj = lu_alloc_instance((PyTypeObject *)callable, &PyBaseObject_Type);
        if (obj == NULL || plan == 2) {
            Py_XDECREF(init);
            result = obj;
            goto call_done;
        }
        *--args = Py_NewRef(obj);
        callee = lu_frame_call(ts, (PyFunctionObject *)init, args, npos + 1, kwnames);
        Py_DECREF(init);
        Py_XDECREF(kwnames);
        Py_DECREF(callable);
        PyMem_Free(to_free);
        sp = base;
        if (callee == NULL) {
            Py_DECREF(obj);
            frame->stackpointer = sp;
            goto error;
        }
        PUSH(obj);
        frame->stackpointer = sp;
        callee->ret = kret = LU_RETURN_INIT;
        goto push_callee;
    }
    if (Py_IS_TYPE(callable, &PyFunction_Type)) {
        target = callable;
    }
    else if (Py_IS_TYPE(callable, &PyCFunction_Type)
             && (((PyCFunctionObject *)callable)->m_ml->ml_flags & LU_METH_FRAME)) {
        if (kwnames != NULL) {
            lu_err_format(PyExc_TypeError, "%s() takes no keyword arguments",
                          ((PyCFunctionObject *)callable)->m_ml->ml_name);
            result = NULL;
            goto call_done;
        }
        struct lu_frame *(*prim)(PyThreadState *, PyObject *const *, Py_ssize_t) =
            (struct lu_frame *(*)(PyThreadState *, PyObject *const *, Py_ssize_t))(void (*)(void))
            ((PyCFunctionObject *)callable)->m_ml->ml_meth;
        callee = prim(ts, args, npos);
        for (Py_ssize_t i = 0; i < total; i++) {
            Py_DECREF(args[i]);
        }
        Py_DECREF(callable);
        PyMem_Free(to_free);
        sp = base;
        frame->stackpointer = sp;
        ERROR_IF(callee == NULL);
        if (callee->owner == LU_FRAME_GENERATOR) {
            goto resume_linked;
        }
        kret = LU_RETURN_PUSH;
        goto push_callee;
    }
    else {
        int found = lu_kernel_target(callable, args, npos, &target, &self);
        if (found < 0) {
            result = NULL;
            goto call_done;
        }
        if (!found && Py_TYPE(callable)->tp_call == lu_slot_tp_call
            && PyVectorcall_Function(callable) == NULL) {
            /* An instance whose type's __call__ is a function or kernel descriptor. */
            PyObject *descr = _PyType_LookupRef(Py_TYPE(callable), LU_ID(__call__));
            PyObject *unused;
            if (descr != NULL && (PyFunction_Check(descr)
                                  || (lu_kernel_target(descr, &callable, 1, &target, &unused) > 0
                                      && unused == NULL))) {
                target = PyFunction_Check(descr) ? descr : target;
                self = callable;
                held = descr;
            }
            else {
                target = NULL;
                Py_XDECREF(descr);
            }
        }
    }
    if (mon && target != NULL && (((PyFunctionObject *)target)->func_code != NULL)
        && (((PyCodeObject *)((PyFunctionObject *)target)->func_code)->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN))) {
        target = NULL;          /* a builtin: the tools see it return */
        Py_CLEAR(held);
    }
    if (target != NULL && self == NULL && kwnames == NULL
        && (((PyCodeObject *)((PyFunctionObject *)target)->func_code)->co_flags & LU_CO_KERNEL)
        && ((result = lu_native_call(ts, target, args, npos)) != NULL || PyErr_Occurred())) {
        Py_XDECREF(held);
        goto call_done;
    }
    if (target != NULL && (self == NULL || free_self)) {
        if (self != NULL) {
            *--args = Py_NewRef(self);
            npos++;
        }
        callee = lu_frame_call(ts, (PyFunctionObject *)target, args, npos, kwnames);
        Py_XDECREF(held);
        Py_XDECREF(kwnames);
        Py_DECREF(callable);
        PyMem_Free(to_free);
        sp = base;
        frame->stackpointer = sp;
        ERROR_IF(callee == NULL);
        kret = LU_RETURN_PUSH;
        goto push_callee;
    }
    /* Foreign C pins the task; kernel primitives will be exempted when
       migration is on (DESIGN 5.4). */
    Py_XDECREF(held);
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    result = lu_call(callable, args, npos, kwnames);
    t->pin--;
    if (mon) {
        if (result == NULL) {
            lu_fire_exc2(ts, PY_MONITORING_EVENT_C_RAISE, frame, mon, callable, arg0);
        }
        else if (lu_fire_2args(ts, PY_MONITORING_EVENT_C_RETURN, frame, mon, callable, arg0) < 0) {
            Py_CLEAR(result);
        }
    }
call_done:
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_DECREF(args[i]);
    }
    Py_XDECREF(kwnames);
    Py_DECREF(callable);
    PyMem_Free(to_free);
    sp = base;
    ERROR_IF(result == NULL);
    PUSH(result);
    POLL();
    DISPATCH();
}

kernel_call: {
    /* kernel[kid](*kargs[:nk]) in this loop. */
    SAVE_FRAME();
    PyObject *kf = ts->interp->kernel[kid];
    if (kf == NULL) {
        for (int i = 0; i < nk; i++) {
            Py_DECREF(kargs[i]);
        }
        lu_err_format(PyExc_SystemError, "kernel function %d is not registered yet", kid);
        goto error;
    }
    callee = lu_frame_call(ts, (PyFunctionObject *)kf, kargs, nk, NULL);
    ERROR_IF(callee == NULL);
    callee->ret = kret;
    goto push_callee;
}

push_callee:
    /* Enters callee, pushed and bound; frame->stackpointer is saved. */
    if (!lu_enter_frame(ts, callee, 0, &result)) {
        ERROR_IF(result == NULL);
        PUSH_RETURNED(kret, result);
        POLL();
        DISPATCH();
    }
    callee->previous = frame;
    ts->current_frame = (struct _PyInterpreterFrame *)callee;
    frame = callee;
    LOAD_FRAME();
    DISPATCH();

resume_linked:
    /* A generator's frame, linked above this one by its resumption; a throw
       enters it by raising. A PEP 523 hook evaluates resumes as frame
       entries too (_PyEval_EvalFrame), and its result continues as the
       yield or the return would. */
    if (ts->interp->eval_frame != NULL && callee->owner != LU_FRAME_KERNEL_HIDDEN) {
        PyObject *hook_result;
        if (!lu_enter_frame(ts, callee, ts->current_exception != NULL, &hook_result)) {
            ERROR_IF(hook_result == NULL);
            uint8_t r = callee->ret;
            if (lu_gen_of(callee)->frame_state >= LU_GEN_COMPLETED) {
                if (r == LU_RETURN_FOR_ITER || r == LU_RETURN_NEXT) {
                    Py_DECREF(hook_result);
                    hook_result = LU_MISSING;
                }
                else if (r == LU_RETURN_SEND) {
                    next_instr += instr_oparg(code, SEND_START());
                }
            }
            PUSH_RETURNED(r, hook_result);
            DISPATCH();
        }
    }
    ts->current_frame = (struct _PyInterpreterFrame *)callee;
    frame = callee;
    LOAD_FRAME();
    if (ts->current_exception != NULL) {
        /* A throw skips the RESUME that brings the code up to date. */
        cur_instr = next_instr;         /* the RESUME it skipped: no line event is hidden behind it */
        SAVE_FRAME();
        PyObject *thrown = PyErr_GetRaisedException();
        int err = lu_instrument(code, ts->interp);
        if (err == 0) {
            PyErr_SetRaisedException(thrown);
            lu_monitor_throw(ts, frame, INSTR_START());
        }
        else {
            Py_DECREF(thrown);
        }
        goto error;
    }
    DISPATCH();

op_INSTRUMENTED_CALL_FUNCTION_EX:
    monitored = next_instr - 1;
op_CALL_FUNCTION_EX: {
    /* func, null, callargs, kwargs or NULL */
    PyObject *callargs = sp[-2];
    SAVE_FRAME();
    if (!PyTuple_CheckExact(callargs)) {
        PyObject *t = lu_args_as_tuple(sp[-4], callargs);
        ERROR_IF(t == NULL);
        Py_SETREF(sp[-2], t);
        callargs = t;
    }
    args = lu_unpack_call(callargs, sp[-1], &npos, &kwnames);
    ERROR_IF(args == NULL);
    total = npos + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
    callable = sp[-4];
    Py_XDECREF(sp[-1]);
    Py_DECREF(callargs);
    base = sp = sp - 4;
    to_free = args - 1;
    free_self = 1;
    goto call_tail;
}

op_INSTRUMENTED_RETURN_VALUE: {
    lu_codeunit *this_instr = next_instr - 1;
    SAVE_FRAME();
    ERROR_IF(lu_fire_arg(ts, PY_MONITORING_EVENT_PY_RETURN, frame, this_instr, TOP()) < 0);
}
op_RETURN_VALUE:
    retval = POP();
    frame->stackpointer = sp;
return_frame: {
    struct lu_frame *caller = frame->previous;
    uint8_t ret = frame->ret;
    int from_builtin = (code->co_flags & LU_CO_KERNEL) != 0;
    if (frame->owner == LU_FRAME_GENERATOR) {
        lu_gen_finish(ts, frame);
        if (ret == LU_RETURN_FOR_ITER || ret == LU_RETURN_NEXT) {
            Py_DECREF(retval);
            retval = LU_MISSING;
        }
    }
    else {
        lu_frame_pop(ts, frame);
    }
    if (caller->owner == LU_FRAME_C_ENTRY) {
        ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
        return retval;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)caller;
    frame = caller;
    LOAD_FRAME();
    if (ret == LU_RETURN_SEND) {
        next_instr += instr_oparg(code, SEND_START());        /* out of the yield-from loop */
    }
    PUSH_RETURNED(ret, retval);
    if (from_builtin && !(code->co_flags & LU_CO_KERNEL)) {
        POLL();                 /* after a builtin's call, as CPython's CALL polls */
    }
    DISPATCH();
}

op_RETURN_GENERATOR:
    /* The function's frame moves into a new generator, which it returns. */
    SAVE_FRAME();
    retval = lu_gen_new(ts, frame);
    ERROR_IF(retval == NULL);
    goto return_frame;

op_INSTRUMENTED_YIELD_VALUE: {
    lu_codeunit *this_instr = next_instr - 1;
    SAVE_FRAME();
    ERROR_IF(lu_fire_arg(ts, PY_MONITORING_EVENT_PY_YIELD, frame, this_instr, TOP()) < 0);
    if (frame->instr != next_instr) {
        next_instr = frame->instr;      /* the tool moved the frame */
        sp = frame->stackpointer;
        DISPATCH();
    }
}
op_YIELD_VALUE:
    retval = POP();
    SAVE_FRAME();
yield_frame: {
    /* The frame is saved at its resume point; YIELD_VALUE's oparg. */
    struct lu_frame *caller = frame->previous;
    uint8_t ret = frame->ret;
    lu_gen_suspend(ts, frame, oparg);
    if (caller->owner == LU_FRAME_C_ENTRY) {
        ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
        return retval;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)caller;
    frame = caller;
    LOAD_FRAME();
    PUSH_RETURNED(ret, retval);
    DISPATCH();
}

op_SEND: {
    /* receiver, v -- receiver, retval; when the receiver returns, jumps by
       oparg past the yield-from loop. Generators and coroutines run in
       this loop; other receivers are the kernel's. */
    SKIP_CACHES();
    PyObject *receiver = sp[-2];
    if ((Py_IS_TYPE(receiver, &PyGen_Type) || Py_IS_TYPE(receiver, &PyCoro_Type))
        && ts->interp->eval_frame == NULL) {
        PyObject *v = POP();
        SAVE_FRAME();
        callee = lu_gen_resume(ts, receiver, v);
        if (callee == NULL) {
            ERROR_IF(PyErr_Occurred());
            PUSH(Py_NewRef(Py_None));
            next_instr += oparg;
            DISPATCH();
        }
        callee->ret = LU_RETURN_SEND;
        goto resume_linked;
    }
    kargs[1] = POP();
    kargs[0] = Py_NewRef(receiver);
    nk = 2;
    kid = LU_K_send;
    kret = LU_RETURN_SENT;
    goto kernel_call;
}

op_INSTRUMENTED_END_SEND: {
    lu_codeunit *this_instr = next_instr - 1;
    PyObject *receiver = sp[-2];
    if (Py_IS_TYPE(receiver, &PyGen_Type) || Py_IS_TYPE(receiver, &PyCoro_Type)) {
        SAVE_FRAME();
        ERROR_IF(lu_monitor_stop_iteration(ts, frame, this_instr, TOP()) < 0);
    }
}
op_END_SEND: {
    /* receiver, value -- value */
    PyObject *v = POP();
    Py_DECREF(TOP());
    TOP() = v;
    DISPATCH();
}

op_GET_YIELD_FROM_ITER: {
    PyObject *v = TOP();
    if (Py_IS_TYPE(v, &PyCoro_Type)) {
        if (!(code->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE))) {
            SAVE_FRAME();
            lu_err_format(PyExc_TypeError, "cannot 'yield from' a coroutine object "
                          "in a non-coroutine generator");
            goto error;
        }
        DISPATCH();
    }
    goto op_GET_ITER;
}

op_GET_AWAITABLE: {
    PyObject *v = TOP();
    if (Py_IS_TYPE(v, &PyCoro_Type)) {
        if (((struct lu_gen *)v)->frame_state == LU_GEN_SUSPENDED_YIELD_FROM) {
            SAVE_FRAME();
            lu_err_format(PyExc_RuntimeError, "coroutine is being awaited already");
            goto error;
        }
        DISPATCH();
    }
    kargs[0] = POP();
    kargs[1] = lu_small_int(oparg);
    nk = 2;
    kid = LU_K_get_awaitable;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_GET_AITER:
    kargs[0] = POP();
    nk = 1;
    kid = LU_K_get_aiter;
    kret = LU_RETURN_PUSH;
    goto kernel_call;

op_GET_ANEXT:
    /* aiter -- aiter, awaitable */
    kargs[0] = Py_NewRef(TOP());
    nk = 1;
    kid = LU_K_get_anext;
    kret = LU_RETURN_PUSH;
    goto kernel_call;

op_INSTRUMENTED_END_ASYNC_FOR: {
    lu_codeunit *this_instr = next_instr - 1;
    INSTRUMENTED_JUMP(next_instr - oparg, this_instr + 1, PY_MONITORING_EVENT_BRANCH_RIGHT);
}
op_END_ASYNC_FOR:
    /* awaitable, exc -- ; StopAsyncIteration ends the loop, anything else
       propagates */
    if (PyErr_GivenExceptionMatches(TOP(), PyExc_StopAsyncIteration)) {
        Py_DECREF(POP());
        Py_DECREF(POP());
        DISPATCH();
    }
    SAVE_FRAME();
    PyErr_SetRaisedException(POP());
    lasti = INSTR_START() - lu_code_start(code);
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RERAISE)) {
        lu_monitor_reraise(ts, frame, INSTR_START());
    }
    goto exception_unwind;

op_CLEANUP_THROW: {
    /* sub_iter, last_sent_val, exc -- None, value: a throw or close that
       ended the delegate with StopIteration gives `yield from` its value */
    PyObject *exc = TOP();
    if (PyErr_GivenExceptionMatches(exc, PyExc_StopIteration)) {
        PyObject *value = ((PyStopIterationObject *)exc)->value;
        value = Py_NewRef(value != NULL ? value : Py_None);
        for (int i = 0; i < 3; i++) {
            Py_DECREF(POP());
        }
        PUSH(Py_NewRef(Py_None));
        PUSH(value);
        DISPATCH();
    }
    SAVE_FRAME();
    PyErr_SetRaisedException(POP());
    lasti = INSTR_START() - lu_code_start(code);
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RERAISE)) {
        lu_monitor_reraise(ts, frame, INSTR_START());
    }
    goto exception_unwind;
}

op_MAKE_FUNCTION: {
    PyObject *codeobj = POP();
    SAVE_FRAME();
    PyObject *func = lu_function_new(codeobj, frame->globals);
    Py_DECREF(codeobj);
    ERROR_IF(func == NULL);
    lu_function_set_version((PyFunctionObject *)func);
    PUSH(func);
    DISPATCH();
}

op_SET_FUNCTION_ATTRIBUTE: {
    PyObject *func = POP();
    PyObject *attr = POP();
    lu_function_set_attribute((PyFunctionObject *)func, oparg, attr);
    PUSH(func);
    DISPATCH();
}

/* The oparg items below the stack top as one tuple, for a kernel builder. */
build_items: {
    PyObject **values = sp - oparg;
    PyObject *items = PyTuple_New(oparg);
    if (items == NULL) {
        for (int i = 0; i < oparg; i++) {
            Py_DECREF(values[i]);
        }
        sp = values;
        goto error;
    }
    for (int i = 0; i < oparg; i++) {
        PyTuple_SET_ITEM(items, i, values[i]);
    }
    sp = values;
    kargs[0] = items;
    nk = 1;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_BUILD_SET:
    SAVE_FRAME();
    kid = LU_K_build_set;
    goto build_items;

op_SET_ADD: {
    PyObject *v = POP();
    SAVE_FRAME();
    kargs[0] = Py_NewRef(sp[-oparg]);
    kargs[1] = v;
    nk = 2;
    kid = LU_K_set_add;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_SET_UPDATE: {
    PyObject *iterable = POP();
    SAVE_FRAME();
    kargs[0] = Py_NewRef(sp[-oparg]);
    kargs[1] = iterable;
    nk = 2;
    kid = LU_K_set_update;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_MAP_ADD: {
    PyObject *value = POP();
    PyObject *key = POP();
    PyObject *map = sp[-oparg];
    SAVE_FRAME();
    if (PyDict_CheckExact(map) && PyUnicode_CheckExact(key)) {
        int err = lu_ns_insert(map, key, value);
        Py_DECREF(key);
        Py_DECREF(value);
        ERROR_IF(err < 0);
        DISPATCH();
    }
    kargs[0] = Py_NewRef(map);
    kargs[1] = key;
    kargs[2] = value;
    nk = 3;
    kid = LU_K_setitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_BUILD_MAP: {
    SAVE_FRAME();
    PyObject **values = sp - 2 * oparg;
    for (int i = 0; i < oparg; i++) {
        if (!PyUnicode_CheckExact(values[2 * i])) {
            oparg *= 2;
            kid = LU_K_build_map;
            goto build_items;
        }
    }
    PyObject *map = PyDict_New();
    for (int i = 0; map != NULL && i < oparg; i++) {
        if (lu_ns_insert(map, values[2 * i], values[2 * i + 1]) < 0) {
            Py_CLEAR(map);
        }
    }
    for (int i = 0; i < 2 * oparg; i++) {
        Py_DECREF(values[i]);
    }
    sp = values;
    ERROR_IF(map == NULL);
    PUSH(map);
    DISPATCH();
}

op_IMPORT_NAME: {
    PyObject *fromlist = POP();
    PyObject *level = POP();
    SAVE_FRAME();
    PyObject *res = import_name(ts, frame, NAME(oparg), fromlist, level);
    Py_DECREF(fromlist);
    Py_DECREF(level);
    ERROR_IF(res == NULL);
    PUSH(res);
    DISPATCH();
}

op_IMPORT_FROM: {
    SAVE_FRAME();
    PyObject *res = import_from(TOP(), NAME(oparg));
    ERROR_IF(res == NULL);
    PUSH(res);
    DISPATCH();
}

op_INSTRUMENTED_END_FOR: {
    lu_codeunit *this_instr = next_instr - 1;
    PyObject *receiver = sp[-2];
    if (Py_IS_TYPE(receiver, &PyGen_Type) || Py_IS_TYPE(receiver, &PyCoro_Type)) {
        SAVE_FRAME();
        ERROR_IF(lu_monitor_stop_iteration(ts, frame, this_instr, TOP()) < 0);
    }
    Py_XDECREF(POP());
    DISPATCH();
}

op_INSTRUMENTED_POP_ITER: {
    lu_codeunit *this_instr = next_instr - 1;
    INSTRUMENTED_JUMP(prev_instr, this_instr + 1, PY_MONITORING_EVENT_BRANCH_RIGHT);
    Py_XDECREF(POP());
    DISPATCH();
}

op_INSTRUMENTED_NOT_TAKEN: {
    lu_codeunit *this_instr = next_instr - 1;
    INSTRUMENTED_JUMP(prev_instr, next_instr, PY_MONITORING_EVENT_BRANCH_LEFT);
    DISPATCH();
}

op_INSTRUMENTED_JUMP_FORWARD: {
    lu_codeunit *this_instr = next_instr - 1;
    INSTRUMENTED_JUMP(this_instr, next_instr + oparg, PY_MONITORING_EVENT_JUMP);
    DISPATCH();
}

op_INSTRUMENTED_JUMP_BACKWARD: {
    lu_codeunit *this_instr = next_instr - 1;
    SKIP_CACHES();
    lu_codeunit *after = next_instr;
    INSTRUMENTED_JUMP(this_instr, next_instr - oparg, PY_MONITORING_EVENT_JUMP);
    POLL_AFTER(after);
    DISPATCH();
}

op_INSTRUMENTED_POP_JUMP_IF_FALSE: {
    lu_codeunit *this_instr = next_instr - 1;
    SKIP_CACHES();
    if (POP() == Py_False) {
        INSTRUMENTED_JUMP(this_instr, next_instr + oparg, PY_MONITORING_EVENT_BRANCH_RIGHT);
    }
    DISPATCH();
}

op_INSTRUMENTED_POP_JUMP_IF_TRUE: {
    lu_codeunit *this_instr = next_instr - 1;
    SKIP_CACHES();
    if (POP() == Py_True) {
        INSTRUMENTED_JUMP(this_instr, next_instr + oparg, PY_MONITORING_EVENT_BRANCH_RIGHT);
    }
    DISPATCH();
}

op_INSTRUMENTED_POP_JUMP_IF_NONE: {
    lu_codeunit *this_instr = next_instr - 1;
    SKIP_CACHES();
    PyObject *v = POP();
    int jump = v == Py_None;
    Py_DECREF(v);
    if (jump) {
        INSTRUMENTED_JUMP(this_instr, next_instr + oparg, PY_MONITORING_EVENT_BRANCH_RIGHT);
    }
    DISPATCH();
}

op_INSTRUMENTED_POP_JUMP_IF_NOT_NONE: {
    lu_codeunit *this_instr = next_instr - 1;
    SKIP_CACHES();
    PyObject *v = POP();
    int jump = v != Py_None;
    Py_DECREF(v);
    if (jump) {
        INSTRUMENTED_JUMP(this_instr, next_instr + oparg, PY_MONITORING_EVENT_BRANCH_RIGHT);
    }
    DISPATCH();
}

op_JUMP_BACKWARD_NO_INTERRUPT:
    next_instr -= oparg;
    DISPATCH();

op_JUMP_FORWARD:
    next_instr += oparg;
    DISPATCH();

op_POP_JUMP_IF_FALSE: {
    int jump = POP() == Py_False;
    lu_jit_observe_branch(next_instr, jump);
    SKIP_CACHES();
    next_instr += oparg * jump;
    DISPATCH();
}

op_POP_JUMP_IF_TRUE: {
    int jump = POP() == Py_True;
    lu_jit_observe_branch(next_instr, jump);
    SKIP_CACHES();
    next_instr += oparg * jump;
    DISPATCH();
}

op_POP_JUMP_IF_NONE: {
    PyObject *v = POP();
    int jump = v == Py_None;
    Py_DECREF(v);
    lu_jit_observe_branch(next_instr, jump);
    SKIP_CACHES();
    next_instr += oparg * jump;
    DISPATCH();
}

op_POP_JUMP_IF_NOT_NONE: {
    PyObject *v = POP();
    int jump = v != Py_None;
    Py_DECREF(v);
    lu_jit_observe_branch(next_instr, jump);
    SKIP_CACHES();
    next_instr += oparg * jump;
    DISPATCH();
}

op_TO_BOOL: {
    SKIP_CACHES();
    PyObject *v = TOP();
    int truth = lu_truth_exact(v);
    if (truth < 0) {
        kargs[0] = POP();
        nk = 1;
        kid = LU_K_truth;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    Py_DECREF(v);
    TOP() = truth ? Py_True : Py_False;
    DISPATCH();
}

op_UNARY_NOT:
    TOP() = TOP() == Py_True ? Py_False : Py_True;
    DISPATCH();

op_IS_OP: {
    PyObject *right = POP();
    PyObject *left = TOP();
    TOP() = (left == right) != oparg ? Py_True : Py_False;
    Py_DECREF(left);
    Py_DECREF(right);
    DISPATCH();
}

op_COPY: {
    PyObject *v = Py_NewRef(sp[-oparg]);
    PUSH(v);
    DISPATCH();
}

op_SWAP: {
    PyObject *top = TOP();
    TOP() = sp[-oparg];
    sp[-oparg] = top;
    DISPATCH();
}

op_MAKE_CELL: {
    PyObject *cell = PyCell_New(GETLOCAL(oparg));
    Py_XDECREF(GETLOCAL(oparg));
    GETLOCAL(oparg) = cell;
    ERROR_IF(cell == NULL);
    DISPATCH();
}

op_COPY_FREE_VARS: {
    PyObject **closure = &PyTuple_GET_ITEM(((PyFunctionObject *)frame->func)->func_closure, 0);
    PyObject **free = localsplus + code->co_nlocalsplus - oparg;
    for (int i = 0; i < oparg; i++) {
        free[i] = Py_NewRef(closure[i]);
    }
    DISPATCH();
}

op_LOAD_DEREF: {
    SAVE_FRAME();
    PyObject *v = lu_cell_get(GETLOCAL(oparg));
    if (v == NULL) {
        format_unbound(code, oparg);
        goto error;
    }
    PUSH(v);
    DISPATCH();
}

op_STORE_DEREF: {
    PyObject *v = POP();
    SAVE_FRAME();
    Py_XDECREF(lu_cell_exchange(GETLOCAL(oparg), v));
    DISPATCH();
}

op_DELETE_DEREF: {
    SAVE_FRAME();
    PyObject *old = lu_cell_exchange(GETLOCAL(oparg), NULL);
    if (old == NULL) {
        format_unbound(code, oparg);
        goto error;
    }
    Py_DECREF(old);
    DISPATCH();
}

op_LOAD_FROM_DICT_OR_DEREF: {
    PyObject *mod_or_class_dict = POP();
    SAVE_FRAME();
    PyObject *name = PyTuple_GET_ITEM(code->co_localsplusnames, oparg);
    PyObject *v = locals_get(mod_or_class_dict, name);
    int failed = v == NULL && PyErr_Occurred();
    if (v == NULL && !failed) {
        v = lu_cell_get(GETLOCAL(oparg));
        if (v == NULL) {
            format_unbound(code, oparg);
        }
    }
    Py_DECREF(mod_or_class_dict);
    ERROR_IF(v == NULL);
    PUSH(v);
    DISPATCH();
}

op_DELETE_FAST:
    if (GETLOCAL(oparg) == NULL) {
        SAVE_FRAME();
        format_unbound(code, oparg);
        goto error;
    }
    Py_CLEAR(GETLOCAL(oparg));
    DISPATCH();

op_LOAD_FAST_AND_CLEAR:
    PUSH(GETLOCAL(oparg));
    GETLOCAL(oparg) = NULL;
    DISPATCH();

op_STORE_GLOBAL: {
    PyObject *v = POP();
    SAVE_FRAME();
    int err = lu_ns_store(frame->globals, NAME(oparg), v);
    Py_DECREF(v);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_DELETE_GLOBAL:
    SAVE_FRAME();
    if (!lu_ns_delete(frame->globals, NAME(oparg))) {
        name_error(NAME(oparg));
        goto error;
    }
    DISPATCH();

op_DELETE_NAME:
    SAVE_FRAME();
    if (frame->locals == NULL) {
        lu_err_format(PyExc_SystemError, "no locals when deleting '%s'",
                      PyUnicode_AsUTF8(NAME(oparg)));
        goto error;
    }
    if (!PyDict_CheckExact(frame->locals)) {
        PyObject *args[2] = { frame->locals, NAME(oparg) };
        PyObject *r = lu_call_kernel(LU_K_delitem, args, 2);
        if (r == NULL && PyErr_ExceptionMatches(PyExc_KeyError)) {
            PyErr_Clear();
            name_error(NAME(oparg));
        }
        Py_XDECREF(r);
        ERROR_IF(r == NULL);
        DISPATCH();
    }
    if (!lu_ns_delete(frame->locals, NAME(oparg))) {
        name_error(NAME(oparg));
        goto error;
    }
    DISPATCH();

op_BUILD_TUPLE: {
    PyObject *t = PyTuple_New(oparg);
    ERROR_IF(t == NULL);
    sp -= oparg;
    for (int i = 0; i < oparg; i++) {
        PyTuple_SET_ITEM(t, i, sp[i]);
    }
    PUSH(t);
    DISPATCH();
}

op_BUILD_LIST: {
    PyObject *l = PyList_New(oparg);
    ERROR_IF(l == NULL);
    sp -= oparg;
    for (int i = 0; i < oparg; i++) {
        PyList_SET_ITEM(l, i, sp[i]);
    }
    PUSH(l);
    DISPATCH();
}

op_LIST_APPEND: {
    PyObject *v = POP();
    int err = lu_list_append(sp[-oparg], v);
    Py_DECREF(v);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_LIST_EXTEND: {
    PyObject *iterable = POP();
    PyObject *list = sp[-oparg];
    SAVE_FRAME();
    if (PyTuple_CheckExact(iterable) || PyList_CheckExact(iterable)) {
        /* A list source is read as subscripts are: another thread may resize it. */
        Py_ssize_t n = Py_SIZE(iterable);
        int err = 0;
        for (Py_ssize_t i = 0; err == 0 && i < n; i++) {
            PyObject *v = PyTuple_CheckExact(iterable) ? Py_NewRef(PyTuple_GET_ITEM(iterable, i))
                                                       : lu_list_item_ref((PyListObject *)iterable, i);
            if (v == NULL) {
                break;
            }
            err = lu_list_append(list, v);
            Py_DECREF(v);
        }
        Py_DECREF(iterable);
        ERROR_IF(err < 0);
        DISPATCH();
    }
    kargs[0] = Py_NewRef(list);
    kargs[1] = iterable;
    nk = 2;
    kid = LU_K_list_extend;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_DICT_MERGE: {
    PyObject *update = POP();
    SAVE_FRAME();
    if (!PyDict_CheckExact(update) || !lu_dict_str_keys(update) || !lu_dict_str_keys(sp[-oparg])) {
        char buf[256];
        PyObject *name = PyUnicode_FromString(lu_function_str(sp[-oparg - 3], buf, sizeof buf));
        if (name == NULL) {
            Py_DECREF(update);
            goto error;
        }
        kargs[0] = Py_NewRef(sp[-oparg]);
        kargs[1] = update;
        kargs[2] = name;
        nk = 3;
        kid = LU_K_dict_merge;
        kret = LU_RETURN_DISCARD;
        goto kernel_call;
    }
    int err = lu_dict_merge(sp[-oparg - 3], sp[-oparg], update);
    Py_DECREF(update);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_DICT_UPDATE: {
    PyObject *update = POP();
    PyObject *dict = sp[-oparg];
    SAVE_FRAME();
    int err = 0;
    if (!PyDict_CheckExact(update) || !lu_dict_str_keys(update)) {
        kargs[0] = Py_NewRef(dict);
        kargs[1] = update;
        nk = 2;
        kid = LU_K_dict_update;
        kret = LU_RETURN_DISCARD;
        goto kernel_call;
    }
    else {
        Py_ssize_t pos = 0;
        PyObject *key, *value;
        Py_BEGIN_CRITICAL_SECTION(update);
        while (err == 0 && lu_ns_next(update, &pos, &key, &value)) {
            err = lu_ns_insert(dict, key, value);
        }
        Py_END_CRITICAL_SECTION();
    }
    Py_DECREF(update);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_CALL_INTRINSIC_1: {
    SAVE_FRAME();
    if (oparg == INTRINSIC_STOPITERATION_ERROR) {
        /* PEP 479: StopIteration leaving a generator's body becomes RuntimeError */
        kargs[0] = POP();
        kargs[1] = lu_small_int((code->co_flags & CO_ASYNC_GENERATOR) ? 2
                                : (code->co_flags & CO_COROUTINE) ? 1 : 0);
        nk = 2;
        kid = LU_K_stopiteration_error;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    if (oparg == INTRINSIC_IMPORT_STAR) {
        if (frame->locals == NULL) {
            lu_err_format(PyExc_SystemError, "no locals found during 'import *'");
            goto error;
        }
        kargs[0] = frame->locals;
        Py_INCREF(kargs[0]);
        kargs[1] = POP();
        nk = 2;
        kid = LU_K_import_star;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    if (oparg == INTRINSIC_PRINT) {
        /* the value of an expression statement in interactive code: sys.displayhook(value) */
        PyObject *value = POP();
        PyObject *sys = lu_ns_lookup(ts->interp->modules, PyUnicode_InternFromString("sys"));
        PyObject *hook = sys != NULL ? PyObject_GetAttrString(sys, "displayhook") : NULL;
        if (hook == NULL && (!PyErr_Occurred() || PyErr_ExceptionMatches(PyExc_AttributeError))) {
            PyErr_Clear();
            lu_err_format(PyExc_RuntimeError, "lost sys.displayhook");
        }
        PyObject *r = hook != NULL ? lu_call(hook, &value, 1, NULL) : NULL;
        Py_XDECREF(hook);
        Py_DECREF(value);
        ERROR_IF(r == NULL);
        PUSH(r);
        DISPATCH();
    }
    if (oparg == INTRINSIC_ASYNC_GEN_WRAP) {
        PyObject *w = lu_async_gen_wrap(TOP());
        ERROR_IF(w == NULL);
        Py_SETREF(TOP(), w);
        DISPATCH();
    }
    if (oparg == INTRINSIC_UNARY_POSITIVE) {
        if (!PyLong_CheckExact(TOP())) {
            kargs[0] = POP();
            nk = 1;
            kid = LU_K_positive;
            kret = LU_RETURN_PUSH;
            goto kernel_call;
        }
        DISPATCH();
    }
    if (oparg >= INTRINSIC_TYPEVAR && oparg <= INTRINSIC_TYPEALIAS) {
        kargs[0] = POP();
        kargs[1] = lu_small_int(oparg);
        nk = 2;
        kid = LU_K_intrinsic_1;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    if (oparg != INTRINSIC_LIST_TO_TUPLE) {
        lu_err_format(PyExc_SystemError, "intrinsic %d arrives with its consumer", oparg);
        goto error;
    }
    PyObject *list = TOP();
    PyObject *t = lu_args_as_tuple(Py_None, list);
    ERROR_IF(t == NULL);
    Py_DECREF(list);
    TOP() = t;
    DISPATCH();
}

op_JUMP_BACKWARD: {
    SKIP_CACHES();
    lu_codeunit *after = next_instr;
    next_instr -= oparg;
    POLL_AFTER(after);
    if (lu_jit_on() && lu_jit_backedge_hot(code, after - 1 - lu_opcode_caches[JUMP_BACKWARD])) {
        goto jit_enter;
    }
    DISPATCH();
}

jit_enter:
    /* Compiled code runs the frame from next_instr (runtime/jit/jit.h). */
    SAVE_FRAME();
    switch (lu_jit_enter(ts, frame, &result)) {
    case LU_JIT_NONE:
        DISPATCH();
    case LU_JIT_RETURN:
        retval = result;
        goto return_frame;
    case LU_JIT_DEOPT:
        frame = (struct lu_frame *)ts->current_frame;
        LOAD_FRAME();
        DISPATCH();
    case LU_JIT_ERROR:
        frame = (struct lu_frame *)ts->current_frame;
        LOAD_FRAME();
        goto error;
    case LU_JIT_YIELD:
        retval = result;
        oparg = frame->last->op.arg;
        goto yield_frame;
    }
    Py_UNREACHABLE();

/* The specialized forms of LOAD_ATTR (owner -- attr, self_or_null[oparg & 1]). */

op_LOAD_ATTR_MODULE: {
    PyObject *owner = TOP();
    uint32_t version = lu_cache_load32(next_instr + LA_VERSION);
    PyObject *v = NULL;
    if (version != 0 && PyModule_CheckExact(owner) && PyDict_CheckExact(((PyModuleObject *)owner)->md_dict)) {
        PyDictObject *md = (PyDictObject *)((PyModuleObject *)owner)->md_dict;
        PyDictKeysObject *dk = __atomic_load_n(&md->ma_keys, __ATOMIC_ACQUIRE);
        Py_ssize_t index = lu_cache_load(next_instr + LA_INDEX);
        if (dk->dk_version == version && lu_dk_is_unicode(dk) && index < dk->dk_nentries) {
            v = lu_try_xget_ref(lu_dk_value_slot(dk, index));
            if (v != NULL && dk != __atomic_load_n(&md->ma_keys, __ATOMIC_ACQUIRE)) {
                Py_CLEAR(v);
            }
        }
    }
    if (v == NULL || !lu_site_unchanged(next_instr - 1, opcode)
        || lu_cache_load32(next_instr + LA_VERSION) != version) {
        Py_XDECREF(v);
        goto op_LOAD_ATTR;
    }
    SKIP_CACHES();
    TOP() = v;
    Py_DECREF(owner);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_LOAD_ATTR_CLASS: {
    PyObject *owner = TOP();
    uint32_t version = lu_cache_load32(next_instr + LA_VERSION);
    int meta_checked = opcode == LOAD_ATTR_CLASS_WITH_METACLASS_CHECK;
    uint32_t meta_version = meta_checked ? lu_cache_load32(next_instr + LA_KEYS_VERSION) : 0;
    PyObject *descr = lu_cache_load_ptr(next_instr + LA_DESCR);
    if (version == 0 || !PyType_Check(owner) || ((PyTypeObject *)owner)->tp_version_tag != version
        || (meta_checked && (meta_version == 0 || Py_TYPE(owner)->tp_version_tag != meta_version))
        || !lu_site_unchanged(next_instr - 1, opcode)
        || lu_cache_load32(next_instr + LA_VERSION) != version) {
        goto op_LOAD_ATTR;
    }
    SKIP_CACHES();
    TOP() = Py_NewRef(descr);       /* deferred: no collection since the load (specialize.c) */
    Py_DECREF(owner);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_LOAD_ATTR_WITH_HINT: {
    PyObject *owner = TOP();
    uint32_t version = lu_cache_load32(next_instr + LA_VERSION);
    PyObject *v = NULL;
    if (version != 0 && Py_TYPE(owner)->tp_version_tag == version) {
        PyObject *dict = LU_MANAGED_DICT(owner);
        if (dict != NULL && PyDict_CheckExact(dict)) {
            PyDictKeysObject *dk = __atomic_load_n(&((PyDictObject *)dict)->ma_keys, __ATOMIC_ACQUIRE);
            Py_ssize_t index = lu_cache_load(next_instr + LA_INDEX);
            if (lu_dk_is_unicode(dk) && index < dk->dk_nentries && lu_dk_key(dk, index) == NAME(oparg >> 1)) {
                v = lu_try_xget_ref(lu_dk_value_slot(dk, index));
                if (v != NULL && dk != __atomic_load_n(&((PyDictObject *)dict)->ma_keys, __ATOMIC_ACQUIRE)) {
                    Py_CLEAR(v);
                }
            }
        }
    }
    if (v == NULL || !lu_site_unchanged(next_instr - 1, opcode)
        || lu_cache_load32(next_instr + LA_VERSION) != version) {
        Py_XDECREF(v);
        goto op_LOAD_ATTR;
    }
    SKIP_CACHES();
    TOP() = v;
    Py_DECREF(owner);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

/* A __slots__ member of the owner's type, or an inline value while the
   owner keeps them, read at its cached offset. */
op_LOAD_ATTR_SLOT: {
    PyObject *owner = TOP();
    uint32_t version = lu_cache_load32(next_instr + LA_VERSION);
    PyObject *v = NULL;
    if (version != 0 && Py_TYPE(owner)->tp_version_tag == version
        && (opcode == LOAD_ATTR_SLOT || lu_inline_live(owner))) {
        /* Another thread may store and free the value: taken only if alive. */
        PyObject **word = (PyObject **)((char *)owner + lu_cache_load(next_instr + LA_INDEX));
        v = opcode == LOAD_ATTR_SLOT ? lu_try_xget_ref(word) : lu_inline_get(owner, word);
    }
    if (v == NULL || !lu_site_unchanged(next_instr - 1, opcode)
        || lu_cache_load32(next_instr + LA_VERSION) != version) {
        Py_XDECREF(v);
        goto op_LOAD_ATTR;
    }
    SKIP_CACHES();
    TOP() = v;
    Py_DECREF(owner);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

/* A method of the type, or a plain value of it, unless the instance's dict
   has the name: a method is pushed under its owner, a value alone. */
op_LOAD_ATTR_METHOD: {
    PyObject *owner = TOP();
    uint32_t version = lu_cache_load32(next_instr + LA_VERSION);
    PyObject *descr = lu_cache_load_ptr(next_instr + LA_DESCR);
    int with_dict = opcode == LOAD_ATTR_METHOD_WITH_VALUES || opcode == LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES;
    if (version == 0 || Py_TYPE(owner)->tp_version_tag != version) {
        goto op_LOAD_ATTR;
    }
    if (with_dict) {
        PyObject *dict = LU_MANAGED_DICT(owner);
        if (dict != NULL && (!PyDict_CheckExact(dict) || lu_ns_lookup(dict, NAME(oparg >> 1)) != NULL)) {
            goto op_LOAD_ATTR;
        }
    }
    if (!lu_site_unchanged(next_instr - 1, opcode) || lu_cache_load32(next_instr + LA_VERSION) != version) {
        goto op_LOAD_ATTR;
    }
    SKIP_CACHES();
    TOP() = Py_NewRef(descr);       /* deferred: no collection since the load (specialize.c) */
    if (opcode == LOAD_ATTR_METHOD_NO_DICT || opcode == LOAD_ATTR_METHOD_WITH_VALUES) {
        PUSH(owner);
    }
    else {
        Py_DECREF(owner);
        if (oparg & 1) {
            PUSH(NULL);
        }
    }
    DISPATCH();
}

op_LOAD_ATTR: {
    /* owner -- attr, self_or_null[oparg & 1]; the method form pushes a NULL
       self and CALL unpacks the bound method. */
    PyObject *name = NAME(oparg >> 1);
    PyObject *owner = TOP();
    ADAPT(lu_specialize_load_attr(owner, next_instr - 1, name));
    SKIP_CACHES();
    kret = (oparg & 1) ? LU_RETURN_METHOD : LU_RETURN_PUSH;
    if (PyModule_CheckExact(owner)) {
        PyObject *v = lu_ns_get(((PyModuleObject *)owner)->md_dict, name);
        if (v != NULL) {
            TOP() = v;
            Py_DECREF(owner);
            if (oparg & 1) {
                PUSH(NULL);
            }
            DISPATCH();
        }
    }
    getattrofunc get = Py_TYPE(owner)->tp_getattro;
    if (get == lu_slot_tp_getattro || get == PyObject_GenericGetAttr) {
        PyObject *descr = _PyType_LookupRef(Py_TYPE(owner), name), *v;
        if ((v = attr_shortcut(owner, name)) != NULL) {
            Py_XDECREF(descr);
            TOP() = v;
            Py_DECREF(owner);
            if (oparg & 1) {
                PUSH(NULL);
            }
            DISPATCH();
        }
        if (descr != NULL) {
            SAVE_FRAME();       /* a getter may read the frame: f_lineno, f_locals */
        }
        if (descr != NULL && object_attr_hook(Py_TYPE(owner), LU_ID(__getattribute__))
            && lu_descr_get_native(descr, owner, &v) > 0) {
            Py_DECREF(descr);
            TOP() = v;
            Py_DECREF(owner);
            if (oparg & 1) {
                PUSH(NULL);
            }
            DISPATCH();
        }
        if (descr != NULL && object_attr_hook(Py_TYPE(owner), LU_ID(__getattribute__))) {
            /* A data descriptor whose __get__ is a Python function decides the
               access: run it in this loop, as generic_getattr would, unless
               an AttributeError from it must go to __getattr__. */
            PyTypeObject *dt = Py_TYPE(descr);
            PyObject *getter = _PyType_LookupRef(dt, LU_ID(__get__));
            if (getter != NULL && Py_IS_TYPE(getter, &PyFunction_Type)
                && _PyType_Lookup(Py_TYPE(owner), LU_ID(__getattr__)) == NULL
                && (_PyType_Lookup(dt, LU_ID(__set__)) != NULL || _PyType_Lookup(dt, LU_ID(__delete__)) != NULL)) {
                kargs[0] = descr;
                kargs[1] = POP();
                kargs[2] = Py_NewRef((PyObject *)Py_TYPE(kargs[1]));
                SAVE_FRAME();
                callee = lu_frame_call(ts, (PyFunctionObject *)getter, kargs, 3, NULL);
                Py_DECREF(getter);
                ERROR_IF(callee == NULL);
                callee->ret = kret;
                goto push_callee;
            }
            Py_XDECREF(getter);
        }
        Py_XDECREF(descr);
        if ((v = lu_getattr_native(owner, name)) != NULL) {
            TOP() = v;
            Py_DECREF(owner);
            if (oparg & 1) {
                PUSH(NULL);
            }
            DISPATCH();
        }
        kid = get == lu_slot_tp_getattro ? LU_K_getattr : LU_K_generic_getattr;
        kargs[0] = POP();
        kargs[1] = Py_NewRef(name);
        nk = 2;
        goto kernel_call;
    }
    SAVE_FRAME();
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    PyObject *v = PyObject_GetAttr(owner, name);
    t->pin--;
    ERROR_IF(v == NULL);
    TOP() = v;
    Py_DECREF(owner);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

/* v, owner -- : a __slots__ member or an inline value written at its
   cached offset, or a name the type lacks inserted into the instance's
   dict. */
op_STORE_ATTR_FAST: {
    PyObject *owner = sp[-1];
    uint32_t version = lu_cache_load32(next_instr + LA_VERSION);
    PyObject *dict = NULL;
    if (version == 0 || Py_TYPE(owner)->tp_version_tag != version) {
        goto op_STORE_ATTR;
    }
    if (opcode == STORE_ATTR_WITH_HINT) {
        dict = LU_MANAGED_DICT(owner);
        if (dict == NULL || !PyDict_CheckExact(dict)) {
            goto op_STORE_ATTR;     /* the generic form makes the dict */
        }
    }
    if (!lu_site_unchanged(next_instr - 1, opcode) || lu_cache_load32(next_instr + LA_VERSION) != version) {
        goto op_STORE_ATTR;
    }
    PyObject *v = sp[-2];
    unsigned offset = lu_cache_load(next_instr + LA_INDEX);
    SKIP_CACHES();
    sp -= 2;
    int err = 0;
    if (opcode == STORE_ATTR_SLOT) {
        Py_XDECREF(lu_member_exchange(owner, (PyObject **)((char *)owner + offset), v));
        Py_DECREF(v);
    }
    else if (opcode == STORE_ATTR_INSTANCE_VALUE) {
        if (!lu_inline_store(owner, offset, v)) {
            SAVE_FRAME();
            err = lu_attr_store(owner, NAME(oparg), v) < 0 ? -1 : 0;
        }
        Py_DECREF(v);
    }
    else {
        SAVE_FRAME();
        err = lu_ns_store(dict, NAME(oparg), v);
        Py_DECREF(v);
    }
    Py_DECREF(owner);
    ERROR_IF(err < 0);
    DISPATCH();
}

op_STORE_ATTR:
op_DELETE_ATTR: {
    /* v, owner -- (STORE_ATTR); owner -- (DELETE_ATTR) */
    PyObject *name = NAME(oparg);
    int store = opcode != DELETE_ATTR;
    PyObject *owner = sp[-1];
    PyObject *v = store ? sp[-2] : NULL;
    if (store) {
        ADAPT(lu_specialize_store_attr(owner, next_instr - 1, name));
    }
    SKIP_CACHES();
    setattrofunc set = Py_TYPE(owner)->tp_setattro;
    if (set == lu_slot_tp_setattro || set == PyObject_GenericSetAttr) {
        SAVE_FRAME();
        int done = lu_setattr_native(owner, name, v);
        if (done != 0) {
            sp -= store ? 2 : 1;
            Py_DECREF(owner);
            Py_XDECREF(v);
            ERROR_IF(done < 0);
            DISPATCH();
        }
        sp -= store ? 2 : 1;
        kargs[0] = owner;
        kargs[1] = Py_NewRef(name);
        kargs[2] = v;
        nk = store ? 3 : 2;
        kid = set == PyObject_GenericSetAttr ? LU_K_generic_setattr : store ? LU_K_setattr : LU_K_delattr;
        if (kid == LU_K_generic_setattr && !store) {
            kargs[2] = Py_NewRef(LU_MISSING);
            nk = 3;
        }
        kret = LU_RETURN_DISCARD;
        goto kernel_call;
    }
    SAVE_FRAME();
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    int err = PyObject_SetAttr(owner, name, v);
    t->pin--;
    Py_DECREF(owner);
    Py_XDECREF(v);
    sp -= store ? 2 : 1;
    ERROR_IF(err < 0);
    DISPATCH();
}

op_INSTRUMENTED_LOAD_SUPER_ATTR: {
    /* LOAD_SUPER_ATTR as a call of super: the tools see the call, and its return. */
    lu_codeunit *this_instr = next_instr - 1;
    PyObject *name = NAME(oparg >> 2);
    SKIP_CACHES();
    SAVE_FRAME();
    PyObject *global_super = sp[-3];
    PyObject *arg = (oparg & 2) ? sp[-2] : &lu_instrumentation_missing;
    ERROR_IF(lu_fire_2args(ts, PY_MONITORING_EVENT_CALL, frame, this_instr, global_super, arg) < 0);
    PyObject *v;
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    if (global_super == (PyObject *)&PySuper_Type && PyType_Check(sp[-2])) {
        PyObject *kargs3[3] = { sp[-2], sp[-1], name };
        v = lu_call_kernel(LU_K_super_getattr, kargs3, 3);
    }
    else {
        PyObject *sup = lu_call(global_super, sp - 2, oparg & 2, NULL);
        v = sup != NULL ? PyObject_GetAttr(sup, name) : NULL;
        Py_XDECREF(sup);
    }
    t->pin--;
    if (v == NULL) {
        lu_fire_exc2(ts, PY_MONITORING_EVENT_C_RAISE, frame, this_instr, global_super, arg);
    }
    else if (lu_fire_2args(ts, PY_MONITORING_EVENT_C_RETURN, frame, this_instr, global_super, arg) < 0) {
        Py_CLEAR(v);
    }
    for (int i = 0; i < 3; i++) {
        Py_DECREF(POP());
    }
    ERROR_IF(v == NULL);
    PUSH(v);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_LOAD_SUPER_ATTR: {
    /* global_super, class, self -- attr, self_or_null[oparg & 1] */
    PyObject *name = NAME(oparg >> 2);
    SKIP_CACHES();
    kret = (oparg & 1) ? LU_RETURN_METHOD : LU_RETURN_PUSH;
    if (sp[-3] == (PyObject *)&PySuper_Type && PyType_Check(sp[-2])) {
        kargs[2] = Py_NewRef(name);
        kargs[1] = POP();
        kargs[0] = POP();
        Py_DECREF(POP());
        kid = LU_K_super_getattr;
        nk = 3;
        goto kernel_call;
    }
    /* super is shadowed: call it, then take the attribute, from C. */
    SAVE_FRAME();
    struct lu_task *t = (struct lu_task *)ts;
    t->pin++;
    PyObject *sup = lu_call(sp[-3], sp - 2, oparg & 2, NULL);
    PyObject *v = sup != NULL ? PyObject_GetAttr(sup, name) : NULL;
    t->pin--;
    Py_XDECREF(sup);
    for (int i = 0; i < 3; i++) {
        Py_DECREF(POP());
    }
    ERROR_IF(v == NULL);
    PUSH(v);
    if (oparg & 1) {
        PUSH(NULL);
    }
    DISPATCH();
}

op_LOAD_BUILD_CLASS: {
    PyObject *bc = ns_get(frame->builtins, LU_ID(__build_class__));
    if (bc == NULL) {
        SAVE_FRAME();
        if (!PyErr_Occurred()) {
            lu_err_format(PyExc_NameError, "__build_class__ not found");
        }
        goto error;
    }
    PUSH(bc);
    DISPATCH();
}

op_LOAD_LOCALS:
    if (frame->locals == NULL) {
        SAVE_FRAME();
        lu_err_format(PyExc_SystemError, "no locals found");
        goto error;
    }
    PUSH(Py_NewRef(frame->locals));
    DISPATCH();

op_LOAD_FROM_DICT_OR_GLOBALS: {
    PyObject *dict = POP();
    PyObject *name = NAME(oparg);
    SAVE_FRAME();
    PyObject *v;
    v = locals_get(dict, name);
    if (v == NULL && !PyErr_Occurred() && (v = global_get(frame, name)) == NULL && !PyErr_Occurred()) {
        name_error(name);
    }
    Py_DECREF(dict);
    ERROR_IF(v == NULL);
    PUSH(v);
    DISPATCH();
}

op_SETUP_ANNOTATIONS: {
    SAVE_FRAME();
    PyObject *name = PyUnicode_InternFromString("__annotations__");
    int err = name == NULL;
    if (!err && frame->locals == NULL) {
        lu_err_format(PyExc_SystemError, "no locals found when setting up annotations");
        err = 1;
    }
    if (!err) {
        PyObject *have = locals_get(frame->locals, name);
        if (have == NULL) {
            PyObject *d = PyErr_Occurred() ? NULL : PyDict_New();
            if (d != NULL && PyDict_CheckExact(frame->locals)) {
                err = lu_ns_insert(frame->locals, name, d) < 0;
            }
            else if (d != NULL) {
                PyObject *args[3] = { frame->locals, name, d };
                PyObject *r = lu_call_kernel(LU_K_setitem, args, 3);
                err = r == NULL;
                Py_XDECREF(r);
            }
            else {
                err = 1;
            }
            Py_XDECREF(d);
        }
        Py_XDECREF(have);
    }
    Py_XDECREF(name);
    ERROR_IF(err);
    DISPATCH();
}

op_GET_ITER: {
    PyObject *iterable = TOP();
    if (Py_IS_TYPE(iterable, &PyGen_Type)) {
        DISPATCH();
    }
    SAVE_FRAME();
    PyObject *it = lu_get_iter_exact(iterable);
    if (it == NULL) {
        ERROR_IF(PyErr_Occurred());
        kargs[0] = POP();
        nk = 1;
        kid = LU_K_get_iter;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    TOP() = it;
    Py_DECREF(iterable);
    DISPATCH();
}

op_FOR_ITER: {
    /* iter -- iter, next; exhausted, jumps past the END_FOR at the target */
    lu_codeunit *this_instr = next_instr - 1;
    PyObject *iter = TOP();
    PyTypeObject *itp = Py_TYPE(iter);
    lu_jit_observe_iter(next_instr, itp);
    SKIP_CACHES();
    PyObject *v;
    if (itp == &PyListIter_Type || itp == &PyTupleIter_Type) {
        v = lu_seqiter_next(iter);
    }
    else if (itp == &PyRangeIter_Type) {
        v = lu_rangeiter_next(iter);
    }
    else if (itp == &PyDictIterKey_Type || itp == &PyDictIterValue_Type || itp == &PyDictIterItem_Type) {
        SAVE_FRAME();
        v = lu_dictiter_next(iter);
        if (v == LU_MISSING) {
            Py_DECREF(v);
            v = NULL;
        }
    }
    else if (itp == &PyEnum_Type && save_frame_then(frame, next_instr, sp, cur_instr) && lu_enum_next(iter, &v)) {
        /* v is the pair, or NULL when the inner iterator ended or failed */
    }
    else if (itp == &PyGen_Type && ts->interp->eval_frame == NULL) {
        SAVE_FRAME();
        callee = lu_gen_resume(ts, iter, NULL);
        if (callee == NULL) {
            ERROR_IF(PyErr_Occurred());
            next_instr += oparg + 1;
            DISPATCH();
        }
        callee->ret = LU_RETURN_FOR_ITER;
        goto resume_linked;
    }
    else {
        SAVE_FRAME();
        kargs[0] = Py_NewRef(iter);
        nk = 1;
        kid = LU_K_next_item;
        kret = LU_RETURN_FOR_ITER;
        goto kernel_call;
    }
    if (v == NULL) {
        SAVE_FRAME();
        ERROR_IF(PyErr_Occurred());
        next_instr += oparg + 1;
        DISPATCH();
    }
    PUSH(v);
    if (opcode == INSTRUMENTED_FOR_ITER) {
        INSTRUMENTED_JUMP(this_instr, next_instr, PY_MONITORING_EVENT_BRANCH_LEFT);
    }
    DISPATCH();
}

op_COMPARE_OP: {
    /* Ints (bools too) and equality that needs no Python code here; the rest is the kernel's compare. */
    PyObject *right = sp[-1];
    PyObject *left = sp[-2];
    lu_jit_observe(next_instr, left, right);
    SKIP_CACHES();
    int r = lu_compare_native(left, right, oparg);
    if (r < 0) {
        sp -= 2;
        kargs[0] = left;
        kargs[1] = right;
        kargs[2] = lu_small_int(oparg);
        nk = 3;
        kid = LU_K_compare;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    sp--;
    TOP() = r ? Py_True : Py_False;
    Py_DECREF(left);
    Py_DECREF(right);
    DISPATCH();
}

op_FORMAT_SIMPLE: {
    /* value -- str(value) through PyObject_Format(value, '') */
    if (PyUnicode_CheckExact(TOP())) {
        DISPATCH();
    }
    kargs[0] = POP();
    kargs[1] = (PyObject *)&_PyRuntime.singletons.empty_str;
    nk = 2;
    kid = LU_K_format;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_FORMAT_WITH_SPEC: {
    /* value, spec -- PyObject_Format(value, spec) */
    sp -= 2;
    kargs[0] = sp[0];
    kargs[1] = sp[1];
    nk = 2;
    kid = LU_K_format;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_CONVERT_VALUE: {
    /* value -- str(value), repr(value) or ascii(value) for oparg 1, 2, 3 */
    kargs[0] = POP();
    nk = 1;
    kid = oparg == 1 ? LU_K_str : oparg == 2 ? LU_K_repr : LU_K_ascii;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_BUILD_STRING: {
    /* pieces... -- the pieces joined; every piece is an exact str */
    PyObject *pieces = PyTuple_New(oparg);
    ERROR_IF(pieces == NULL);
    sp -= oparg;
    for (int i = 0; i < oparg; i++) {
        PyTuple_SET_ITEM(pieces, i, sp[i]);
    }
    PyObject *res = lu_str_join_tuple((PyObject *)&_PyRuntime.singletons.empty_str, pieces);
    Py_DECREF(pieces);
    ERROR_IF(res == NULL);
    PUSH(res);
    DISPATCH();
}

op_MATCH_MAPPING: {
    /* subject -- subject, whether it is a mapping */
    SAVE_FRAME();
    kargs[0] = Py_NewRef(TOP());
    nk = 1;
    kid = LU_K_match_mapping;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_MATCH_SEQUENCE: {
    /* subject -- subject, whether it is a sequence that is not a str, bytes or bytearray */
    SAVE_FRAME();
    kargs[0] = Py_NewRef(TOP());
    nk = 1;
    kid = LU_K_match_sequence;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_MATCH_KEYS: {
    /* subject, keys -- subject, keys, tuple of the values or None */
    SAVE_FRAME();
    kargs[0] = Py_NewRef(sp[-2]);
    kargs[1] = Py_NewRef(sp[-1]);
    nk = 2;
    kid = LU_K_match_keys;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_MATCH_CLASS: {
    /* subject, type, names -- tuple of the attributes or None; oparg positional patterns */
    SAVE_FRAME();
    sp -= 3;
    kargs[0] = sp[0];
    kargs[1] = sp[1];
    kargs[2] = sp[2];
    kargs[3] = lu_small_int(oparg);
    nk = 4;
    kid = LU_K_match_class;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_EXIT_INIT_CHECK: {
    /* value -- ; __init__ must have returned None */
    PyObject *value = POP();
    if (value != Py_None) {
        SAVE_FRAME();
        lu_err_format(PyExc_TypeError, "__init__() should return None, not '%.200s'", Py_TYPE(value)->tp_name);
        Py_DECREF(value);
        goto error;
    }
    Py_DECREF(value);
    DISPATCH();
}

op_BUILD_TEMPLATE: {
    /* strings, interpolations -- template */
    sp -= 2;
    kargs[0] = sp[0];
    kargs[1] = sp[1];
    nk = 2;
    kid = LU_K_build_template;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_BUILD_INTERPOLATION: {
    /* value, expression [, format_spec] -- interpolation; oparg >> 2 is the conversion */
    kargs[3] = (oparg & 1) ? POP() : Py_NewRef((PyObject *)&_PyRuntime.singletons.empty_str);
    kargs[2] = lu_small_int(oparg >> 2);
    kargs[1] = POP();
    kargs[0] = POP();
    nk = 4;
    kid = LU_K_build_interpolation;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_BINARY_OP: {
    /* lhs, rhs -- res; exact ints here, everything else reflects in the kernel. */
    PyObject *right = sp[-1];
    PyObject *left = sp[-2];
    lu_jit_observe(next_instr + 1, left, right);
    SKIP_CACHES();
    if (oparg == 26) {   /* NB_SUBSCR */
        PyObject *item = lu_subscr_native(left, right);
        sp -= 2;
        if (item != NULL || PyErr_Occurred()) {
            Py_DECREF(left);
            Py_DECREF(right);
            ERROR_IF(item == NULL);
            PUSH(item);
            DISPATCH();
        }
        kargs[0] = left;
        kargs[1] = right;
        nk = 2;
        kid = LU_K_getitem;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    int bitwise = oparg == NB_AND || oparg == NB_OR || oparg == NB_XOR
                  || oparg == NB_INPLACE_AND || oparg == NB_INPLACE_OR || oparg == NB_INPLACE_XOR;
    /* A bool computes as an int, except under the bitwise operators, which keep bool. */
    if ((PyLong_CheckExact(left) || (!bitwise && PyBool_Check(left)))
        && (PyLong_CheckExact(right) || (!bitwise && PyBool_Check(right)))) {
        SAVE_FRAME();
        PyObject *res = lu_long_binary(oparg, left, right);
        ERROR_IF(res == NULL);
        if (res != Py_NotImplemented) {
            sp--;
            TOP() = res;
            Py_DECREF(left);
            Py_DECREF(right);
            DISPATCH();
        }
        Py_DECREF(res);
    }
    else if ((oparg == NB_ADD || oparg == NB_INPLACE_ADD) && PyUnicode_CheckExact(left)
             && PyUnicode_CheckExact(right)) {
        SAVE_FRAME();
        PyObject *res = lu_str_concat(left, right);
        ERROR_IF(res == NULL);
        sp--;
        TOP() = res;
        Py_DECREF(left);
        Py_DECREF(right);
        DISPATCH();
    }
    sp -= 2;
    kargs[0] = left;
    kargs[1] = right;
    kargs[2] = lu_small_int(oparg);
    nk = 3;
    kid = LU_K_binary_op;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_UNARY_NEGATIVE:
op_UNARY_INVERT: {
    PyObject *v = TOP();
    if (PyLong_CheckExact(v)) {
        PyObject *res = opcode == UNARY_NEGATIVE ? lu_long_negate(v) : lu_long_invert(v);
        if (res == NULL) {
            SAVE_FRAME();
            goto error;
        }
        TOP() = res;
        Py_DECREF(v);
        DISPATCH();
    }
    kargs[0] = POP();
    nk = 1;
    kid = opcode == UNARY_NEGATIVE ? LU_K_negative : LU_K_invert;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_CONTAINS_OP: {
    /* item, container -- bool; oparg 1 is `not in` */
    SKIP_CACHES();
    int found = contains_native(sp[-1], sp[-2]);
    if (found >= 0) {
        PyObject *item = sp[-2], *container = sp[-1];
        sp -= 2;
        PUSH(Py_NewRef((found ^ oparg) ? Py_True : Py_False));
        Py_DECREF(item);
        Py_DECREF(container);
        DISPATCH();
    }
    sp -= 2;
    kargs[0] = sp[1];
    kargs[1] = sp[0];
    kargs[2] = lu_small_int(oparg);
    nk = 3;
    kid = LU_K_contains;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_UNPACK_SEQUENCE: {
    lu_jit_observe(next_instr, TOP(), TOP());
    SKIP_CACHES();
    PyObject *seq = POP();
    if (unpack_exact(seq, oparg, sp)) {
        Py_DECREF(seq);
        sp += oparg;
        DISPATCH();
    }
    SAVE_FRAME();
    kargs[0] = seq;
    kargs[1] = PyLong_FromLong(oparg);      /* a long target list outgrows the small ints */
    nk = 2;
    kid = LU_K_unpack;
    kret = LU_RETURN_UNPACK;
    goto kernel_call;
}

op_UNPACK_EX: {
    SAVE_FRAME();
    kargs[0] = POP();
    kargs[1] = lu_small_int(oparg & 0xFF);
    kargs[2] = lu_small_int(oparg >> 8);
    nk = 3;
    kid = LU_K_unpack_ex;
    kret = LU_RETURN_UNPACK;
    goto kernel_call;
}

op_STORE_SUBSCR: {
    /* value, container, sub -- */
    PyObject *sub = sp[-1];
    PyObject *container = sp[-2];
    lu_jit_observe(next_instr, container, sub);
    SKIP_CACHES();
    PyObject *old = store_subscr_list(container, sub, sp[-3]);
    if (old != NULL) {
        sp -= 3;
        Py_DECREF(old);
        Py_DECREF(container);
        Py_DECREF(sub);
        DISPATCH();
    }
    if (PyDict_CheckExact(container)) {
        SAVE_FRAME();
        int done = lu_dict_put_native(container, sub, sp[-3]);
        ERROR_IF(done == -2);
        if (done == 1) {
            PyObject *value = sp[-3];
            sp -= 3;
            Py_DECREF(value);
            Py_DECREF(container);
            Py_DECREF(sub);
            DISPATCH();
        }
    }
    sp -= 3;
    kargs[0] = container;
    kargs[1] = sub;
    kargs[2] = sp[0];
    nk = 3;
    kid = LU_K_setitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_DELETE_SUBSCR: {
    /* container, sub -- */
    sp -= 2;
    kargs[0] = sp[0];
    kargs[1] = sp[1];
    nk = 2;
    kid = LU_K_delitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_BUILD_SLICE: {
    /* start, stop[, step] -- slice */
    PyObject *step = oparg == 3 ? POP() : NULL;
    PyObject *stop = POP();
    PyObject *start = TOP();
    SAVE_FRAME();
    PyObject *slice = PySlice_New(start, stop, step);
    Py_DECREF(start);
    Py_DECREF(stop);
    Py_XDECREF(step);
    ERROR_IF(slice == NULL);
    TOP() = slice;
    DISPATCH();
}

op_BINARY_SLICE: {
    /* container, start, stop -- container[start:stop] */
    SAVE_FRAME();
    PyObject *sliced = lu_slice_native(sp[-3], sp[-2], sp[-1], NULL);
    if (sliced != NULL) {
        Py_DECREF(sp[-3]);
        Py_DECREF(sp[-2]);
        Py_DECREF(sp[-1]);
        sp -= 2;
        TOP() = sliced;
        DISPATCH();
    }
    ERROR_IF(PyErr_Occurred());
    PyObject *stop = POP();
    PyObject *start = POP();
    PyObject *slice = PySlice_New(start, stop, NULL);
    Py_DECREF(start);
    Py_DECREF(stop);
    if (slice == NULL) {
        Py_DECREF(POP());
        goto error;
    }
    kargs[0] = POP();
    kargs[1] = slice;
    nk = 2;
    kid = LU_K_getitem;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_STORE_SLICE: {
    /* value, container, start, stop -- container[start:stop] = value */
    SAVE_FRAME();
    PyObject *stop = POP();
    PyObject *start = POP();
    PyObject *slice = PySlice_New(start, stop, NULL);
    Py_DECREF(start);
    Py_DECREF(stop);
    if (slice == NULL) {
        Py_DECREF(POP());
        Py_DECREF(POP());
        goto error;
    }
    kargs[1] = slice;
    kargs[0] = POP();
    kargs[2] = POP();
    nk = 3;
    kid = LU_K_setitem;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_GET_LEN: {
    /* obj -- obj, len(obj) */
    SAVE_FRAME();
    kargs[0] = Py_NewRef(TOP());
    nk = 1;
    kid = LU_K_length;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_RAISE_VARARGS: {
    /* raise, raise exc, raise exc from cause */
    SAVE_FRAME();
    if (oparg == 0) {
        PyObject *exc = lu_topmost_exc_info(ts)->exc_value;
        if (exc == NULL || exc == Py_None) {
            lu_err_format(PyExc_RuntimeError, "No active exception to reraise");
            goto error;
        }
        PyErr_SetRaisedException(Py_NewRef(exc));
        lasti = INSTR_START() - lu_code_start(code);
        if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RERAISE)) {
            lu_monitor_reraise(ts, frame, INSTR_START());
        }
        goto exception_unwind;
    }
    if (lu_raise_native(sp[-oparg], oparg == 2 ? sp[-1] : LU_MISSING)) {
        while (oparg-- > 0) {
            Py_DECREF(POP());
        }
        goto error;
    }
    sp -= oparg;
    kargs[0] = sp[0];
    kargs[1] = oparg == 2 ? sp[1] : Py_NewRef(LU_MISSING);
    kid = LU_K_do_raise;
    nk = 2;
    kret = LU_RETURN_DISCARD;
    goto kernel_call;
}

op_RERAISE: {
    PyObject *exc = POP();
    long saved;
    lasti = oparg && lu_long_as_long(sp[-oparg], &saved) == 0
        ? saved : INSTR_START() - lu_code_start(code);
    PyErr_SetRaisedException(exc);
    reraise_at = lu_code_start(code) + lasti;
    reraise_at += 1 + lu_caches_at(code, reraise_at);
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RERAISE)) {
        SAVE_FRAME();
        frame->instr = reraise_at;
        lu_monitor_reraise(ts, frame, INSTR_START());
    }
    goto exception_unwind;
}

op_PUSH_EXC_INFO: {
    /* exc -- prev_exc, exc */
    PyObject *exc = TOP();
    _PyErr_StackItem *info = ts->exc_info;
    PyObject *prev = info->exc_value;
    info->exc_value = Py_NewRef(exc);
    TOP() = prev != NULL ? prev : Py_NewRef(Py_None);
    PUSH(exc);
    DISPATCH();
}

op_POP_EXCEPT: {
    PyObject *prev = POP();
    _PyErr_StackItem *info = ts->exc_info;
    PyObject *old = info->exc_value;
    if (prev == Py_None) {
        Py_DECREF(prev);
        prev = NULL;
    }
    info->exc_value = prev;
    Py_XDECREF(old);
    DISPATCH();
}

op_CHECK_EXC_MATCH: {
    /* left, right -- left, left matches right */
    PyObject *right = POP();
    int match = lu_exc_match(TOP(), right);
    Py_DECREF(right);
    if (match < 0) {
        SAVE_FRAME();
        goto error;
    }
    PUSH(match ? Py_True : Py_False);
    DISPATCH();
}

op_CHECK_EG_MATCH: {
    /* exc_value, match_type -- rest, match */
    SAVE_FRAME();
    kargs[1] = POP();
    kargs[0] = POP();
    kid = LU_K_exc_group_match;
    nk = 2;
    kret = LU_RETURN_PAIR;
    goto kernel_call;
}

op_CALL_INTRINSIC_2: {
    SAVE_FRAME();
    if (oparg >= INTRINSIC_TYPEVAR_WITH_BOUND && oparg <= INTRINSIC_SET_TYPEPARAM_DEFAULT) {
        kargs[1] = POP();
        kargs[0] = POP();
        kargs[2] = lu_small_int(oparg);
        nk = 3;
        kid = LU_K_intrinsic_2;
        kret = LU_RETURN_PUSH;
        goto kernel_call;
    }
    if (oparg != INTRINSIC_PREP_RERAISE_STAR) {
        lu_err_format(PyExc_SystemError, "intrinsic %d arrives with its consumer", oparg);
        goto error;
    }
    kargs[1] = POP();
    kargs[0] = POP();
    kid = LU_K_prep_reraise_star;
    nk = 2;
    kret = LU_RETURN_PUSH;
    goto kernel_call;
}

op_LOAD_SPECIAL: {
    /* owner -- attr, self_or_null; looked up on the type, never the instance */
    SAVE_FRAME();
    kargs[0] = POP();
    kargs[1] = lu_small_int(oparg);
    kid = LU_K_load_special;
    nk = 2;
    kret = LU_RETURN_METHOD;
    goto kernel_call;
}

op_WITH_EXCEPT_START: {
    /* exit_func, exit_self, lasti, prev_exc, val -- ..., res: calls
       exit_func([exit_self,] type(val), val, val.__traceback__) as a CALL
       whose operands stay on the stack. */
    PyObject *val = TOP();
    PyObject *tb = ((PyBaseExceptionObject *)val)->traceback;
    PyObject *exit_self = sp[-4];
    exit_args[0] = NULL;
    exit_args[1] = exit_self;
    exit_args[2] = (PyObject *)Py_TYPE(val);
    exit_args[3] = val;
    exit_args[4] = tb ? tb : Py_None;
    SAVE_FRAME();
    callable = Py_NewRef(sp[-5]);
    args = exit_self != NULL ? exit_args + 1 : exit_args + 2;
    npos = total = exit_self != NULL ? 4 : 3;
    for (Py_ssize_t i = 0; i < total; i++) {
        Py_INCREF(args[i]);
    }
    kwnames = NULL;
    base = sp;
    free_self = exit_self == NULL;
    to_free = NULL;
    goto call_tail;
}

op_LOAD_COMMON_CONSTANT: {
    /* _common_constants of Lib/opcode.py */
    static const char *const names[] = { NULL, NULL, "tuple", "all", "any" };
    PyObject *v = oparg == 0 ? PyExc_AssertionError : oparg == 1 ? PyExc_NotImplementedError : NULL;
    if (v == NULL) {
        SAVE_FRAME();
        PyObject *name = oparg < 5 ? PyUnicode_InternFromString(names[oparg]) : NULL;
        v = name != NULL ? lu_ns_get(ts->interp->builtins, name) : NULL;
        Py_XDECREF(name);
        if (v == NULL) {
            lu_err_format(PyExc_SystemError, "common constant %d is not defined yet", oparg);
            goto error;
        }
    }
    else {
        Py_INCREF(v);
    }
    PUSH(v);
    DISPATCH();
}

error:
    /* The raising frame joins the traceback; kernel frames look native and
       do not. */
    reraise_at = NULL;
    monitored = NULL;          /* a monitored call that failed before its tail */
    lasti = INSTR_START() - lu_code_start(code);
    if (frame->owner != LU_FRAME_KERNEL_HIDDEN) {
        lu_traceback_here(frame, lasti);
    }
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RAISE)) {
        SAVE_FRAME();
        lu_monitor_raise(ts, frame, lu_code_start(code) + lasti);
    }
exception_unwind: {
    /* The handler of the instruction the loop is at (RERAISE and a bare
       raise arrive here without a new traceback entry), or the caller's
       turn. */
    int level, handler, push_lasti;
    int offset = (int)(next_instr - lu_code_start(code)) - 1;
    if (lu_exception_handler(code, offset, &level, &handler, &push_lasti)) {
        PyObject **new_top = localsplus + code->co_nlocalsplus + level;
        while (sp > new_top) {
            Py_XDECREF(*--sp);
        }
        if (push_lasti) {
            PUSH(lasti <= 256 ? lu_small_int((int)lasti) : PyLong_FromLong((long)lasti));
        }
        PUSH(PyErr_GetRaisedException());
        next_instr = lu_code_start(code) + handler;
        reraise_at = NULL;
        if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_EXCEPTION_HANDLED)) {
            SAVE_FRAME();
            if (lu_monitor_handled(ts, frame, next_instr, TOP()) < 0) {
                goto exception_unwind;
            }
        }
        DISPATCH();
    }
    /* The frame ends at the instruction that last raised in it: a RERAISE out of
       a cleanup handler restores that (f_lasti, f_lineno of a dead frame). */
    if (lasti != offset) {
        next_instr = lu_code_start(code) + lasti + 1;
    }
    SAVE_FRAME();
    if (reraise_at != NULL) {
        frame->instr = reraise_at;
    }
    if (!lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_PY_UNWIND)) {
        lu_monitor_unwind(ts, frame, INSTR_START());
    }
    struct lu_frame *caller = frame->previous;
    if (frame->owner == LU_FRAME_GENERATOR) {
        lu_gen_finish(ts, frame);
    }
    else {
        lu_frame_pop(ts, frame);
    }
    if (caller->owner == LU_FRAME_C_ENTRY) {
        ts->current_frame = (struct _PyInterpreterFrame *)caller->previous;
        return NULL;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)caller;
    frame = caller;
    LOAD_FRAME();
    goto error;
}
}

PyObject *
lu_eval(PyThreadState *ts, struct lu_frame *frame)
{
    /* C may call Python with an exception pending (an error path that looks
       up a dict or drops an object), and the loop reads a set indicator as a
       failure: the exception waits outside the call and is set again when
       the call succeeds. */
    PyObject *pending = ts->current_exception;
    ts->current_exception = NULL;
    PyObject *result;
    if (!(ts->current_frame != NULL && lu_jit_on() && lu_jit_run(ts, frame, &result))) {
        result = eval_frame(ts, frame, EVAL_RUN);
    }
    if (result == LU_EVAL_PARKED) {
        result = lu_sched_home(ts);
    }
    if (pending != NULL && result != NULL) {
        PyErr_SetRaisedException(pending);
    }
    else {
        Py_XDECREF(pending);
    }
    return result;
}

PyObject *
lu_eval_continue(PyThreadState *ts, int error)
{
    return eval_frame(ts, NULL, error ? EVAL_CONTINUE_ERROR : EVAL_CONTINUE);
}

PyObject *
lu_resume(PyThreadState *ts)
{
    return eval_frame(ts, NULL, EVAL_RESUME);
}

PyObject *
_PyEval_EvalFrameDefault(PyThreadState *ts, struct _PyInterpreterFrame *f, int throwflag)
{
    PyObject *result = eval_frame(ts, (struct lu_frame *)f, throwflag ? EVAL_THROW : EVAL_RUN);
    return result == LU_EVAL_PARKED ? lu_sched_home(ts) : result;
}

PyObject *
PyEval_EvalCode(PyObject *co, PyObject *globals, PyObject *locals)
{
    PyThreadState *ts = PyThreadState_Get();
    if (locals == NULL) {
        locals = globals;
    }
    if (lu_ns_lookup(globals, LU_ID(__builtins__)) == NULL
        && lu_ns_insert(globals, LU_ID(__builtins__), ts->interp->builtins) < 0) {
        return NULL;
    }
    PyObject *func = lu_function_new(co, globals);
    if (func == NULL) {
        return NULL;
    }
    struct lu_frame *f = lu_frame_push(ts, (PyFunctionObject *)func, locals);
    Py_DECREF(func);
    if (f == NULL) {
        return NULL;
    }
    /* Called with a frame running, this is C calling Python: a nested eval. */
    struct lu_task *t = (struct lu_task *)ts;
    int nested = ts->current_frame != NULL;
    PyObject *result;
    t->pin += nested;
    if (lu_enter_frame(ts, f, 0, &result)) {
        result = lu_eval(ts, f);
    }
    t->pin -= nested;
    return result;
}

/* The legacy entry (Python/ceval.c): a function made from the parts, called
   with `locals`. Keywords come as name/value pairs. */
PyObject *
PyEval_EvalCodeEx(PyObject *co, PyObject *globals, PyObject *locals,
                  PyObject *const *args, int argcount, PyObject *const *kws, int kwcount,
                  PyObject *const *defs, int defcount, PyObject *kwdefs, PyObject *closure)
{
    if (!PyDict_Check(globals)) {
        PyErr_BadInternalCall();
        return NULL;
    }
    PyFunctionObject *func = (PyFunctionObject *)lu_function_new(co, globals);
    if (func == NULL) {
        return NULL;
    }
    Py_SETREF(func->func_qualname, Py_NewRef(((PyCodeObject *)co)->co_name));
    func->func_defaults = PyTuple_New(defcount);
    PyObject *kwnames = kwcount ? PyTuple_New(kwcount) : NULL;
    PyObject **owned = PyMem_Malloc((argcount + kwcount + 1) * sizeof(PyObject *));
    if (func->func_defaults == NULL || (kwcount && kwnames == NULL) || owned == NULL) {
        Py_DECREF(func);
        Py_XDECREF(kwnames);
        PyMem_Free(owned);
        return PyErr_NoMemory();
    }
    for (int i = 0; i < defcount; i++) {
        PyTuple_SET_ITEM(func->func_defaults, i, Py_NewRef(defs[i]));
    }
    func->func_kwdefaults = Py_XNewRef(kwdefs);
    func->func_closure = Py_XNewRef(closure);
    for (int i = 0; i < argcount; i++) {
        owned[i] = Py_NewRef(args[i]);
    }
    for (int i = 0; i < kwcount; i++) {
        PyTuple_SET_ITEM(kwnames, i, Py_NewRef(kws[2 * i]));
        owned[argcount + i] = Py_NewRef(kws[2 * i + 1]);
    }
    PyThreadState *ts = PyThreadState_Get();
    struct lu_frame *f = lu_frame_call(ts, func, owned, argcount, kwnames);
    PyMem_Free(owned);
    Py_XDECREF(kwnames);
    Py_DECREF(func);
    if (f == NULL) {
        return NULL;
    }
    f->locals = Py_NewRef(locals != NULL ? locals : globals);
    struct lu_task *t = (struct lu_task *)ts;
    PyObject *result;
    if (lu_enter_frame(ts, f, 0, &result)) {
        t->pin++;
        result = lu_eval(ts, f);
        t->pin--;
    }
    return result;
}
