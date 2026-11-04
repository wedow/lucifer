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
