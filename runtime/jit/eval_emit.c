/* What the evaluator puts in the graph (eval.h): constants, guards with
   their exits, helper calls, virtual objects made real, boxes and their
   raw values, and the releases an instruction's end owes. */
#include "jit/eval.h"

#include <string.h>

lu_v
lu_ev_emit(struct ctx *c, int op, int rep, lu_v a, lu_v b, lu_v cc, int64_t imm)
{
    if (LU_IS_VIRT(a) || LU_IS_VIRT(b) || LU_IS_VIRT(cc)) {
        /* An object that does not exist is no operand: the build gives up
           rather than compile a read of nothing. */
        c->g->f->fail = "an instruction over an object that does not exist";
        a = LU_IS_VIRT(a) ? LU_NOV : a;
        b = LU_IS_VIRT(b) ? LU_NOV : b;
        cc = LU_IS_VIRT(cc) ? LU_NOV : cc;
    }
    lu_v v = lu_ir_emit(c->g->f, c->g->cur, op, rep, a, b, cc, imm);
    I(c, v)->src = c->code;
    I(c, v)->src_pc = c->pc;
    return v;
}

/* A constant runs nowhere (block -1) and is one value per build, so that
   two slots holding it compare equal at a join. */
static lu_v
constant(struct ctx *c, int op, int rep, int64_t imm, PyObject *o)
{
    struct gctx *g = c->g;
    if (2 * (g->nkonsts + 1) > g->capkonsts) {
        int cap = g->capkonsts ? 2 * g->capkonsts : 64;
        struct konst *tab = lu_ir_alloc(g->f, sizeof(struct konst) * (size_t)cap);
        for (int i = 0; i < cap; i++) {
            tab[i].v = LU_NOV;
        }
        for (int i = 0; i < g->capkonsts; i++) {
            if (g->konsts[i].v != LU_NOV) {
                uint64_t h = ((uint64_t)g->konsts[i].imm * 0x9e3779b97f4a7c15ULL) ^ (uint64_t)g->konsts[i].op;
                int at = (int)(h >> 20) & (cap - 1);
                while (tab[at].v != LU_NOV) {
                    at = (at + 1) & (cap - 1);
                }
                tab[at] = g->konsts[i];
            }
        }
        g->konsts = tab;
        g->capkonsts = cap;
    }
    uint64_t h = ((uint64_t)imm * 0x9e3779b97f4a7c15ULL) ^ (uint64_t)op;
    int at = (int)(h >> 20) & (g->capkonsts - 1);
    while (g->konsts[at].v != LU_NOV) {
        if (g->konsts[at].op == op && g->konsts[at].imm == imm) {
            return g->konsts[at].v;
        }
        at = (at + 1) & (g->capkonsts - 1);
    }
    lu_v v = op == OP_CONST ? lu_ir_const(g->f, -1, o) : lu_ir_emit(g->f, -1, op, rep, LU_NOV, LU_NOV, LU_NOV, imm);
    g->konsts[at] = (struct konst){ op, imm, v };
    g->nkonsts++;
    return v;
}

lu_v
lu_ev_konst(struct ctx *c, PyObject *o)
{
    return constant(c, OP_CONST, REP_OBJ, (int64_t)(intptr_t)o, o);
}

lu_v
lu_ev_const_i(struct ctx *c, int64_t i)
{
    return constant(c, OP_CONST_I, REP_I64, i, NULL);
}

lu_v
lu_ev_const_f(struct ctx *c, double d)
{
    int64_t bits;
    memcpy(&bits, &d, 8);
    return constant(c, OP_CONST_F, REP_F64, bits, NULL);
}

lu_v
lu_ev_guard(struct ctx *c, int op, lu_v a, int64_t imm)
{
    lu_v v = lu_ev_emit(c, op, REP_NONE, a, LU_NOV, LU_NOV, imm);
    I(c, v)->fs = lu_ev_exit_fs(c);
    return v;
}

void
lu_ev_guard_type(struct ctx *c, lu_v r, PyTypeObject *t)
{
    if (lu_ev_type(c, r) == t) {
        return;
    }
    lu_v v = lu_ev_val(c, r);
    lu_ev_guard(c, OP_GUARD_TYPE, v, (int64_t)(intptr_t)t);
    lu_ev_set_type(c, v, t);
}

/* The root's record of what r, the `which`-th value the current
   instruction asks about, has been: kept per chain of inlined frames. */
struct lu_prof *
lu_ev_prof(struct ctx *c, int which)
{
    uint64_t key = (uint64_t)which;
    for (struct ctx *k = c; k != NULL; k = k->parent) {
        key = lu_jit_ctx_mix(key, k->code, k->pc);
    }
    return lu_jit_prof(c->g->f->code, key);
}

void
lu_ev_profile(struct ctx *c, struct lu_prof *p, lu_v r)
{
    lu_v args[3] = { lu_ev_const_i(c, (int64_t)(intptr_t)p), r, lu_ev_const_i(c, (int64_t)c->g->learned) };
    I(c, lu_ev_guard(c, OP_GUARD_BIT, lu_ev_helper(c, H_profile, 3, args, 0), 1))->prof = p;
}

PyTypeObject *
lu_ev_learn_type(struct ctx *c, lu_v r, int which)
{
    PyTypeObject *t = lu_ev_type(c, r);
    if (t == NULL && LU_IS_VIRT(r)) {
        lu_ev_virt(c->g, &c->g->st, r, &r);     /* an object the code made, real by now */
    }
    if (t != NULL || r == LU_NOV || LU_IS_VIRT(r) || I(c, r)->rep != REP_OBJ) {
        return t;
    }
    struct lu_prof *p = lu_ev_prof(c, which);
    int poly = 1;
    t = p != NULL ? lu_jit_prof_type(p, &poly) : NULL;
    if (poly) {
        Py_XDECREF(t);
        if (p != NULL && lu_jit_prof_learns(p)) {
            lu_ev_profile(c, p, r);
        }
        return NULL;
    }
    if (t != NULL) {
        lu_ev_konst(c, (PyObject *)t);
        Py_DECREF(t);           /* the unit holds its constants */
        I(c, lu_ev_guard(c, OP_GUARD_TYPE, r, (int64_t)(intptr_t)t))->prof = p;
        lu_ev_set_type(c, r, t);
        return lu_ev_type(c, r);        /* none, of a class with no version to rely on */
    }
    lu_ev_profile(c, p, r);
    return NULL;
}

int
lu_ev_learn_kind(struct ctx *c, lu_v r, int which, unsigned long kind)
{
    PyTypeObject *t = lu_ev_learn_type(c, r, which);
    if (t != NULL || r == LU_NOV || LU_IS_VIRT(r) || I(c, r)->rep != REP_OBJ) {
        return t != NULL && (t->tp_flags & kind) == kind;
    }
    struct lu_prof *p = lu_ev_prof(c, which);
    if (p == NULL || nospec(c, c->pc) || (lu_jit_prof_kinds(p) & kind) != kind) {
        return 0;
    }
    I(c, lu_ev_guard(c, OP_GUARD_KIND, r, (int64_t)kind))->prof = p;
    return 1;
}

/* r as a constant: the one it is known to be, else the one of this kind
   the root met as the `which`-th value this instruction asks about
   (guarded, and r stands for it from here); NULL when it knows none yet,
   and compiled code then reports r's value here, or when it met others. */
PyObject *
lu_ev_learn_const(struct ctx *c, lu_v *r, int which, enum lu_prof_value_kind kind)
{
    PyObject *k = lu_ev_const(c, *r);
    if (k != NULL || *r == LU_NOV || LU_IS_VIRT(*r) || I(c, *r)->rep != REP_OBJ || nospec(c, c->pc)) {
        return k;
    }
    struct lu_prof *p = lu_ev_prof(c, 64 * kind + which);
    int poly = 1;
    k = p != NULL ? lu_jit_prof_value(p, kind, &poly) : NULL;
    if (poly || k == NULL) {
        if (!poly) {
            lu_ev_profile(c, p, *r);
        }
        Py_XDECREF(k);
        return NULL;
    }
    I(c, lu_ev_guard(c, OP_GUARD_IS, *r, (int64_t)(intptr_t)k))->prof = p;
    *r = lu_ev_konst(c, k);
    Py_DECREF(k);               /* held by the unit's constants */
    return k;
}

/* owner's type has version `version`. A type the path knows is relied on
   as of the version, a dependency of the unit (facts.c); a version the
   path read already is relied on the same way, by a guard marked proven. */
void
lu_ev_guard_version(struct ctx *c, lu_v owner, uint32_t version)
{
    PyTypeObject *t = lu_ev_type(c, owner);
    if (t != NULL && lu_ev_class_relied(c, lu_ev_konst(c, (PyObject *)t), version)) {
        return;
    }
    lu_v v = lu_ev_val(c, owner);
    struct fact *fa = lu_ev_fact(c, v, 0);
    lu_v g = lu_ev_guard(c, OP_GUARD_VERSION, v, version);
    I(c, g)->imm2 = GV_TYPE_OF;
    if (fa != NULL && fa->version == version) {
        I(c, g)->flags |= INS_PROVEN;
    }
    lu_ev_fact(c, v, 1)->version = version;
}

void
lu_ev_retain(struct ctx *c, lu_v v)
{
    PyObject *k = lu_ev_const(c, v);
    if (v == LU_NOV || (k != NULL && _Py_IsImmortal(k)) || I(c, v)->op == OP_BOOL
        || (I(c, v)->op == OP_CONST && k == NULL)) {
        return;
    }
    lu_ev_emit(c, OP_INCREF, REP_NONE, v, LU_NOV, LU_NOV, 0);
}

lu_v
lu_ev_val(struct ctx *c, lu_v r)
{
    if (!LU_IS_VIRT(r)) {
        return r;
    }
    struct gctx *g = c->g;
    lu_v real;
    struct lu_vobj *o = lu_ev_virt(g, &g->st, r, &real);
    if (real != LU_NOV) {
        return real;
    }
    lu_v v = LU_NOV;
    switch (o->kind) {
    case VK_BOX_INT:
        v = lu_ev_own(c, lu_ev_emit(c, OP_BOX_INT, REP_OBJ, o->f[0], o->n == 2 ? o->f[1] : LU_NOV, LU_NOV, 0));
        lu_ev_def_type(c, v, &PyLong_Type);
        break;
    case VK_BOX_FLOAT:
        v = lu_ev_own(c, lu_ev_emit(c, OP_BOX_FLOAT, REP_OBJ, o->f[0], o->n == 2 ? o->f[1] : LU_NOV, LU_NOV, 0));
        lu_ev_def_type(c, v, &PyFloat_Type);
        break;
    case VK_BOOL: {
        lu_v bit = o->neg ? lu_ev_emit(c, OP_NOT, REP_BIT, o->f[0], LU_NOV, LU_NOV, 0) : o->f[0];
        v = lu_ev_emit(c, OP_BOOL, REP_OBJ, bit, LU_NOV, LU_NOV, 0);
        lu_ev_def_type(c, v, &PyBool_Type);
        break;
    }
    case VK_TUPLE: {
        lu_v *items = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(o->n ? o->n : 1));
        for (int i = 0; i < o->n; i++) {
            items[i] = o->f[i];
        }
        v = lu_ev_helper(c, H_build_tuple, o->n, items, 0);
        lu_ev_def_type(c, v, &PyTuple_Type);
        break;
    }
    case VK_METHOD:
        v = lu_ev_helper(c, H_bind_method, 2, o->f, 0);
        lu_ev_def_type(c, v, &PyMethod_Type);
        break;
    case VK_FIELDS: {
        lu_v *args = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(o->n + 1));
        args[0] = lu_ev_konst(c, (PyObject *)o->type);
        memcpy(args + 1, o->f, sizeof(lu_v) * (size_t)o->n);
        v = lu_ev_helper(c, H_fields_new, o->n + 1, args, 0);
        lu_ev_def_type(c, v, o->type);
        break;
    }
    case VK_INST:
        v = lu_ev_instance_real(c, o, LU_VIRT_ID(r));
        break;
    case VK_EXC: {
        lu_v k = lu_ev_konst(c, (PyObject *)o->type);
        v = lu_ev_helper(c, H_exc_new, 1, &k, 0);
        lu_ev_def_type(c, v, o->type);
        break;
    }
    case VK_LIST:
        v = lu_ev_helper(c, H_build_list, o->n, o->f, 0);
        lu_ev_def_type(c, v, &PyList_Type);
        break;
    case VK_DICT:
        v = lu_ev_helper(c, o->n == 0 ? H_new_dict : H_build_map, o->n, o->f, 0);
        lu_ev_def_type(c, v, &PyDict_Type);
        break;
    case VK_GEN:
        v = lu_ev_gen_real(c, o);
        break;
    case VK_SPLICE: {
        lu_v *args = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(o->n + 1));
        args[0] = lu_ev_konst(c, (PyObject *)o->type);
        memcpy(args + 1, o->f, sizeof(lu_v) * (size_t)o->n);
        v = lu_ev_helper(c, H_splice, o->n + 1, args, 0);
        lu_ev_def_type(c, v, o->type);
        break;
    }
    case VK_SEQITER: {
        lu_v fields[2] = { o->f[0], o->f[1] };
        v = lu_ev_helper(c, o->type == &PyListIter_Type ? H_listiter_new : H_tupleiter_new, 2, fields, 0);
        lu_ev_def_type(c, v, o->type);
        break;
    }
    }
    /* The object holds its fields now, with references of its own: the
       record's stop naming them. A kind that cannot change keeps its
       record, so what is read from it later is still known. */
    for (int i = 0; i < o->n; i++) {
        lu_ev_drop(c, o->f[i]);
    }
    lu_ev_bind(g, &g->st, LU_VIRT_ID(r), v, LU_VKIND_MUTABLE(o->kind) ? NULL : o);
    return v;
}

lu_v
lu_ev_helper(struct ctx *c, int id, int nargs, const lu_v *args, int drop)
{
    struct gctx *g = c->g;
    const struct lu_helper *h = &lu_helpers[id];
    lu_v *vals = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nargs ? nargs : 1));
    for (int i = 0; i < nargs; i++) {
        vals[i] = args[i] == LU_NOV ? lu_ev_konst(c, NULL) : lu_ev_val(c, args[i]);
    }
    lu_v v = lu_ev_emit(c, OP_HELPER, h->rep, LU_NOV, LU_NOV, LU_NOV, id);
    I(c, v)->args = vals;
    I(c, v)->nargs = nargs;
    I(c, v)->drop = (uint8_t)drop;
    if (h->fx & (FX_RAISE | FX_PY | FX_SEEN)) {
        I(c, v)->fs = lu_ev_exit_fs(c);
    }
    g->observed |= (h->fx & (FX_PY | FX_SEEN)) != 0;
    if ((h->fx & FX_RAISE) && h->rep == REP_OBJ && c->kernel) {
        lu_ev_caught(c, v);
    }
    if (h->fx & FX_DECLINES) {
        /* Before the result counts as a reference: declined, it is none. */
        lu_v d = lu_ev_guard(c, OP_GUARD_NOT_NULL, v, 1);
        I(c, d)->flags |= INS_NOSTRESS;
    }
    if (h->rep == REP_OBJ) {
        lu_ev_own(c, v);
    }
    return v;
}

static int
fits(PyObject *k, int64_t *out)
{
    if (!PyLong_CheckExact(k)) {
        return 0;
    }
    int overflow;
    long long x = PyLong_AsLongLongAndOverflow(k, &overflow);
    *out = x;
    return !overflow && x != INT64_MIN;
}

lu_v
lu_ev_unbox_int(struct ctx *c, lu_v r)
{
    if (LU_IS_VIRT(r)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
        if (o != NULL && o->kind == VK_BOX_INT) {
            return o->f[0];
        }
    }
    int64_t i;
    PyObject *k = lu_ev_const(c, r);
    if (k != NULL && fits(k, &i)) {
        return lu_ev_const_i(c, i);
    }
    lu_v v = lu_ev_val(c, r);
    struct fact *fa = lu_ev_fact(c, v, 1);
    if (fa->raw == LU_NOV || I(c, fa->raw)->rep != REP_I64) {
        lu_v raw = lu_ev_guard(c, OP_UNBOX_INT, v, 0);
        I(c, raw)->rep = REP_I64;
        fa = lu_ev_fact(c, v, 1);
        fa->raw = raw;
        fa->t = &PyLong_Type;
    }
    return fa->raw;
}

lu_v
lu_ev_unbox_float(struct ctx *c, lu_v r)
{
    if (LU_IS_VIRT(r)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
        if (o != NULL && o->kind == VK_BOX_FLOAT) {
            return o->f[0];
        }
    }
    PyObject *k = lu_ev_const(c, r);
    if (k != NULL && PyFloat_CheckExact(k)) {
        return lu_ev_const_f(c, PyFloat_AS_DOUBLE(k));
    }
    lu_v v = lu_ev_val(c, r);
    struct fact *fa = lu_ev_fact(c, v, 1);
    if (fa->raw == LU_NOV || I(c, fa->raw)->rep != REP_F64) {
        lu_v raw = lu_ev_guard(c, OP_UNBOX_FLOAT, v, 0);
        I(c, raw)->rep = REP_F64;
        fa = lu_ev_fact(c, v, 1);
        fa->raw = raw;
        fa->t = &PyFloat_Type;
    }
    return fa->raw;
}

lu_v
lu_ev_box_int(struct ctx *c, lu_v raw)
{
    struct lu_vobj *o;
    lu_v r = lu_ev_virt_new(c, VK_BOX_INT, &PyLong_Type, 1, &o);
    o->f[0] = raw;
    return r;
}

lu_v
lu_ev_box_float(struct ctx *c, lu_v raw)
{
    struct lu_vobj *o;
    lu_v r = lu_ev_virt_new(c, VK_BOX_FLOAT, &PyFloat_Type, 1, &o);
    o->f[0] = raw;
    return r;
}

lu_v
lu_ev_bool(struct ctx *c, lu_v bit, int neg)
{
    if (I(c, bit)->op == OP_CONST_I) {
        return lu_ev_konst(c, (I(c, bit)->imm != 0) != (neg != 0) ? Py_True : Py_False);
    }
    struct lu_vobj *o;
    lu_v r = lu_ev_virt_new(c, VK_BOOL, &PyBool_Type, 1, &o);
    o->f[0] = bit;
    o->neg = (uint8_t)(neg != 0);
    return r;
}

lu_v
lu_ev_truth(struct ctx *c, lu_v r)
{
    PyObject *k = lu_ev_const(c, r);
    if (k == Py_True || k == Py_False || k == Py_None) {
        return lu_ev_const_i(c, k == Py_True);
    }
    if (LU_IS_VIRT(r)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
        if (o != NULL && o->kind == VK_BOOL) {
            return o->neg ? lu_ev_emit(c, OP_NOT, REP_BIT, o->f[0], LU_NOV, LU_NOV, 0) : o->f[0];
        }
        if (o != NULL && real == LU_NOV && (o->kind == VK_TUPLE || o->kind == VK_DICT || o->kind == VK_LIST)) {
            return lu_ev_const_i(c, o->n > 0);
        }
        if (o != NULL && o->kind == VK_SEQITER) {
            return lu_ev_const_i(c, 1);
        }
    }
    PyTypeObject *t = lu_ev_type(c, r);
    if (t == &PyBool_Type) {
        return lu_ev_emit(c, OP_IS, REP_BIT, lu_ev_val(c, r), lu_ev_konst(c, Py_True), LU_NOV, 0);
    }
    if (t == Py_TYPE(Py_None)) {
        return lu_ev_const_i(c, 0);
    }
    if (t == &PyLong_Type) {
        lu_v x = lu_ev_unbox_int(c, r);
        return I(c, x)->op == OP_CONST_I ? lu_ev_const_i(c, I(c, x)->imm != 0)
            : lu_ev_emit(c, OP_CMP_I, REP_BIT, x, lu_ev_const_i(c, 0), LU_NOV, Py_NE);
    }
    if (t == &PyFloat_Type) {
        /* -0.0 compares equal to 0.0, as it is false. */
        return lu_ev_emit(c, OP_CMP_F, REP_BIT, lu_ev_unbox_float(c, r), lu_ev_const_f(c, 0.0), LU_NOV, Py_NE);
    }
    /* The class version covers inherited methods as well. Read the MRO
       because slot refresh can finish after a type change is published. */
    uint32_t version;
    if (t != NULL && lu_ev_class_as_of(c, t, &version)
        && _PyType_Lookup(t, LU_ID(__bool__)) == NULL && _PyType_Lookup(t, LU_ID(__len__)) == NULL
        && lu_ev_class_relied(c, lu_ev_konst(c, (PyObject *)t), version)) {
        return lu_ev_const_i(c, 1);
    }
    lu_v bit = lu_ev_emit(c, OP_TRUTH, REP_BIT, lu_ev_val(c, r), LU_NOV, LU_NOV, 0);
    I(c, bit)->fs = lu_ev_exit_fs(c);
    I(c, bit)->drop = 1;
    c->g->observed = 1;
    return bit;
}

/* Whether freeing an object of the type runs nothing a program can see:
   it holds no other object and has no finalizer. */
static int
quiet(PyTypeObject *t)
{
    return t == &PyLong_Type || t == &PyFloat_Type || t == &PyBool_Type || t == &PyUnicode_Type
        || t == &PyBytes_Type || t == Py_TYPE(Py_None);
}

void
lu_ev_settle(struct ctx *c, int pc)
{
    struct gctx *g = c->g;
    if (g->cur < 0 || (g->npending == 0 && !g->observed)) {
        g->npending = 0;
        g->observed = 0;
        return;
    }
    /* What the state names now (as an exit here would read it: this
       frame's slots and its callers' after their calls) and which virtual
       objects every frame's slots reach, found in one walk; each pending
       value is then looked up, whatever their number. A walker's sight
       alone needs none. */
    if (g->npending > 0) {
        lu_ev_walk(c);
    }
    /* pending grows as dead virtual objects give up their fields. */
    lu_v *rel = NULL;
    int nrel = 0, caprel = 0;
    for (int i = 0; i < g->npending; i++) {
        lu_v r = g->pending[i];
        if (LU_IS_VIRT(r)) {
            if (lu_ev_reached(g, LU_VIRT_ID(r))) {
                continue;       /* reached, or given up already */
            }
            lu_v real;
            struct lu_vobj *o = lu_ev_virt(g, &g->st, r, &real);
            if (real != LU_NOV) {
                r = real;
            }
            else {
                for (int k = 0; k < o->n; k++) {
                    lu_ev_drop(c, o->f[k]);
                }
                continue;
            }
        }
        if (!(I(c, r)->flags & INS_OWNED) || r == g->keep || lu_ev_named(g, r)) {
            continue;           /* named, or released already */
        }
        if (nrel == caprel) {
            caprel = caprel ? 2 * caprel : 8;
            lu_v *p = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)caprel);
            memcpy(p, rel, sizeof(lu_v) * (size_t)nrel);
            rel = p;
        }
        rel[nrel++] = r;
    }
    g->npending = 0;
    struct lu_fs *fs = NULL;    /* the state after the instruction, copied when something leaves from it */
    for (int i = 0; i < nrel; i++) {
        PyTypeObject *t = lu_ev_type(c, rel[i]);
        if (t == &PyBool_Type || t == Py_TYPE(Py_None)) {
            continue;           /* its objects are immortal */
        }
        int noisy = !quiet(t);
        lu_v d = lu_ev_emit(c, OP_DECREF, REP_NONE, rel[i], LU_NOV, LU_NOV, noisy);
        if (noisy && !g->leaving) {
            /* Its finalizer sees the frames as they are after the
               instruction; an exit from there owes the releases to come.
               A returning root's releases take no state: an exit would
               resume its RETURN_VALUE with the value and the locals gone.
               The frame is off the chain (OP_UNLINK, a generator's too),
               so no walker finds it there; one that reaches a generator's
               through gi_frame finds nothing published and reads its
               locals unbound. Nor does the return need its unit alive. */
            fs = fs != NULL ? fs : lu_ev_snapshot(c, pc);
            struct lu_fs *at = lu_ir_alloc(g->f, sizeof(struct lu_fs));
            *at = *fs;
            at->held = rel + i + 1;
            at->nheld = nrel - i - 1;
            I(c, d)->fs = at;
            g->observed = 1;
        }
    }
    if (g->observed && !g->leaving) {
        lu_v w = lu_ev_emit(c, OP_GUARD_FRAMES, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        I(c, w)->fs = fs != NULL ? fs : lu_ev_snapshot(c, pc);
        I(c, w)->flags |= INS_NOSTRESS;
    }
    g->observed = 0;
}

void
lu_ev_side_exit(struct ctx *c, int reason)
{
    if (c->g->cur < 0) {
        return;
    }
    lu_v v = lu_ev_emit(c, OP_DEOPT, REP_NONE, LU_NOV, LU_NOV, LU_NOV, reason);
    I(c, v)->fs = lu_ev_exit_fs(c);
    c->g->f->bailouts[c->base & 255]++;
    c->g->cur = -1;
    c->g->npending = 0;
    c->g->observed = 0;
}
