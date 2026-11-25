/* What the evaluator knows of the kernel's primitives (eval.h): a
   transfer function answers a call from the state, as a constant, a field
   of an object that does not exist, or a few instructions, where the
   operands' types let it. A primitive without one, or operands it cannot
   decide, is called (eval_call.c): one that runs no Python (LU_METH_NOPY)
   with nothing made observable but what it may release, any other as a
   call that may do anything. */
#include "jit/eval.h"

#include <string.h>

static PyTypeObject *
type_at(struct ctx *c, lu_v *args, int i)
{
    return lu_ev_type(c, args[i]);
}

/* The i-th argument's type, learned where it is not known (guarded): a
   str primitive's operand that came in from outside the unit. */
static PyTypeObject *
learned_at(struct ctx *c, lu_v *args, int i)
{
    return lu_ev_learn_type(c, args[i], i);
}

static lu_v
truth_const(struct ctx *c, int v)
{
    return lu_ev_konst(c, v ? Py_True : Py_False);
}

/* A guarded float operation leaves before the call of the primitive. */
static lu_v
float_ir(struct ctx *c, int op, lu_v a, lu_v b)
{
    lu_v r = lu_ev_emit(c, op, REP_F64, a, b, LU_NOV, 0);
    if (lu_op_flags[op] & IF_GUARD) {
        I(c, r)->fs = lu_ev_exit_fs(c);
    }
    return lu_ev_box_float(c, r);
}

/* What a class's attributes and bases are is read as of a version: a
   static type has none to change (*version 0); a class that may change
   has the one it has now, unless this site's guesses kept failing. What
   was read then stands when class_relied says the version is still the
   class's: it is guarded there, and the guard becomes a dependency of the
   unit (facts.c), which a change to the class or to a base of it kills. */
int
lu_ev_class_as_of(struct ctx *c, PyTypeObject *t, uint32_t *version)
{
    *version = 0;
    if (t->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) {
        return 1;
    }
    if (nospec(c, c->pc) || !lu_type_assign_version(t)) {
        return 0;
    }
    *version = __atomic_load_n(&t->tp_version_tag, __ATOMIC_ACQUIRE);
    return *version != 0;
}

int
lu_ev_class_relied(struct ctx *c, lu_v type, uint32_t version)
{
    if (version == 0) {
        return 1;
    }
    PyTypeObject *t = (PyTypeObject *)lu_ev_const(c, type);
    if (__atomic_load_n(&t->tp_version_tag, __ATOMIC_ACQUIRE) != version) {
        return 0;
    }
    lu_v gv = lu_ev_guard(c, OP_GUARD_VERSION, type, version);
    I(c, gv)->imm2 = GV_TYPE_ITSELF;
    return 1;
}

int
lu_ev_plain_value(struct ctx *c, PyObject *v)
{
    PyTypeObject *t = Py_TYPE(v);
    uint32_t version;
    return lu_ev_class_as_of(c, t, &version) && t->tp_descr_get == NULL
        && lu_ev_class_relied(c, lu_ev_konst(c, (PyObject *)t), version);
}

/* A tuple's or a list's class, any subclass of one, that r's is (known,
   or as every class the site met was, guarded): &PyTuple_Type or
   &PyList_Type, else NULL. */
static PyTypeObject *
seq_kind(struct ctx *c, lu_v *args, int i)
{
    return lu_ev_learn_kind(c, args[i], i, Py_TPFLAGS_TUPLE_SUBCLASS) ? &PyTuple_Type
        : lu_ev_learn_kind(c, args[i], i, Py_TPFLAGS_LIST_SUBCLASS) ? &PyList_Type : NULL;
}

static lu_v
size_of(struct ctx *c, lu_v *args)
{
    lu_v r = args[0];
    if (LU_IS_VIRT(r)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
        if (o != NULL && o->kind == VK_TUPLE) {
            return lu_ev_box_int(c, lu_ev_const_i(c, o->n));    /* decided: what tests it folds */
        }
        return LU_EV_NONE;
    }
    PyObject *k = lu_ev_const(c, r);
    if (k != NULL && PyTuple_CheckExact(k)) {
        return lu_ev_box_int(c, lu_ev_const_i(c, PyTuple_GET_SIZE(k)));
    }
    PyTypeObject *t = lu_ev_learn_type(c, r, 0);
    Py_ssize_t at = t == &PyUnicode_Type ? (Py_ssize_t)offsetof(PyASCIIObject, length)
        : t == &PySet_Type || t == &PyFrozenSet_Type ? (Py_ssize_t)offsetof(PySetObject, used)
        : t == &PyBytes_Type || t == &PyByteArray_Type || seq_kind(c, args, 0) != NULL ? (Py_ssize_t)offsetof(PyVarObject, ob_size)
        : lu_ev_learn_kind(c, r, 0, Py_TPFLAGS_DICT_SUBCLASS) ? (Py_ssize_t)offsetof(PyDictObject, ma_used) : -1;
    return at < 0 ? LU_EV_NONE : lu_ev_box_int(c, lu_ev_emit(c, OP_LOAD, REP_I64, lu_ev_val(c, r), LU_NOV, LU_NOV, at));
}

/* The type flag that says an object is of kind k (prim_kind's), or 0. */
static unsigned long
kind_flag(PyObject *k)
{
    static const struct { const char *name; unsigned long flag; } flags[] = {
        { "str", Py_TPFLAGS_UNICODE_SUBCLASS }, { "type", Py_TPFLAGS_TYPE_SUBCLASS },
        { "dict", Py_TPFLAGS_DICT_SUBCLASS }, { "int", Py_TPFLAGS_LONG_SUBCLASS },
        { "bytes", Py_TPFLAGS_BYTES_SUBCLASS }, { "tuple", Py_TPFLAGS_TUPLE_SUBCLASS },
        { "list", Py_TPFLAGS_LIST_SUBCLASS },
    };
    const char *name = k != NULL && PyUnicode_CheckExact(k) ? PyUnicode_AsUTF8(k) : NULL;
    for (size_t i = 0; name != NULL && i < sizeof flags / sizeof flags[0]; i++) {
        if (strcmp(name, flags[i].name) == 0) {
            return flags[i].flag;
        }
    }
    PyErr_Clear();
    return 0;
}

/* seq_get(seq, i) of a tuple or a list, of any class, at an int i: the item
   read in line, which leaves for an index out of range (the primitive
   raises) and wherever LIST_ITEM does. */
static lu_v
seq_item(struct ctx *c, lu_v *args)
{
    if (lu_ev_learn_type(c, args[1], 1) != &PyLong_Type) {
        return LU_EV_NONE;
    }
    PyTypeObject *t = seq_kind(c, args, 0);
    if (t == NULL) {
        return LU_EV_NONE;
    }
    lu_v i = lu_ev_unbox_int(c, args[1]);
    if (t == &PyTuple_Type) {
        /* LIST_ITEM counts a tuple's negative index from the end. */
        lu_v ok = lu_ev_emit(c, OP_CMP_I, REP_BIT, i, lu_ev_const_i(c, 0), LU_NOV, Py_GE);
        lu_ev_guard(c, OP_GUARD_BIT, ok, 1);
    }
    lu_v r = lu_ev_emit(c, OP_LIST_ITEM, REP_OBJ, lu_ev_val(c, args[0]), i, LU_NOV, t == &PyTuple_Type);
    I(c, r)->fs = lu_ev_exit_fs(c);
    return lu_ev_own(c, r);
}

/* typed_get(ba, typecode, i) and typed_set(ba, typecode, i, value)
   (runtime/prim/memoryview.c, array.array's items) where ba's class is
   known to be a bytearray's, the typecode is one the site met alone (array
   instances keep theirs for life), i is an int and the value an int or a
   float: the item read or stored in line (TYPED_LOAD, TYPED_STORE), which
   leaves for whatever the primitive answers MISSING to. */
static lu_v
typed_item(struct ctx *c, lu_v *args, int nargs)
{
    PyTypeObject *t = lu_ev_learn_type(c, args[0], 0);
    if (t == NULL || !PyType_IsSubtype(t, &PyByteArray_Type) || lu_ev_type(c, args[2]) != &PyLong_Type) {
        return LU_EV_NONE;
    }
    lu_v typecode = args[1];
    PyObject *k = lu_ev_learn_const(c, &typecode, 1, LU_PROF_IMMORTAL);
    int code = k != NULL && PyUnicode_CheckExact(k) && PyUnicode_GET_LENGTH(k) == 1
        ? (int)PyUnicode_READ_CHAR(k, 0) : 0;
    if (code == 0 || code > 127 || strchr("bBhHiIlLqQfd", code) == NULL) {
        return LU_EV_NONE;
    }
    int f = code == 'f' || code == 'd';
    lu_v value = LU_NOV;
    if (nargs == 4) {
        PyTypeObject *vt = lu_ev_learn_type(c, args[3], 3);
        if (vt == &PyFloat_Type && f) {
            value = lu_ev_unbox_float(c, args[3]);
        }
        else if (vt == &PyLong_Type && f) {
            value = lu_ev_emit(c, OP_I2F, REP_F64, lu_ev_unbox_int(c, args[3]), LU_NOV, LU_NOV, 1);
            I(c, value)->fs = lu_ev_exit_fs(c);         /* exact below 2**53; any other leaves */
        }
        else if (vt == &PyLong_Type) {
            value = lu_ev_unbox_int(c, args[3]);
        }
        else {
            return LU_EV_NONE;
        }
    }
    lu_v ba = lu_ev_val(c, args[0]), i = lu_ev_unbox_int(c, args[2]);
    lu_v r = lu_ev_emit(c, nargs == 3 ? OP_TYPED_LOAD : OP_TYPED_STORE, nargs == 4 ? REP_NONE : f ? REP_F64 : REP_I64,
                        ba, i, value, code);
    I(c, r)->fs = lu_ev_exit_fs(c);
    return nargs == 4 ? lu_ev_konst(c, Py_True) : f ? lu_ev_box_float(c, r) : lu_ev_box_int(c, r);
}

/* The exact type of every result of the primitive, or NULL: what the
   C of each returns (runtime/prim), read there. */
PyTypeObject *
lu_ev_prim_type(const char *name)
{
    static const char *const strs[] = { "int_format", "float_repr", "float_format", "str_concat", "str_replace",
        "str_slice", "str_slice_step", "str_case", "str_chr", "str_item", "str_strip", "str_repeat",
        "str_expandtabs", NULL };
    static const char *const ints[] = { "str_find", "str_count", "str_hash", "int_hash", "size", "str_len",
        "str_cmp", "int_cmp", "int_mul", "str_decimal", "tuple_hash", NULL };
    static const char *const bools[] = { "kind", "is_subtype", "type_flag", "str_is", "type_can_new", NULL };
    static const struct { const char *const *names; PyTypeObject *type; } known[] = {
        { strs, &PyUnicode_Type }, { ints, &PyLong_Type }, { bools, &PyBool_Type },
    };
    for (size_t k = 0; k < sizeof known / sizeof known[0]; k++) {
        for (const char *const *n = known[k].names; *n != NULL; n++) {
            if (strcmp(*n, name) == 0) {
                return known[k].type;
            }
        }
    }
    return NULL;
}

lu_v
lu_ev_prim(struct ctx *c, const char *name, lu_v *args, int nargs)
{
    for (int i = 0; i < nargs; i++) {
        if (args[i] == LU_NOV) {
            return LU_EV_NONE;
        }
    }
    if (nargs == 2 && type_at(c, args, 0) == &PyFloat_Type && type_at(c, args, 1) == &PyFloat_Type) {
        static const struct { const char *name; int op; } fops[] = {
            { "float_add", OP_ADD_F }, { "float_sub", OP_SUB_F }, { "float_mul", OP_MUL_F },
            { "float_truediv", OP_DIV_F },
        };
        for (size_t i = 0; i < sizeof fops / sizeof fops[0]; i++) {
            if (strcmp(name, fops[i].name) == 0) {
                return float_ir(c, fops[i].op, lu_ev_unbox_float(c, args[0]), lu_ev_unbox_float(c, args[1]));
            }
        }
    }
    if (nargs == 3 && strcmp(name, "float_cmp") == 0 && type_at(c, args, 0) == &PyFloat_Type
        && type_at(c, args, 1) == &PyFloat_Type && lu_ev_const(c, args[2]) != NULL
        && PyLong_CheckExact(lu_ev_const(c, args[2]))) {
        long op = PyLong_AsLong(lu_ev_const(c, args[2]));
        return lu_ev_bool(c, lu_ev_emit(c, OP_CMP_F, REP_BIT, lu_ev_unbox_float(c, args[0]),
                                        lu_ev_unbox_float(c, args[1]), LU_NOV, op), 0);
    }
    if (nargs == 1 && type_at(c, args, 0) == &PyFloat_Type) {
        if (strcmp(name, "float_neg") == 0) {
            return float_ir(c, OP_NEG_F, lu_ev_unbox_float(c, args[0]), LU_NOV);
        }
        if (strcmp(name, "float_abs") == 0) {
            return float_ir(c, OP_ABS_F, lu_ev_unbox_float(c, args[0]), LU_NOV);
        }
        if (strcmp(name, "m_sqrt") == 0) {
            return float_ir(c, OP_SQRT_F, lu_ev_unbox_float(c, args[0]), LU_NOV);
        }
        if (strcmp(name, "m_to_double") == 0) {
            return args[0];
        }
    }
    if (nargs == 1 && type_at(c, args, 0) == &PyLong_Type && !nospec(c, c->pc)) {
        /* (An int past a word keeps failing the unboxing: the primitive's call.) */
        if (strcmp(name, "int_abs") == 0 || strcmp(name, "int_neg") == 0) {
            lu_v r = lu_ev_emit(c, name[4] == 'a' ? OP_ABS_I : OP_NEG_I, REP_I64, lu_ev_unbox_int(c, args[0]),
                                LU_NOV, LU_NOV, 0);
            I(c, r)->fs = lu_ev_exit_fs(c);
            return lu_ev_box_int(c, r);
        }
        if (strcmp(name, "m_to_double") == 0) {
            return lu_ev_box_float(c, lu_ev_emit(c, OP_I2F, REP_F64, lu_ev_unbox_int(c, args[0]), LU_NOV, LU_NOV, 0));
        }
    }
    if (nargs == 1 && (strcmp(name, "size") == 0 || (strcmp(name, "str_len") == 0 && learned_at(c, args, 0) == &PyUnicode_Type))) {
        return size_of(c, args);
    }
    if (nargs == 2 && strcmp(name, "seq_get") == 0) {
        return seq_item(c, args);
    }
    if (nargs == 1) {
        /* A word the primitive would box: the helper's, unboxed. */
        static const struct { const char *name; int helper; unsigned long kind; } words[] = {
            { "str_hash", H_str_hash, Py_TPFLAGS_UNICODE_SUBCLASS },
            { "dict_keys_id", H_dict_keys_id, Py_TPFLAGS_DICT_SUBCLASS },
            { "dict_stamp", H_dict_stamp, Py_TPFLAGS_DICT_SUBCLASS },
        };
        for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
            if (strcmp(name, words[i].name) == 0 && lu_ev_learn_kind(c, args[0], 0, words[i].kind)) {
                lu_v v = lu_ev_val(c, args[0]);
                return lu_ev_box_int(c, lu_ev_helper(c, words[i].helper, 1, &v, c->oparg + 2));
            }
        }
    }
    if (nargs == 2 && strcmp(name, "str_ord") == 0 && learned_at(c, args, 1) == &PyLong_Type
        && learned_at(c, args, 0) == &PyUnicode_Type) {
        lu_v r = lu_ev_emit(c, OP_STR_ORD, REP_I64, lu_ev_val(c, args[0]), lu_ev_unbox_int(c, args[1]), LU_NOV, 0);
        I(c, r)->fs = lu_ev_exit_fs(c);
        return lu_ev_box_int(c, r);
    }
    if (nargs == 2 && strcmp(name, "str_item") == 0 && !nospec(c, c->pc) && learned_at(c, args, 1) == &PyLong_Type
        && learned_at(c, args, 0) == &PyUnicode_Type) {
        /* The code point read in line, and its str: a latin-1 one is the
           runtime's singleton (emit.c), any other made in C. */
        lu_v cp = lu_ev_emit(c, OP_STR_ORD, REP_I64, lu_ev_val(c, args[0]), lu_ev_unbox_int(c, args[1]), LU_NOV, 0);
        I(c, cp)->fs = lu_ev_exit_fs(c);
        lu_v r = lu_ev_helper(c, H_str_char, 1, &cp, c->oparg + 2);
        I(c, r)->imm2 = 1 + CNT_item;           /* counted as the item read it is */
        lu_ev_def_type(c, r, &PyUnicode_Type);
        return r;
    }
    for (int j = 0; nargs == 2 && j < 2 && strcmp(name, "str_cmp") == 0 && !nospec(c, c->pc); j++) {
        /* A str against one character the code names: its first character
           and its length tell, in line. */
        PyObject *k = lu_ev_const(c, args[1 - j]);
        if (k != NULL && PyUnicode_CheckExact(k) && PyUnicode_GET_LENGTH(k) == 1
            && learned_at(c, args, j) == &PyUnicode_Type) {
            lu_v r = lu_ev_emit(c, OP_STR_CMP1, REP_I64, lu_ev_val(c, args[j]), LU_NOV, LU_NOV,
                                PyUnicode_READ_CHAR(k, 0));
            I(c, r)->imm2 = j;
            I(c, r)->fs = lu_ev_exit_fs(c);
            return lu_ev_box_int(c, r);
        }
    }
    PyObject *k0 = nargs > 0 ? lu_ev_const(c, args[0]) : NULL, *k1 = nargs > 1 ? lu_ev_const(c, args[1]) : NULL;
    if (nargs == 1 && strcmp(name, "static_type") == 0 && k0 != NULL && PyUnicode_CheckExact(k0)) {
        /* A static type by name: immortal, and the same every time. */
        const char *tname = PyUnicode_AsUTF8(k0);
        PyTypeObject *t = tname != NULL ? lu_static_type(tname) : NULL;
        PyErr_Clear();
        return t != NULL ? lu_ev_konst(c, (PyObject *)t) : LU_EV_NONE;
    }
    uint32_t version;
    int lookup = nargs == 2 && strcmp(name, "type_lookup") == 0 && k1 != NULL && PyUnicode_CheckExact(k1);
    long which = -1;
    if (nargs == 2 && strcmp(name, "attr_way") == 0 && k1 != NULL && PyLong_CheckExact(k1)
        && lu_long_as_long(k1, &which) < 0) {
        PyErr_Clear();
        which = -1;
    }
    if (lookup || (which >= 0 && which <= 2)) {
        /* A class's attribute, or a field of its attribute protocol word, as
           of its version. */
        lu_v cls = args[0];
        PyObject *t = lu_ev_learn_const(c, &cls, 0, LU_PROF_CLASS);
        if (t == NULL || !PyType_Check(t) || !lu_ev_class_as_of(c, (PyTypeObject *)t, &version)) {
            return LU_EV_NONE;
        }
        PyObject *r = lookup ? _PyType_LookupRef((PyTypeObject *)t, k1)
            : PyLong_FromLong(lu_attr_way((PyTypeObject *)t, LU_ATTR_READ + 3 * (int)which));
        lu_v k = lu_ev_konst(c, r != NULL ? r : LU_MISSING);
        Py_XDECREF(r);
        return lu_ev_class_relied(c, cls, version) ? k : LU_EV_NONE;
    }
    if (nargs == 1 && strcmp(name, "type_can_new") == 0 && k0 != NULL && PyType_Check(k0)
        && (((PyTypeObject *)k0)->tp_flags & Py_TPFLAGS_IMMUTABLETYPE)) {
        return truth_const(c, ((PyTypeObject *)k0)->tp_new != NULL);
    }
    if (nargs == 1 && strcmp(name, "next_missing_of") == 0 && k0 != NULL) {
        /* The registry is made at boot and only read after. */
        return lu_ev_konst(c, lu_next_missing_of(k0));
    }
    if (nargs == 2 && strcmp(name, "is_subtype") == 0 && k0 != NULL && PyType_Check(k0) && k1 != NULL
        && PyType_Check(k1) && lu_ev_class_as_of(c, (PyTypeObject *)k0, &version)) {
        int sub = PyType_IsSubtype((PyTypeObject *)k0, (PyTypeObject *)k1);
        return lu_ev_class_relied(c, args[0], version) ? truth_const(c, sub) : LU_EV_NONE;
    }
    unsigned long flag = nargs == 2 && strcmp(name, "kind") == 0 ? kind_flag(k1) : 0;
    if (flag != 0 && lu_ev_type(c, args[0]) == NULL && !LU_IS_VIRT(args[0]) && I(c, args[0])->rep == REP_OBJ
        && lu_ev_learn_type(c, args[0], 0) == NULL) {
        /* Of a class not known: what every class the site met was, else
           the class's flags tested in line. */
        if (lu_ev_learn_kind(c, args[0], 0, flag)) {
            return lu_ev_konst(c, Py_True);
        }
        lu_v type = lu_ev_emit(c, OP_LOAD, REP_OBJ, lu_ev_val(c, args[0]), LU_NOV, LU_NOV, offsetof(PyObject, ob_type));
        lu_v flags = lu_ev_emit(c, OP_LOAD, REP_I64, type, LU_NOV, LU_NOV, offsetof(PyTypeObject, tp_flags));
        lu_v has = lu_ev_emit(c, OP_AND_I, REP_I64, flags, lu_ev_const_i(c, (int64_t)flag), LU_NOV, 0);
        return lu_ev_bool(c, lu_ev_emit(c, OP_CMP_I, REP_BIT, has, lu_ev_const_i(c, 0), LU_NOV, Py_NE), 0);
    }
    if (nargs == 2 && strcmp(name, "kind") == 0 && k1 != NULL && PyUnicode_CheckExact(k1)
        && lu_ev_learn_type(c, args[0], 0) != NULL) {
        /* A known type's kind: by its flags, or by its bases where they
           cannot change. */
        PyTypeObject *t = type_at(c, args, 0);
        const char *k = PyUnicode_AsUTF8(k1);
        int r = k != NULL ? lu_kind_of_type(t, k) : -1;
        PyErr_Clear();
        int flagged = k != NULL && strcmp(k, "float") != 0 && strcmp(k, "complex") != 0
            && strcmp(k, "bytearray") != 0 && strcmp(k, "set") != 0 && strcmp(k, "frozenset") != 0;
        if (r >= 0 && (flagged || (t->tp_flags & Py_TPFLAGS_IMMUTABLETYPE))) {
            return truth_const(c, r);
        }
    }
    if (nargs == 2 && strcmp(name, "alloc") == 0 && k0 != NULL && k0 == k1 && PyType_Check(k0)
        && (((PyTypeObject *)k0)->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) && (((PyTypeObject *)k0)->tp_alloc == NULL || ((PyTypeObject *)k0)->tp_alloc == PyType_GenericAlloc)
        && lu_field_offset((PyTypeObject *)k0, 0) >= 0) {
        /* An instance of a kernel type whose state is its fields: they are
           the object, until something needs it. */
        int n = 1;
        while (lu_field_offset((PyTypeObject *)k0, n) >= 0) {
            n++;
        }
        struct lu_vobj *o;
        lu_v r = lu_ev_virt_new(c, VK_FIELDS, (PyTypeObject *)k0, n, &o);
        for (int i = 0; i < n; i++) {
            o->f[i] = lu_ev_konst(c, NULL);
        }
        return r;
    }
    if (nargs == 2 && strcmp(name, "iter_new") == 0
        && ((k0 == (PyObject *)&PyListIter_Type && type_at(c, args, 1) == &PyList_Type)
            || (k0 == (PyObject *)&PyTupleIter_Type && type_at(c, args, 1) == &PyTuple_Type))) {
        return lu_ev_seqiter_new(c, args[1]);       /* the iterator the kernel makes, as GET_ITER's */
    }
    if ((nargs == 2 && strcmp(name, "field_get") == 0) || (nargs == 3 && strcmp(name, "field_set") == 0)) {
        lu_v real;
        struct lu_vobj *o = LU_IS_VIRT(args[0]) ? lu_ev_virt(c->g, &c->g->st, args[0], &real) : NULL;
        long i = k1 != NULL && PyLong_CheckExact(k1) ? PyLong_AsLong(k1) : -1;
        PyErr_Clear();
        if (o != NULL && real == LU_NOV && o->kind == VK_FIELDS && i >= 0 && i < o->n) {
            if (nargs == 2) {
                /* An unset field (the constant NULL) reads as None; one that holds an object not yet made is that object. */
                lu_v held = o->f[i];
                int unset = !LU_IS_VIRT(held) && I(c, held)->op == OP_CONST && I(c, held)->imm == 0;
                return unset ? lu_ev_konst(c, Py_None) : held;
            }
            struct lu_vobj *now = lu_ir_vobj(c->g->f, VK_FIELDS, o->type, o->n);
            memcpy(now->f, o->f, sizeof(lu_v) * (size_t)o->n);
            now->f[i] = args[2];
            lu_ev_drop(c, o->f[i]);
            lu_ev_bind(c->g, &c->g->st, LU_VIRT_ID(args[0]), LU_NOV, now);      /* the same object, changed */
            return lu_ev_konst(c, Py_None);
        }
    }
    if (nargs == 1 && strcmp(name, "truth_exact") == 0) {
        /* A bool is its own truth; a number's is whether it is not zero. */
        PyTypeObject *t = lu_ev_learn_type(c, args[0], 0);
        if (t == &PyBool_Type) {
            return args[0];
        }
        if (t == &PyLong_Type || t == &PyFloat_Type) {
            return lu_ev_bool(c, lu_ev_truth(c, args[0]), 0);
        }
    }
    if (nargs == 1 && strcmp(name, "ns_items") == 0 && LU_IS_VIRT(args[0])) {
        /* The items of a dict that does not exist and is empty: none. The
           kernel only iterates over them. */
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, args[0], &real);
        if (o != NULL && real == LU_NOV && o->kind == VK_DICT && o->n == 0) {
            return lu_ev_konst(c, (PyObject *)&_PyRuntime.singletons.empty_tuple);
        }
    }
    if (nargs == 2 && strcmp(name, "field_get") == 0 && k0 != NULL && k1 != NULL && PyLong_CheckExact(k1)
        && (Py_IS_TYPE(k0, &PyStaticMethod_Type) || Py_IS_TYPE(k0, &PyClassMethod_Type))) {
        /* What a staticmethod or classmethod wraps never changes. */
        Py_ssize_t off = lu_field_offset(Py_TYPE(k0), PyLong_AsLong(k1));
        PyObject *r = off >= 0 ? lu_field_get(k0, off) : NULL;
        PyErr_Clear();
        if (r != NULL) {
            lu_v v = lu_ev_konst(c, r);
            Py_DECREF(r);       /* the unit holds its constants */
            return v;
        }
    }
    if ((nargs == 3 && strcmp(name, "typed_get") == 0) || (nargs == 4 && strcmp(name, "typed_set") == 0)) {
        return typed_item(c, args, nargs);
    }
    if (nargs == 2 && strcmp(name, "list_append") == 0 && type_at(c, args, 0) == &PyList_Type) {
        lu_ev_helper(c, H_list_append, 2, args, c->oparg + 2);
        return lu_ev_konst(c, Py_None);
    }
    return LU_EV_NONE;
}

/* A primitive whose index and hash operands it reads as C integers, where
   those are ints: called with them as machine words (H_prim_words), which
   an int the code computed (a hash, an entry number) needs no box for.
   argv is the call's: the primitive, then its n operands. */
lu_v
lu_ev_prim_words(struct ctx *c, const char *name, lu_v *argv, int n, int nops)
{
    static const struct { const char *name; int n; unsigned mask; } prims[] = {
        { "set_probe", 4, 1 << 2 }, { "set_add_entry", 4, 1 << 2 }, { "set_discard_entry", 5, 1 << 2 },
        { "dict_probe", 4, 1 << 2 | 1 << 3 }, { "dict_key", 2, 1 << 1 }, { "dict_value", 2, 1 << 1 },
        { "dict_entry", 2, 1 << 1 }, { "dict_insert", 5, 1 << 1 | 1 << 3 }, { "dict_replace", 4, 1 << 1 | 1 << 2 },
        { "dict_remove", 3, 1 << 1 | 1 << 2 },
    };
    size_t k = 0;
    while (k < sizeof prims / sizeof prims[0] && (prims[k].n != n || strcmp(prims[k].name, name) != 0)) {
        k++;
    }
    if (k == sizeof prims / sizeof prims[0]) {
        return LU_EV_NONE;
    }
    for (int i = 0; i < n; i++) {
        if ((prims[k].mask >> i & 1) && lu_ev_learn_type(c, argv[1 + i], i) != &PyLong_Type) {
            return LU_EV_NONE;
        }
    }
    lu_v w[8];
    w[0] = argv[0];
    w[1] = lu_ev_const_i(c, prims[k].mask);
    for (int i = 0; i < n; i++) {
        w[2 + i] = prims[k].mask >> i & 1 ? lu_ev_unbox_int(c, argv[1 + i]) : argv[1 + i];
    }
    return lu_ev_helper(c, H_prim_words, n + 2, w, nops);
}
