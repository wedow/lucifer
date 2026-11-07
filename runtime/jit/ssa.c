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
    case OP_GUARD_TYPE: return CNT_guard_type;
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
    case OP_LOAD:
        return in->imm == offsetof(PyObject, ob_type) ? CNT_load_type
            : in->imm == offsetof(PyTypeObject, tp_version_tag) && in->imm2 == 4 ? CNT_load_version
            : in->imm == offsetof(PyVarObject, ob_size) ? CNT_load_size : CNT_load_other;
    case OP_INLINE_LOAD: case OP_INLINE_LOAD_F: case OP_INLINE_LOAD_I: case OP_ATTR_LOAD: return CNT_attr_load;
    case OP_INLINE_STORE: case OP_INLINE_STORE_F: case OP_INLINE_STORE_I: case OP_ATTR_STORE: return CNT_attr_store;
    case OP_GLOBAL_LOAD: return CNT_global_load;
    case OP_LIST_ITEM: case OP_LIST_STORE: case OP_STR_ORD: case OP_BYTES_ITEM: return CNT_item;
    case OP_HELPER: return in->imm2 > 0 ? (int)in->imm2 - 1 : CNT_helper;
    case OP_INCREF: return CNT_incref;
    case OP_DECREF: return CNT_decref;
    case OP_BOX_INT: case OP_BOX_FLOAT: return (in->flags & INS_VIRTUAL) ? -1 : CNT_box;
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

void *
lu_ir_alloc(struct lu_func *f, size_t size)
{
    size = (size + 15) & ~(size_t)15;
    if (f->arena == NULL || f->arena_used + size > f->arena_cap) {
        size_t cap = size > 65536 ? size : 65536;
        f->chunks = realloc(f->chunks, sizeof(char *) * (size_t)(f->nchunks + 1));
        f->arena = calloc(1, cap);
        if (f->chunks == NULL || f->arena == NULL) {
            Py_FatalError("lucifer jit: out of memory");
        }
        f->chunks[f->nchunks++] = f->arena;
        f->arena_used = 0;
        f->arena_cap = cap;
    }
    void *p = f->arena + f->arena_used;
    f->arena_used += size;
    return p;
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
    else {
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
    for (int v = 0; v < f->nins; v++) {
        if (f->ins[v].op == OP_PHI) {
            free(f->ins[v].args);
        }
    }
    free(f->blocks);
    free(f->ins);
    free(f->known);
    free(f->known_block);
    free(f->deps);
    free(f->order);
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
dump_fs(struct lu_fs *fs, FILE *out)
{
    for (; fs != NULL; fs = fs->caller) {
        fprintf(out, " [%s@%d", PyUnicode_AsUTF8(fs->code->co_name), fs->pc);
        for (int i = 0; i < fs->nlocals + fs->nstack; i++) {
            fprintf(out, "%s%s%d%s", i == fs->nlocals ? " |" : "", " v", fs->vals[i], fs->owned[i] ? "+" : "");
        }
        fprintf(out, "]");
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
            fprintf(out, "  v%d = %s%s r%d", v, lu_op_names[in->op], in->flags & INS_VIRTUAL ? "(virtual)" : "",
                    in->rep);
            if (in->a != LU_NOV) fprintf(out, " v%d", in->a);
            if (in->b != LU_NOV) fprintf(out, " v%d", in->b);
            if (in->c != LU_NOV) fprintf(out, " v%d", in->c);
            for (int j = 0; j < in->nargs; j++) {
                fprintf(out, " v%d", in->args[j]);
            }
            fprintf(out, " #%lld", (long long)in->imm);
            if (in->loc != LU_LOC_NONE) {
                fprintf(out, " @%d", in->loc);
            }
            if (in->fs != NULL) {
                dump_fs(in->fs, out);
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
