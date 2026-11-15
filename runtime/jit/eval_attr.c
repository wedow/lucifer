/* Names and attributes (eval.h): LOAD_GLOBAL, LOAD_ATTR, STORE_ATTR. A
   global is a constant, guarded where it is read; an attribute site the
   interpreter specialized is compiled as its specialization does it;
   anything else is the interpreter's own lookup, called. */
#include "jit/eval.h"

PyObject *
lu_ev_dict_const(struct ctx *c, PyObject *d, PyObject *b, PyObject *name)
{
    if (!PyDict_CheckExact(d) || (b != NULL && !PyDict_CheckExact(b))) {
        return NULL;
    }
    /* Versioned first: a table still of that version after the lookup has
       not changed meanwhile, so the entry found is its own. */
    uint32_t dv = lu_dict_keys_version((PyDictObject *)d), bv = 0;
    PyObject *at = d;
    Py_ssize_t ix = lu_ns_index(d, name);
    if (ix < 0 && b != NULL) {
        at = b;
        bv = lu_dict_keys_version((PyDictObject *)b);
        ix = lu_ns_index(b, name);
    }
    PyDictKeysObject *dk = __atomic_load_n(&((PyDictObject *)at)->ma_keys, __ATOMIC_ACQUIRE);
    if (ix < 0 || dv == 0 || (at == b && bv == 0) || !lu_dk_is_unicode(dk) || ix >= dk->dk_nentries
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
    else if (nospec(c, c->pc) && PyDict_CheckExact(c->globals) && PyDict_CheckExact(c->builtins)
             && !binds_code(c, name)) {
        /* Its guards kept failing (a value rebound on every call): loaded
           through the site's own cache each time; a miss leaves for the
           NameError. */
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
    int op = __atomic_load_n(&site->op.code, __ATOMIC_RELAXED);
    *version = lu_cache_load32(c->cache + LA_VERSION);
    *index = lu_cache_load(c->cache + LA_INDEX);
    *descr = generic == LOAD_ATTR ? lu_cache_load_ptr(c->cache + LA_DESCR) : NULL;
    if (c->parent == NULL) {
        c->g->f->sites++;
        c->g->f->cold += op == generic && lu_cache_load(c->cache + LA_COUNTER) == 0;
    }
    if (*version == 0 || nospec(c, c->pc) || !lu_site_unchanged(site, op)
        || lu_cache_load32(c->cache + LA_VERSION) != *version) {
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
        lu_v args[2] = { owner, lu_ev_const_i(c, index) };
        v = lu_ev_helper(c, H_slot_load, 2, args, 0);
        lu_ev_guard(c, OP_GUARD_NOT_NULL, v, 0);
        replace_owner(c, v, LU_NOV);
        break;
    }
    case LOAD_ATTR_INSTANCE_VALUE:
        if (descr == NULL) {
            lu_ev_guard_version(c, owner, version);
            v = lu_ev_emit(c, OP_INLINE_LOAD, REP_OBJ, owner, LU_NOV, LU_NOV, index);
            I(c, v)->fs = lu_ev_exit_fs(c);
            replace_owner(c, lu_ev_own(c, v), LU_NOV);
            break;
        }
        /* fall through: a class default stands behind the instance's value */
    default: {
        lu_v args[3] = { owner, lu_ev_konst(c, name), lu_ev_const_i(c, (int64_t)(intptr_t)pic) };
        v = lu_ev_helper(c, method ? H_load_method : H_load_attr, 3, args, 1);
        /* load_method answers the bound method or attribute; CALL unbinds it. */
        replace_owner(c, v, LU_NOV);
        break;
    }
    }
    return 0;
}

/* The site's shapes from the i-th of n on: a test of the owner's type
   version for each, with the load as that shape has it; then any other
   owner's, generic. Each way is built to the instruction's end, and they
   join after it. A unit built for fewer shapes than the cache has now
   leaves from its generic load to be built again, itself alone. */
static int
load_ways(struct ctx *c, struct lu_pic *pic, int n, int i)
{
    lu_v owner = lu_ev_peek(c, 1);
    if (i == n) {
        /* A cache that is full, or that has stopped looking, learns no
           more: the generic load tells it nothing. */
        if (!lu_jit_pic_learns(pic, n)) {
            return load_as(c, LOAD_ATTR, 0, 0, NULL, NULL);
        }
        uint64_t key = (uint64_t)n;
        for (struct ctx *k = c; k != NULL; k = k->parent) {
            key = lu_jit_ctx_mix(key, k->code, k->pc);
        }
        lu_v args[2] = { lu_ev_const_i(c, (int64_t)(intptr_t)pic), lu_ev_const_i(c, n) };
        I(c, lu_ev_guard(c, OP_GUARD_BIT, lu_ev_helper(c, H_pic_same, 2, args, 0), 1))->prof
            = lu_jit_prof(c->g->f->code, key);
        return load_as(c, LOAD_ATTR, 0, 0, NULL, pic);
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
    if (load_as(c, e.op, e.version, e.index, e.descr, pic) < 0) {
        return -1;
    }
    lu_ev_split_else(c, &s);
    if (load_ways(c, pic, n, i + 1) < 0) {
        return -1;
    }
    lu_ev_split_end(c, &s);
    return 0;
}

int
lu_ev_load_attr(struct ctx *c)
{
    lu_v owner = lu_ev_peek(c, 1), v;
    int method = c->oparg & 1;
    PyObject *name = name_at(c, c->oparg >> 1);
    /* A known module's attribute: a constant, guarded
       as a global is. */
    PyObject *mod = lu_ev_const(c, owner);
    if (mod != NULL && PyModule_CheckExact(mod) && !nospec(c, c->pc)) {
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
    /* An object the code made has no cache to read: its own lookup. */
    int op = LU_IS_VIRT(owner) ? LOAD_ATTR : cached_site(c, LOAD_ATTR, &version, &index, &descr);
    /* A site with no one shape has a cache of the few it met (jit.h). */
    struct lu_pic *pic = op == LOAD_ATTR && owner >= 0 && !nospec(c, c->pc)
        ? lu_jit_pic(c->code, (int)(c->cache - 1 - lu_code_start(c->code)), 1) : NULL;
    int done = pic != NULL ? load_ways(c, pic, __atomic_load_n(&pic->n, __ATOMIC_ACQUIRE), 0)
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

int
lu_ev_store_attr(struct ctx *c)
{
    lu_v owner = lu_ev_peek(c, 1), v = lu_ev_peek(c, 2);
    PyObject *name = name_at(c, c->oparg);
    uint32_t version = 0;
    uint16_t index = 0;
    PyObject *unused;
    int op = LU_IS_VIRT(owner) ? STORE_ATTR : cached_site(c, STORE_ATTR, &version, &index, &unused);
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
        c->g->observed = 1;     /* the value it replaces is released */
    }
    else if (op == STORE_ATTR_SLOT) {
        lu_ev_guard_version(c, owner, version);
        lu_v args[3] = { owner, lu_ev_const_i(c, index), v };
        lu_ev_helper(c, H_slot_store, 3, args, 0);
    }
    else {
        lu_v args[4] = { owner, lu_ev_konst(c, name), v, lu_ev_const_i(c, 0) };
        lu_ev_helper(c, H_store_attr, 4, args, 2);
    }
    lu_ev_pop(c);
    lu_ev_pop(c);
    return 0;
}
