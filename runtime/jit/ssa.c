/* The IR's storage and printing (runtime/jit/ssa.h). */
#include "jit/target.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const lu_op_names[OP_COUNT] = {
#define LU_OP_NAME(name, rep, flags) #name,
    LU_IR_OPS(LU_OP_NAME)
#undef LU_OP_NAME
};

const uint8_t lu_op_flags[OP_COUNT] = {
#define LU_OP_FLAGS(name, rep, flags) flags,
    LU_IR_OPS(LU_OP_FLAGS)
#undef LU_OP_FLAGS
};

const char *const lu_count_names[CNT_COUNT] = {
#define LU_COUNT_NAME(name) #name,
    LU_COUNT_KINDS(LU_COUNT_NAME)
#undef LU_COUNT_NAME
};

int
lu_count_kind(const struct lu_ins *in)
{
    switch (in->op) {
    case OP_GUARD_TYPE: case OP_GUARD_KIND: return CNT_guard_type;
    case OP_GUARD_VERSION:
        return in->imm2 == GV_TYPE_OF ? CNT_guard_type_of : in->imm2 == GV_TYPE_ITSELF ? CNT_guard_type_itself
            : in->imm2 == GV_DICT_KEYS ? CNT_guard_keys : CNT_guard_func;
    case OP_GUARD_IS:
        return GI_MODE(in->imm2) == 0 ? CNT_guard_is : GI_MODE(in->imm2) == 1 ? CNT_guard_cell : CNT_guard_global;
    case OP_GUARD_BIT: return CNT_guard_bit;
    case OP_GUARD_NOT_NULL: return CNT_guard_not_null;
    case OP_GUARD_DEPTH: case OP_GUARD_FRAMES: return CNT_guard_other;
    case OP_UNBOX_INT: case OP_UNBOX_FLOAT: return CNT_unbox;
    case OP_POLL: return CNT_poll;
    case OP_CHECK_LIVE: return CNT_live;
    case OP_COUNT_OUT: return CNT_call_refused;
    case OP_LOAD:
        return in->imm == offsetof(PyObject, ob_type) ? CNT_load_type
            : in->imm == offsetof(PyTypeObject, tp_version_tag) && in->imm2 == 4 ? CNT_load_version
            : in->imm == offsetof(PyVarObject, ob_size) ? CNT_load_size : CNT_load_other;
    case OP_INLINE_LOAD: case OP_INLINE_LOAD_F: case OP_INLINE_LOAD_I: case OP_INLINE_ORIGIN: case OP_ATTR_LOAD:
    case OP_SLOT_LOAD:
        return CNT_attr_load;
    case OP_INLINE_STORE: case OP_INLINE_STORE_F: case OP_INLINE_STORE_I: case OP_ATTR_STORE: return CNT_attr_store;
    case OP_GLOBAL_LOAD: return CNT_global_load;
    case OP_LIST_ITEM: case OP_LIST_STORE: case OP_STR_ORD: case OP_BYTES_ITEM: case OP_TYPED_LOAD:
    case OP_TYPED_STORE: case OP_STR_CMP1:
        return CNT_item;
    case OP_HELPER: return in->imm2 > 0 ? (int)in->imm2 - 1 : CNT_helper;
    case OP_TRUTH: return CNT_truth;
    case OP_INCREF: return CNT_incref;
    case OP_DECREF: return CNT_decref;
    case OP_BOX_INT: case OP_BOX_FLOAT: return CNT_box;
    default: return -1;
    }
}

struct lu_vobj *
lu_ir_vobj(struct lu_func *f, int kind, PyTypeObject *type, int n)
{
    struct lu_vobj *o = lu_ir_alloc(f, sizeof(struct lu_vobj));
    o->kind = (uint8_t)kind;
    o->type = type;
    o->n = n;
    o->f = lu_ir_alloc(f, sizeof(lu_v) * (size_t)(n ? n : 1));
    for (int i = 0; i < n; i++) {
        o->f[i] = LU_NOV;
    }
    return o;
}

struct lu_vobj *
lu_fs_vobj(struct lu_func *f, const struct lu_fs *fs, int id, lu_v *real)
{
    *real = LU_NOV;
    for (int i = fs->nbinds - 1; i >= 0; i--) {
        if (fs->binds[i].id == id) {
            *real = fs->binds[i].real;
            return fs->binds[i].obj;
        }
    }
    return f->vobjs[id];
}

/* The values virtual object reference r stands for at fs: the object it
   became, or its fields, through any that are virtual themselves. Records
   form no cycles: a field is set when its record is made. */
static void
each_ref(struct lu_func *f, const struct lu_fs *fs, lu_v r, void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    if (r == LU_NOV) {
        return;
    }
    if (!LU_IS_VIRT(r)) {
        fn(f, r, arg);
        return;
    }
    lu_v real;
    struct lu_vobj *o = lu_fs_vobj(f, fs, LU_VIRT_ID(r), &real);
    if (real != LU_NOV) {
        fn(f, real, arg);
        return;
    }
    for (int i = 0; i < o->n; i++) {
        each_ref(f, fs, o->f[i], fn, arg);
    }
}

void
lu_fs_each(struct lu_func *f, const struct lu_fs *fs, void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    for (int i = 0; i < fs->nheld; i++) {
        fn(f, fs->held[i], arg);
    }
    for (const struct lu_fs *p = fs; p != NULL; p = p->caller) {
        lu_fs_each_frame(f, fs, p, fn, arg);
    }
}

void
lu_fs_each_frame(struct lu_func *f, const struct lu_fs *fs, const struct lu_fs *p,
                 void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    if (p->funcv >= 0) {
        fn(f, p->funcv, arg);
    }
    if (p->gen < 0) {
        fn(f, -1 - p->gen, arg);
    }
    for (int i = 0; i < p->nlocals + p->nstack; i++) {
        each_ref(f, fs, *lu_fs_slot(p, i), fn, arg);
    }
}

/* Whether fs's locals are the last state's the walk under mark saw:
   they are now. */
static int
locals_seen(struct lu_func *f, const struct lu_fs *fs, uint32_t mark)
{
    int seen = f->last_mark == mark && f->last_locals == fs->locals;
    f->last_mark = mark;
    f->last_locals = fs->locals;
    return seen;
}

void
lu_fs_each_once(struct lu_func *f, const struct lu_fs *fs, uint32_t mark,
                void (*fn)(struct lu_func *, lu_v, void *), void *arg)
{
    for (int i = 0; i < fs->nheld; i++) {
        fn(f, fs->held[i], arg);
    }
    int from = locals_seen(f, fs, mark) ? fs->nlocals : 0;
    for (int i = 0; i < from; i++) {
        if (LU_IS_VIRT(fs->locals[i])) {
            each_ref(f, fs, fs->locals[i], fn, arg);
        }
    }
    for (struct lu_fs *p = (struct lu_fs *)fs; p != NULL; p = p->caller, from = 0) {
        int n = p->nlocals + p->nstack;
        if (p != fs && p->mark == mark) {
            for (int i = 0; p->virt && i < n; i++) {
                lu_v r = *lu_fs_slot(p, i);
                if (LU_IS_VIRT(r)) {
                    each_ref(f, fs, r, fn, arg);
                }
            }
            continue;
        }
        if (p->funcv >= 0) {
            fn(f, p->funcv, arg);
        }
        if (p->gen < 0) {
            fn(f, -1 - p->gen, arg);
        }
        uint8_t virt = 0;
        for (int i = from; i < n; i++) {
            lu_v r = *lu_fs_slot(p, i);
            virt |= LU_IS_VIRT(r);
            each_ref(f, fs, r, fn, arg);
        }
        if (p != fs) {
            p->mark = mark;
            p->virt = virt;
        }
    }
}

void
lu_fs_rewrite(struct lu_func *f, struct lu_fs *fs, uint32_t mark, lu_v (*fn)(lu_v))
{
    for (int i = 0; i < fs->nheld; i++) {
        fs->held[i] = fn(fs->held[i]);
    }
    for (int i = 0; i < fs->nbinds; i++) {
        if (fs->binds[i].real != LU_NOV) {
            fs->binds[i].real = fn(fs->binds[i].real);
        }
    }
    int from = locals_seen(f, fs, mark) ? fs->nlocals : 0;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller, from = 0) {
        if (p != fs && p->mark == mark) {
            continue;
        }
        if (p != fs) {
            p->mark = mark;
        }
        if (p->funcv >= 0) {
            p->funcv = fn(p->funcv);
        }
        if (p->gen < 0) {
            p->gen = -1 - fn(-1 - p->gen);
        }
        for (int i = from; i < p->nlocals + p->nstack; i++) {
            lu_v *slot = lu_fs_slot(p, i);
            if (*slot >= 0) {
                *slot = fn(*slot);
            }
        }
    }
}

int
lu_alloc_kind(const struct lu_ins *in)
{
    if (in->flags & INS_DEAD) {
        return -1;
    }
    switch (in->op) {
    case OP_BOX_INT: return ALLOC_int;
    case OP_BOX_FLOAT: return ALLOC_float;
    case OP_HELPER:
        break;
    default:
        return -1;
    }
    switch (in->imm) {
    case H_box_int: return ALLOC_int;
    case H_box_float: return ALLOC_float;
    case H_build_tuple: case H_list_to_tuple: case H_splice: return ALLOC_tuple;
    case H_build_list: case H_list_repeat: return ALLOC_list;
    case H_new_dict: case H_build_map: return ALLOC_dict;
    case H_build_set: return ALLOC_set;
    case H_new_object: case H_fields_new: return ALLOC_instance;
    case H_get_iter: case H_get_iter_seq: case H_range_iter: case H_listiter_new: case H_tupleiter_new:
    case H_rangeiter_new: case H_gen_new:
        return ALLOC_iterator;
    case H_make_function: return ALLOC_function;
    case H_call: return ALLOC_class_call;
    default: return -1;
    }
}

static const uint8_t op_reps[OP_COUNT] = {
#define LU_OP_REP(name, rep, flags) rep,
    LU_IR_OPS(LU_OP_REP)
#undef LU_OP_REP
};

static void *
grow(void *p, int *cap, int need, size_t size)
{
    if (need <= *cap) {
        return p;
    }
    int n = *cap ? *cap : 16;
    while (n < need) {
        n *= 2;
    }
    p = realloc(p, (size_t)n * size);
    if (p == NULL) {
        Py_FatalError("lucifer jit: out of memory");
    }
    memset((char *)p + (size_t)*cap * size, 0, (size_t)(n - *cap) * size);
    *cap = n;
    return p;
}

void
lu_ir_dep(struct lu_func *f, struct lu_fdep *dep)
{
    for (int i = 0; i < f->ndeps; i++) {
        struct lu_fdep *d = &f->deps[i];
        if (d->kind == dep->kind && d->obj == dep->obj && d->sub == dep->sub) {
            return;
        }
    }
    if (f->ndeps == f->capdeps) {
        f->capdeps = f->capdeps ? 2 * f->capdeps : 8;
        f->deps = realloc(f->deps, sizeof(struct lu_fdep) * (size_t)f->capdeps);
    }
    f->deps[f->ndeps++] = *dep;
}

void *
lu_ir_alloc_chunk(struct lu_func *f, size_t size)
{
    size_t cap = size > 65536 ? size : 65536;
    f->chunks = realloc(f->chunks, sizeof(char *) * (size_t)(f->nchunks + 1));
    f->arena = calloc(1, cap);
    if (f->chunks == NULL || f->arena == NULL) {
        Py_FatalError("lucifer jit: out of memory");
    }
    f->chunks[f->nchunks++] = f->arena;
    f->arena_used = size;
    f->arena_cap = cap;
    return f->arena;
}

int
lu_ir_vobj_id(struct lu_func *f, struct lu_vobj *o)
{
    f->vobjs = grow(f->vobjs, &f->capvobjs, f->nvobjs + 1, sizeof(struct lu_vobj *));
    f->vobjs[f->nvobjs] = o;
    return f->nvobjs++;
}

int
lu_ir_block(struct lu_func *f)
{
    f->blocks = grow(f->blocks, &f->capblocks, f->nblocks + 1, sizeof(struct lu_block));
    struct lu_block *b = &f->blocks[f->nblocks];
    memset(b, 0, sizeof(*b));
    b->loop_end = -1;
    return f->nblocks++;
}

static void
block_append(struct lu_func *f, int block, int v)
{
    struct lu_block *b = &f->blocks[block];
    b->ins = grow(b->ins, &b->cap, b->nins + 1, sizeof(int));
    b->ins[b->nins++] = v;
}

lu_v
lu_ir_emit(struct lu_func *f, int block, int op, int rep, lu_v a, lu_v b, lu_v c, int64_t imm)
{
    f->ins = grow(f->ins, &f->capins, f->nins + 1, sizeof(struct lu_ins));
    lu_v v = f->nins++;
    struct lu_ins *in = &f->ins[v];
    memset(in, 0, sizeof(*in));
    in->op = (uint8_t)op;
    in->rep = (uint8_t)(rep != REP_NONE ? rep : op_reps[op]);
    in->block = block;
    in->a = a;
    in->b = b;
    in->c = c;
    in->imm = imm;
    in->loc = LU_LOC_NONE;
    if (op == OP_PHI) {
        /* Phis go before every other instruction of their block. */
        struct lu_block *bl = &f->blocks[block];
        bl->ins = grow(bl->ins, &bl->cap, bl->nins + 1, sizeof(int));
        int at = 0;
        while (at < bl->nins && f->ins[bl->ins[at]].op == OP_PHI) {
            at++;
        }
        memmove(&bl->ins[at + 1], &bl->ins[at], sizeof(int) * (size_t)(bl->nins - at));
        bl->ins[at] = v;
        bl->nins++;
    }
    else if (block >= 0) {
        block_append(f, block, v);
    }
    return v;
}

void
lu_ir_edge(struct lu_func *f, int from, int to)
{
    struct lu_block *a = &f->blocks[from], *b = &f->blocks[to];
    if (a->nsucc >= 2) {
        Py_FatalError("lucifer jit: a block with a third successor");
    }
    a->succ[a->nsucc++] = to;
    b->preds = grow(b->preds, &b->cappreds, b->npreds + 1, sizeof(int));
    b->preds[b->npreds++] = from;
}

lu_v
lu_ir_const(struct lu_func *f, int block, PyObject *o)
{
    f->consts = grow(f->consts, &f->capconsts, f->nconsts + 1, sizeof(PyObject *));
    f->consts[f->nconsts++] = Py_XNewRef(o);
    return lu_ir_emit(f, block, OP_CONST, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, (int64_t)(intptr_t)o);
}

void
lu_ir_free(struct lu_func *f)
{
    for (int i = 0; i < f->nblocks; i++) {
        free(f->blocks[i].ins);
        free(f->blocks[i].preds);
    }
    free(f->blocks);
    free(f->ins);
    free(f->outs);
    free(f->hots);
    free(f->known);
    free(f->vobjs);
    free(f->deps);
    free(f->order);
    free(f->idom);
    for (int i = 0; i < f->nconsts; i++) {
        Py_XDECREF(f->consts[i]);
    }
    free(f->consts);
    for (int i = 0; i < f->nchunks; i++) {
        free(f->chunks[i]);
    }
    free(f->chunks);
}

static void
dump_ref(struct lu_func *f, struct lu_fs *fs, lu_v r, FILE *out)
{
    static const char *const vkind_names[VK_KINDS] = { "int", "float", "bool", "tuple", "iter", "dict", "list", "method", "fields", "inst", "exc", "gen", "splice" };
    if (!LU_IS_VIRT(r)) {
        fprintf(out, " v%d", r);
        return;
    }
    lu_v real;
    struct lu_vobj *o = lu_fs_vobj(f, fs, LU_VIRT_ID(r), &real);
    if (real != LU_NOV) {
        fprintf(out, " o%d=v%d", LU_VIRT_ID(r), real);
        return;
    }
    fprintf(out, " o%d:%s(", LU_VIRT_ID(r), vkind_names[o->kind]);
    for (int i = 0; i < o->n; i++) {
        dump_ref(f, fs, o->f[i], out);
    }
    fprintf(out, " )");
}

static void
dump_fs(struct lu_func *f, struct lu_fs *fs, FILE *out)
{
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        fprintf(out, " [%s@%d", PyUnicode_AsUTF8(p->code->co_name), p->pc);
        for (int i = 0; i < p->nlocals + p->nstack; i++) {
            fprintf(out, "%s", i == p->nlocals ? " |" : "");
            dump_ref(f, fs, *lu_fs_slot(p, i), out);
        }
        fprintf(out, "]");
    }
    for (int i = 0; i < fs->nheld; i++) {
        fprintf(out, " held v%d", fs->held[i]);
    }
}

void
lu_ir_dump(struct lu_func *f, FILE *out)
{
    fprintf(out, "function %s entry %d\n", PyUnicode_AsUTF8(f->code->co_qualname), f->entry_pc);
    int n = f->norder ? f->norder : f->nblocks;
    for (int k = 0; k < n; k++) {
        int bi = f->norder ? f->order[k] : k;
        struct lu_block *b = &f->blocks[bi];
        fprintf(out, "b%d: preds", bi);
        for (int i = 0; i < b->npreds; i++) {
            fprintf(out, " b%d", b->preds[i]);
        }
        fprintf(out, "\n");
        for (int i = 0; i < b->nins; i++) {
            lu_v v = b->ins[i];
            struct lu_ins *in = &f->ins[v];
            if (in->flags & INS_DEAD) {
                continue;
            }
            fprintf(out, "  v%d = %s%s r%d", v, lu_op_names[in->op], in->flags & INS_OWNED ? "+" : "", in->rep);
            if (in->a != LU_NOV) fprintf(out, " v%d", in->a);
            if (in->b != LU_NOV) fprintf(out, " v%d", in->b);
            if (in->c != LU_NOV) fprintf(out, " v%d", in->c);
            for (int j = 0; j < in->nargs; j++) {
                fprintf(out, " v%d", in->args[j]);
            }
            if (in->op == OP_HELPER) {
                fprintf(out, " %s", lu_helpers[in->imm].name);
                if (in->nargs > 0 && f->ins[in->args[0]].op == OP_CONST && f->ins[in->args[0]].imm != 0
                    && PyCFunction_Check((PyObject *)(intptr_t)f->ins[in->args[0]].imm)) {
                    fprintf(out, ":%s", ((PyCFunctionObject *)(intptr_t)f->ins[in->args[0]].imm)->m_ml->ml_name);
                }
            }
            else {
                fprintf(out, " #%lld", (long long)in->imm);
            }
            if (in->loc != LU_LOC_NONE) {
                fprintf(out, " @%d", in->loc);
            }
            if (in->fs != NULL) {
                dump_fs(f, in->fs, out);
            }
            fprintf(out, "\n");
        }
        if (b->nsucc) {
            fprintf(out, "  -> b%d%s", b->succ[0], b->nsucc > 1 ? "" : "\n");
            if (b->nsucc > 1) {
                fprintf(out, " b%d\n", b->succ[1]);
            }
        }
    }
}
