/* The C that compiled code calls (runtime/jit/target.h LU_HELPERS). Each
   does what the interpreter's instruction does for the same operands: its
   native fast path, else the kernel function the interpreter would push
   (here called through a nested eval, as C callers do). None takes a
   reference it is given; each result is a new reference. */
#include "jit/target.h"
#include "interp/exceptions.h"
#include "interp/gen.h"
#include "prim/prim.h"

#include <pthread.h>
#include <string.h>

/* --- representation and references --- */

PyObject *
lu_jit_box_int(int64_t v)
{
    return PyLong_FromLongLong(v);
}

/* An exact int past one digit as a machine word, or INT64_MIN (which
   leaves) when it does not fit. */
int64_t
lu_jit_unbox_wide(PyObject *o)
{
    int overflow;
    long long x = PyLong_AsLongLongAndOverflow(o, &overflow);
    return overflow ? INT64_MIN : x;
}

PyObject *
lu_jit_box_float(double v)
{
    return PyFloat_FromDouble(v);
}

void
lu_jit_decref(PyObject *o)
{
    Py_XDECREF(o);
}

void
lu_jit_store_local(struct lu_frame *f, intptr_t i, PyObject *v)
{
    PyObject *old = f->localsplus[i];
    f->localsplus[i] = v;
    Py_XDECREF(old);
}

int
lu_jit_truth(PyObject *v)
{
    int t = lu_truth_exact(v);
    return t >= 0 ? t : lu_is_true(v);
}

/* --- containers --- */

PyObject *
lu_jit_list_item(PyObject *list, int64_t i)
{
    return i >= 0 ? lu_list_item_ref((PyListObject *)list, (Py_ssize_t)i) : NULL;
}

/* t[i] for -len <= i < len, else NULL (which leaves). */
PyObject *
lu_jit_tuple_item(PyObject *t, int64_t i)
{
    i += i < 0 ? Py_SIZE(t) : 0;
    return i >= 0 && i < Py_SIZE(t) ? Py_NewRef(PyTuple_GET_ITEM(t, i)) : NULL;
}

/* list[i] = v for 0 <= i < len: 1, else 0 and nothing stored. */
int
lu_jit_list_store(PyObject *list, int64_t i, PyObject *v)
{
    PyObject *old = NULL;
    if (i < 0) {
        return 0;
    }
    Py_BEGIN_CRITICAL_SECTION(list);
    if (i < Py_SIZE(list)) {
        old = ((PyListObject *)list)->ob_item[i];
        _Py_atomic_store_ptr_release(&((PyListObject *)list)->ob_item[i], Py_NewRef(v));
    }
    Py_END_CRITICAL_SECTION();
    if (old == NULL) {
        return 0;
    }
    Py_DECREF(old);
    return 1;
}

static int
h_list_append(PyObject *list, PyObject *v)
{
    int err;
    Py_BEGIN_CRITICAL_SECTION(list);    /* as list_append's primitive commits */
    err = lu_list_append(list, v);
    Py_END_CRITICAL_SECTION();
    return err;
}

/* LIST_APPEND: a comprehension's or display's own list, which no other
   thread sees yet, appended to as the interpreter appends. */
static int
h_list_push(PyObject *list, PyObject *v)
{
    return lu_list_append(list, v);
}

static PyObject *
h_build_list(PyObject **argv, intptr_t n)
{
    PyObject *l = PyList_New(n);
    if (l != NULL) {
        for (intptr_t i = 0; i < n; i++) {
            PyList_SET_ITEM(l, i, Py_NewRef(argv[i]));
        }
    }
    return l;
}

static PyObject *
h_build_tuple(PyObject **argv, intptr_t n)
{
    PyObject *t = PyTuple_New(n);
    if (t != NULL) {
        for (intptr_t i = 0; i < n; i++) {
            PyTuple_SET_ITEM(t, i, Py_NewRef(argv[i]));
        }
    }
    return t;
}

/* The oparg items of BUILD_SET or BUILD_MAP as one tuple, for the kernel's builder. */
static PyObject *
build_kernel(enum lu_kernel_id kid, PyObject **argv, intptr_t n)
{
    PyObject *items = h_build_tuple(argv, n);
    PyObject *r = items != NULL ? lu_call_kernel(kid, &items, 1) : NULL;
    Py_XDECREF(items);
    return r;
}

static PyObject *
h_build_set(PyObject **argv, intptr_t n)
{
    return build_kernel(LU_K_build_set, argv, n);
}

/* BUILD_MAP of n / 2 key, value pairs: str keys go straight in. */
static PyObject *
h_build_map(PyObject **argv, intptr_t n)
{
    for (intptr_t i = 0; i < n; i += 2) {
        if (!PyUnicode_CheckExact(argv[i])) {
            return build_kernel(LU_K_build_map, argv, n);
        }
    }
    PyObject *map = PyDict_New();
    for (intptr_t i = 0; map != NULL && i < n; i += 2) {
        if (lu_ns_insert(map, argv[i], argv[i + 1]) < 0) {
            Py_CLEAR(map);
        }
    }
    return map;
}

static int
h_set_add(PyObject *set, PyObject *v)
{
    PyObject *args[2] = { set, v };
    PyObject *r = lu_call_kernel(LU_K_set_add, args, 2);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* An exact list's items as a tuple, read under its lock: a tuple display's
   list (INTRINSIC_LIST_TO_TUPLE, n < 0), or a list UNPACK_SEQUENCE unpacks
   into n items, declined when it has another number. */
static PyObject *
h_list_to_tuple(PyObject *list, int64_t n)
{
    if (n < 0) {
        return lu_args_as_tuple(Py_None, list);     /* a display's own list: no other thread sees it */
    }
    PyObject *t;
    Py_BEGIN_CRITICAL_SECTION(list);
    t = Py_SIZE(list) == n ? lu_args_as_tuple(Py_None, list) : LU_JIT_DECLINED;
    Py_END_CRITICAL_SECTION();
    return t;
}

/* A **kwargs parameter's dict, for a call without keywords. */
static PyObject *
h_new_dict(PyObject **argv, intptr_t n)
{
    return PyDict_New();
}

/* A primitive that never runs Python (LU_METH_NOPY): argv[0] is the
   primitive, then its arguments. */
static PyObject *
h_prim(PyObject **argv, intptr_t n)
{
    PyCFunctionObject *f = (PyCFunctionObject *)argv[0];
    return ((PyCFunctionFast)(void (*)(void))f->m_ml->ml_meth)(f->m_self, argv + 1, n - 1);
}

/* MAKE_FUNCTION; function watchers may run Python. */
static PyObject *
h_make_function(PyObject *code, PyObject *globals)
{
    PyObject *func = lu_function_new(code, globals);
    if (func != NULL) {
        lu_function_set_version((PyFunctionObject *)func);
    }
    return func;
}

static void
h_set_function_attribute(PyObject *func, PyObject *attr, int64_t which)
{
    lu_function_set_attribute((PyFunctionObject *)func, (int)which, Py_NewRef(attr));
}

/* STORE_DEREF */
static void
h_cell_store(PyObject *cell, PyObject *v)
{
    Py_XDECREF(lu_cell_exchange(cell, Py_NewRef(v)));
}

/* --- attributes --- */

/* LOAD_ATTR_WITH_HINT: the instance dict's entry `index`, while the type
   has `version` and the entry's key is `name`; NULL otherwise. */
PyObject *
lu_jit_attr_load(PyObject *owner, int64_t version, int64_t index, PyObject *name)
{
    if (Py_TYPE(owner)->tp_version_tag != (uint32_t)version) {
        return NULL;
    }
    PyObject *dict = LU_MANAGED_DICT(owner);
    if (dict == NULL || !PyDict_CheckExact(dict)) {
        return NULL;
    }
    PyDictKeysObject *dk = __atomic_load_n(&((PyDictObject *)dict)->ma_keys, __ATOMIC_ACQUIRE);
    if (!lu_dk_is_unicode(dk) || index >= dk->dk_nentries || lu_dk_key(dk, index) != name) {
        return NULL;
    }
    PyObject *v = lu_try_xget_ref(lu_dk_value_slot(dk, index));
    if (v != NULL && dk != __atomic_load_n(&((PyDictObject *)dict)->ma_keys, __ATOMIC_ACQUIRE)) {
        Py_CLEAR(v);
    }
    return v;
}

/* STORE_ATTR_WITH_HINT: 1 stored, 0 when the type or dict is not what was
   compiled (the interpreter then stores). */
static int
h_attr_store(PyObject *owner, PyObject *v, PyObject *name, int64_t version)
{
    if (Py_TYPE(owner)->tp_version_tag != (uint32_t)version) {
        return 0;
    }
    PyObject *dict = LU_MANAGED_DICT(owner);
    if (dict == NULL || !PyDict_CheckExact(dict)) {
        return 0;
    }
    if (lu_ns_store(dict, name, v) < 0) {
        PyErr_Clear();          /* the interpreter stores again and raises */
        return 0;
    }
    return 1;
}

/* LOAD_ATTR_SLOT and STORE_ATTR_SLOT, as the interpreter runs them: NULL
   when the slot is empty or its value is being replaced. */
static PyObject *
h_slot_load(PyObject *owner, int64_t offset)
{
    return lu_try_xget_ref((PyObject **)((char *)owner + offset));
}

static void
h_slot_store(PyObject *owner, int64_t offset, PyObject *v)
{
    Py_XDECREF(lu_member_exchange(owner, (PyObject **)((char *)owner + offset), v));
}

/* LOAD_ATTR_METHOD_WITH_VALUES: the instance's dict does not shadow name. */
static int
h_dict_lacks(PyObject *owner, PyObject *name)
{
    PyObject *dict = LU_MANAGED_DICT(owner);
    return dict == NULL || (PyDict_CheckExact(dict) && lu_ns_lookup(dict, name) == NULL);
}

static PyObject *
h_cell_load(PyObject *cell)
{
    return PyCell_Check(cell) ? lu_cell_get(cell) : NULL;
}

static PyObject *
h_load_attr(PyObject *owner, PyObject *name)
{
    PyObject *native = lu_getattr_native(owner, name);
    if (native != NULL || PyErr_Occurred()) {
        return native;
    }
    struct lu_task *t = (struct lu_task *)PyThreadState_Get();
    t->pin++;
    PyObject *v = PyObject_GetAttr(owner, name);
    t->pin--;
    return v;
}

/* A generic site with a polymorphic cache records what it meets. */
static PyObject *
h_load_attr_pic(PyObject *owner, PyObject *name, int64_t pic)
{
    if (pic != 0) {
        lu_jit_pic_note((struct lu_pic *)(intptr_t)pic, owner, name);
    }
    return h_load_attr(owner, name);
}

static PyObject *
h_load_method(PyObject *owner, PyObject *name, int64_t site)
{
    return h_load_attr_pic(owner, name, site);
}

static int
h_store_attr(PyObject *owner, PyObject *name, PyObject *v)
{
    int done = lu_setattr_native(owner, name, v);
    if (done != 0) {
        return done < 0 ? -1 : 0;
    }
    struct lu_task *t = (struct lu_task *)PyThreadState_Get();
    t->pin++;
    int r = PyObject_SetAttr(owner, name, v);
    t->pin--;
    return r;
}

/* --- operators --- */

/* Answers per thread, by type version: a version names one type in one
   state, so an entry holds while its type keeps it. */
#define EQ_SEEN 64
static __thread struct {
    unsigned int version;
    int answer;
} eq_seen[EQ_SEEN];

static int
uses_object_eq(PyObject *o)
{
    PyTypeObject *t = Py_TYPE(o);
    unsigned int version = t->tp_version_tag;
    unsigned int at = version % EQ_SEEN;
    if (version != 0 && eq_seen[at].version == version) {
        return eq_seen[at].answer;
    }
    int answer = _PyType_Lookup(t, LU_ID(__eq__)) == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__eq__))
        && _PyType_Lookup(t, LU_ID(__ne__)) == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__ne__));
    if (version != 0 && version == __atomic_load_n(&t->tp_version_tag, __ATOMIC_ACQUIRE)) {
        eq_seen[at].version = version;
        eq_seen[at].answer = answer;
    }
    return answer;
}

/* Whether `a == b` (or !=) is identity: both types keep object's __eq__
   and __ne__, so compare reaches its `a is b` fallback (operators.compare). */
static int
h_compare_ideq(PyObject *a, PyObject *b, int64_t op)
{
    return uses_object_eq(a) && uses_object_eq(b);
}

static PyObject *
h_binary_op(PyObject *a, PyObject *b, int64_t op)
{
    if (op == NB_SUBSCR) {
        PyObject *item = lu_subscr_native(a, b);
        if (item != NULL || PyErr_Occurred()) {
            return item;
        }
        PyObject *args[2] = { a, b };
        return lu_call_kernel(LU_K_getitem, args, 2);
    }
    int bitwise = op == NB_AND || op == NB_OR || op == NB_XOR || op == NB_INPLACE_AND
        || op == NB_INPLACE_OR || op == NB_INPLACE_XOR;
    if ((PyLong_CheckExact(a) || (!bitwise && PyBool_Check(a)))
        && (PyLong_CheckExact(b) || (!bitwise && PyBool_Check(b)))) {
        PyObject *r = lu_long_binary((int)op, a, b);
        if (r != Py_NotImplemented) {
            return r;
        }
        Py_DECREF(r);
    }
    else if ((op == NB_ADD || op == NB_INPLACE_ADD) && PyUnicode_CheckExact(a) && PyUnicode_CheckExact(b)) {
        return lu_str_concat(a, b);
    }
    PyObject *args[3] = { a, b, lu_small_int((int)op) };
    return lu_call_kernel(LU_K_binary_op, args, 3);
}

static PyObject *
h_compare_op(PyObject *a, PyObject *b, int64_t oparg)
{
    int r = lu_compare_native(a, b, (int)oparg);
    if (r < 0) {
        PyObject *args[3] = { a, b, lu_small_int((int)oparg) };
        return lu_call_kernel(LU_K_compare, args, 3);
    }
    return Py_NewRef(r ? Py_True : Py_False);
}

static PyObject *
h_unary(PyObject *v, int64_t which)
{
    return lu_call_kernel(which ? LU_K_invert : LU_K_negative, &v, 1);
}

static int
h_setitem(PyObject *container, PyObject *sub, PyObject *v)
{
    if (PyDict_CheckExact(container)) {
        int done = lu_dict_put_native(container, sub, v);
        if (done == -2) {
            return -1;
        }
        if (done == 1) {
            return 0;
        }
    }
    PyObject *args[3] = { container, sub, v };
    PyObject *r = lu_call_kernel(LU_K_setitem, args, 3);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

static PyObject *
h_contains(PyObject *item, PyObject *container, int64_t negate)
{
    PyObject *args[3] = { container, item, lu_small_int((int)negate) };
    return lu_call_kernel(LU_K_contains, args, 3);
}

/* --- iteration --- */

static PyObject *
h_get_iter(PyObject *v)
{
    PyObject *it = lu_get_iter_exact(v);
    return it != NULL || PyErr_Occurred() ? it : lu_call_kernel(LU_K_get_iter, &v, 1);
}

/* The next item, LU_MISSING when exhausted, or NULL with an exception. A
   generator runs as the interpreter's FOR_ITER resumes it: its return is
   the loop's end, with no StopIteration. */
static PyObject *
h_for_iter(PyObject *it)
{
    PyTypeObject *t = Py_TYPE(it);
    if (t == &PyListIter_Type || t == &PyTupleIter_Type || t == &PyRangeIter_Type) {
        PyObject *v = t == &PyRangeIter_Type ? lu_rangeiter_next(it) : lu_seqiter_next(it);
        return v != NULL ? v : PyErr_Occurred() ? NULL : Py_NewRef(LU_MISSING);
    }
    struct lu_task *task = lu_current_task();
    if (t == &PyGen_Type && task->ts.interp->eval_frame == NULL) {
        struct lu_frame *f = lu_gen_resume(&task->ts, it, NULL);
        if (f == NULL) {
            return PyErr_Occurred() ? NULL : Py_NewRef(LU_MISSING);
        }
        f->ret = LU_RETURN_FOR_ITER;
        task->pin++;
        PyObject *v = lu_eval(&task->ts, f);
        task->pin--;
        return v;
    }
    return lu_call_kernel(LU_K_next_item, &it, 1);
}

static PyObject *
h_seqiter_next(PyObject *it)
{
    return lu_seqiter_next(it);
}

/* iter(range(start, stop, step)) for C longs, as range_iter computes the
   length; NULL when the values do not fit (the interpreter makes it). */
static PyObject *
h_range_iter(int64_t start, int64_t stop, int64_t step)
{
    long len = 0, last;
    if (step > 0 && start < stop) {
        len = (long)(((unsigned long)stop - (unsigned long)start - 1) / (unsigned long)step + 1);
    }
    else if (step < 0 && start > stop) {
        len = (long)(((unsigned long)start - (unsigned long)stop - 1) / (0 - (unsigned long)step) + 1);
    }
    if (len < 0 || (len > 0 && (__builtin_mul_overflow(len - 1, step, &last)
                                || __builtin_add_overflow(start, last, &last)
                                || __builtin_add_overflow(last, step, &last)))) {
        return NULL;
    }
    rangeiterobject *it = (rangeiterobject *)lu_object_new(&PyRangeIter_Type, sizeof(rangeiterobject));
    if (it == NULL) {
        PyErr_Clear();
        return NULL;
    }
    it->start = start;
    it->step = step;
    it->len = len;
    return (PyObject *)it;
}

/* --- calls --- */

/* How much C stack a call from compiled code leaves for the callee's
   activation: below it the interpreter makes the call, without
   recursing (DESIGN 5.2). */
#define STACK_RESERVE (192 * 1024)

static __thread char *stack_floor;

static int
stack_low(void)
{
    char here;
    if (stack_floor == NULL) {
        pthread_attr_t attr;
        void *addr;
        size_t size;
        if (pthread_getattr_np(pthread_self(), &attr) != 0) {
            return 1;
        }
        pthread_attr_getstack(&attr, &addr, &size);
        pthread_attr_destroy(&attr);
        stack_floor = (char *)addr + STACK_RESERVE;
    }
    return &here < stack_floor;
}

/* A Python function's call, as CALL makes it: the frame is pushed and
   bound (by plain stores when the arguments are exactly its positional
   parameters), then run by lu_eval, which enters a compiled unit directly. */
static PyObject *
call_function(struct lu_task *t, PyFunctionObject *func, PyObject *const *args, Py_ssize_t nargs)
{
    PyCodeObject *co = (PyCodeObject *)func->func_code;
    struct lu_frame *f;
    if (co->co_argcount == nargs && co->co_kwonlyargcount == 0
        && !(co->co_flags & (CO_VARARGS | CO_VARKEYWORDS))) {
        if ((f = lu_frame_push(&t->ts, func, NULL)) == NULL) {
            return NULL;
        }
        for (Py_ssize_t i = 0; i < nargs; i++) {
            f->localsplus[i] = Py_NewRef(args[i]);
        }
    }
    else {
        for (Py_ssize_t i = 0; i < nargs; i++) {
            Py_INCREF(args[i]);
        }
        if ((f = lu_frame_call(&t->ts, func, (PyObject **)args, nargs, NULL)) == NULL) {
            return NULL;
        }
    }
    t->pin++;
    PyObject *r = lu_eval(&t->ts, f);
    t->pin--;
    return r;
}

/* Whether the interpreter makes a call of callable: the C stack is too
   deep to recurse, or it is a primitive that hands the interpreter a frame
   to run (a generator's resumption). */
static int
declines(PyObject *callable)
{
    return stack_low() || (Py_IS_TYPE(callable, &PyCFunction_Type)
                           && (((PyCFunctionObject *)callable)->m_ml->ml_flags & LU_METH_FRAME));
}

/* The interpreter polls after a call: a signal the callee tripped is
   handled here, and its exception is the call's. */
static PyObject *
polled(struct lu_task *t, PyObject *r)
{
    if (r != NULL && (__atomic_load_n(&t->carrier->poll, __ATOMIC_RELAXED) & LU_POLL_EVENTS_MASK)
        && lu_poll_slow(&t->ts) < 0) {
        Py_CLEAR(r);
    }
    return r;
}

/* A call through lu_call, which C callers make. */
static PyObject *
call_generic(PyObject *callable, PyObject *const *args, size_t nargs, PyObject *kwnames)
{
    struct lu_task *t = lu_current_task();
    t->pin++;
    PyObject *r = lu_call(callable, args, nargs, kwnames);
    t->pin--;
    return polled(t, r);
}

/* argv: the callable, self or NULL, then the arguments. */
static PyObject *
h_call(PyObject **argv, intptr_t n)
{
    if (declines(argv[0])) {
        return LU_JIT_DECLINED;
    }
    struct lu_task *t = lu_current_task();
    PyObject *callable = argv[0];
    PyObject *const *args = argv[1] != NULL ? argv + 1 : argv + 2;
    size_t nargs = (size_t)(argv[1] != NULL ? n - 1 : n - 2);
    PyObject *target, *self, *r = NULL;
    if (t->ts.interp->eval_frame == NULL) {
        if (PyFunction_Check(callable)) {
            r = call_function(t, (PyFunctionObject *)callable, args, (Py_ssize_t)nargs);
            goto called;
        }
        if (Py_IS_TYPE(callable, &PyMethod_Type) && argv[1] == NULL
            && PyFunction_Check(PyMethod_GET_FUNCTION(callable))) {
            /* A bound method: its self goes in the free word before the arguments. */
            argv[1] = PyMethod_GET_SELF(callable);
            r = call_function(t, (PyFunctionObject *)PyMethod_GET_FUNCTION(callable), argv + 1,
                              (Py_ssize_t)nargs + 1);
            argv[1] = NULL;
            goto called;
        }
    }
    /* A kernel function C answers runs no frame, as from the interpreter. */
    int kernel = lu_kernel_target(callable, args, (Py_ssize_t)nargs, &target, &self);
    if (kernel < 0) {
        return NULL;
    }
    if (kernel && self == NULL) {
        r = lu_native_call(&t->ts, target, args, (Py_ssize_t)nargs);
    }
    if (kernel && r == NULL && !PyErr_Occurred() && t->ts.interp->eval_frame == NULL
        && PyFunction_Check(target) && (self == NULL || argv[1] == NULL)) {
        /* The kernel function C does not answer runs as the interpreter
           calls it: its frame pushed directly, self in the free word. */
        if (self == NULL) {
            r = call_function(t, (PyFunctionObject *)target, args, (Py_ssize_t)nargs);
        }
        else {
            argv[1] = self;
            r = call_function(t, (PyFunctionObject *)target, argv + 1, (Py_ssize_t)nargs + 1);
            argv[1] = NULL;
        }
        goto called;
    }
    if (r == NULL && !PyErr_Occurred()) {
        return call_generic(callable, args, nargs, NULL);
    }
called:
    return polled(t, r);
}

/* CALL_KW: argv as h_call's, then the keyword names. */
static PyObject *
h_call_kw(PyObject **argv, intptr_t n)
{
    if (declines(argv[0])) {
        return LU_JIT_DECLINED;
    }
    PyObject *kwnames = argv[n - 1];
    PyObject *const *args = argv[1] != NULL ? argv + 1 : argv + 2;
    size_t total = (size_t)(argv[1] != NULL ? n - 2 : n - 3);
    return call_generic(argv[0], args, total - (size_t)PyTuple_GET_SIZE(kwnames), kwnames);
}

/* CALL_FUNCTION_EX: callable(*callargs, **kwargs), kwargs NULL without. */
static PyObject *
h_call_ex(PyObject *callable, PyObject *callargs, PyObject *kwargs)
{
    if (declines(callable)) {
        return LU_JIT_DECLINED;
    }
    PyObject *t = lu_args_as_tuple(callable, callargs), *kwnames, *r = NULL;
    Py_ssize_t npos;
    PyObject **args = t != NULL ? lu_unpack_call(t, kwargs, &npos, &kwnames) : NULL;
    Py_XDECREF(t);
    if (args != NULL) {
        r = call_generic(callable, args, (size_t)npos, kwnames);
        Py_ssize_t total = npos + (kwnames != NULL ? PyTuple_GET_SIZE(kwnames) : 0);
        for (Py_ssize_t i = 0; i < total; i++) {
            Py_DECREF(args[i]);
        }
        Py_XDECREF(kwnames);
        PyMem_Free(args - 1);
    }
    return r;
}

/* DICT_MERGE: a call's **update into its keyword dict. */
static int
h_dict_merge(PyObject *func, PyObject *dict, PyObject *update)
{
    if (PyDict_CheckExact(update) && lu_dict_str_keys(update) && lu_dict_str_keys(dict)) {
        return lu_dict_merge(func, dict, update);
    }
    char buf[256];
    PyObject *args[3] = { dict, update, PyUnicode_FromString(lu_function_str(func, buf, sizeof buf)) };
    PyObject *r = args[2] != NULL ? lu_call_kernel(LU_K_dict_merge, args, 3) : NULL;
    Py_XDECREF(args[2]);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* BINARY_SLICE: container[start:stop]. Unless `generic`, it declines what
   needs the kernel, which runs Python. */
static PyObject *
h_binary_slice(PyObject *container, PyObject *start, PyObject *stop, int64_t generic)
{
    PyObject *r = lu_slice_native(container, start, stop, NULL);
    if (r != NULL || PyErr_Occurred()) {
        return r;
    }
    if (!generic) {
        return LU_JIT_DECLINED;
    }
    PyObject *args[2] = { container, PySlice_New(start, stop, NULL) };
    r = args[1] != NULL ? lu_call_kernel(LU_K_getitem, args, 2) : NULL;
    Py_XDECREF(args[1]);
    return r;
}

/* STORE_SLICE: container[start:stop] = v. */
static int
h_store_slice(PyObject *v, PyObject *container, PyObject *start, PyObject *stop)
{
    PyObject *slice = PySlice_New(start, stop, NULL);
    int err = slice != NULL ? h_setitem(container, slice, v) : -1;
    Py_XDECREF(slice);
    return err;
}

/* DELETE_SUBSCR */
static int
h_delitem(PyObject *container, PyObject *sub)
{
    if (PyDict_CheckExact(container) && lu_dict_del_native(container, sub) == 1) {
        return 0;
    }
    PyObject *args[2] = { container, sub };
    PyObject *r = lu_call_kernel(LU_K_delitem, args, 2);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* LIST_EXTEND; an exact tuple or list source runs no Python (list_extend_seq). */
static int
h_list_extend(PyObject *list, PyObject *iterable)
{
    if (PyTuple_CheckExact(iterable) || PyList_CheckExact(iterable)) {
        /* A list source is read as subscripts are: another thread may resize it. */
        for (Py_ssize_t i = 0; i < Py_SIZE(iterable); i++) {
            PyObject *v = PyTuple_CheckExact(iterable) ? Py_NewRef(PyTuple_GET_ITEM(iterable, i))
                                                       : lu_list_item_ref((PyListObject *)iterable, i);
            if (v == NULL) {
                break;
            }
            int err = lu_list_append(list, v);
            Py_DECREF(v);
            if (err < 0) {
                return -1;
            }
        }
        return 0;
    }
    PyObject *args[2] = { list, iterable };
    PyObject *r = lu_call_kernel(LU_K_list_extend, args, 2);
    Py_XDECREF(r);
    return r == NULL ? -1 : 0;
}

/* call_special(descr, obj, *args) for a function, wrapper or method
   descriptor, which it calls unbound: descr(obj, *args). Anything else is
   bound through its __get__ first, which the interpreter does. */
static PyObject *
h_call_special(PyObject **argv, intptr_t n)
{
    PyTypeObject *t = Py_TYPE(argv[0]);
    if (t != &PyFunction_Type && t != &PyWrapperDescr_Type && t != &PyMethodDescr_Type) {
        return LU_JIT_DECLINED;
    }
    return h_call(argv, n);     /* obj as the self word: args are argv + 1 */
}

/* --- exceptions --- */

/* The landing of an exception a handler of the unit takes (ssa_build.c
   catch_here): the exception, taken, after the root frame's traceback
   entry at code unit lasti (none when -1). NULL with the exception still
   set where the interpreter unwinds instead: a tool watches exceptions
   raised or handled, or a call kept inlined frames pushed, so that the
   root is not the task's current frame. */
static PyObject *
h_catch(PyObject *code, int64_t lasti)
{
    PyThreadState *ts = PyThreadState_Get();
    struct lu_frame *f = (struct lu_frame *)ts->current_frame;
    if (f->code != (PyCodeObject *)code || !lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_RAISE)
        || !lu_no_tools_for_global_event(ts, PY_MONITORING_EVENT_EXCEPTION_HANDLED)) {
        return NULL;
    }
    if (lasti >= 0) {
        lu_traceback_here(f, lasti);
    }
    return PyErr_GetRaisedException();
}

/* PUSH_EXC_INFO: exc becomes the handled exception; the one before it (or None). */
static PyObject *
h_push_exc_info(PyObject *exc)
{
    _PyErr_StackItem *info = PyThreadState_Get()->exc_info;
    PyObject *prev = info->exc_value;
    info->exc_value = Py_NewRef(exc);
    return prev != NULL ? prev : Py_NewRef(Py_None);
}

/* POP_EXCEPT: prev (None for none) is the handled exception again. */
static void
h_pop_except(PyObject *prev)
{
    _PyErr_StackItem *info = PyThreadState_Get()->exc_info;
    PyObject *old = info->exc_value;
    info->exc_value = prev == Py_None ? NULL : Py_NewRef(prev);
    Py_XDECREF(old);
}

static PyObject *
h_exc_match(PyObject *exc, PyObject *type)
{
    int match = lu_exc_match(exc, type);
    return match < 0 ? NULL : match ? Py_True : Py_False;
}

/* RAISE_VARARGS with an exception, and its cause or MISSING: always NULL. */
static PyObject *
h_raise(PyObject *exc, PyObject *cause)
{
    if (lu_raise_native(exc, cause)) {
        return NULL;
    }
    PyObject *args[2] = { exc, cause };
    PyObject *r = lu_call_kernel(LU_K_do_raise, args, 2);
    if (r != NULL) {
        Py_DECREF(r);
        lu_err_format(PyExc_SystemError, "do_raise returned");
    }
    return NULL;
}

const struct lu_helper lu_helpers[H_COUNT] = {
    [H_box_int] = { (void *)lu_jit_box_int, "box_int", REP_OBJ, 0, 1 },
    [H_box_float] = { (void *)lu_jit_box_float, "box_float", REP_OBJ, 0, 1 },
    [H_decref] = { (void *)lu_jit_decref, "decref", REP_NONE, 0, 1 },
    [H_store_local] = { (void *)lu_jit_store_local, "store_local", REP_NONE, 0, 3 },
    [H_truth] = { (void *)lu_jit_truth, "truth", REP_BIT, IF_RAISE | IF_PY, 1 },
    [H_list_item] = { (void *)lu_jit_list_item, "list_item", REP_OBJ, 0, 2 },
    [H_list_store] = { (void *)lu_jit_list_store, "list_store", REP_BIT, 0, 3 },
    [H_attr_load] = { (void *)lu_jit_attr_load, "attr_load", REP_OBJ, 0, 4 },
    [H_attr_store] = { (void *)h_attr_store, "attr_store", REP_BIT, 0, 4 },
    [H_slot_load] = { (void *)h_slot_load, "slot_load", REP_OBJ, 0, 2 },
    [H_slot_store] = { (void *)h_slot_store, "slot_store", REP_NONE, 0, 3 },
    [H_dict_lacks] = { (void *)h_dict_lacks, "dict_lacks", REP_BIT, 0, 2 },
    [H_cell_load] = { (void *)h_cell_load, "cell_load", REP_OBJ, 0, 1 },
    [H_compare_ideq] = { (void *)h_compare_ideq, "compare_ideq", REP_BIT, 0, 3 },
    [H_str_concat] = { (void *)lu_str_concat, "str_concat", REP_OBJ, IF_RAISE, 2 },
    [H_binary_op] = { (void *)h_binary_op, "binary_op", REP_OBJ, IF_RAISE | IF_PY, 3 },
    [H_compare_op] = { (void *)h_compare_op, "compare_op", REP_OBJ, IF_RAISE | IF_PY, 3 },
    [H_setitem] = { (void *)h_setitem, "setitem", REP_BIT, IF_RAISE | IF_PY, 3 },
    [H_delitem] = { (void *)h_delitem, "delitem", REP_BIT, IF_RAISE | IF_PY, 2 },
    [H_list_extend] = { (void *)h_list_extend, "list_extend", REP_BIT, IF_RAISE | IF_PY, 2 },
    [H_list_extend_seq] = { (void *)h_list_extend, "list_extend_seq", REP_BIT, IF_RAISE, 2 },
    [H_load_attr] = { (void *)h_load_attr_pic, "load_attr", REP_OBJ, IF_RAISE | IF_PY, 3 },
    [H_load_method] = { (void *)h_load_method, "load_method", REP_OBJ, IF_RAISE | IF_PY, 3 },
    [H_store_attr] = { (void *)h_store_attr, "store_attr", REP_BIT, IF_RAISE | IF_PY, 3 },
    [H_call] = { (void *)h_call, "call", REP_OBJ, IF_RAISE | IF_PY, 0 },
    [H_call_special] = { (void *)h_call_special, "call_special", REP_OBJ, IF_RAISE | IF_PY, 0 },
    [H_prim] = { (void *)h_prim, "prim", REP_OBJ, IF_RAISE, 0 },
    [H_make_function] = { (void *)h_make_function, "make_function", REP_OBJ, IF_RAISE | IF_PY, 2 },
    [H_set_function_attribute] = { (void *)h_set_function_attribute, "set_function_attribute", REP_NONE, 0, 3 },
    [H_cell_store] = { (void *)h_cell_store, "cell_store", REP_NONE, 0, 2 },
    [H_get_iter] = { (void *)h_get_iter, "get_iter", REP_OBJ, IF_RAISE | IF_PY, 1 },
    [H_for_iter] = { (void *)h_for_iter, "for_iter", REP_OBJ, IF_RAISE | IF_PY, 1 },
    [H_seqiter_next] = { (void *)h_seqiter_next, "seqiter_next", REP_OBJ, 0, 1 },
    [H_range_iter] = { (void *)h_range_iter, "range_iter", REP_OBJ, 0, 3 },
    [H_unary] = { (void *)h_unary, "unary", REP_OBJ, IF_RAISE | IF_PY, 2 },
    [H_list_append] = { (void *)h_list_append, "list_append", REP_BIT, IF_RAISE, 2 },
    [H_build_list] = { (void *)h_build_list, "build_list", REP_OBJ, IF_RAISE, 0 },
    [H_build_tuple] = { (void *)h_build_tuple, "build_tuple", REP_OBJ, IF_RAISE, 0 },
    [H_new_dict] = { (void *)h_new_dict, "new_dict", REP_OBJ, IF_RAISE, 0 },
    [H_contains] = { (void *)h_contains, "contains", REP_OBJ, IF_RAISE | IF_PY, 3 },
    [H_list_push] = { (void *)h_list_push, "list_push", REP_BIT, IF_RAISE, 2 },
    [H_build_set] = { (void *)h_build_set, "build_set", REP_OBJ, IF_RAISE | IF_PY, 0 },
    [H_build_map] = { (void *)h_build_map, "build_map", REP_OBJ, IF_RAISE | IF_PY, 0 },
    [H_set_add] = { (void *)h_set_add, "set_add", REP_BIT, IF_RAISE | IF_PY, 2 },
    [H_list_to_tuple] = { (void *)h_list_to_tuple, "list_to_tuple", REP_OBJ, IF_RAISE, 2 },
    [H_get_iter_seq] = { (void *)lu_get_iter_exact, "get_iter_seq", REP_OBJ, IF_RAISE, 1 },
    [H_binary_slice] = { (void *)h_binary_slice, "binary_slice", REP_OBJ, IF_RAISE | IF_PY, 4 },
    [H_slice_native] = { (void *)h_binary_slice, "slice_native", REP_OBJ, IF_RAISE, 4 },
    [H_store_slice] = { (void *)h_store_slice, "store_slice", REP_BIT, IF_RAISE | IF_PY, 4 },
    [H_call_kw] = { (void *)h_call_kw, "call_kw", REP_OBJ, IF_RAISE | IF_PY, 0 },
    [H_call_ex] = { (void *)h_call_ex, "call_ex", REP_OBJ, IF_RAISE | IF_PY, 3 },
    [H_dict_merge] = { (void *)h_dict_merge, "dict_merge", REP_BIT, IF_RAISE | IF_PY, 3 },
    [H_catch] = { (void *)h_catch, "catch", REP_OBJ, IF_RAISE, 2 },
    [H_push_exc_info] = { (void *)h_push_exc_info, "push_exc_info", REP_OBJ, 0, 1 },
    [H_pop_except] = { (void *)h_pop_except, "pop_except", REP_NONE, 0, 1 },
    [H_exc_match] = { (void *)h_exc_match, "exc_match", REP_OBJ, IF_RAISE, 2 },
    [H_raise] = { (void *)h_raise, "raise", REP_OBJ, IF_RAISE | IF_PY, 2 },
};
