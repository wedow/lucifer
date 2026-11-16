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

static lu_v
size_of(struct ctx *c, lu_v r)
{
    PyTypeObject *t = lu_ev_type(c, r);
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
    if (t == &PyList_Type || t == &PyTuple_Type || t == &PyBytes_Type || t == &PyByteArray_Type) {
        return lu_ev_box_int(c, lu_ev_emit(c, OP_LOAD, REP_I64, r, LU_NOV, LU_NOV, offsetof(PyVarObject, ob_size)));
    }
    if (t == &PyUnicode_Type) {
        return lu_ev_box_int(c, lu_ev_emit(c, OP_LOAD, REP_I64, r, LU_NOV, LU_NOV, offsetof(PyASCIIObject, length)));
    }
    return LU_EV_NONE;
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
        "str_cmp", "str_decimal", "tuple_hash", NULL };
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
    if (nargs == 1 && type_at(c, args, 0) == &PyLong_Type) {
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
    if (nargs == 1 && (strcmp(name, "size") == 0 || (strcmp(name, "str_len") == 0 && type_at(c, args, 0) == &PyUnicode_Type))) {
        return size_of(c, args[0]);
    }
    if (nargs == 2 && strcmp(name, "str_ord") == 0 && type_at(c, args, 0) == &PyUnicode_Type
        && type_at(c, args, 1) == &PyLong_Type) {
        lu_v r = lu_ev_emit(c, OP_STR_ORD, REP_I64, lu_ev_val(c, args[0]), lu_ev_unbox_int(c, args[1]), LU_NOV, 0);
        I(c, r)->fs = lu_ev_exit_fs(c);
        return lu_ev_box_int(c, r);
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
    if (nargs == 2 && strcmp(name, "type_lookup") == 0 && k0 != NULL && PyType_Check(k0) && k1 != NULL
        && PyUnicode_CheckExact(k1) && lu_ev_class_as_of(c, (PyTypeObject *)k0, &version)) {
        PyObject *r = _PyType_Lookup((PyTypeObject *)k0, k1);
        lu_v k = lu_ev_konst(c, r != NULL ? r : LU_MISSING);
        return lu_ev_class_relied(c, args[0], version) ? k : LU_EV_NONE;
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
    if (nargs == 2 && strcmp(name, "list_append") == 0 && type_at(c, args, 0) == &PyList_Type) {
        lu_ev_helper(c, H_list_append, 2, args, c->oparg + 2);
        return lu_ev_konst(c, Py_None);
    }
    return LU_EV_NONE;
}
