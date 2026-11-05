/* The x86-64 backend (FOUNDATIONS 2.11): an encoder, the lowering of each
   IR instruction to it, parallel moves on edges and at calls, the exits'
   stubs and the publication of the code.

   Frame: rbp chains (perf and faulthandler unwind through it), rbx and
   r12-r15 are saved; r14 holds the PyThreadState and r15 the frame record
   for the unit's whole run. Below the saves are the result pointer, the
   spill words and, at rsp, the argument array of variadic helpers.
   rax, rcx, rdx and r11 are scratch, as are xmm0, xmm1 and xmm15.

   Each exit is `push index; jmp common`. The common exit saves every
   general and float register below the stack pointer and calls
   lu_jit_deopt with them and the spill words, which rebuilds the frames
   from the exit's description, so no per-exit code moves values. */
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
#define XMM(n) (LU_LOC_FPR + (n))

static const int8_t gprs[] = { RBX, R12, R13, RSI, RDI, R8, R9, R10 };
static const int8_t fprs[] = { XMM(2), XMM(3), XMM(4), XMM(5), XMM(6), XMM(7), XMM(8), XMM(9),
                               XMM(10), XMM(11), XMM(12), XMM(13), XMM(14) };
const struct lu_target lu_target = {
    .name = "x86_64", .ngpr = sizeof gprs, .nfpr = sizeof fprs, .gpr = gprs, .fpr = fprs,
};

#define SAVES 40                /* rbx, r12-r15 below rbp */
#define RESULT_SLOT (-(SAVES + 8))
#define SPILL_BASE (-(SAVES + 16))

struct patch {
    int at;                     /* the rel32 to fill */
    int label;
};

struct exit_site {
    int label;                  /* the stub's label */
    struct lu_deopt *d;
};

struct asm_ {
    uint8_t *buf;
    int n, cap;
    int *labels;
    int nlabels, caplabels;
    struct patch *patches;
    int npatches, cappatches;
    struct exit_site *exits;
    int nexits, capexits;
    int deopts_patch;           /* the imm64 that holds the exits' table */
    int epilogue;
    struct lu_func *f;
    struct lu_unit *unit;
    int frame_bytes;
};

/* --- the buffer --- */

static void
put(struct asm_ *a, const void *p, int n)
{
    if (a->n + n > a->cap) {
        a->cap = a->cap ? a->cap * 2 : 4096;
        while (a->n + n > a->cap) {
            a->cap *= 2;
        }
        a->buf = realloc(a->buf, (size_t)a->cap);
    }
    memcpy(a->buf + a->n, p, (size_t)n);
    a->n += n;
}

static void
b1(struct asm_ *a, int x)
{
    uint8_t c = (uint8_t)x;
    put(a, &c, 1);
}

static void
b4(struct asm_ *a, int32_t x)
{
    put(a, &x, 4);
}

static void
b8(struct asm_ *a, int64_t x)
{
    put(a, &x, 8);
}

static int
new_label(struct asm_ *a)
{
    if (a->nlabels == a->caplabels) {
        a->caplabels = a->caplabels ? a->caplabels * 2 : 64;
        a->labels = realloc(a->labels, sizeof(int) * (size_t)a->caplabels);
    }
    a->labels[a->nlabels] = -1;
    return a->nlabels++;
}

static void
bind(struct asm_ *a, int label)
{
    a->labels[label] = a->n;
}

static void
rel32_to(struct asm_ *a, int label)
{
    if (a->npatches == a->cappatches) {
        a->cappatches = a->cappatches ? a->cappatches * 2 : 64;
        a->patches = realloc(a->patches, sizeof(struct patch) * (size_t)a->cappatches);
    }
    a->patches[a->npatches++] = (struct patch){ a->n, label };
    b4(a, 0);
}

/* --- encoding --- */

static void
rex(struct asm_ *a, int w, int reg, int index, int base)
{
    int r = 0x40 | (w ? 8 : 0) | ((reg & 8) ? 4 : 0) | ((index & 8) ? 2 : 0) | ((base & 8) ? 1 : 0);
    if (r != 0x40) {
        b1(a, r);
    }
}

/* rex forced (for byte registers sil/dil and the like is not needed here). */
static void
modrm_rr(struct asm_ *a, int reg, int rm)
{
    b1(a, 0xC0 | ((reg & 7) << 3) | (rm & 7));
}

static void
modrm_mem(struct asm_ *a, int reg, int base, int32_t disp)
{
    b1(a, 0x80 | ((reg & 7) << 3) | (base & 7));
    if ((base & 7) == RSP) {
        b1(a, 0x24);
    }
    b4(a, disp);
}

/* op r64, r/m64 with reg in the reg field: `op` is one or two bytes. */
static void
op_rr(struct asm_ *a, int w, int opcode, int reg, int rm)
{
    rex(a, w, reg, 0, rm);
    if (opcode > 0xFF) {
        b1(a, opcode >> 8);
    }
    b1(a, opcode & 0xFF);
    modrm_rr(a, reg, rm);
}

static void
op_rm(struct asm_ *a, int w, int opcode, int reg, int base, int32_t disp)
{
    rex(a, w, reg, 0, base);
    if (opcode > 0xFF) {
        b1(a, opcode >> 8);
    }
    b1(a, opcode & 0xFF);
    modrm_mem(a, reg, base, disp);
}

static void
mov_rr(struct asm_ *a, int dst, int src)
{
    if (dst != src) {
        op_rr(a, 1, 0x89, src, dst);
    }
}

static void
load(struct asm_ *a, int dst, int base, int32_t disp)
{
    op_rm(a, 1, 0x8B, dst, base, disp);
}

static void
load32(struct asm_ *a, int dst, int base, int32_t disp)
{
    op_rm(a, 0, 0x8B, dst, base, disp);
}

static void
store(struct asm_ *a, int base, int32_t disp, int src)
{
    op_rm(a, 1, 0x89, src, base, disp);
}

static void
mov_ri(struct asm_ *a, int dst, int64_t imm)
{
    if (imm == 0) {
        op_rr(a, 0, 0x31, dst, dst);            /* xor r32, r32 */
    }
    else if (imm > 0 && imm <= 0xFFFFFFFFLL) {
        rex(a, 0, 0, 0, dst);
        b1(a, 0xB8 + (dst & 7));
        b4(a, (int32_t)(uint32_t)imm);
    }
    else if (imm >= INT32_MIN && imm < 0) {
        rex(a, 1, 0, 0, dst);
        b1(a, 0xC7);
        modrm_rr(a, 0, dst);
        b4(a, (int32_t)imm);
    }
    else {
        rex(a, 1, 0, 0, dst);
        b1(a, 0xB8 + (dst & 7));
        b8(a, imm);
    }
}

enum { ALU_ADD = 0, ALU_OR = 1, ALU_AND = 4, ALU_SUB = 5, ALU_XOR = 6, ALU_CMP = 7 };

static void
alu_rr(struct asm_ *a, int alu, int dst, int src)
{
    op_rr(a, 1, 0x01 + alu * 8, src, dst);
}

static void
alu_ri(struct asm_ *a, int alu, int dst, int32_t imm)
{
    rex(a, 1, 0, 0, dst);
    b1(a, 0x81);
    modrm_rr(a, alu, dst);
    b4(a, imm);
}

/* cmp r32, imm8 (sign-extended) */
static void
cmp32_ri8(struct asm_ *a, int r, int imm)
{
    rex(a, 0, 0, 0, r);
    b1(a, 0x83);
    modrm_rr(a, ALU_CMP, r);
    b1(a, imm);
}

/* cmp qword/dword [base+disp], imm32 */
static void
cmp_mi(struct asm_ *a, int w, int base, int32_t disp, int32_t imm)
{
    rex(a, w, 0, 0, base);
    b1(a, 0x81);
    modrm_mem(a, ALU_CMP, base, disp);
    b4(a, imm);
}

static void
cmp_rm(struct asm_ *a, int reg, int base, int32_t disp)
{
    op_rm(a, 1, 0x3B, reg, base, disp);
}

static void
test_rr(struct asm_ *a, int x, int y)
{
    op_rr(a, 1, 0x85, y, x);
}

static void
unary(struct asm_ *a, int digit, int r)         /* F7 /digit: not 2, neg 3, idiv 7 */
{
    rex(a, 1, 0, 0, r);
    b1(a, 0xF7);
    modrm_rr(a, digit, r);
}

static void
shift_cl(struct asm_ *a, int digit, int r)      /* D3 /digit: shl 4, sar 7 */
{
    rex(a, 1, 0, 0, r);
    b1(a, 0xD3);
    modrm_rr(a, digit, r);
}

enum { CC_O = 0, CC_NO = 1, CC_B = 2, CC_AE = 3, CC_E = 4, CC_NE = 5, CC_BE = 6, CC_A = 7,
       CC_S = 8, CC_NS = 9, CC_P = 10, CC_NP = 11, CC_L = 12, CC_GE = 13, CC_LE = 14, CC_G = 15 };

static void
jcc(struct asm_ *a, int cc, int label)
{
    b1(a, 0x0F);
    b1(a, 0x80 + cc);
    rel32_to(a, label);
}

static void
jmp(struct asm_ *a, int label)
{
    b1(a, 0xE9);
    rel32_to(a, label);
}

/* setcc into al or cl only: no byte-register REX cases. */
static void
setcc(struct asm_ *a, int cc, int r)
{
    b1(a, 0x0F);
    b1(a, 0x90 + cc);
    modrm_rr(a, 0, r);
}

static void
movzx8(struct asm_ *a, int dst, int src)
{
    if (src >= 4 && src < 8) {
        b1(a, 0x40 | ((dst & 8) ? 4 : 0));
    }
    else {
        rex(a, 0, dst, 0, src);
    }
    b1(a, 0x0F);
    b1(a, 0xB6);
    modrm_rr(a, dst, src);
}

static void
call_abs(struct asm_ *a, const void *fn)
{
    mov_ri(a, RAX, (int64_t)(intptr_t)fn);
    b1(a, 0xFF);
    b1(a, 0xD0);
}

/* A reference to the object in RAX (r) for the owning thread, taken in
   place: an immortal needs none; another thread's object leaves (biased
   counting lets only the owner skip the atomic try). Clobbers RCX, R11. */
static void
incref_owned_or_exit(struct asm_ *a, int r, int fail)
{
    int done = new_label(a);
    load32(a, RCX, r, offsetof(PyObject, ob_ref_local));
    rex(a, 0, 0, 0, RCX);
    b1(a, 0x83);
    modrm_rr(a, ALU_ADD, RCX);
    b1(a, 1);                                   /* add ecx, 1 */
    jcc(a, CC_E, done);                         /* immortal */
    b1(a, 0x64);
    b1(a, 0x4C);
    b1(a, 0x8B);
    b1(a, 0x1C);
    b1(a, 0x25);
    b4(a, 0);                                   /* mov r11, fs:[0] */
    cmp_rm(a, R11, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, fail);
    op_rm(a, 0, 0x89, RCX, r, offsetof(PyObject, ob_ref_local));
    bind(a, done);
}

/* A call on a slow path that keeps every allocatable register: the
   caller-saved ones go to the stack around it (rsi, rdi, r8-r10 and
   xmm2-14; 160 bytes keep the stack aligned). Its arguments are set by
   `setup` after the saves. */
static void sse_rm(struct asm_ *a, int prefix, int op, int reg, int base, int32_t disp);

static void
save_volatile(struct asm_ *a)
{
    static const int regs[] = { RSI, RDI, R8, R9, R10 };
    for (int i = 0; i < 5; i++) {
        rex(a, 0, 0, 0, regs[i]);
        b1(a, 0x50 + (regs[i] & 7));            /* push */
    }
    alu_ri(a, ALU_SUB, RSP, 120);
    for (int x = 2; x <= 14; x++) {
        sse_rm(a, 0xF2, 0x11, x, RSP, 8 * (x - 2));
    }
}

static void
restore_volatile(struct asm_ *a)
{
    static const int regs[] = { RSI, RDI, R8, R9, R10 };
    for (int x = 2; x <= 14; x++) {
        sse_rm(a, 0xF2, 0x10, x, RSP, 8 * (x - 2));
    }
    alu_ri(a, ALU_ADD, RSP, 120);
    for (int i = 4; i >= 0; i--) {
        rex(a, 0, 0, 0, regs[i]);
        b1(a, 0x58 + (regs[i] & 7));            /* pop */
    }
}

/* SSE2: prefix, 0F, op, with xmm registers as 0..15. */
static void
sse_rr(struct asm_ *a, int prefix, int op, int reg, int rm, int w)
{
    if (prefix) {
        b1(a, prefix);
    }
    rex(a, w, reg, 0, rm);
    b1(a, 0x0F);
    b1(a, op);
    modrm_rr(a, reg, rm);
}

static void
sse_rm(struct asm_ *a, int prefix, int op, int reg, int base, int32_t disp)
{
    b1(a, prefix);
    rex(a, 0, reg, 0, base);
    b1(a, 0x0F);
    b1(a, op);
    modrm_mem(a, reg, base, disp);
}

static void
movsd_rr(struct asm_ *a, int dst, int src)
{
    if (dst != src) {
        sse_rr(a, 0x66, 0x28, dst, src, 0);     /* movapd */
    }
}

static void
movq_xr(struct asm_ *a, int xmm, int gpr)     /* movq xmm, r64 */
{
    sse_rr(a, 0x66, 0x6E, xmm, gpr, 1);
}

static void
movq_rx(struct asm_ *a, int gpr, int xmm)     /* movq r64, xmm */
{
    sse_rr(a, 0x66, 0x7E, xmm, gpr, 1);
}

/* --- locations --- */

static int32_t
spill_disp(int loc)
{
    return SPILL_BASE - 8 * LU_LOC_SPILL_INDEX(loc);
}

static struct lu_ins *
I(struct asm_ *a, lu_v v)
{
    return &a->f->ins[v];
}

/* Puts value v into general register r. */
static void
to_gpr(struct asm_ *a, int r, lu_v v)
{
    struct lu_ins *in = I(a, v);
    if (in->op == OP_CONST || in->op == OP_CONST_I) {
        mov_ri(a, r, in->imm);
    }
    else if (in->loc >= 0 && in->loc < LU_LOC_FPR) {
        mov_rr(a, r, in->loc);
    }
    else if (LU_LOC_IS_SPILL(in->loc)) {
        load(a, r, RBP, spill_disp(in->loc));
    }
    else if (in->loc >= LU_LOC_FPR) {
        movq_rx(a, r, in->loc - LU_LOC_FPR);
    }
    else {
        mov_ri(a, r, 0);        /* a value without a location has no use */
    }
}

/* A general register holding v: its own, or `scratch` loaded. */
static int
gpr_of(struct asm_ *a, lu_v v, int scratch)
{
    struct lu_ins *in = I(a, v);
    if (in->op != OP_CONST && in->op != OP_CONST_I && in->loc >= 0 && in->loc < LU_LOC_FPR) {
        return in->loc;
    }
    to_gpr(a, scratch, v);
    return scratch;
}

static void
to_fpr(struct asm_ *a, int x, lu_v v)
{
    struct lu_ins *in = I(a, v);
    if (in->op == OP_CONST_F) {
        mov_ri(a, RAX, in->imm);
        movq_xr(a, x, RAX);
    }
    else if (in->loc >= LU_LOC_FPR) {
        movsd_rr(a, x, in->loc - LU_LOC_FPR);
    }
    else if (LU_LOC_IS_SPILL(in->loc)) {
        sse_rm(a, 0xF2, 0x10, x, RBP, spill_disp(in->loc));
    }
    else if (in->loc >= 0) {
        movq_xr(a, x, in->loc);
    }
}

static int
fpr_of(struct asm_ *a, lu_v v, int scratch)
{
    struct lu_ins *in = I(a, v);
    if (in->op != OP_CONST_F && in->loc >= LU_LOC_FPR) {
        return in->loc - LU_LOC_FPR;
    }
    to_fpr(a, scratch, v);
    return scratch;
}

/* Stores general register r as v's value. */
static void
def_gpr(struct asm_ *a, lu_v v, int r)
{
    int loc = I(a, v)->loc;
    if (loc >= 0 && loc < LU_LOC_FPR) {
        mov_rr(a, loc, r);
    }
    else if (LU_LOC_IS_SPILL(loc)) {
        store(a, RBP, spill_disp(loc), r);
    }
}

static void
def_fpr(struct asm_ *a, lu_v v, int x)
{
    int loc = I(a, v)->loc;
    if (loc >= LU_LOC_FPR) {
        movsd_rr(a, loc - LU_LOC_FPR, x);
    }
    else if (LU_LOC_IS_SPILL(loc)) {
        sse_rm(a, 0xF2, 0x11, x, RBP, spill_disp(loc));
    }
}

/* --- parallel moves --- */

struct move {
    int dst;                    /* a location */
    lu_v src;                   /* a value, or LU_NOV with srcloc */
    int srcloc;
    int fpr;
};

static int
move_src_loc(struct asm_ *a, struct move *m)
{
    if (m->src == LU_NOV) {
        return m->srcloc;
    }
    struct lu_ins *in = I(a, m->src);
    return in->op == OP_CONST || in->op == OP_CONST_I || in->op == OP_CONST_F ? LU_LOC_NONE : in->loc;
}

static void
do_move(struct asm_ *a, struct move *m)
{
    int s = move_src_loc(a, m);
    if (m->dst == s && s != LU_LOC_NONE) {
        return;
    }
    if (m->src != LU_NOV && s == LU_LOC_NONE) {
        /* A constant. */
        if (m->dst >= LU_LOC_FPR) {
            to_fpr(a, m->dst - LU_LOC_FPR, m->src);
        }
        else if (m->dst >= 0) {
            to_gpr(a, m->dst, m->src);
        }
        else if (m->fpr) {
            to_fpr(a, 15, m->src);
            sse_rm(a, 0xF2, 0x11, 15, RBP, spill_disp(m->dst));
        }
        else {
            to_gpr(a, R11, m->src);
            store(a, RBP, spill_disp(m->dst), R11);
        }
        return;
    }
    /* Location to location. */
    if (s >= LU_LOC_FPR || m->dst >= LU_LOC_FPR || m->fpr) {
        int x = s >= LU_LOC_FPR ? s - LU_LOC_FPR : 15;
        if (LU_LOC_IS_SPILL(s)) {
            sse_rm(a, 0xF2, 0x10, 15, RBP, spill_disp(s));
        }
        else if (s >= 0 && s < LU_LOC_FPR) {
            movq_xr(a, 15, s);
        }
        if (m->dst >= LU_LOC_FPR) {
            movsd_rr(a, m->dst - LU_LOC_FPR, x);
        }
        else if (LU_LOC_IS_SPILL(m->dst)) {
            sse_rm(a, 0xF2, 0x11, x, RBP, spill_disp(m->dst));
        }
        else {
            movq_rx(a, m->dst, x);
        }
        return;
    }
    int r = s >= 0 ? s : R11;
    if (LU_LOC_IS_SPILL(s)) {
        load(a, R11, RBP, spill_disp(s));
    }
    if (m->dst >= 0) {
        mov_rr(a, m->dst, r);
    }
    else {
        store(a, RBP, spill_disp(m->dst), r);
    }
}

/* Performs the moves as if at once. Spill-to-spill moves never conflict
   with register moves; register cycles break through a scratch (rax or
   xmm1). */
static void
parallel_moves(struct asm_ *a, struct move *moves, int n)
{
    int *done = calloc((size_t)n + 1, sizeof(int));
    int left = n;
    for (int i = 0; i < n; i++) {
        if (moves[i].dst == move_src_loc(a, &moves[i]) && moves[i].dst != LU_LOC_NONE) {
            done[i] = 1;
            left--;
        }
    }
    while (left > 0) {
        int progress = 0;
        for (int i = 0; i < n; i++) {
            if (done[i]) {
                continue;
            }
            int blocked = 0;
            for (int j = 0; j < n; j++) {
                if (!done[j] && j != i && move_src_loc(a, &moves[j]) == moves[i].dst) {
                    blocked = 1;
                    break;
                }
            }
            if (!blocked) {
                do_move(a, &moves[i]);
                done[i] = 1;
                left--;
                progress = 1;
            }
        }
        if (!progress) {
            /* A cycle: park one source in a scratch register. */
            for (int i = 0; i < n; i++) {
                if (done[i]) {
                    continue;
                }
                int s = move_src_loc(a, &moves[i]);
                int scratch = s >= LU_LOC_FPR ? XMM(1) : RAX;
                struct move park = { scratch, LU_NOV, s, s >= LU_LOC_FPR };
                do_move(a, &park);
                for (int j = 0; j < n; j++) {
                    if (!done[j] && move_src_loc(a, &moves[j]) == s) {
                        moves[j].src = LU_NOV;
                        moves[j].srcloc = scratch;
                    }
                }
                break;
            }
        }
    }
    free(done);
}

/* --- exits --- */

static struct lu_dv
describe(struct asm_ *a, lu_v v, int owned)
{
    struct lu_dv dv = { .kind = DV_NULL, .loc = LU_LOC_NONE, .vid = v };
    if (v == LU_NOV) {
        return dv;
    }
    struct lu_ins *in = I(a, v);
    if (in->op == OP_CONST) {
        dv.kind = in->imm == 0 ? DV_NULL : DV_CONST;
        dv.imm = in->imm;
        return dv;
    }
    if ((in->flags & INS_VIRTUAL) && in->b != LU_NOV) {
        return describe(a, in->b, owned);      /* the object it was unboxed from */
    }
    if (in->flags & INS_VIRTUAL) {
        struct lu_ins *raw = I(a, in->a);
        dv.kind = in->op == OP_BOX_INT ? DV_BOX_I64 : DV_BOX_F64;
        if (raw->op == OP_CONST_I || raw->op == OP_CONST_F) {
            dv.imm = raw->imm;
        }
        else {
            dv.loc = raw->loc;
        }
        if (in->c != LU_NOV) {
            struct lu_ins *bit = I(a, in->c);
            dv.maybe_null = 1;
            dv.bitloc = bit->op == OP_CONST_I ? LU_LOC_NONE : bit->loc;
            dv.bitimm = (uint8_t)(bit->op == OP_CONST_I && bit->imm);
        }
        return dv;
    }
    dv.kind = DV_OBJ;
    dv.loc = in->loc;
    dv.owned = (uint8_t)owned;
    return dv;
}

static struct lu_deopt *
make_deopt(struct asm_ *a, struct lu_fs *fs, int reason, int error, int drop)
{
    struct lu_deopt *d = calloc(1, sizeof(struct lu_deopt));
    int n = 0;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        n++;
    }
    d->nframes = n;
    d->frames = calloc((size_t)n, sizeof(struct lu_deopt_frame));
    int k = n - 1;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller, k--) {
        struct lu_deopt_frame *fr = &d->frames[k];
        fr->code = p->code;
        fr->func = p->func;
        fr->pc = p->pc;
        fr->nlocals = p->nlocals;
        fr->nstack = p->nstack;
        int m = p->nlocals + p->nstack;
        fr->vals = calloc((size_t)(m ? m : 1), sizeof(struct lu_dv));
        for (int i = 0; i < m; i++) {
            fr->vals[i] = describe(a, p->vals[i], p->owned[i]);
        }
    }
    d->reason = reason;
    d->error = error;
    d->drop = drop;
    d->opcode = lu_base_opcode(fs->code, fs->pc);
    return d;
}

/* A label to jump to that leaves at fs. */
static int
exit_to(struct asm_ *a, struct lu_fs *fs, int reason, int error, int drop)
{
    if (a->nexits == a->capexits) {
        a->capexits = a->capexits ? a->capexits * 2 : 32;
        a->exits = realloc(a->exits, sizeof(struct exit_site) * (size_t)a->capexits);
    }
    int label = new_label(a);
    a->exits[a->nexits++] = (struct exit_site){ label, make_deopt(a, fs, reason, error, drop) };
    return label;
}

/* A guard's failure jumps here; under -X lucifer_stress=deopt:N, every
   Nth guard execution also leaves (FOUNDATIONS 2.7). */
static int
guard_exit(struct asm_ *a, struct lu_ins *in)
{
    if (a->f->stress && !(in->flags & INS_NOSTRESS)) {
        int stress = exit_to(a, in->fs, EXIT_STRESS, 0, 0);
        a->exits[a->nexits - 1].d->stress = a->unit->stress_counter;
        mov_ri(a, R11, (int64_t)(intptr_t)a->unit->stress_counter);
        rex(a, 1, 0, 0, R11);
        b1(a, 0xFF);
        modrm_mem(a, 1, R11, 0);                /* dec qword [r11] */
        jcc(a, CC_E, stress);
    }
    return exit_to(a, in->fs, EXIT_GUARD, 0, 0);
}

/* --- calls --- */

extern int lu_jit_next_pc(PyCodeObject *code, int pc);

static void
call_helper(struct asm_ *a, lu_v v)
{
    struct lu_ins *in = I(a, v);
    const struct lu_helper *h = &lu_helpers[in->imm];
    if ((h->flags & IF_PY) && in->fs != NULL) {
        /* The frame shows the call's instruction as the one that ran. */
        struct lu_fs *root = in->fs;
        while (root->caller != NULL) {
            root = root->caller;
        }
        int next = lu_jit_next_pc(root->code, root->pc);
        mov_ri(a, RAX, (int64_t)(intptr_t)(lu_code_start(root->code) + next));
        store(a, R15, offsetof(struct lu_frame, instr), RAX);
        store(a, R15, offsetof(struct lu_frame, last), RAX);
    }
    static const int argregs[] = { RDI, RSI, RDX, RCX, R8, R9 };
    struct move moves[8];
    int nm = 0;
    if (h->nargs == 0) {
        /* Variadic: the values in the argument array at rsp. */
        for (int i = 0; i < in->nargs; i++) {
            to_gpr(a, RAX, in->args[i]);
            store(a, RSP, 8 * i, RAX);
        }
        mov_rr(a, RDI, RSP);
        mov_ri(a, RSI, in->nargs);
    }
    else {
        int gi = 0, fi = 0;
        for (int i = 0; i < in->nargs; i++) {
            struct lu_ins *arg = I(a, in->args[i]);
            if (arg->rep == REP_F64) {
                moves[nm++] = (struct move){ XMM(fi++), in->args[i], 0, 1 };
            }
            else {
                moves[nm++] = (struct move){ argregs[gi++], in->args[i], 0, 0 };
            }
        }
        parallel_moves(a, moves, nm);
    }
    call_abs(a, h->fn);
}

/* --- the instructions --- */

static void
incref(struct asm_ *a, int r)
{
    /* Py_INCREF on the free-threaded build: immortal objects stay; the
       owning thread counts locally, others in the shared count. */
    int done = new_label(a), shared = new_label(a);
    test_rr(a, r, r);
    jcc(a, CC_E, done);                         /* Py_XINCREF: a phi may carry NULL */
    load32(a, RCX, r, offsetof(PyObject, ob_ref_local));
    rex(a, 0, 0, 0, RCX);
    b1(a, 0x83);
    modrm_rr(a, ALU_ADD, RCX);
    b1(a, 1);                                   /* add ecx, 1 */
    jcc(a, CC_E, done);                         /* immortal: the count wraps to 0 */
    b1(a, 0x64);
    b1(a, 0x48);
    b1(a, 0x8B);
    b1(a, 0x04);
    b1(a, 0x25);
    b4(a, 0);                                   /* mov rax, fs:[0] */
    cmp_rm(a, RAX, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, shared);
    op_rm(a, 0, 0x89, RCX, r, offsetof(PyObject, ob_ref_local));
    jmp(a, done);
    bind(a, shared);
    b1(a, 0xF0);
    rex(a, 1, 0, 0, r);
    b1(a, 0x83);
    modrm_mem(a, ALU_ADD, r, offsetof(PyObject, ob_ref_shared));
    b1(a, 1 << _Py_REF_SHARED_SHIFT);
    bind(a, done);
}

extern void lu_jit_decref(PyObject *o);
extern void lu_jit_store_local(struct lu_frame *f, intptr_t i, PyObject *v);
extern PyObject *lu_jit_box_int(int64_t v);
extern PyObject *lu_jit_box_float(double v);
extern PyObject *lu_jit_list_item(PyObject *list, int64_t i);
extern int lu_jit_list_store(PyObject *list, int64_t i, PyObject *v);
extern PyObject *lu_jit_attr_load(PyObject *owner, int64_t version, int64_t index, PyObject *name);
extern int lu_jit_truth(PyObject *v);

/* A box of in->a's raw value, in RAX. */
static void
emit_box(struct asm_ *a, struct lu_ins *in)
{
    save_volatile(a);
    if (in->op == OP_BOX_INT) {
        to_gpr(a, RDI, in->a);
        call_abs(a, (void *)lu_jit_box_int);
    }
    else {
        to_fpr(a, 0, in->a);
        call_abs(a, (void *)lu_jit_box_float);
    }
    restore_volatile(a);
}

static int
cond_of(int op, int swap)
{
    static const int cc[] = { CC_L, CC_LE, CC_E, CC_NE, CC_G, CC_GE };
    static const int sw[] = { CC_G, CC_GE, CC_E, CC_NE, CC_L, CC_LE };
    return swap ? sw[op] : cc[op];
}

/* INLINE_LOAD_F and _I: an inline value as binary64 (int64): an unboxed
   value's raw word, or an exact float's (a compact int's) value, read only
   when the owning thread holds the object (as INLINE_LOAD takes a
   reference only then). */
static void
inline_load_raw(struct asm_ *a, struct lu_ins *in, lu_v v)
{
    int is_int = in->op == OP_INLINE_LOAD_I;
    int fail = guard_exit(a, in);
    int boxed = new_label(a), done = new_label(a);
    int r = gpr_of(a, in->a, RDX);
    rex(a, 0, 0, 0, r);
    b1(a, 0xF6);
    modrm_mem(a, 0, r, offsetof(PyObject, ob_flags) + 1);
    b1(a, LU_ATTRS_IN_DICT >> 8);
    jcc(a, CC_NE, fail);
    load(a, RAX, r, (int32_t)in->imm);
    alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)(is_int ? LU_UNBOXED_INT : LU_UNBOXED));
    jcc(a, CC_NE, boxed);
    if (is_int) {
        load(a, RAX, r, (int32_t)in->imm + 8);
    }
    else {
        sse_rm(a, 0xF2, 0x10, 0, r, (int32_t)in->imm + 8);     /* movsd xmm0, the binary64 */
    }
    jmp(a, done);
    bind(a, boxed);
    alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
    jcc(a, CC_BE, fail);                        /* absent, or the other kind unboxed */
    mov_ri(a, RCX, (int64_t)(intptr_t)(is_int ? &PyLong_Type : &PyFloat_Type));
    cmp_rm(a, RCX, RAX, offsetof(PyObject, ob_type));
    jcc(a, CC_NE, fail);
    int held = new_label(a);
    load32(a, RCX, RAX, offsetof(PyObject, ob_ref_local));
    cmp32_ri8(a, RCX, -1);
    jcc(a, CC_E, held);                         /* immortal (a small int) */
    b1(a, 0x64);
    b1(a, 0x4C);
    b1(a, 0x8B);
    b1(a, 0x1C);
    b1(a, 0x25);
    b4(a, 0);                                   /* mov r11, fs:[0] */
    cmp_rm(a, R11, RAX, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, fail);
    bind(a, held);
    if (is_int) {
        /* A compact int, as UNBOX_INT reads it. */
        load(a, RCX, RAX, offsetof(PyLongObject, long_value.lv_tag));
        alu_ri(a, ALU_CMP, RCX, 2 << _PyLong_NON_SIZE_BITS);
        jcc(a, CC_AE, fail);
        load32(a, RAX, RAX, offsetof(PyLongObject, long_value.ob_digit));
        int pos = new_label(a);
        rex(a, 0, 0, 0, RCX);
        b1(a, 0xF6);
        modrm_rr(a, 0, RCX);
        b1(a, 2);                               /* test cl, 2: negative */
        jcc(a, CC_E, pos);
        unary(a, 3, RAX);
        bind(a, pos);
    }
    else {
        sse_rm(a, 0xF2, 0x10, 0, RAX, offsetof(PyFloatObject, ob_fval));
    }
    bind(a, done);
    if (is_int) {
        def_gpr(a, v, RAX);
    }
    else {
        def_fpr(a, v, 0);
    }
}

/* INLINE_STORE_F and _I: lu_inline_store_f (_i) in line. Under the
   owner's lock byte, taken with one cmpxchg, a word that already holds a
   value takes the raw value and its marker; the old value, an object, is
   released after the unlock. Anything else is C's, and leaves when it
   declines. */
static void
inline_store_raw(struct asm_ *a, struct lu_ins *in)
{
    int is_int = in->op == OP_INLINE_STORE_I;
    struct move m[2] = { { RDI, in->a, 0, 0 }, is_int ? (struct move){ RSI, in->b, 0, 0 }
                                                      : (struct move){ XMM(0), in->b, 0, 1 } };
    int fail = guard_exit(a, in);
    int slow = new_label(a), unlock_slow = new_label(a), unlocked = new_label(a), done = new_label(a);
    int32_t flags = offsetof(PyObject, ob_flags) + 1, mutex = offsetof(PyObject, ob_mutex);
    int32_t off = (int32_t)in->imm;
    parallel_moves(a, m, 2);
    mov_rr(a, R12, RDI);                        /* kept across calls: nothing is live here (IF_CALL) */
    if (is_int) {
        mov_rr(a, R13, RSI);
    }
    else {
        movq_rx(a, R13, 0);
    }
    b1(a, 0xF6);
    modrm_mem(a, 0, RDI, flags);
    b1(a, LU_ATTRS_IN_DICT >> 8);               /* test byte [rdi+flags+1], LU_ATTRS_IN_DICT */
    jcc(a, CC_NE, slow);
    alu_rr(a, ALU_XOR, RAX, RAX);
    b1(a, 0xB1);
    b1(a, 1);                                   /* mov cl, 1 */
    b1(a, 0xF0);
    b1(a, 0x0F);
    b1(a, 0xB0);
    modrm_mem(a, RCX, RDI, mutex);              /* lock cmpxchg [rdi+mutex], cl */
    jcc(a, CC_NE, slow);
    b1(a, 0xF6);
    modrm_mem(a, 0, RDI, flags);
    b1(a, LU_ATTRS_IN_DICT >> 8);
    jcc(a, CC_NE, unlock_slow);                 /* moved to the dict before the lock */
    load(a, RBX, RDI, off);                     /* the old value, kept across calls */
    test_rr(a, RBX, RBX);
    jcc(a, CC_E, unlock_slow);                  /* a new name: insertion order is C's */
    store(a, RDI, off + 8, R13);                /* the raw value */
    mov_ri(a, RAX, (int64_t)(intptr_t)(is_int ? LU_UNBOXED_INT : LU_UNBOXED));
    store(a, RDI, off, RAX);                    /* after the raw word: x86 stores stay in order */
    b1(a, 0xB0);
    b1(a, 1);                                   /* mov al, 1 */
    alu_rr(a, ALU_XOR, RCX, RCX);
    b1(a, 0xF0);
    b1(a, 0x0F);
    b1(a, 0xB0);
    modrm_mem(a, RCX, RDI, mutex);              /* lock cmpxchg [rdi+mutex], cl */
    jcc(a, CC_E, unlocked);
    alu_ri(a, ALU_ADD, RDI, mutex);             /* a waiter parked: wake it */
    call_abs(a, (void *)PyMutex_Unlock);
    bind(a, unlocked);
    alu_ri(a, ALU_CMP, RBX, (int32_t)(intptr_t)LU_UNBOXED_INT);
    jcc(a, CC_BE, done);                        /* no object to release */
    mov_rr(a, RDI, RBX);
    call_abs(a, (void *)lu_jit_decref);         /* the old value */
    jmp(a, done);
    bind(a, unlock_slow);
    alu_ri(a, ALU_ADD, RDI, mutex);
    call_abs(a, (void *)PyMutex_Unlock);
    bind(a, slow);
    mov_rr(a, RDI, R12);
    mov_ri(a, RSI, in->imm);
    if (is_int) {
        mov_rr(a, RDX, R13);
        call_abs(a, (void *)lu_inline_store_i);
    }
    else {
        movq_xr(a, 0, R13);
        call_abs(a, (void *)lu_inline_store_f);
    }
    b1(a, 0x85);
    b1(a, 0xC0);                                /* test eax, eax */
    jcc(a, CC_E, fail);
    bind(a, done);
}

static int
emit_ins(struct asm_ *a, lu_v v, int next_block_label, int *block_labels)
{
    struct lu_ins *in = I(a, v);
    struct lu_func *f = a->f;
    int r, x, y;
    switch (in->op) {
    case OP_CONST: case OP_CONST_I: case OP_CONST_F: case OP_PHI:
        return 0;
    case OP_ENTRY_LOCAL:
        load(a, RAX, R15, offsetof(struct lu_frame, localsplus) + 8 * (int)in->imm);
        def_gpr(a, v, RAX);
        return 0;
    case OP_ENTRY_STACK:
        load(a, RAX, R15, offsetof(struct lu_frame, localsplus) + 8 * (f->code->co_nlocalsplus + (int)in->imm));
        def_gpr(a, v, RAX);
        return 0;
    case OP_UNBOX_INT: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        test_rr(a, r, r);
        jcc(a, CC_E, fail);                     /* an unbound local */
        mov_ri(a, RAX, (int64_t)(intptr_t)&PyLong_Type);
        cmp_rm(a, RAX, r, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, fail);
        load(a, RCX, r, offsetof(PyLongObject, long_value.lv_tag));
        alu_ri(a, ALU_CMP, RCX, 2 << _PyLong_NON_SIZE_BITS);
        jcc(a, CC_AE, fail);
        load32(a, RAX, r, offsetof(PyLongObject, long_value.ob_digit));
        int pos = new_label(a);
        rex(a, 0, 0, 0, RCX);
        b1(a, 0xF6);
        modrm_rr(a, 0, RCX);
        b1(a, 2);                               /* test cl, 2: negative */
        jcc(a, CC_E, pos);
        unary(a, 3, RAX);
        bind(a, pos);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_UNBOX_FLOAT: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        test_rr(a, r, r);
        jcc(a, CC_E, fail);                     /* an unbound local */
        mov_ri(a, RAX, (int64_t)(intptr_t)&PyFloat_Type);
        cmp_rm(a, RAX, r, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, fail);
        sse_rm(a, 0xF2, 0x10, 0, r, offsetof(PyFloatObject, ob_fval));
        def_fpr(a, v, 0);
        return 0;
    }
    case OP_I2F: {
        r = gpr_of(a, in->a, RCX);
        if (in->imm == 1) {
            int fail = guard_exit(a, in);
            mov_ri(a, RAX, (int64_t)1 << 53);
            alu_rr(a, ALU_CMP, r, RAX);
            jcc(a, CC_G, fail);
            unary(a, 3, RAX);
            alu_rr(a, ALU_CMP, r, RAX);
            jcc(a, CC_L, fail);
        }
        sse_rr(a, 0xF2, 0x2A, 0, r, 1);         /* cvtsi2sd xmm0, r64 */
        def_fpr(a, v, 0);
        return 0;
    }
    case OP_BOX_INT: case OP_BOX_FLOAT:
        if (in->flags & INS_VIRTUAL) {
            return 0;
        }
        if (in->b != LU_NOV) {
            to_gpr(a, RAX, in->b);              /* the object it was unboxed from */
            def_gpr(a, v, RAX);
            return 0;
        }
        if (in->c != LU_NOV) {
            /* NULL when the bit says the local was unbound. */
            int boxed = new_label(a), done = new_label(a);
            to_gpr(a, RAX, in->c);
            test_rr(a, RAX, RAX);
            jcc(a, CC_NE, boxed);
            def_gpr(a, v, RAX);
            jmp(a, done);
            bind(a, boxed);
            emit_box(a, in);
            def_gpr(a, v, RAX);
            bind(a, done);
            return 0;
        }
        emit_box(a, in);
        def_gpr(a, v, RAX);
        return 0;
    case OP_ADD_I: case OP_SUB_I: case OP_MUL_I: case OP_AND_I: case OP_OR_I: case OP_XOR_I: {
        to_gpr(a, RAX, in->a);
        r = gpr_of(a, in->b, RCX);
        if (in->op == OP_MUL_I) {
            op_rr(a, 1, 0x0FAF, RAX, r);        /* imul rax, r */
        }
        else {
            int alu = in->op == OP_ADD_I ? ALU_ADD : in->op == OP_SUB_I ? ALU_SUB
                    : in->op == OP_AND_I ? ALU_AND : in->op == OP_OR_I ? ALU_OR : ALU_XOR;
            alu_rr(a, alu, RAX, r);
        }
        if (in->op == OP_ADD_I || in->op == OP_SUB_I || in->op == OP_MUL_I) {
            jcc(a, CC_O, guard_exit(a, in));
        }
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_NEG_I: case OP_ABS_I: {
        to_gpr(a, RAX, in->a);
        int done = new_label(a), fail = guard_exit(a, in);
        if (in->op == OP_ABS_I) {
            test_rr(a, RAX, RAX);
            jcc(a, CC_NS, done);
        }
        unary(a, 3, RAX);
        jcc(a, CC_O, fail);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_SHL_I: case OP_SAR_I: {
        int fail = guard_exit(a, in);
        to_gpr(a, RCX, in->b);
        to_gpr(a, RAX, in->a);
        if (in->op == OP_SHL_I) {
            alu_ri(a, ALU_CMP, RCX, 63);
            jcc(a, CC_A, fail);                 /* negative counts too */
            mov_rr(a, RDX, RAX);
            shift_cl(a, 4, RAX);
            mov_rr(a, R11, RAX);
            shift_cl(a, 7, R11);
            alu_rr(a, ALU_CMP, R11, RDX);
            jcc(a, CC_NE, fail);                /* a bit was lost */
        }
        else {
            test_rr(a, RCX, RCX);
            jcc(a, CC_S, fail);
            int ok = new_label(a);
            alu_ri(a, ALU_CMP, RCX, 63);
            jcc(a, CC_BE, ok);
            mov_ri(a, RCX, 63);
            bind(a, ok);
            shift_cl(a, 7, RAX);
        }
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_FLOORDIV_I: case OP_MOD_I: {
        int fail = guard_exit(a, in);
        to_gpr(a, RCX, in->b);
        to_gpr(a, RAX, in->a);
        test_rr(a, RCX, RCX);
        jcc(a, CC_E, fail);
        int ok = new_label(a);
        alu_ri(a, ALU_CMP, RCX, -1);
        jcc(a, CC_NE, ok);
        mov_ri(a, R11, INT64_MIN);
        alu_rr(a, ALU_CMP, RAX, R11);
        jcc(a, CC_E, fail);
        bind(a, ok);
        b1(a, 0x48);
        b1(a, 0x99);                            /* cqo */
        unary(a, 7, RCX);                       /* idiv rcx: rax quotient, rdx remainder */
        int done = new_label(a);
        test_rr(a, RDX, RDX);
        jcc(a, CC_E, done);
        mov_rr(a, R11, RDX);
        alu_rr(a, ALU_XOR, R11, RCX);
        jcc(a, CC_NS, done);                    /* same signs: truncation is the floor */
        if (in->op == OP_FLOORDIV_I) {
            alu_ri(a, ALU_SUB, RAX, 1);
        }
        else {
            alu_rr(a, ALU_ADD, RDX, RCX);
        }
        bind(a, done);
        def_gpr(a, v, in->op == OP_FLOORDIV_I ? RAX : RDX);
        return 0;
    }
    case OP_ADD_F: case OP_SUB_F: case OP_MUL_F: case OP_DIV_F: {
        static const int ops[] = { [OP_ADD_F] = 0x58, [OP_SUB_F] = 0x5C, [OP_MUL_F] = 0x59, [OP_DIV_F] = 0x5E };
        to_fpr(a, 0, in->a);
        y = fpr_of(a, in->b, 1);
        if (in->op == OP_DIV_F) {
            int fail = guard_exit(a, in), ok = new_label(a);
            sse_rr(a, 0x66, 0x57, 15, 15, 0);   /* xorpd xmm15, xmm15 */
            sse_rr(a, 0x66, 0x2E, y, 15, 0);    /* ucomisd y, 0 */
            jcc(a, CC_P, ok);
            jcc(a, CC_E, fail);
            bind(a, ok);
        }
        sse_rr(a, 0xF2, ops[in->op], 0, y, 0);
        def_fpr(a, v, 0);
        return 0;
    }
    case OP_NEG_F: case OP_ABS_F:
        to_fpr(a, 0, in->a);
        mov_ri(a, RAX, in->op == OP_NEG_F ? INT64_MIN : INT64_MAX);
        movq_xr(a, 1, RAX);
        sse_rr(a, 0x66, in->op == OP_NEG_F ? 0x57 : 0x54, 0, 1, 0);
        def_fpr(a, v, 0);
        return 0;
    case OP_SQRT_F: {
        int fail = guard_exit(a, in);
        x = fpr_of(a, in->a, 0);
        sse_rr(a, 0x66, 0x57, 15, 15, 0);
        sse_rr(a, 0x66, 0x2E, x, 15, 0);        /* ucomisd x, 0: below or unordered leaves */
        jcc(a, CC_B, fail);
        sse_rr(a, 0xF2, 0x51, 0, x, 0);
        def_fpr(a, v, 0);
        return 0;
    }
    case OP_CMP_I:
        r = gpr_of(a, in->a, RAX);
        y = gpr_of(a, in->b, RCX);
        alu_rr(a, ALU_CMP, r, y);
        setcc(a, cond_of((int)in->imm, 0), RAX);
        movzx8(a, RAX, RAX);
        def_gpr(a, v, RAX);
        return 0;
    case OP_CMP_F: {
        int op = (int)in->imm;
        x = fpr_of(a, in->a, 0);
        y = fpr_of(a, in->b, 1);
        if (op == Py_LT || op == Py_LE) {
            sse_rr(a, 0x66, 0x2E, y, x, 0);
            setcc(a, op == Py_LT ? CC_A : CC_AE, RAX);
        }
        else if (op == Py_GT || op == Py_GE) {
            sse_rr(a, 0x66, 0x2E, x, y, 0);
            setcc(a, op == Py_GT ? CC_A : CC_AE, RAX);
        }
        else {
            sse_rr(a, 0x66, 0x2E, x, y, 0);
            setcc(a, op == Py_EQ ? CC_E : CC_NE, RAX);
            setcc(a, op == Py_EQ ? CC_NP : CC_P, RCX);
            op_rr(a, 0, op == Py_EQ ? 0x20 : 0x08, RCX, RAX);  /* and/or al, cl */
        }
        movzx8(a, RAX, RAX);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_IS:
        r = gpr_of(a, in->a, RAX);
        y = gpr_of(a, in->b, RCX);
        alu_rr(a, ALU_CMP, r, y);
        setcc(a, CC_E, RAX);
        movzx8(a, RAX, RAX);
        def_gpr(a, v, RAX);
        return 0;
    case OP_NOT:
        to_gpr(a, RAX, in->a);
        alu_ri(a, ALU_XOR, RAX, 1);
        def_gpr(a, v, RAX);
        return 0;
    case OP_BOOL:
        r = gpr_of(a, in->a, RCX);
        mov_ri(a, RAX, (int64_t)(intptr_t)Py_False);
        mov_ri(a, RDX, (int64_t)(intptr_t)Py_True);
        test_rr(a, r, r);
        op_rr(a, 1, 0x0F45, RAX, RDX);          /* cmovnz rax, rdx */
        def_gpr(a, v, RAX);
        return 0;
    case OP_TRUTH: {
        int t = new_label(a), fz = new_label(a), done = new_label(a);
        r = gpr_of(a, in->a, RDX);
        mov_ri(a, RAX, (int64_t)(intptr_t)Py_True);
        alu_rr(a, ALU_CMP, r, RAX);
        jcc(a, CC_E, t);
        mov_ri(a, RAX, (int64_t)(intptr_t)Py_False);
        alu_rr(a, ALU_CMP, r, RAX);
        jcc(a, CC_E, fz);
        mov_ri(a, RAX, (int64_t)(intptr_t)Py_None);
        alu_rr(a, ALU_CMP, r, RAX);
        jcc(a, CC_E, fz);
        if (in->imm == 1) {
            jmp(a, exit_to(a, in->fs, EXIT_GUARD, 0, 0));
        }
        else {
            mov_rr(a, RDI, r);
            call_abs(a, (void *)lu_jit_truth);
            int err = exit_to(a, in->fs, EXIT_ERROR, 1, in->drop);
            b1(a, 0x83);
            b1(a, 0xF8);
            b1(a, 0xFF);                        /* cmp eax, -1 */
            jcc(a, CC_E, err);
            jmp(a, done);
        }
        bind(a, t);
        mov_ri(a, RAX, 1);
        jmp(a, done);
        bind(a, fz);
        mov_ri(a, RAX, 0);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_GUARD_TYPE: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        test_rr(a, r, r);
        jcc(a, CC_E, fail);
        mov_ri(a, RAX, in->imm);
        cmp_rm(a, RAX, r, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, fail);
        return 0;
    }
    case OP_GUARD_VERSION: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        switch (in->imm2) {
        case GV_TYPE_OF:
            test_rr(a, r, r);
            jcc(a, CC_E, fail);
            load(a, RAX, r, offsetof(PyObject, ob_type));
            cmp_mi(a, 0, RAX, offsetof(PyTypeObject, tp_version_tag), (int32_t)in->imm);
            break;
        case GV_TYPE_ITSELF:
            cmp_mi(a, 0, r, offsetof(PyTypeObject, tp_version_tag), (int32_t)in->imm);
            break;
        case GV_DICT_KEYS:
            load(a, RAX, r, offsetof(PyDictObject, ma_keys));
            cmp_mi(a, 0, RAX, offsetof(PyDictKeysObject, dk_version), (int32_t)in->imm);
            break;
        case GV_FUNC:
            cmp_mi(a, 0, r, offsetof(PyFunctionObject, func_version), (int32_t)in->imm);
            break;
        }
        jcc(a, CC_NE, fail);
        return 0;
    }
    case OP_GUARD_IS: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        int mode = (int)GI_MODE(in->imm2);
        if (mode == 1) {
            load(a, RCX, r, GI_OFFSET(in->imm2));
            r = RCX;
        }
        else if (mode == 2) {
            load(a, RCX, r, offsetof(PyDictObject, ma_keys));
            load(a, RCX, RCX, GI_OFFSET(in->imm2));
            r = RCX;
        }
        mov_ri(a, RAX, in->imm);
        alu_rr(a, ALU_CMP, r, RAX);
        jcc(a, CC_NE, fail);
        return 0;
    }
    case OP_GUARD_BIT: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RAX);
        alu_ri(a, ALU_CMP, r, (int32_t)in->imm);
        jcc(a, CC_NE, fail);
        return 0;
    }
    case OP_GUARD_NOT_NULL: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RAX);
        if (in->imm == 1) {
            alu_ri(a, ALU_CMP, r, 1);           /* LU_JIT_DECLINED */
            jcc(a, CC_E, fail);
        }
        else {
            test_rr(a, r, r);
            jcc(a, CC_E, fail);
        }
        return 0;
    }
    case OP_LOAD:
        r = gpr_of(a, in->a, RAX);
        load(a, RAX, r, (int32_t)in->imm);
        def_gpr(a, v, RAX);
        return 0;
    case OP_LIST_ITEM: {
        struct move m[2] = { { RDI, in->a, 0, 0 }, { RSI, in->b, 0, 0 } };
        int fail = guard_exit(a, in);
        parallel_moves(a, m, 2);
        call_abs(a, (void *)lu_jit_list_item);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_LIST_STORE: {
        struct move m[3] = { { RDI, in->a, 0, 0 }, { RSI, in->b, 0, 0 }, { RDX, in->c, 0, 0 } };
        int fail = guard_exit(a, in);
        parallel_moves(a, m, 3);
        call_abs(a, (void *)lu_jit_list_store);
        b1(a, 0x85);
        b1(a, 0xC0);                            /* test eax, eax */
        jcc(a, CC_E, fail);
        return 0;
    }
    case OP_INLINE_STORE: {
        /* STORE_ATTR_INSTANCE_VALUE in line, after its version guard, as
           CPython locks the object: the owner's lock byte taken with one
           cmpxchg, a word that already holds a value replaced, a value the
           owning thread holds (or an immortal) referenced in place. Anything
           else is lu_inline_store's, and leaves when it declines. */
        struct move m[2] = { { RDI, in->a, 0, 0 }, { RSI, in->b, 0, 0 } };
        int fail = guard_exit(a, in);
        int slow = new_label(a), unlock_slow = new_label(a), immortal = new_label(a);
        int unlocked = new_label(a), done = new_label(a);
        int32_t flags = offsetof(PyObject, ob_flags) + 1, mutex = offsetof(PyObject, ob_mutex);
        int32_t off = (int32_t)in->imm;
        parallel_moves(a, m, 2);
        mov_rr(a, R12, RDI);                    /* kept across calls: nothing is live here (IF_CALL) */
        mov_rr(a, R13, RSI);
        b1(a, 0xF6);
        modrm_mem(a, 0, RDI, flags);
        b1(a, LU_ATTRS_IN_DICT >> 8);           /* test byte [rdi+flags+1], LU_ATTRS_IN_DICT */
        jcc(a, CC_NE, slow);
        alu_rr(a, ALU_XOR, RAX, RAX);
        b1(a, 0xB1);
        b1(a, 1);                               /* mov cl, 1 */
        b1(a, 0xF0);
        b1(a, 0x0F);
        b1(a, 0xB0);
        modrm_mem(a, RCX, RDI, mutex);          /* lock cmpxchg [rdi+mutex], cl */
        jcc(a, CC_NE, slow);
        b1(a, 0xF6);
        modrm_mem(a, 0, RDI, flags);
        b1(a, LU_ATTRS_IN_DICT >> 8);
        jcc(a, CC_NE, unlock_slow);             /* moved to the dict before the lock */
        load(a, RBX, RDI, off);                 /* the old value, kept across calls */
        alu_ri(a, ALU_CMP, RBX, (int32_t)(intptr_t)LU_UNBOXED_INT);
        jcc(a, CC_BE, unlock_slow);             /* a new name (insertion order is C's), or an unboxed value */
        load32(a, RCX, RSI, offsetof(PyObject, ob_ref_local));
        rex(a, 0, 0, 0, RCX);
        b1(a, 0x83);
        modrm_rr(a, ALU_ADD, RCX);
        b1(a, 1);                               /* add ecx, 1 */
        jcc(a, CC_E, immortal);
        b1(a, 0x64);
        b1(a, 0x4C);
        b1(a, 0x8B);
        b1(a, 0x1C);
        b1(a, 0x25);
        b4(a, 0);                               /* mov r11, fs:[0] */
        cmp_rm(a, R11, RSI, offsetof(PyObject, ob_tid));
        jcc(a, CC_NE, unlock_slow);
        op_rm(a, 0, 0x89, RCX, RSI, offsetof(PyObject, ob_ref_local));
        bind(a, immortal);
        store(a, RDI, off, RSI);
        b1(a, 0xB0);
        b1(a, 1);                               /* mov al, 1 */
        alu_rr(a, ALU_XOR, RCX, RCX);
        b1(a, 0xF0);
        b1(a, 0x0F);
        b1(a, 0xB0);
        modrm_mem(a, RCX, RDI, mutex);          /* lock cmpxchg [rdi+mutex], cl */
        jcc(a, CC_E, unlocked);
        alu_ri(a, ALU_ADD, RDI, mutex);         /* a waiter parked: wake it */
        call_abs(a, (void *)PyMutex_Unlock);
        bind(a, unlocked);
        mov_rr(a, RDI, RBX);
        call_abs(a, (void *)lu_jit_decref);     /* the old value */
        jmp(a, done);
        bind(a, unlock_slow);
        alu_ri(a, ALU_ADD, RDI, mutex);
        call_abs(a, (void *)PyMutex_Unlock);
        mov_rr(a, RDI, R12);
        mov_rr(a, RSI, R13);
        bind(a, slow);
        mov_ri(a, RDX, in->imm);
        b1(a, 0x48);
        b1(a, 0x87);
        b1(a, 0xD6);                            /* xchg rsi, rdx: (owner, offset, value) */
        call_abs(a, (void *)lu_inline_store);
        b1(a, 0x85);
        b1(a, 0xC0);                            /* test eax, eax */
        jcc(a, CC_E, fail);
        bind(a, done);
        return 0;
    }
    case OP_INLINE_LOAD: {
        /* LOAD_ATTR_INSTANCE_VALUE in line, after its version guard: the
           owner still keeps its inline values, the word at imm holds a value,
           and the owning thread takes the reference in place. */
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        rex(a, 0, 0, 0, r);
        b1(a, 0xF6);
        modrm_mem(a, 0, r, offsetof(PyObject, ob_flags) + 1);
        b1(a, LU_ATTRS_IN_DICT >> 8);           /* test byte [r+flags+1], LU_ATTRS_IN_DICT */
        jcc(a, CC_NE, fail);
        int unboxed = new_label(a), done = new_label(a);
        load(a, RAX, r, (int32_t)in->imm);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
        jcc(a, CC_BE, unboxed);
        incref_owned_or_exit(a, RAX, fail);
        jmp(a, done);
        bind(a, unboxed);
        /* An unboxed value: its box, made once in C and kept by the owner. */
        save_volatile(a);
        if (r != RDI) {
            mov_rr(a, RDI, r);
        }
        rex(a, 1, RSI, 0, RDI);
        b1(a, 0x8D);
        modrm_mem(a, RSI, RDI, (int32_t)in->imm);      /* lea rsi, [rdi+imm]: the word */
        call_abs(a, (void *)lu_inline_get);
        restore_volatile(a);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_INLINE_LOAD_F: case OP_INLINE_LOAD_I:
        inline_load_raw(a, in, v);
        return 0;
    case OP_INLINE_STORE_F: case OP_INLINE_STORE_I:
        inline_store_raw(a, in);
        return 0;
    case OP_ATTR_LOAD: {
        /* LOAD_ATTR_WITH_HINT in line: the type's version, the instance's
           exact dict, a unicode keys table with entry imm2 holding the name,
           and the value with a reference the owning thread takes in place.
           Anything else leaves: another thread's object is not taken here
           (biased counting lets only the owner skip the atomic try). */
        int fail = guard_exit(a, in);
        int64_t entry = 16 * in->imm2;
        r = gpr_of(a, in->a, RDX);
        test_rr(a, r, r);
        jcc(a, CC_E, fail);
        load(a, RAX, r, offsetof(PyObject, ob_type));
        cmp_mi(a, 0, RAX, offsetof(PyTypeObject, tp_version_tag), (int32_t)in->imm);
        jcc(a, CC_NE, fail);
        load(a, R11, r, LU_MANAGED_DICT_OFFSET);
        test_rr(a, R11, R11);
        jcc(a, CC_E, fail);
        mov_ri(a, RAX, (int64_t)(intptr_t)&PyDict_Type);
        cmp_rm(a, RAX, R11, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, fail);
        load(a, R11, R11, offsetof(PyDictObject, ma_keys));
        rex(a, 0, 0, 0, R11);
        b1(a, 0x80);
        modrm_mem(a, ALU_CMP, R11, offsetof(PyDictKeysObject, dk_kind));
        b1(a, DICT_KEYS_GENERAL);               /* cmp byte [r11+kind], GENERAL */
        jcc(a, CC_E, fail);
        cmp_mi(a, 1, R11, offsetof(PyDictKeysObject, dk_nentries), (int32_t)in->imm2);
        jcc(a, CC_LE, fail);
        rex(a, 0, RCX, 0, R11);
        b1(a, 0x0F);
        b1(a, 0xB6);
        modrm_mem(a, RCX, R11, offsetof(PyDictKeysObject, dk_log2_index_bytes));   /* movzx ecx, byte */
        mov_ri(a, RAX, 1);
        shift_cl(a, 4, RAX);                    /* the index bytes */
        alu_rr(a, ALU_ADD, RAX, R11);
        to_gpr(a, RCX, in->c);                  /* the name */
        cmp_rm(a, RCX, RAX, (int32_t)(offsetof(PyDictKeysObject, dk_indices) + entry));
        jcc(a, CC_NE, fail);
        load(a, RAX, RAX, (int32_t)(offsetof(PyDictKeysObject, dk_indices) + entry + 8));
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        incref_owned_or_exit(a, RAX, fail);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_INCREF:
        r = gpr_of(a, in->a, RDX);
        incref(a, r);
        return 0;
    case OP_DECREF: {
        /* Py_DECREF: the owning thread's count above one drops in place;
           anything else (immortal, last reference, another thread's object)
           goes to C, which keeps the registers. */
        int done = new_label(a), slow = new_label(a);
        r = gpr_of(a, in->a, RDX);
        test_rr(a, r, r);
        jcc(a, CC_E, done);
        load32(a, RCX, r, offsetof(PyObject, ob_ref_local));
        cmp32_ri8(a, RCX, -1);
        jcc(a, CC_E, done);                     /* immortal */
        cmp32_ri8(a, RCX, 1);
        jcc(a, CC_BE, slow);                    /* the last local reference, or none */
        b1(a, 0x64);
        b1(a, 0x48);
        b1(a, 0x8B);
        b1(a, 0x04);
        b1(a, 0x25);
        b4(a, 0);                               /* mov rax, fs:[0] */
        cmp_rm(a, RAX, r, offsetof(PyObject, ob_tid));
        jcc(a, CC_NE, slow);
        rex(a, 0, 0, 0, RCX);
        b1(a, 0x83);
        modrm_rr(a, ALU_SUB, RCX);
        b1(a, 1);                               /* sub ecx, 1 */
        op_rm(a, 0, 0x89, RCX, r, offsetof(PyObject, ob_ref_local));
        jmp(a, done);
        bind(a, slow);
        save_volatile(a);
        mov_rr(a, RDI, r);
        call_abs(a, (void *)lu_jit_decref);
        restore_volatile(a);
        bind(a, done);
        return 0;
    }
    case OP_STORE_LOCAL:
        to_gpr(a, RDX, in->a);
        mov_rr(a, RDI, R15);
        mov_ri(a, RSI, in->imm);
        call_abs(a, (void *)lu_jit_store_local);
        return 0;
    case OP_RITER_DONE:
        r = gpr_of(a, in->a, RDX);
        cmp_mi(a, 1, r, offsetof(rangeiterobject, len), 0);
        setcc(a, CC_LE, RAX);
        movzx8(a, RAX, RAX);
        def_gpr(a, v, RAX);
        return 0;
    case OP_RITER_NEXT:
        r = gpr_of(a, in->a, RDX);
        load(a, RAX, r, offsetof(rangeiterobject, start));
        load(a, RCX, r, offsetof(rangeiterobject, step));
        alu_rr(a, ALU_ADD, RCX, RAX);
        store(a, r, offsetof(rangeiterobject, start), RCX);
        rex(a, 1, 0, 0, r);
        b1(a, 0xFF);
        modrm_mem(a, 1, r, offsetof(rangeiterobject, len));    /* dec qword [r+len] */
        def_gpr(a, v, RAX);
        return 0;
    case OP_HELPER: {
        const struct lu_helper *h = &lu_helpers[in->imm];
        call_helper(a, v);
        if ((h->flags & IF_RAISE) && in->fs != NULL) {
            if (h->rep == REP_BIT) {
                int err = exit_to(a, in->fs, EXIT_ERROR, 1, in->drop);
                b1(a, 0x83);
                b1(a, 0xF8);
                b1(a, 0xFF);                    /* cmp eax, -1 */
                jcc(a, CC_E, err);
            }
            else if (h->rep == REP_OBJ) {
                int err = exit_to(a, in->fs, EXIT_ERROR, 1, in->drop);
                test_rr(a, RAX, RAX);
                jcc(a, CC_E, err);
            }
        }
        if (h->rep == REP_BIT) {
            b1(a, 0x48);
            b1(a, 0x63);
            b1(a, 0xC0);                        /* movsxd rax, eax */
        }
        if (h->rep != REP_NONE) {
            def_gpr(a, v, RAX);
        }
        return 0;
    }
    case OP_POLL: {
        guard_exit(a, in);                          /* the stress count */
        /* The poll word: an event, or a monitoring version other than the
           one the code was compiled under (tools turned on), leaves. */
        load(a, RAX, R14, offsetof(struct lu_task, carrier));
        load(a, RCX, RAX, offsetof(struct lu_carrier, poll));
        int poll = exit_to(a, in->fs, EXIT_POLL, 0, 0);
        mov_ri(a, R11, (int64_t)a->f->code->_co_instrumentation_version);
        alu_rr(a, ALU_CMP, RCX, R11);
        jcc(a, CC_NE, poll);
        /* The carrier passes a quiescent state, as the interpreter's poll does. */
        int same = new_label(a);
        mov_ri(a, RCX, (int64_t)(intptr_t)&_PyRuntime.qsbr.wr_seq);
        load(a, RCX, RCX, 0);
        cmp_rm(a, RCX, RAX, offsetof(struct lu_carrier, qsbr.seq));
        jcc(a, CC_E, same);
        store(a, RAX, offsetof(struct lu_carrier, qsbr.seq), RCX);
        bind(a, same);
        mov_ri(a, RCX, (int64_t)(intptr_t)&_PyRuntime.sched.active);
        cmp_mi(a, 0, RCX, 0, 0);
        jcc(a, CC_NE, poll);
        return 0;
    }
    case OP_DEOPT:
        jmp(a, exit_to(a, in->fs, (int)in->imm, 0, 0));
        return 0;
    case OP_RETURN:
        to_gpr(a, RCX, in->a);
        load(a, RAX, RBP, RESULT_SLOT);
        store(a, RAX, 0, RCX);
        mov_ri(a, RAX, 1);                      /* LU_JIT_RETURN */
        jmp(a, a->epilogue);
        return 0;
    case OP_JUMP: case OP_BRANCH:
        return 0;               /* the block's end does it */
    default:
        f->fail = "an instruction the backend lacks";
        return -1;
    }
}

/* The moves into succ's phis on the edge from block b. */
static void
edge_moves(struct asm_ *a, int b, int succ)
{
    struct lu_func *f = a->f;
    struct lu_block *sbl = &f->blocks[succ];
    int p = 0;
    while (p < sbl->npreds && sbl->preds[p] != b) {
        p++;
    }
    struct move *moves = malloc(sizeof(struct move) * (size_t)(sbl->nins + 1));
    int nm = 0;
    for (int i = 0; i < sbl->nins; i++) {
        lu_v v = sbl->ins[i];
        struct lu_ins *phi = &f->ins[v];
        if (phi->op != OP_PHI) {
            break;
        }
        if (phi->loc == LU_LOC_NONE || p >= phi->nargs) {
            continue;
        }
        lu_v src = phi->args[p];
        if (f->ins[src].flags & INS_VIRTUAL) {
            continue;
        }
        moves[nm++] = (struct move){ phi->loc, src, 0, phi->rep == REP_F64 };
    }
    parallel_moves(a, moves, nm);
    free(moves);
}

static void
prologue(struct asm_ *a)
{
    b1(a, 0x55);                                /* push rbp */
    mov_rr(a, RBP, RSP);
    b1(a, 0x53);                                /* push rbx */
    b1(a, 0x41); b1(a, 0x54);                   /* push r12 */
    b1(a, 0x41); b1(a, 0x55);                   /* push r13 */
    b1(a, 0x41); b1(a, 0x56);                   /* push r14 */
    b1(a, 0x41); b1(a, 0x57);                   /* push r15 */
    alu_ri(a, ALU_SUB, RSP, a->frame_bytes);
    mov_rr(a, R14, RDI);
    mov_rr(a, R15, RSI);
    store(a, RBP, RESULT_SLOT, RDX);
}

static void
epilogue(struct asm_ *a)
{
    bind(a, a->epilogue);
    rex(a, 1, RSP, 0, RBP);
    b1(a, 0x8D);
    modrm_mem(a, RSP, RBP, -SAVES);             /* lea rsp, [rbp-40] */
    b1(a, 0x41); b1(a, 0x5F);
    b1(a, 0x41); b1(a, 0x5E);
    b1(a, 0x41); b1(a, 0x5D);
    b1(a, 0x41); b1(a, 0x5C);
    b1(a, 0x5B);
    b1(a, 0x5D);
    b1(a, 0xC3);
}

/* The exits: each pushes its index; the common path saves everything and
   calls lu_jit_deopt. */
static void
exits(struct asm_ *a)
{
    int common = new_label(a);
    for (int i = 0; i < a->nexits; i++) {
        bind(a, a->exits[i].label);
        b1(a, 0x68);
        b4(a, i);                               /* push imm32 */
        jmp(a, common);
    }
    bind(a, common);
    alu_ri(a, ALU_SUB, RSP, 264);              /* the push left the stack 8 off */
    for (int r = 0; r < 16; r++) {
        if (r != RSP) {
            store(a, RSP, 8 * r, r);
        }
    }
    for (int x = 0; x < 16; x++) {
        sse_rm(a, 0xF2, 0x11, x, RSP, 128 + 8 * x);
    }
    load(a, RAX, RSP, 264);                     /* the index */
    a->deopts_patch = a->n + 2;
    mov_ri(a, RDX, 0x1122334455667788LL);       /* the exits' table, patched */
    load(a, RDX, RDX, 0);
    /* rdx = table[rax] */
    b1(a, 0x48); b1(a, 0x8B); b1(a, 0x14); b1(a, 0xC2);        /* mov rdx, [rdx+rax*8] */
    mov_rr(a, RDI, R14);
    mov_rr(a, RSI, R15);
    mov_rr(a, RCX, RSP);
    rex(a, 1, R8, 0, RSP);
    b1(a, 0x8D);
    modrm_mem(a, R8, RSP, 128);                 /* lea r8, [rsp+128] */
    rex(a, 1, R9, 0, RBP);
    b1(a, 0x8D);
    modrm_mem(a, R9, RBP, SPILL_BASE);          /* lea r9, [rbp+SPILL_BASE] */
    call_abs(a, (void *)lu_jit_deopt);
    jmp(a, a->epilogue);
}

int
lu_target_emit(struct lu_func *f, struct lu_unit *unit)
{
    struct asm_ A = { 0 }, *a = &A;
    a->f = f;
    a->unit = unit;
    a->epilogue = new_label(a);
    int maxargs = 0;
    for (int v = 0; v < f->nins; v++) {
        if (f->ins[v].op == OP_HELPER && f->ins[v].nargs > maxargs) {
            maxargs = f->ins[v].nargs;
        }
    }
    /* After push rbp the stack is 16-aligned and the five saves leave it
       8 off, so the frame is 8 more than a multiple of 16. */
    int bytes = 8 + 8 * f->nspill + 8 * maxargs;
    a->frame_bytes = ((bytes + 15) & ~15) + 8;
    prologue(a);
    int *labels = malloc(sizeof(int) * (size_t)f->nblocks);
    for (int b = 0; b < f->nblocks; b++) {
        labels[b] = new_label(a);
    }
    for (int k = 0; k < f->norder; k++) {
        int b = f->order[k];
        struct lu_block *bl = &f->blocks[b];
        bind(a, labels[b]);
        int next = k + 1 < f->norder ? f->order[k + 1] : -1;
        for (int i = 0; i < bl->nins; i++) {
            if (emit_ins(a, bl->ins[i], next, labels) < 0) {
                free(labels);
                return -1;
            }
        }
        if (b == 0) {
            /* Compiled code owns the entry stack's references now. */
            rex(a, 1, RAX, 0, R15);
            b1(a, 0x8D);
            modrm_mem(a, RAX, R15, offsetof(struct lu_frame, localsplus) + 8 * f->code->co_nlocalsplus);
            store(a, R15, offsetof(struct lu_frame, stackpointer), RAX);
        }
        lu_v last = bl->nins ? bl->ins[bl->nins - 1] : LU_NOV;
        struct lu_ins *term = last != LU_NOV ? &f->ins[last] : NULL;
        if (term == NULL) {
            continue;
        }
        if (term->op == OP_JUMP) {
            edge_moves(a, b, bl->succ[0]);
            if (bl->succ[0] != next) {
                jmp(a, labels[bl->succ[0]]);
            }
        }
        else if (term->op == OP_BRANCH) {
            int r = gpr_of(a, term->a, RAX);
            test_rr(a, r, r);
            if (bl->succ[1] == next) {
                jcc(a, CC_NE, labels[bl->succ[0]]);
            }
            else if (bl->succ[0] == next) {
                jcc(a, CC_E, labels[bl->succ[1]]);
            }
            else {
                jcc(a, CC_NE, labels[bl->succ[0]]);
                jmp(a, labels[bl->succ[1]]);
            }
        }
    }
    free(labels);
    epilogue(a);
    exits(a);
    /* Labels. */
    for (int i = 0; i < a->npatches; i++) {
        int target = a->labels[a->patches[i].label];
        int32_t rel = target - (a->patches[i].at + 4);
        memcpy(a->buf + a->patches[i].at, &rel, 4);
    }
    unit->ndeopts = a->nexits;
    unit->deopts = malloc(sizeof(struct lu_deopt *) * (size_t)(a->nexits + 1));
    for (int i = 0; i < a->nexits; i++) {
        unit->deopts[i] = a->exits[i].d;
        unit->deopts[i]->unit = unit;
    }
    struct lu_deopt ***table = malloc(sizeof(*table));
    *table = unit->deopts;
    unit->deopt_table = table;
    int64_t tp = (int64_t)(intptr_t)table;
    memcpy(a->buf + a->deopts_patch, &tp, 8);
    /* Publication: written, then executable. */
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t size = ((size_t)a->n + page - 1) & ~(page - 1);
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        f->fail = "no memory for code";
        free(a->buf);
        return -1;
    }
    memcpy(mem, a->buf, (size_t)a->n);
    mprotect(mem, size, PROT_READ | PROT_EXEC);
    unit->mem = mem;
    unit->size = size;
    unit->code_bytes = (size_t)a->n;
    unit->fn = (int (*)(PyThreadState *, struct lu_frame *, PyObject **))mem;
    free(a->buf);
    free(a->labels);
    free(a->patches);
    free(a->exits);
    return 0;
}

void
lu_target_free(struct lu_unit *unit)
{
    if (unit->mem != NULL) {
        munmap(unit->mem, unit->size);
    }
}
