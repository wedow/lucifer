/* Names and attributes (eval.h): LOAD_GLOBAL, LOAD_ATTR, STORE_ATTR. A
   global is a constant, guarded where it is read; an attribute site the
   interpreter specialized is compiled as its specialization does it;
   anything else is the interpreter's own lookup, called. */
#include "jit/eval.h"
#include "interp/specialize.h"

PyObject *
lu_ev_dict_const(struct ctx *c, PyObject *d, PyObject *b, PyObject *name)
{
    if (!PyDict_CheckExact(d) || (b != NULL && !PyDict_CheckExact(b))) {
        return NULL;
    }
    /* Versioned first: a table still of that version after the lookup has
       not changed meanwhile, so the entry found is its own. A mixed globals
       table may have a key that compares equal before we reach builtins. */
    uint32_t dv = lu_dict_keys_version((PyDictObject *)d), bv = 0;
    if (dv == 0 || !lu_dict_str_keys(d)) {
        return NULL;
    }
    PyObject *at = d;
    Py_ssize_t ix = lu_ns_index(d, name);
    if (ix < 0 && b != NULL) {
        at = b;
        bv = lu_dict_keys_version((PyDictObject *)b);
        ix = lu_ns_index(b, name);
    }
    PyDictKeysObject *dk = __atomic_load_n(&((PyDictObject *)at)->ma_keys, __ATOMIC_ACQUIRE);
    if (ix < 0 || (at == b && bv == 0) || !lu_dk_is_unicode(dk) || ix >= dk->dk_nentries
        || __atomic_load_n(&dk->dk_version, __ATOMIC_ACQUIRE) != (at == b ? bv : dv)) {
        return NULL;
    }
    PyObject *v = lu_try_xget_ref(lu_dk_value_slot(dk, ix));
    if (v != NULL && dk != __atomic_load_n(&((PyDictObject *)at)->ma_keys, __ATOMIC_ACQUIRE)) {
        Py_CLEAR(v);
    }
    if (v == NULL) {
        return NULL;
    }
    lu_v g1 = lu_ev_guard(c, OP_GUARD_VERSION, lu_ev_konst(c, d), dv);
    I(c, g1)->imm2 = GV_DICT_KEYS;
    if (at == b) {
        lu_v g2 = lu_ev_guard(c, OP_GUARD_VERSION, lu_ev_konst(c, b), bv);
        I(c, g2)->imm2 = GV_DICT_KEYS;
    }
    lu_v g3 = lu_ev_guard(c, OP_GUARD_IS, lu_ev_konst(c, at), (int64_t)(intptr_t)v);
    I(c, g3)->imm2 = GI_KEYS_FIELD | ((char *)lu_dk_value_slot(dk, ix) - (char *)dk);
    return v;
}

/* Whether the global `name` now names a function, class, module or C
   function: a binding programs make once, which stays a constant even
   where its guards failed while the module was still defining its names. */
static int
binds_code(struct ctx *c, PyObject *name)
{
    PyObject *v = lu_ns_get(c->globals, name);
    if (v == NULL && !PyErr_Occurred()) {
        v = lu_ns_get(c->builtins, name);
    }
    PyErr_Clear();
    int r = v != NULL && (PyFunction_Check(v) || PyType_Check(v) || PyModule_Check(v) || PyCFunction_Check(v));
    Py_XDECREF(v);
    return r;
}

int
lu_ev_load_global(struct ctx *c)
{
    PyObject *name = name_at(c, c->oparg >> 1);
    PyObject *v = NULL;
    if (c->kernel && lu_ns_lookup(PyThreadState_Get()->interp->kernel_rebound, name) == NULL) {
        /* Kernel code's module globals are constants: the kernel binds its
           names once at boot. A name it takes from builtins is a program's
           to rebind: a constant guarded where it is read, as any global. */
        v = lu_ns_get(c->globals, name);
        PyErr_Clear();
        if (v == NULL) {
            v = lu_ev_dict_const(c, c->builtins, NULL, name);
        }
    }
    else if ((nospec(c, c->pc) || lu_jit_nohoist(c->code, c->pc)) && PyDict_CheckExact(c->globals)
             && PyDict_CheckExact(c->builtins) && !binds_code(c, name)) {
        /* Its guards kept failing (a value rebound on every call), or a
           writer took its value back before: loaded through the site's own
           cache each time, so no unit holds a value the global let go (a
           guard kept in place would, until the unit dies); a miss leaves
           for the NameError. */
        lu_v args[4] = { lu_ev_konst(c, c->globals), lu_ev_konst(c, c->builtins), lu_ev_konst(c, name),
                         lu_ev_const_i(c, (int64_t)(intptr_t)(c->cache - 1)) };
        lu_ev_push(c, lu_ev_helper(c, H_load_global, 4, args, 0));
        if (c->oparg & 1) {
            lu_ev_push(c, LU_NOV);
        }
        return 0;
    }
    else {
        v = lu_ev_dict_const(c, c->globals, c->builtins, name);
    }
    if (v == NULL) {
        lu_ev_side_exit(c, EXIT_SIDE);
        return 0;
    }
    lu_ev_push(c, lu_ev_konst(c, v));
    Py_DECREF(v);               /* the unit holds its constants */
    if (c->oparg & 1) {
        lu_ev_push(c, LU_NOV);
    }
    return 0;
}

/* The attribute instruction's specialization, read as the interpreter
   reads it (specialize.h): its opcode, version, index word and, for
   LOAD_ATTR, descriptor (a new reference), all of one specialization; or
   `generic` when there is none, the site is not to be speculated on, or
   it changed meanwhile. */
static int
cached_site(struct ctx *c, int generic, uint32_t *version, uint16_t *index, PyObject **descr)
{
    lu_codeunit *site = c->cache - 1;
    uint64_t seq = lu_site_read();
    int op = __atomic_load_n(&site->op.code, __ATOMIC_RELAXED);
    *version = lu_cache_load32(c->cache + LA_VERSION);
    *index = lu_cache_load(c->cache + LA_INDEX);
    *descr = generic == LOAD_ATTR ? lu_cache_load_ptr(c->cache + LA_DESCR) : NULL;
    if (c->parent == NULL) {
        c->g->f->sites++;
        c->g->f->cold += op == generic && lu_cache_load(c->cache + LA_COUNTER) == 0;
    }
    if (*version == 0 || nospec(c, c->pc) || !lu_site_unchanged(site, op, seq)) {
        *descr = NULL;
        return generic;
    }
    Py_XINCREF(*descr);
    return op;
}

/* The owner's place is taken by the attribute, then self or NULL in the
   method form. */
static void
replace_owner(struct ctx *c, lu_v attr, lu_v self)
{
    lu_ev_pop(c);
    lu_ev_push(c, attr);
    if (c->oparg & 1) {
        lu_ev_push(c, self);
    }
}

/* Whether instances of tp can be kept as VK_INST: made by object.__new__
   (class_init), their attributes their own (__slots__ alone or values
   inline, no dict of their own yet), found and set by object's own
   __getattribute__ and __setattr__, and nothing run when they die. */
static int
instance_kind(PyTypeObject *tp)
{
    int slots_only = tp->tp_dictoffset == 0 && !(tp->tp_flags & Py_TPFLAGS_MANAGED_DICT);
    return (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) && tp->tp_finalize == NULL && tp->tp_del == NULL
        && (slots_only || (tp->tp_flags & Py_TPFLAGS_INLINE_VALUES))
        && _PyType_Lookup(tp, LU_ID(__getattribute__)) == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__getattribute__))
        && _PyType_Lookup(tp, LU_ID(__setattr__)) == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__setattr__))
        && _PyType_Lookup(tp, LU_ID(__getattr__)) == NULL;
}

lu_v
lu_ev_new_instance(struct ctx *c, PyTypeObject *tp)
{
    lu_v k = lu_ev_konst(c, (PyObject *)tp);
    if (!instance_kind(tp)) {
        lu_v obj = lu_ev_helper(c, H_new_object, 1, &k, 0);
        lu_ev_def_type(c, obj, tp);
        return obj;
    }
    struct lu_vobj *o;
    return lu_ev_virt_new(c, VK_INST, tp, 0, &o);      /* no attributes yet: nothing has needed it */
}

/* The record of an instance that does not exist, or NULL. */
static struct lu_vobj *
unmade_instance(struct ctx *c, lu_v r)
{
    lu_v real;
    struct lu_vobj *o = LU_IS_VIRT(r) ? lu_ev_virt(c->g, &c->g->st, r, &real) : NULL;
    return o != NULL && real == LU_NOV && o->kind == VK_INST ? o : NULL;
}

/* The index of name's value in record o, or -1. */
static int
held_at(struct ctx *c, const struct lu_vobj *o, PyObject *name)
{
    for (int i = 0; i < o->n; i += 2) {
        if (lu_ev_const(c, o->f[i]) == name) {
            return i + 1;
        }
    }
    return -1;
}

/* A __slots__ member holding an object, which name finds on the class. */
static int
slot_member(PyObject *d)
{
    return d != NULL && Py_IS_TYPE(d, &PyMemberDescr_Type)
        && ((PyMemberDescrObject *)d)->d_member->type == Py_T_OBJECT_EX;
}

/* The class's attribute `name` as of a version the unit then relies on
   (what made the instance virtual holds while it does): *d borrowed
   (NULL: it has none). 0 when the class has no version. */
static int
class_entry(struct ctx *c, PyTypeObject *tp, PyObject *name, PyObject **d)
{
    uint32_t version;
    if (!lu_ev_class_as_of(c, tp, &version)) {
        return 0;
    }
    *d = _PyType_Lookup(tp, name);
    return lu_ev_class_relied(c, lu_ev_konst(c, (PyObject *)tp), version);
}

/* A new reference to the class whose instances the version guard on
   owner admits, or NULL: known, or found by the version, which is read
   before anything is read from the class (a change after the read takes
   the version back, and the guard fails). */
static PyTypeObject *
owner_class(struct ctx *c, lu_v owner, uint32_t version)
{
    PyTypeObject *t = lu_ev_type(c, owner);
    return t != NULL ? (PyTypeObject *)Py_NewRef(t) : lu_type_of_version(version);
}

/* An inline store into an instance of class t (NULL: not known) may take
   a new name in line: every instance has at least the room settled now
   (ssa.h INLINE_STORE imm2). */
static void
store_room(struct ctx *c, lu_v st, PyTypeObject *t, uint16_t index)
{
    if (t == NULL || !(t->tp_flags & Py_TPFLAGS_INLINE_VALUES)) {
        return;
    }
    int64_t word = LU_INLINE_STRIDE * sizeof(PyObject *);
    int64_t room = (int64_t)lu_inline_settle_room(t) - ((int64_t)index - (int64_t)t->tp_basicsize) / word;
    I(c, st)->imm2 = room > 0 ? room : 0;
}

/* Whether an inline store into class t's word at `index` must go to C,
   which keeps the name's kind (attrs.c): an object store unless the name
   holds objects already, a raw int or float unless it holds that kind.
   Built under a kind that then moves, a store still writes a complete
   pair of that kind until the version guard stops it. t NULL: not known,
   C. */
static void
store_kind(struct ctx *c, lu_v st, PyTypeObject *t, uint16_t index)
{
    int op = I(c, st)->op;
    int k = t != NULL ? lu_inline_kind_at(t, index) : LU_FIELD_UNSEEN;
    if (op == OP_INLINE_STORE && k == LU_FIELD_INT) {
        I(c, st)->imm2 |= LU_STORE_INT_RAW;
        return;
    }
    if (op == OP_INLINE_STORE && k == LU_FIELD_FLOAT) {
        I(c, st)->imm2 |= LU_STORE_FLOAT_RAW;
        return;
    }
    int in_line = op == OP_INLINE_STORE ? k == LU_FIELD_OBJECT || k == LU_FIELD_ANY
                : op == OP_INLINE_STORE_I ? k == LU_FIELD_INT : k == LU_FIELD_FLOAT;
    if (!in_line) {
        I(c, st)->imm2 |= LU_STORE_BY_C;
    }
}

/* Whether no instruction from `from` on publishes its state: an object
   made at `from` has reached no other thread. */
static int
unpublished_since(struct ctx *c, lu_v from)
{
    for (lu_v v = from; v < c->g->f->nins; v++) {
        if (!(I(c, v)->flags & INS_DEAD) && lu_publishes(I(c, v))) {
            return 0;
        }
    }
    return 1;
}

/* The raw value of field value v of instance `id`, made real now, when v
   is an int or a float the code made, which no object is yet, and nothing
   but the instance names: the attribute takes the number itself, as a
   store of a temporary does (temporary_number). Else LU_NOV. */
static lu_v
field_number(struct ctx *c, lu_v v, int id, int *op)
{
    lu_v real;
    struct lu_vobj *o = LU_IS_VIRT(v) ? lu_ev_virt(c->g, &c->g->st, v, &real) : NULL;
    if (o == NULL || real != LU_NOV || o->n != 1 || (o->kind != VK_BOX_INT && o->kind != VK_BOX_FLOAT)
        || lu_ev_reaches_past(c->g, &c->g->st, LU_VIRT_ID(v), id)) {
        return LU_NOV;
    }
    *op = o->kind == VK_BOX_INT ? OP_INLINE_STORE_I : OP_INLINE_STORE_F;
    return o->f[0];
}

lu_v
lu_ev_instance_real(struct ctx *c, const struct lu_vobj *o, int id)
{
    PyTypeObject *tp = o->type;
    lu_v k = lu_ev_konst(c, (PyObject *)tp);
    lu_v obj = lu_ev_helper(c, H_new_object, 1, &k, 0);
    lu_ev_def_type(c, obj, tp);
    for (int i = 0; i < o->n; i += 2) {
        PyObject *name = lu_ev_const(c, o->f[i]), *d = _PyType_Lookup(tp, name);
        uint32_t version;
        uint16_t index;
        if (slot_member(d)) {
            lu_v args[3] = { obj, lu_ev_const_i(c, ((PyMemberDescrObject *)d)->d_member->offset),
                             lu_ev_val(c, o->f[i + 1]) };
            lu_ev_helper(c, H_slot_store, 3, args, 0);
        }
        else if (lu_specialize_store_fresh(tp, name, &version, &index) == STORE_ATTR_INSTANCE_VALUE
                 && lu_ev_class_relied(c, k, version)) {
            int fresh = unpublished_since(c, obj), raw_op;
            lu_v raw = field_number(c, o->f[i + 1], id, &raw_op);
            lu_v st = raw != LU_NOV ? lu_ev_emit(c, raw_op, REP_NONE, obj, raw, LU_NOV, index)
                                    : lu_ev_emit(c, OP_INLINE_STORE, REP_NONE, obj, lu_ev_val(c, o->f[i + 1]), LU_NOV, index);
            I(c, st)->fs = lu_ev_exit_fs(c);
            store_room(c, st, tp, index);
            store_kind(c, st, tp, index);
            if (fresh) {
                I(c, st)->flags |= INS_FRESH;
            }
            c->g->observed |= !fresh;
        }
        else {
            lu_v args[4] = { obj, lu_ev_konst(c, name), lu_ev_val(c, o->f[i + 1]), lu_ev_const_i(c, 0) };
            lu_ev_helper(c, H_store_attr, 4, args, 0);
        }
    }
    return obj;
}

/* LOAD_ATTR on an instance that does not exist, as object.__getattribute__
   finds the name: 1 when answered in place of the owner (an unset slot
   leaves, for the interpreter to raise), 0 to make it and load as usual. */
static int
instance_load(struct ctx *c, lu_v owner, struct lu_vobj *o, PyObject *name)
{
    PyObject *d;
    if (!class_entry(c, o->type, name, &d)) {
        return 0;
    }
    int at = held_at(c, o, name);
    descrgetfunc get = d != NULL ? Py_TYPE(d)->tp_descr_get : NULL;
    if (get != NULL && Py_TYPE(d)->tp_descr_set != NULL) {
        if (!slot_member(d)) {
            return 0;           /* a property, or another data descriptor */
        }
        if (at < 0) {
            lu_ev_side_exit(c, EXIT_SIDE);
            return 1;
        }
        replace_owner(c, o->f[at], LU_NOV);
    }
    else if (at >= 0) {
        replace_owner(c, o->f[at], LU_NOV);
    }
    else if (d != NULL && PyFunction_Check(d) && (c->oparg & 1)) {
        replace_owner(c, lu_ev_konst(c, d), owner);
    }
    else if (d != NULL && PyFunction_Check(d)) {
        struct lu_vobj *m;
        lu_v v = lu_ev_virt_new(c, VK_METHOD, &PyMethod_Type, 2, &m);
        m->f[0] = lu_ev_konst(c, d);
        m->f[1] = owner;
        replace_owner(c, v, LU_NOV);
    }
    else if (d != NULL && get == NULL) {
        replace_owner(c, lu_ev_konst(c, d), LU_NOV);
    }
    else {
        return 0;
    }
    return 1;
}

/* STORE_ATTR on an instance that does not exist, as object.__setattr__
   sets the name: its slot, or its value inline (a new name after the
   others). 1 when done, its operands popped; 0 to make it and store as usual. */
static int
instance_store(struct ctx *c, lu_v owner, struct lu_vobj *o, PyObject *name)
{
    PyObject *d;
    if (!class_entry(c, o->type, name, &d)) {
        return 0;
    }
    int data = d != NULL && Py_TYPE(d)->tp_descr_set != NULL;
    if (data ? !slot_member(d) || (((PyMemberDescrObject *)d)->d_member->flags & Py_READONLY)
             : !(o->type->tp_flags & Py_TPFLAGS_INLINE_VALUES)) {
        return 0;
    }
    if (lu_ev_reaches_id(c->g, &c->g->st, lu_ev_peek(c, 2), LU_VIRT_ID(owner))) {
        return 0;               /* it would hold itself: made, it can */
    }
    int at = held_at(c, o, name);
    struct lu_vobj *now = lu_ir_vobj(c->g->f, VK_INST, o->type, o->n + (at < 0 ? 2 : 0));
    memcpy(now->f, o->f, sizeof(lu_v) * (size_t)o->n);
    if (at < 0) {
        at = o->n + 1;
        now->f[at - 1] = lu_ev_konst(c, name);
    }
    else {
        lu_ev_drop(c, o->f[at]);
    }
    now->f[at] = lu_ev_peek(c, 2);
    lu_ev_bind(c->g, &c->g->st, LU_VIRT_ID(owner), LU_NOV, now);      /* the same object, changed */
    lu_ev_pop(c);
    lu_ev_pop(c);
    return 1;
}

/* The interpreter's own lookup, called, which tells `pic` (or NULL) what it meets. */
static void
load_generic(struct ctx *c, struct lu_pic *pic)
{
    lu_v args[3] = { lu_ev_peek(c, 1), lu_ev_konst(c, name_at(c, c->oparg >> 1)),
                     lu_ev_const_i(c, (int64_t)(intptr_t)pic) };
    /* load_method answers the bound method or attribute; CALL unbinds it. */
    replace_owner(c, lu_ev_helper(c, c->oparg & 1 ? H_load_method : H_load_attr, 3, args, 1), LU_NOV);
}

/* The attribute loaded as specialization `op` has it (version, index and
   descr are that specialization's), in place of the owner on the stack;
   any other way through the generic load, which tells `pic` (the site's
   cache of shapes, or NULL) what it meets. */
static int
load_as(struct ctx *c, int op, uint32_t version, uint16_t index, PyObject *descr, struct lu_pic *pic)
{
    lu_v owner = lu_ev_peek(c, 1), v;
    int method = c->oparg & 1;
    PyObject *name = name_at(c, c->oparg >> 1);
    switch (op) {
    case LOAD_ATTR_METHOD_NO_DICT: case LOAD_ATTR_METHOD_WITH_VALUES:
    case LOAD_ATTR_NONDESCRIPTOR_NO_DICT: case LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES: {
        lu_ev_guard_version(c, owner, version);
        if (op == LOAD_ATTR_METHOD_WITH_VALUES || op == LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES) {
            lu_v args[2] = { owner, lu_ev_konst(c, name) };
            lu_ev_guard(c, OP_GUARD_BIT, lu_ev_helper(c, H_dict_lacks, 2, args, 0), 1);
        }
        int is_method = op == LOAD_ATTR_METHOD_NO_DICT || op == LOAD_ATTR_METHOD_WITH_VALUES;
        if (is_method && !method) {
            /* The bound method, which nothing has needed yet. */
            struct lu_vobj *o;
            v = lu_ev_virt_new(c, VK_METHOD, &PyMethod_Type, 2, &o);
            o->f[0] = lu_ev_konst(c, descr);
            o->f[1] = owner;
            replace_owner(c, v, LU_NOV);
        }
        else {
            replace_owner(c, lu_ev_konst(c, descr), is_method ? owner : LU_NOV);
        }
        break;
    }
    case LOAD_ATTR_WITH_HINT:
        v = lu_ev_emit(c, OP_ATTR_LOAD, REP_OBJ, owner, LU_NOV, lu_ev_konst(c, name), version);
        I(c, v)->imm2 = index;
        I(c, v)->fs = lu_ev_exit_fs(c);
        replace_owner(c, lu_ev_own(c, v), LU_NOV);
        break;
    case LOAD_ATTR_SLOT: {
        lu_ev_guard_version(c, owner, version);
        v = lu_ev_emit(c, OP_SLOT_LOAD, REP_OBJ, lu_ev_val(c, owner), LU_NOV, LU_NOV, index);
        I(c, v)->fs = lu_ev_exit_fs(c);
        replace_owner(c, lu_ev_own(c, v), LU_NOV);
        break;
    }
    case LOAD_ATTR_PROPERTY: {
        /* The getter, a function, called with the owner: read from a
           property each time (its __init__ may change it), so guarded. */
        PyObject *fget = lu_property_fget(descr);
        if (method || fget == NULL || !PyFunction_Check(fget)) {
            load_generic(c, pic);
            break;
        }
        lu_ev_guard_version(c, owner, version);
        if (Py_IS_TYPE(descr, &PyProperty_Type)) {
            lu_v g = lu_ev_guard(c, OP_GUARD_IS, lu_ev_konst(c, descr), (int64_t)(intptr_t)fget);
            I(c, g)->imm2 = GI_FIELD | (int64_t)lu_field_offset(&PyProperty_Type, 0);
        }
        if (pic == NULL) {
            return lu_ev_call_function(c, fget, &owner, 1, 1);
        }
        /* One way of several, which join after the instruction: called out of line. */
        lu_v args[3] = { lu_ev_konst(c, fget), LU_NOV, owner };
        replace_owner(c, lu_ev_helper(c, H_call, 3, args, 1), LU_NOV);
        break;
    }
    case LOAD_ATTR_INSTANCE_VALUE:
        if (descr == NULL || _Py_IsImmortal(descr)) {
            /* A class value that is no descriptor stands behind the
               instance's (an _IOBase's __closed): the version guard keeps
               it the class's, so a word with none reads as it. */
            lu_ev_guard_version(c, owner, version);
            v = lu_ev_emit(c, OP_INLINE_LOAD, REP_OBJ, owner, LU_NOV, LU_NOV, index);
            PyTypeObject *t = owner_class(c, owner, version);
            int kind = t != NULL ? lu_inline_kind_at(t, index) : LU_FIELD_ANY;
            Py_XDECREF(t);
            I(c, v)->imm2 = (int64_t)(intptr_t)descr | (int64_t)kind << LU_LOAD_KIND_SHIFT;
            I(c, v)->fs = lu_ev_exit_fs(c);
            replace_owner(c, lu_ev_own(c, v), LU_NOV);
            break;
        }
        /* fall through: a class default that is not immortal */
    default:
        load_generic(c, pic);
        break;
    }
    return 0;
}

/* How an attribute instruction is built as one specialization has it. */
typedef int attr_as(struct ctx *c, int op, uint32_t version, uint16_t index, PyObject *descr, struct lu_pic *pic);

/* The site's shapes from the i-th of n on: a test of the owner's type
   version for each, with the load or store as that shape has it (`as`);
   then any other owner's, `generic`. Each way is built to the
   instruction's end, and they join after it. A unit built for fewer
   shapes than the cache has now leaves from its generic way to be built
   again, itself alone. */
static int
attr_ways(struct ctx *c, struct lu_pic *pic, int n, int i, attr_as *as, int generic)
{
    lu_v owner = lu_ev_peek(c, 1);
    if (i == n) {
        /* A cache that is full, or that has stopped looking, learns no
           more: the generic load tells it nothing. */
        if (!lu_jit_pic_learns(pic, n)) {
            return as(c, generic, 0, 0, NULL, NULL);
        }
        uint64_t key = (uint64_t)n;
        for (struct ctx *k = c; k != NULL; k = k->parent) {
            key = lu_jit_ctx_mix(key, k->code, k->pc);
        }
        lu_v args[2] = { lu_ev_const_i(c, (int64_t)(intptr_t)pic), lu_ev_const_i(c, n) };
        I(c, lu_ev_guard(c, OP_GUARD_BIT, lu_ev_helper(c, H_pic_same, 2, args, 0), 1))->prof
            = lu_jit_prof(c->g->f->code, key);
        return as(c, generic, 0, 0, NULL, pic);
    }
    struct lu_pic_entry e = pic->e[i];
    if (i == 0) {
        lu_ev_guard(c, OP_GUARD_NOT_NULL, owner, 0);
    }
    lu_v type = lu_ev_emit(c, OP_LOAD, REP_OBJ, owner, LU_NOV, LU_NOV, offsetof(PyObject, ob_type));
    lu_v version = lu_ev_emit(c, OP_LOAD, REP_I64, type, LU_NOV, LU_NOV, offsetof(PyTypeObject, tp_version_tag));
    I(c, version)->imm2 = 4;
    struct split s;
    lu_ev_split(c, lu_ev_emit(c, OP_CMP_I, REP_BIT, version, lu_ev_const_i(c, e.version), LU_NOV, Py_EQ), &s);
    if (e.descr != NULL) {
        lu_ev_konst(c, e.descr);
    }
    if (as(c, e.op, e.version, e.index, e.descr, pic) < 0) {
        return -1;
    }
    lu_ev_split_else(c, &s);
    if (attr_ways(c, pic, n, i + 1, as, generic) < 0) {
        return -1;
    }
    lu_ev_split_end(c, &s);
    return 0;
}

/* A plain attribute of a class the unit knows (Color.RED, a constant kept
   on a class), as type.__getattribute__ finds it: the metaclass has
   nothing by the name and its lookup is type's own, and what the class
   has is no descriptor. Read as of both classes' versions, which the unit
   then depends on. 1 with the attribute in place of the owner; 0 when
   it is not such an attribute. */
static int
class_attribute(struct ctx *c, lu_v owner, PyTypeObject *cls, PyObject *name)
{
    PyTypeObject *meta = Py_TYPE(cls);
    uint32_t vc, vm;
    if (nospec(c, c->pc) || !PyType_Check((PyObject *)meta) || !lu_ev_class_as_of(c, cls, &vc)
        || !lu_ev_class_as_of(c, meta, &vm)) {
        return 0;
    }
    PyObject *k = _PyType_Lookup(cls, name);
    if (k == NULL || Py_TYPE(k)->tp_descr_get != NULL || _PyType_Lookup(meta, name) != NULL
        || _PyType_Lookup(meta, LU_ID(__getattr__)) != NULL
        || _PyType_Lookup(meta, LU_ID(__getattribute__)) != _PyType_Lookup(&PyType_Type, LU_ID(__getattribute__))) {
        return 0;
    }
    lu_v value = lu_ev_konst(c, k);
    if (!lu_ev_class_relied(c, owner, vc) || !lu_ev_class_relied(c, lu_ev_konst(c, (PyObject *)meta), vm)) {
        return 0;
    }
    replace_owner(c, value, LU_NOV);
    return 1;
}

int
lu_ev_load_attr(struct ctx *c)
{
    lu_v owner = lu_ev_peek(c, 1);
    PyObject *name = name_at(c, c->oparg >> 1);
    /* A known module's attribute: a constant, guarded
       as a global is. */
    PyObject *mod = lu_ev_const(c, owner);
    if (mod != NULL && PyModule_CheckExact(mod) && !nospec(c, c->pc) && !lu_jit_nohoist(c->code, c->pc)) {
        PyObject *k = lu_ev_dict_const(c, PyModule_GetDict(mod), NULL, name);
        if (k != NULL) {
            replace_owner(c, lu_ev_konst(c, k), LU_NOV);
            Py_DECREF(k);       /* the unit holds its constants */
            return 0;
        }
    }
    if (mod != NULL && PyType_Check(mod) && Py_IS_TYPE(mod, &PyType_Type)
        && (((PyTypeObject *)mod)->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) && _PyType_Lookup(&PyType_Type, name) == NULL) {
        /* A function of a type that cannot change, read from the type: itself. */
        PyObject *k = _PyType_Lookup((PyTypeObject *)mod, name);
        if (k != NULL && PyFunction_Check(k)) {
            replace_owner(c, lu_ev_konst(c, k), LU_NOV);
            return 0;
        }
    }
    uint32_t version = 0;
    uint16_t index = 0;
    PyObject *descr = NULL;
    if (mod != NULL && PyType_Check(mod) && class_attribute(c, owner, (PyTypeObject *)mod, name)) {
        return 0;
    }
    struct lu_vobj *inst = unmade_instance(c, owner);
    if (inst != NULL && instance_load(c, owner, inst, name)) {
        return 0;
    }
    int op = LOAD_ATTR;
    if (!LU_IS_VIRT(owner)) {
        op = cached_site(c, LOAD_ATTR, &version, &index, &descr);
    }
    else if ((c->oparg & 1) && !nospec(c, c->pc)) {
        /* An exact builtin the unit made can supply its method even if the
           bytecode site has never seen this receiver. */
        PyTypeObject *tp = lu_ev_type(c, owner);
        if (tp != NULL && (tp->tp_flags & Py_TPFLAGS_IMMUTABLETYPE)) {
            int fresh = lu_specialize_load_fresh(tp, name, 1, &version, &index, &descr);
            if (fresh == LOAD_ATTR_METHOD_NO_DICT) {
                op = fresh;
            }
        }
    }
    /* A site with no one shape has a cache of the few it met (jit.h), and
       so has one whose one shape kept failing: its tests leave nowhere. */
    struct lu_pic *pic = op == LOAD_ATTR && owner >= 0
        ? lu_jit_pic(c->code, (int)(c->cache - 1 - lu_code_start(c->code)), 1) : NULL;
    int done = pic != NULL ? attr_ways(c, pic, __atomic_load_n(&pic->n, __ATOMIC_ACQUIRE), 0, load_as, LOAD_ATTR)
                           : load_as(c, op, version, index, descr, NULL);
    Py_XDECREF(descr);
    return done;
}

/* The raw value of v, the instruction's operand on top of the stack but
   one, when v is an int or a float that does not exist and nothing else
   names: the attribute takes the number itself (object/attrs.c, unboxed
   values), and its first reader that needs an object makes the only one
   there ever is. LU_NOV for any other value, which is stored as an
   object; *op is the store's instruction. */
static lu_v
temporary_number(struct ctx *c, lu_v v, int *op)
{
    lu_v real;
    struct lu_vobj *o = LU_IS_VIRT(v) ? lu_ev_virt(c->g, &c->g->st, v, &real) : NULL;
    if (o == NULL || real != LU_NOV || o->n != 1 || (o->kind != VK_BOX_INT && o->kind != VK_BOX_FLOAT)) {
        return LU_NOV;
    }
    lu_v *slot = &slots(c)[c->nlocals + *depth_of(c) - 2];
    *slot = LU_NOV;
    int named = lu_ev_reaches(c->g, &c->g->st, LU_VIRT_ID(v));
    *slot = v;
    *op = o->kind == VK_BOX_INT ? OP_INLINE_STORE_I : OP_INLINE_STORE_F;
    return named ? LU_NOV : o->f[0];
}

/* The store as specialization `op` has it, its operands popped; any other
   way through the generic store, which tells `pic` what it meets. */
static int
store_as(struct ctx *c, int op, uint32_t version, uint16_t index, PyObject *descr, struct lu_pic *pic)
{
    lu_v owner = lu_ev_peek(c, 1), v = lu_ev_peek(c, 2);
    PyObject *name = name_at(c, c->oparg);
    if (op == STORE_ATTR_WITH_HINT) {
        lu_v args[4] = { owner, v, lu_ev_konst(c, name), lu_ev_const_i(c, version) };
        lu_v g = lu_ev_guard(c, OP_GUARD_BIT, lu_ev_helper(c, H_attr_store, 4, args, 0), 1);
        I(c, g)->flags |= INS_NOSTRESS;
    }
    else if (op == STORE_ATTR_INSTANCE_VALUE) {
        lu_ev_guard_version(c, owner, version);
        int raw_op;
        lu_v raw = temporary_number(c, v, &raw_op);
        lu_v st = raw != LU_NOV ? lu_ev_emit(c, raw_op, REP_NONE, owner, raw, LU_NOV, index)
                                : lu_ev_emit(c, OP_INLINE_STORE, REP_NONE, owner, lu_ev_val(c, v), LU_NOV, index);
        I(c, st)->fs = lu_ev_exit_fs(c);
        PyTypeObject *t = owner_class(c, owner, version);
        store_room(c, st, t, index);
        store_kind(c, st, t, index);
        Py_XDECREF(t);
        c->g->observed = 1;     /* the value it replaces is released */
    }
    else if (op == STORE_ATTR_SLOT) {
        lu_ev_guard_version(c, owner, version);
        lu_v args[3] = { owner, lu_ev_const_i(c, index), v };
        lu_ev_helper(c, H_slot_store, 3, args, 0);
    }
    else {
        lu_v args[4] = { owner, lu_ev_konst(c, name), v, lu_ev_const_i(c, (int64_t)(intptr_t)pic) };
        lu_ev_helper(c, H_store_attr, 4, args, 2);
    }
    lu_ev_pop(c);
    lu_ev_pop(c);
    return 0;
}

int
lu_ev_store_attr(struct ctx *c)
{
    lu_v owner = lu_ev_peek(c, 1);
    uint32_t version = 0;
    uint16_t index = 0;
    PyObject *unused;
    struct lu_vobj *inst = unmade_instance(c, owner);
    if (inst != NULL && instance_store(c, owner, inst, name_at(c, c->oparg))) {
        return 0;
    }
    int op = LU_IS_VIRT(owner) ? STORE_ATTR : cached_site(c, STORE_ATTR, &version, &index, &unused);
    struct lu_pic *pic = op == STORE_ATTR && owner >= 0
        ? lu_jit_pic(c->code, (int)(c->cache - 1 - lu_code_start(c->code)), 1) : NULL;
    return pic != NULL ? attr_ways(c, pic, __atomic_load_n(&pic->n, __ATOMIC_ACQUIRE), 0, store_as, STORE_ATTR)
                       : store_as(c, op, version, index, NULL, NULL);
}
