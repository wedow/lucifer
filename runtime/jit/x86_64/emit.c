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
#include "jit/jit.h"
#include "interp/perf.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
#define XMM(n) (LU_LOC_FPR + (n))

static const int8_t gprs[] = { RBX, R12, R13, RSI, RDI, R8, R9, R10 };
static const int8_t saved_gprs[] = { RBX, R12, R13 };
static int clobbers_saved(const struct lu_ins *in);
static const int8_t fprs[] = { XMM(2), XMM(3), XMM(4), XMM(5), XMM(6), XMM(7), XMM(8), XMM(9),
                               XMM(10), XMM(11), XMM(12), XMM(13), XMM(14) };
/* push rbp; mov rbp, rsp; call *rcx; pop rbp; ret */
static const uint8_t trampoline[] = { 0x55, 0x48, 0x89, 0xe5, 0xff, 0xd1, 0x5d, 0xc3 };
/* CIE: id 0, version 1, "zR", code alignment 1, data alignment -8, return
   address in DWARF register 16, PC-relative FDE pointers; at entry the
   CFA is rsp + 8 and the return address is at CFA - 8. */
static const uint8_t cie[] = { 0, 0, 0, 0, 1, 'z', 'R', 0, 1, 0x78, 16, 1, 0x1b, 0x0c, 7, 8, 0x90, 1 };
/* After push rbp the CFA is rsp + 16 and rbp is at CFA - 16; after
   mov rbp, rsp the CFA is rbp + 16. */
static const uint8_t fp_cfi[] = { 0x41, 0x0e, 16, 0x86, 2, 0x43, 0x0d, 6 };
const struct lu_target lu_target = {
    .name = "x86_64", .ngpr = sizeof gprs, .nfpr = sizeof fprs, .gpr = gprs, .fpr = fprs,
    .nsaved = sizeof saved_gprs, .saved = saved_gprs, .clobbers_saved = clobbers_saved,
    .trampoline = trampoline, .ntrampoline = sizeof trampoline, .elf_machine = 62,
    .cie = cie, .ncie = sizeof cie, .fp_cfi = fp_cfi, .nfp_cfi = sizeof fp_cfi,
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
    struct lu_deopt *d;         /* its description as built, a struct pending's */
};

/* An exit's description until the unit's are packed: its index too. */
struct pending {
    struct lu_deopt d;
    int index;
};

/* Indices into an array, by a hash of what they index (lookup): -1 is
   none, and there are a power of two slots. */
struct table {
    int *slots;
    uint32_t cap, n;
};

/* A unit's descriptions as they are built, packed into one block once
   its code is done (pack): the frame records its exits share, their
   virtual objects (each exit's in a row), the values frames and objects
   name, the constant words those name, and owned locations (each exit's
   in a row). */
struct build {
    struct lu_deopt_frame *frames;
    struct lu_dobj *objs;
    lu_pv *vals;
    int64_t *imms;
    int32_t *owned;
    uint32_t nframes, nobjs, nvals, nimms, nowned;
    uint32_t capframes, capobjs, capvals, capimms, capowned;
    struct table frame_table, imm_table;
    lu_pv *room;                /* an exit's slots, as make_deopt describes them */
    struct lu_fs **chain;       /* its frame states, outermost first */
    uint32_t caproom, capchain;
};

/* The references the frames' slots are owed for an object a published
   call is handed: the value, and how many beyond compiled code's own. */
struct hold {
    lu_v v;
    int n;
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
    int save_all, restore_all;  /* the shared stubs of parked_call, emitted when used */
    int parks;
    uint32_t volatiles;         /* the caller-saved registers the unit allocates, by location */
    struct hold *held;          /* what the published call in hand holds of what it is handed */
    int nheld;
    int inlined;                /* the published call's inlined frames, counted toward the limit across it */
    struct build b;
    /* The code that publishes an exit's state: where its address goes. */
    struct { int at, exit; } *pubs;
    uint32_t npubs, cappubs;
};

/* The instructions whose code keeps its own values in RBX, R12 and R13
   across the calls it makes. */
static int
clobbers_saved(const struct lu_ins *in)
{
    switch (in->op) {
    case OP_LIST_STORE: case OP_INLINE_STORE: case OP_INLINE_STORE_F: case OP_INLINE_STORE_I:
        return 1;
    case OP_HELPER:
        return in->imm == H_build_tuple || in->imm == H_build_list || in->imm == H_new_object;
    default:
        return 0;
    }
}

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

/* cmp byte [base+disp], imm8 and mov byte [base+disp], imm8 */
static void
cmp8_mi(struct asm_ *a, int base, int32_t disp, int imm)
{
    rex(a, 0, 0, 0, base);
    b1(a, 0x80);
    modrm_mem(a, ALU_CMP, base, disp);
    b1(a, imm);
}

static void
store8_mi(struct asm_ *a, int base, int32_t disp, int imm)
{
    rex(a, 0, 0, 0, base);
    b1(a, 0xC6);
    modrm_mem(a, 0, base, disp);
    b1(a, imm);
}

/* mov qword [base+disp], imm32 (sign-extended) */
static void
store_mi(struct asm_ *a, int base, int32_t disp, int32_t imm)
{
    rex(a, 1, 0, 0, base);
    b1(a, 0xC7);
    modrm_mem(a, 0, base, disp);
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

/* mov (width 4) or movzx (1, 2) dst32, [base + index * width + disp] */
static void
load_indexed(struct asm_ *a, int dst, int base, int index, int width, int32_t disp)
{
    rex(a, 0, dst, index, base);
    if (width == 4) {
        b1(a, 0x8B);
    }
    else {
        b1(a, 0x0F);
        b1(a, width == 1 ? 0xB6 : 0xB7);
    }
    b1(a, 0x84 | ((dst & 7) << 3));             /* [base + index * scale + disp32] */
    b1(a, ((width == 4 ? 2 : width == 2 ? 1 : 0) << 6) | ((index & 7) << 3) | (base & 7));
    b4(a, disp);
}

/* op reg, [base + index * 8 + disp], 64-bit */
static void
op_indexed8(struct asm_ *a, int opcode, int reg, int base, int index, int32_t disp)
{
    rex(a, 1, reg, index, base);
    b1(a, opcode);
    b1(a, 0x84 | ((reg & 7) << 3));
    b1(a, (3 << 6) | ((index & 7) << 3) | (base & 7));
    b4(a, disp);
}

static void
load_indexed8(struct asm_ *a, int dst, int base, int index, int32_t disp)
{
    op_indexed8(a, 0x8B, dst, base, index, disp);
}

static void
store_indexed8(struct asm_ *a, int base, int index, int32_t disp, int src)
{
    op_indexed8(a, 0x89, src, base, index, disp);
}

/* A compact str's state word with the given kind and ascii bits, and no
   others: the bit-field layout is the C compiler's. */
static int32_t
str_state(int kind, int ascii)
{
    PyASCIIObject o;
    memset(&o, 0, sizeof o);
    o.state.kind = kind;
    o.state.compact = 1;
    o.state.ascii = ascii;
    int32_t w;
    _Static_assert(sizeof o.state == sizeof w, "a str's state is one 32-bit word");
    memcpy(&w, &o.state, sizeof w);
    return w;
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
   caller-saved ones the unit allocates at all (a->volatiles, by location)
   go to the stack around it, in a multiple of 16 bytes that keeps it
   aligned. Its arguments are set after the saves. */
static void sse_rm(struct asm_ *a, int prefix, int op, int reg, int base, int32_t disp);

static int
volatile_bytes(struct asm_ *a)
{
    return (8 * __builtin_popcount(a->volatiles) + 15) & ~15;
}

static void
save_volatile(struct asm_ *a)
{
    if (a->volatiles == 0) {
        return;
    }
    alu_ri(a, ALU_SUB, RSP, volatile_bytes(a));
    for (int r = 0, k = 0; r < 32; r++) {
        if (a->volatiles & (1u << r)) {
            if (r < LU_LOC_FPR) {
                store(a, RSP, 8 * k++, r);
            }
            else {
                sse_rm(a, 0xF2, 0x11, r - LU_LOC_FPR, RSP, 8 * k++);
            }
        }
    }
}

static void
restore_volatile(struct asm_ *a)
{
    if (a->volatiles == 0) {
        return;
    }
    for (int r = 0, k = 0; r < 32; r++) {
        if (a->volatiles & (1u << r)) {
            if (r < LU_LOC_FPR) {
                load(a, r, RSP, 8 * k++);
            }
            else {
                sse_rm(a, 0xF2, 0x10, r - LU_LOC_FPR, RSP, 8 * k++);
            }
        }
    }
    alu_ri(a, ALU_ADD, RSP, volatile_bytes(a));
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

/* p, grown to hold `need` items of `size` bytes: *cap doubles. */
static void *
grow(void *p, uint32_t *cap, uint32_t need, size_t size)
{
    if (need > *cap) {
        *cap = need > 2 * *cap ? need : 2 * *cap;
        p = realloc(p, size * *cap);
    }
    return p;
}

static uint64_t
mix(uint64_t h, uint64_t v)
{
    h = (h ^ v) * 0x100000001b3ull;
    return h ^ h >> 29;
}

/* The slot of t for a key of hash h: the one whose index `same` matches
   to the key, or the empty one the key's index goes in. t grows first,
   rehashing its indices by `hash`. */
static int *
lookup(struct table *t, struct build *b, uint64_t h, const void *key,
       uint64_t (*hash)(struct build *, int), int (*same)(struct build *, int, const void *))
{
    if (2 * (t->n + 1) > t->cap) {
        uint32_t cap = t->cap ? 2 * t->cap : 256;
        int *slots = malloc(sizeof(int) * cap);
        memset(slots, -1, sizeof(int) * cap);
        for (uint32_t i = 0; i < t->cap; i++) {
            if (t->slots[i] >= 0) {
                uint32_t j = (uint32_t)hash(b, t->slots[i]) & (cap - 1);
                while (slots[j] >= 0) {
                    j = (j + 1) & (cap - 1);
                }
                slots[j] = t->slots[i];
            }
        }
        free(t->slots);
        t->slots = slots;
        t->cap = cap;
    }
    uint32_t j = (uint32_t)h & (t->cap - 1);
    while (t->slots[j] >= 0 && !same(b, t->slots[j], key)) {
        j = (j + 1) & (t->cap - 1);
    }
    return &t->slots[j];
}

static uint64_t
imm_hash(struct build *b, int i)
{
    return mix(0xcbf29ce484222325ull, (uint64_t)b->imms[i]);
}

static int
imm_same(struct build *b, int i, const void *key)
{
    return b->imms[i] == *(const int64_t *)key;
}

/* The index of constant word w among the unit's, entered if new. */
static uint32_t
imm_index(struct build *b, int64_t w)
{
    int *slot = lookup(&b->imm_table, b, mix(0xcbf29ce484222325ull, (uint64_t)w), &w, imm_hash, imm_same);
    if (*slot < 0) {
        b->imms = grow(b->imms, &b->capimms, b->nimms + 1, sizeof *b->imms);
        b->imms[b->nimms] = w;
        *slot = (int)b->nimms++;
        b->imm_table.n++;
    }
    return (uint32_t)*slot;
}

/* Where instruction value v is, for an exit: a raw word's or an
   object's location, or a constant. */
static lu_pv
place(struct asm_ *a, lu_v v)
{
    struct lu_ins *in = I(a, v);
    if (in->op == OP_CONST) {
        return in->imm == 0 ? LU_PV(DV_NULL, 0) : LU_PV(DV_CONST, imm_index(&a->b, in->imm));
    }
    if (in->op == OP_CONST_I || in->op == OP_CONST_F) {
        return in->rep == REP_OBJ ? LU_PV(DV_OBJ, LU_PV_NOLOC) : LU_PV(DV_IMM, imm_index(&a->b, in->imm));
    }
    return LU_PV(in->rep == REP_OBJ ? DV_OBJ : DV_RAW, lu_pv_arg_of_loc(in->loc));
}

/* What a slot or a field of state fs holds: a value's place, or a virtual
   object, entered once among d's (after the objects already entered,
   which are d's alone while make_deopt builds d) with its fields
   described. */
static lu_pv
describe(struct asm_ *a, struct lu_deopt *d, struct lu_fs *fs, lu_v r)
{
    if (r == LU_NOV) {
        return LU_PV(DV_NULL, 0);
    }
    if (!LU_IS_VIRT(r)) {
        return place(a, r);
    }
    lu_v real;
    struct lu_vobj *o = lu_fs_vobj(a->f, fs, LU_VIRT_ID(r), &real);
    if (real != LU_NOV) {
        return place(a, real);
    }
    struct build *b = &a->b;
    uint32_t k = 0;
    while (k < d->nobjs && b->objs[d->objs + k].id != LU_VIRT_ID(r)) {
        k++;
    }
    if (k == d->nobjs) {
        /* Entered before its fields look for theirs; their values have
           their row before any object they enter. */
        uint32_t fields = b->nvals;
        b->vals = grow(b->vals, &b->capvals, b->nvals + (uint32_t)o->n, sizeof *b->vals);
        b->nvals += (uint32_t)o->n;
        b->objs = grow(b->objs, &b->capobjs, b->nobjs + 1, sizeof *b->objs);
        b->objs[b->nobjs++] = (struct lu_dobj){ .kind = o->kind, .neg = o->neg, .id = LU_VIRT_ID(r),
                                                .type = o->type, .nfields = o->n, .fields = fields };
        d->nobjs++;
        for (int i = 0; i < o->n; i++) {
            lu_pv v = describe(a, d, fs, o->f[i]);      /* may move b->vals */
            b->vals[fields + (uint32_t)i] = v;
        }
    }
    return LU_PV(DV_VIRT, k);
}

/* An exit drops the reference of every owned value its state names: d's
   row of owned locations, the last in the unit's while it is built. */
struct owning {
    struct build *b;
    struct lu_deopt *d;
};

static void
note_owned(struct lu_func *f, lu_v v, void *arg)
{
    struct owning *w = arg;
    struct build *b = w->b;
    struct lu_ins *in = &f->ins[v];
    if (!(in->flags & INS_OWNED) || in->loc == LU_LOC_NONE) {
        return;
    }
    for (uint32_t i = w->d->owned; i < b->nowned; i++) {
        if (b->owned[i] == in->loc) {
            return;             /* named twice: one value, one location */
        }
    }
    b->owned = grow(b->owned, &b->capowned, b->nowned + 1, sizeof *b->owned);
    b->owned[b->nowned++] = in->loc;
    w->d->nowned++;
}

/* A frame record to find among the unit's: its values elsewhere. */
struct frame_key {
    struct lu_deopt_frame fr;
    const lu_pv *vals;
};

static uint64_t
frame_hash_of(const struct lu_deopt_frame *fr, const lu_pv *vals)
{
    uint64_t h = 0xcbf29ce484222325ull;
    h = mix(h, (uintptr_t)fr->code);
    h = mix(h, (uintptr_t)fr->func);
    h = mix(h, (uint64_t)(uint32_t)fr->pc << 32 | (uint32_t)fr->caller);
    h = mix(h, (uint64_t)(uint32_t)fr->nlocals << 32 | (uint64_t)(uint32_t)fr->nstack << 8 | fr->ret);
    for (int i = 0; i < fr->nlocals + fr->nstack; i++) {
        h = mix(h, vals[i]);
    }
    return h;
}

static uint64_t
frame_hash(struct build *b, int i)
{
    return frame_hash_of(&b->frames[i], b->vals + b->frames[i].vals);
}

static int
frame_same(struct build *b, int i, const void *key)
{
    const struct frame_key *k = key;
    const struct lu_deopt_frame *x = &b->frames[i], *y = &k->fr;
    return x->code == y->code && x->func == y->func && x->pc == y->pc && x->ret == y->ret
        && x->caller == y->caller && x->nlocals == y->nlocals && x->nstack == y->nstack
        && memcmp(b->vals + x->vals, k->vals, sizeof(lu_pv) * (size_t)(x->nlocals + x->nstack)) == 0;
}

/* The unit's record of a frame: one it has, or a copy entered now. */
static int
frame_record(struct build *b, const struct frame_key *k)
{
    int *slot = lookup(&b->frame_table, b, frame_hash_of(&k->fr, k->vals), k, frame_hash, frame_same);
    if (*slot >= 0) {
        return *slot;
    }
    uint32_t m = (uint32_t)(k->fr.nlocals + k->fr.nstack);
    b->vals = grow(b->vals, &b->capvals, b->nvals + m, sizeof *b->vals);
    memcpy(b->vals + b->nvals, k->vals, sizeof(lu_pv) * m);
    b->frames = grow(b->frames, &b->capframes, b->nframes + 1, sizeof *b->frames);
    b->frames[b->nframes] = k->fr;
    b->frames[b->nframes].vals = b->nvals;
    b->nvals += m;
    *slot = (int)b->nframes++;
    b->frame_table.n++;
    return *slot;
}

static struct lu_deopt *
make_deopt(struct asm_ *a, struct lu_fs *fs, int reason, int error, int drop)
{
    struct build *b = &a->b;
    struct pending *x = calloc(1, sizeof *x);
    x->index = a->nexits;
    struct lu_deopt *d = &x->d;
    d->unit = a->unit;
    d->objs = b->nobjs;
    d->owned = b->nowned;
    /* The frames outermost first, each slot described into the room,
       innermost first (the order its virtual objects are entered in). */
    uint32_t n = 0, nvals = 0;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        n++;
        nvals += (uint32_t)(p->nlocals + p->nstack);
    }
    b->chain = grow(b->chain, &b->capchain, n, sizeof *b->chain);
    b->room = grow(b->room, &b->caproom, nvals, sizeof *b->room);
    uint32_t k = n, end = nvals;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        b->chain[--k] = p;
        int m = p->nlocals + p->nstack;
        end -= (uint32_t)m;
        for (int i = 0; i < m; i++) {
            b->room[end + (uint32_t)i] = describe(a, d, fs, *lu_fs_slot(p, i));
        }
        d->visible |= k > 0 && !(p->code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN));
    }
    int caller = -1;
    for (k = 0; k < n; k++) {
        struct lu_fs *p = b->chain[k];
        struct frame_key key = { .fr = { .code = p->code, .func = p->func, .pc = p->pc, .ret = p->ret,
                                         .nlocals = p->nlocals, .nstack = p->nstack, .caller = caller },
                                 .vals = b->room + end };
        end += (uint32_t)(p->nlocals + p->nstack);
        caller = frame_record(b, &key);
    }
    d->nframes = (int32_t)n;
    d->frame = caller;
    lu_fs_each(a->f, fs, note_owned, &(struct owning){ b, d });
    d->reason = (uint8_t)reason;
    d->error = (uint8_t)error;
    d->drop = drop;
    d->opcode = (uint8_t)lu_base_opcode(fs->code, fs->pc);
    return d;
}

/* The unit's descriptions in one block, from the exits on (struct
   lu_unit), each exit's builder freed; and the addresses of the exits
   and of each published state written into the code that reads them. */
static void
pack(struct asm_ *a, struct lu_unit *u)
{
    struct build *b = &a->b;
    size_t sizes[] = { sizeof(struct lu_deopt) * (size_t)a->nexits, sizeof(struct lu_deopt_frame) * b->nframes,
                       sizeof(struct lu_dobj) * b->nobjs, sizeof(int64_t) * b->nimms, sizeof(lu_pv) * b->nvals,
                       sizeof(int32_t) * b->nowned };
    const void *from[] = { NULL, b->frames, b->objs, b->imms, b->vals, b->owned };
    void *to[6];
    size_t total = 0;
    for (int i = 0; i < 6; i++) {
        total += sizes[i];
    }
    char *block = malloc(total ? total : 1);
    for (int i = 0; i < 6; i++) {
        to[i] = block;
        if (from[i] != NULL) {
            memcpy(block, from[i], sizes[i]);
        }
        block += sizes[i];
    }
    u->exits = to[0];
    u->frames = to[1];
    u->objs = to[2];
    u->imms = to[3];
    u->vals = to[4];
    u->owned = to[5];
    u->ndeopts = a->nexits;
    u->nframes = (int)b->nframes;
    for (int i = 0; i < a->nexits; i++) {
        u->exits[i] = *a->exits[i].d;
        free(a->exits[i].d);
    }
    a->nexits = 0;
    int64_t p = (int64_t)(intptr_t)u->exits;
    memcpy(a->buf + a->deopts_patch, &p, 8);
    for (uint32_t i = 0; i < a->npubs; i++) {
        p = (int64_t)(intptr_t)&u->exits[a->pubs[i].exit];
        memcpy(a->buf + a->pubs[i].at, &p, 8);
    }
}

/* What building the descriptions holds, the exits not yet packed too. */
static void
build_free(struct asm_ *a)
{
    struct build *b = &a->b;
    for (int i = 0; i < a->nexits; i++) {
        free(a->exits[i].d);
    }
    free(b->frames);
    free(b->objs);
    free(b->vals);
    free(b->imms);
    free(b->owned);
    free(b->frame_table.slots);
    free(b->imm_table.slots);
    free(b->room);
    free(b->chain);
    free(a->pubs);
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
    struct lu_deopt *d = make_deopt(a, fs, reason, error, drop);      /* its index is nexits */
    a->exits[a->nexits++] = (struct exit_site){ label, d };
    return label;
}

/* Under -X lucifer_stress=deopt:N, every Nth guard execution leaves
   (FOUNDATIONS 2.7). */
static void
stress_exit(struct asm_ *a, struct lu_ins *in)
{
    if (a->f->stress && !(in->flags & INS_NOSTRESS)) {
        int stress = exit_to(a, in->fs, EXIT_STRESS, 0, 0);
        mov_ri(a, R11, (int64_t)(intptr_t)a->unit->stress_counter);
        rex(a, 1, 0, 0, R11);
        b1(a, 0xFF);
        modrm_mem(a, 1, R11, 0);                /* dec qword [r11] */
        jcc(a, CC_E, stress);
    }
}

/* A guard's failure jumps here, blaming the site it was moved from. */
static int
guard_exit(struct asm_ *a, struct lu_ins *in)
{
    stress_exit(a, in);
    int label = exit_to(a, in->fs, EXIT_GUARD, 0, 0);
    if (in->blame != NULL) {
        a->exits[a->nexits - 1].d->blame_code = in->blame;
        a->exits[a->nexits - 1].d->blame_pc = in->blame_pc;
    }
    a->exits[a->nexits - 1].d->prof = in->prof;
    a->exits[a->nexits - 1].d->tier = in->op == OP_GUARD_BIT;
    return label;
}

/* --- calls --- */

extern int lu_jit_next_pc(PyCodeObject *code, int pc);

/* The description an instruction that publishes (lu_publishes) shows
   walkers: an exit's that is never taken. */
static struct lu_deopt *
published(struct asm_ *a, struct lu_ins *in)
{
    exit_to(a, in->fs, EXIT_SIDE, 0, 0);
    return a->exits[a->nexits - 1].d;
}

static void incref(struct asm_ *a, int r);
static void decref(struct asm_ *a, int r, struct lu_ins *in, struct lu_deopt *d, int left);

/* Whether the instruction hands value v to the C it calls. */
static int
hands(const struct lu_ins *in, lu_v v)
{
    for (int j = 0; j < in->nargs; j++) {
        if (in->args[j] == v) {
            return 1;
        }
    }
    return in->a == v || in->b == v || in->c == v;
}

/* What the frames' slots hold of the objects a published call is handed,
   beyond compiled code's one reference per object
   (docs/design/jit-architecture.md 10): the interpreter's frames hold a
   reference per slot, and the C the call runs may count them. One hold
   per further slot that names an object compiled code owns, one per slot
   for an object it only borrows. Every frame of the state counts, an
   inlined one as the frame the interpreter would have pushed; an object
   not yet made holds nothing. A constant is the unit's, held as long as
   the unit is. */
static int
holds_of(struct asm_ *a, struct lu_ins *in, struct hold **out)
{
    struct lu_fs *fs = in->fs;
    int n = 0, nh = 0;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        n += p->nlocals + p->nstack;
    }
    struct hold *h = lu_ir_alloc(a->f, sizeof(struct hold) * (size_t)(n ? n : 1));
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        for (int i = 0; i < p->nlocals + p->nstack; i++) {
            lu_v v = *lu_fs_slot(p, i);
            if (LU_IS_VIRT(v)) {
                lu_fs_vobj(a->f, fs, LU_VIRT_ID(v), &v);        /* the object it became, if it did */
            }
            if (v < 0 || I(a, v)->rep != REP_OBJ || I(a, v)->op == OP_CONST || I(a, v)->op == OP_BOOL
                || !hands(in, v)) {
                continue;
            }
            int k = 0;
            while (k < nh && h[k].v != v) {
                k++;
            }
            if (k < nh) {
                h[k].n++;
            }
            else {
                h[nh++] = (struct hold){ v, (I(a, v)->flags & INS_OWNED) ? 0 : 1 };
            }
        }
    }
    int kept = 0;
    for (int k = 0; k < nh; k++) {
        if (h[k].n > 0) {
            h[kept++] = h[k];
        }
    }
    *out = h;
    return kept;
}

/* The arguments of a call (bits by index) whose stack entries are among
   the holds: a Python callee's parameters take those references, as the
   interpreter moves its operands into the callee's frame
   (runtime/jit/helpers.c call_function). */
static uint64_t
takes_of(struct asm_ *a, struct lu_ins *in, const struct hold *h, int nh)
{
    struct lu_fs *fs = in->fs;
    if (in->op != OP_HELPER || (in->imm != H_call && in->imm != H_call_kw)) {
        return 0;
    }
    int *left = lu_ir_alloc(a->f, sizeof(int) * (size_t)(nh ? nh : 1));
    for (int k = 0; k < nh; k++) {
        /* No more than the call's own operands name it. */
        left[k] = 0;
        for (int i = fs->nstack - in->drop; i < fs->nstack; i++) {
            lu_v v = i >= 0 ? fs->stack[i] : LU_NOV;
            if (LU_IS_VIRT(v)) {
                lu_fs_vobj(a->f, fs, LU_VIRT_ID(v), &v);
            }
            left[k] += v == h[k].v && left[k] < h[k].n;
        }
    }
    uint64_t takes = 0;
    for (int j = 0; j < in->nargs && j < 64; j++) {
        for (int k = 0; k < nh; k++) {
            if (in->args[j] == h[k].v && left[k] > 0) {
                left[k]--;
                takes |= (uint64_t)1 << j;
                break;
            }
        }
    }
    return takes;
}

/* Before a call during which a walker may see the frames: the state `d`
   of in->fs, where walkers read them, and the instruction the innermost
   frame shows as the one after the one running (runtime/jit/exit.c): the
   state is before that instruction, or, for a release's, after it. The
   objects the call is handed get the references the frames' slots are
   owed (holds_of), unless
   the call only parks (its registers saved: no code of this thread runs
   that could count them). The inlined frames count toward the recursion
   limit only here, across the call, where Python code may run or a walker
   may read the count: entering and leaving one costs nothing, and an exit
   pushes them (runtime/jit/exit.c). Clobbers RAX, R11 and the flags. */
static void
depth_add(struct asm_ *a, int32_t n)
{
    rex(a, 0, 0, 0, R14);
    b1(a, 0x81);
    modrm_mem(a, ALU_ADD, R14, offsetof(PyThreadState, py_recursion_remaining));
    b4(a, n);                                   /* add dword [r14+remaining], n */
}

static void
publish(struct asm_ *a, struct lu_ins *in, struct lu_deopt *d)
{
    struct lu_fs *fs = in->fs;
    a->inlined = d->nframes - 1;
    if (a->inlined > 0) {
        depth_add(a, -a->inlined);
    }
    d->instr = in->op != OP_DECREF ? lu_jit_next_pc(fs->code, fs->pc) : fs->pc;
    a->pubs = grow(a->pubs, &a->cappubs, a->npubs + 1, sizeof *a->pubs);
    a->pubs[a->npubs].at = a->n + 2;
    a->pubs[a->npubs++].exit = ((struct pending *)d)->index;
    mov_ri(a, RAX, 0x1122334455667788LL);       /* d's address, patched (pack) */
    store(a, R15, offsetof(struct lu_frame, jit_desc), RAX);
    a->nheld = d->regs ? 0 : holds_of(a, in, &a->held);
    d->takes = takes_of(a, in, a->held, a->nheld);
    if (a->nheld > 0) {
        b1(a, 0x51);                            /* push rcx: an argument may be in it already */
        for (int k = 0; k < a->nheld; k++) {
            to_gpr(a, R11, a->held[k].v);
            for (int i = 0; i < a->held[k].n; i++) {
                incref(a, R11);
            }
        }
        b1(a, 0x59);                            /* pop rcx */
    }
}

/* After the call: the holds go, with the call's result (RAX, or XMM0 for
   `rep` REP_F64) kept. The values are in memory, where the allocator puts
   what a published state names. */
static void
unpublish(struct asm_ *a, int rep)
{
    if (a->inlined > 0) {
        depth_add(a, a->inlined);
        a->inlined = 0;
    }
    store_mi(a, R15, offsetof(struct lu_frame, jit_desc), 0);
    if (a->nheld == 0) {
        return;
    }
    alu_ri(a, ALU_SUB, RSP, 16);
    store(a, RSP, 0, RAX);
    if (rep == REP_F64) {
        sse_rm(a, 0xF2, 0x11, 0, RSP, 8);
    }
    for (int k = 0; k < a->nheld; k++) {
        for (int i = 0; i < a->held[k].n; i++) {
            to_gpr(a, RDX, a->held[k].v);
            decref(a, RDX, NULL, NULL, -1);
        }
    }
    if (rep == REP_F64) {
        sse_rm(a, 0xF2, 0x10, 0, RSP, 8);
    }
    load(a, RAX, RSP, 0);
    alu_ri(a, ALU_ADD, RSP, 16);
    a->nheld = 0;
}

/* `d`, when the helper publishes: the state it publishes. */
static void
call_helper(struct asm_ *a, lu_v v, struct lu_deopt *d)
{
    struct lu_ins *in = I(a, v);
    const struct lu_helper *h = &lu_helpers[in->imm];
    if (d != NULL) {
        publish(a, in, d);
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
    if (d != NULL) {
        unpublish(a, h->rep);
    }
}

/* After a call that published: a walker that made the frames real during
   it leaves at `left`, when the instruction has such an exit of its own. */
static void
settle(struct asm_ *a, int left)
{
    if (left >= 0) {
        cmp8_mi(a, R15, offsetof(struct lu_frame, owner), LU_FRAME_JIT);
        jcc(a, CC_NE, left);
    }
}

extern void lu_jit_decref(PyObject *o);

/* A slow path of an instruction that is not a call (its values may be in
   any register) whose C may park the task in a lock, where another task's
   stop-the-world walker may read the frame: every register is saved where
   the published state reads them (jit_regs), and restored after but RAX,
   the result. A walker that wrote the locals meanwhile (the frame is no
   longer compiled code's) leaves at the instruction's own state, which
   nothing has changed but the reference the call took (RAX), released
   first. Arguments are set up after parked_begin; the call is the
   caller's. */
static void
parked_begin(struct asm_ *a, struct lu_ins *in)
{
    a->parks = 1;
    alu_ri(a, ALU_SUB, RSP, 256);
    b1(a, 0xE8);
    rel32_to(a, a->save_all);                      /* call save_all */
    store(a, R15, offsetof(struct lu_frame, jit_regs), RSP);
    struct lu_deopt *d = published(a, in);
    d->regs = 1;
    publish(a, in, d);
}

static void
parked_end(struct asm_ *a, struct lu_ins *in)
{
    int kept = new_label(a);
    int left = exit_to(a, in->fs, EXIT_FRAME, 0, 0);
    unpublish(a, REP_NONE);
    cmp8_mi(a, R15, offsetof(struct lu_frame, owner), LU_FRAME_JIT);
    jcc(a, CC_E, kept);
    mov_rr(a, RDI, RAX);
    call_abs(a, (void *)lu_jit_decref);
    b1(a, 0xE8);
    rel32_to(a, a->restore_all);
    alu_ri(a, ALU_ADD, RSP, 256);
    jmp(a, left);
    bind(a, kept);
    b1(a, 0xE8);
    rel32_to(a, a->restore_all);                   /* call restore_all */
    alu_ri(a, ALU_ADD, RSP, 256);
}

static void
parked_call(struct asm_ *a, lu_v v, void *fn, struct move *m, int nm)
{
    parked_begin(a, I(a, v));
    parallel_moves(a, m, nm);
    call_abs(a, fn);
    parked_end(a, I(a, v));
}

/* The stubs: every register at [rsp+8] as an exit saves them (the call's
   return address below), and back but RAX. */
static void
parked_stubs(struct asm_ *a)
{
    bind(a, a->save_all);
    for (int r = 0; r < 16; r++) {
        if (r != RSP) {
            store(a, RSP, 8 + 8 * r, r);
        }
    }
    for (int x = 0; x < 16; x++) {
        sse_rm(a, 0xF2, 0x11, x, RSP, 8 + 128 + 8 * x);
    }
    b1(a, 0xC3);
    bind(a, a->restore_all);
    for (int r = 0; r < 16; r++) {
        if (r != RSP && r != RAX) {
            load(a, r, RSP, 8 + 8 * r);
        }
    }
    for (int x = 0; x < 16; x++) {
        sse_rm(a, 0xF2, 0x10, x, RSP, 8 + 128 + 8 * x);
    }
    b1(a, 0xC3);
}

/* --- the instructions --- */

/* The carrier (in RAX) passes a quiescent state, as the interpreter's poll
   does: compiled code's poll when the poll word asks (LU_POLL_QSBR). Clobbers
   RCX. */
static void
quiescent(struct asm_ *a)
{
    int same = new_label(a);
    mov_ri(a, RCX, (int64_t)(intptr_t)&_PyRuntime.qsbr.wr_seq);
    load(a, RCX, RCX, 0);
    cmp_rm(a, RCX, RAX, offsetof(struct lu_carrier, qsbr.seq));
    jcc(a, CC_E, same);
    store(a, RAX, offsetof(struct lu_carrier, qsbr.seq), RCX);
    bind(a, same);
}

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


/* Py_DECREF of r: the owning thread's count above one drops in place;
   anything else (immortal, last reference, another thread's object) goes
   to C. DECREF is a call to the allocator (no value is in a register
   across it). When the release may run a finalizer, C runs with `d`, the
   state of `in`, published, and a walker's write then leaves at `left`
   if it is a label. Clobbers RAX, RCX. */
static void settle(struct asm_ *a, int left);

static void
decref(struct asm_ *a, int r, struct lu_ins *in, struct lu_deopt *d, int left)
{
    int done = new_label(a), slow = new_label(a);
    test_rr(a, r, r);
    jcc(a, CC_E, done);
    load32(a, RCX, r, offsetof(PyObject, ob_ref_local));
    cmp32_ri8(a, RCX, -1);
    jcc(a, CC_E, done);                         /* immortal */
    cmp32_ri8(a, RCX, 1);
    jcc(a, CC_BE, slow);                        /* the last local reference, or none */
    b1(a, 0x64);
    b1(a, 0x48);
    b1(a, 0x8B);
    b1(a, 0x04);
    b1(a, 0x25);
    b4(a, 0);                                   /* mov rax, fs:[0] */
    cmp_rm(a, RAX, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, slow);
    rex(a, 0, 0, 0, RCX);
    b1(a, 0x83);
    modrm_rr(a, ALU_SUB, RCX);
    b1(a, 1);                                   /* sub ecx, 1 */
    op_rm(a, 0, 0x89, RCX, r, offsetof(PyObject, ob_ref_local));
    jmp(a, done);
    bind(a, slow);
    int keep = in != NULL && !lu_alloc_call(in);   /* a release values stay in registers across */
    if (keep) {
        save_volatile(a);
    }
    mov_rr(a, RDI, r);
    if (d != NULL) {
        publish(a, in, d);
    }
    call_abs(a, (void *)lu_jit_decref);
    if (d != NULL) {
        unpublish(a, REP_NONE);
        settle(a, left);
    }
    if (keep) {
        restore_volatile(a);
    }
    bind(a, done);
}
extern PyObject *lu_jit_box_int(int64_t v);
extern int64_t lu_jit_unbox_wide(PyObject *o);
extern PyObject *lu_jit_box_float(double v);
extern PyObject *lu_jit_list_item(PyObject *list, int64_t i);
extern PyObject *lu_jit_tuple_item(PyObject *t, int64_t i);
extern int lu_jit_list_store(PyObject *list, int64_t i, PyObject *v);
extern PyObject *lu_jit_attr_load(PyObject *owner, int64_t version, int64_t index, PyObject *name);
extern int lu_jit_truth(PyObject *v);

static void
lea(struct asm_ *a, int dst, int base, int32_t disp)
{
    op_rm(a, 1, 0x8D, dst, base, disp);
}

/* mov dword [base+disp], imm32 */
static void
store32_mi(struct asm_ *a, int base, int32_t disp, int32_t imm)
{
    rex(a, 0, 0, 0, base);
    b1(a, 0xC7);
    modrm_mem(a, 0, base, disp);
    b4(a, imm);
}

/* mov r, fs:[0]: the running thread's id (_Py_ThreadId, ob_tid's owner). */
static void
thread_id(struct asm_ *a, int r)
{
    b1(a, 0x64);
    rex(a, 1, r, 0, 0);
    b1(a, 0x8B);
    b1(a, 0x04 | ((r & 7) << 3));
    b1(a, 0x25);
    b4(a, 0);
}

/* Pops a block of `size` bytes from the carrier's mimalloc heap `heap`
   (the entry in RAX), as mi_heap_malloc_small does: the block in `dst`, its
   page in `page`. An empty free list takes the blocks the thread freed on
   that page since (_mi_page_free_collect's usual case); `none` when there
   are none either (C takes another page). Clobbers R11. */
static void
pop_block(struct asm_ *a, int heap, size_t size, int dst, int page, int none)
{
    int32_t direct = (int32_t)(offsetof(struct lu_entry, mimalloc.heaps) + heap * sizeof(mi_heap_t)
                               + offsetof(mi_heap_t, pages_free_direct) + sizeof(mi_page_t *) * ((size + 7) / 8));
    int have = new_label(a);
    load(a, page, RAX, direct);
    load(a, dst, page, offsetof(mi_page_t, free));
    test_rr(a, dst, dst);
    jcc(a, CC_NE, have);
    load(a, dst, page, offsetof(mi_page_t, local_free));
    test_rr(a, dst, dst);
    jcc(a, CC_E, none);
    store_mi(a, page, offsetof(mi_page_t, local_free), 0);
    /* free_is_zero = false: a bit field, found by setting it. */
    mi_page_t probe;
    memset(&probe, 0, sizeof probe);
    probe.free_is_zero = 1;
    int byte = 0;
    while (((uint8_t *)&probe)[byte] == 0) {
        byte++;
    }
    rex(a, 0, 0, 0, page);
    b1(a, 0x80);
    modrm_mem(a, ALU_AND, page, byte);
    b1(a, (uint8_t)~((uint8_t *)&probe)[byte]);    /* and byte [page+byte], ~bit */
    bind(a, have);
    load(a, R11, dst, 0);                       /* the next free block */
    store(a, page, offsetof(mi_page_t, free), R11);
    b1(a, 0x66);
    rex(a, 0, 0, 0, page);
    b1(a, 0xFF);
    modrm_mem(a, 0, page, offsetof(mi_page_t, used));  /* inc word [page+used] */
}

/* -X lucifer_jit_stats: one more allocation of in's kind at its site.
   Clobbers R11. */
static void
alloc_count(struct asm_ *a, struct lu_ins *in)
{
    int k = a->unit->counts != NULL && in->src != NULL ? lu_alloc_kind(in) : -1;
    if (k >= 0) {
        mov_ri(a, R11, (int64_t)(intptr_t)lu_jit_alloc_site(in->src, in->src_pc, k));
        rex(a, 1, 0, 0, R11);
        b1(a, 0xFF);
        modrm_mem(a, 0, R11, 0);                /* inc qword [r11] */
    }
}

/* The same before an instruction that allocates; a call counts only when
   it calls a class, and an int box counts in emit_box (not a small int). */
static void
alloc_count_before(struct asm_ *a, lu_v v)
{
    struct lu_ins *in = I(a, v);
    int k = a->unit->counts != NULL && in->src != NULL ? lu_alloc_kind(in) : -1;
    if (k < 0 || k == ALLOC_int || k == ALLOC_float) {
        return;
    }
    int skip = new_label(a);
    if (k == ALLOC_class_call) {
        to_gpr(a, R11, in->args[0]);
        load(a, R11, R11, offsetof(PyObject, ob_type));
        rex(a, 0, 0, 0, R11);
        b1(a, 0xF6);
        modrm_mem(a, 0, R11, offsetof(PyTypeObject, tp_flags) + 3);
        b1(a, (uint8_t)(Py_TPFLAGS_TYPE_SUBCLASS >> 24));      /* test byte: a type */
        jcc(a, CC_E, skip);
    }
    alloc_count(a, in);
    bind(a, skip);
}

/* A box of in->a's raw value, in RAX, as PyLong_FromLongLong and
   PyFloat_FromDouble make it: a small int from the cache, a one-digit int
   or a float from the carrier's freelist, else from its mimalloc object
   heap, with the header _Py_NewReference gives it. Anything else is C's,
   which keeps the registers: a wider int, a page with no block at hand, or
   a hook that sees allocations. */
static void
emit_box(struct asm_ *a, struct lu_ins *in)
{
    int is_int = in->op == OP_BOX_INT, slow = new_label(a), done = new_label(a);
    int32_t list = is_int ? (int32_t)offsetof(struct lu_entry, free_ints) : (int32_t)offsetof(struct lu_entry, free_floats);
    int x = 0;
    if (is_int) {
        int fresh = new_label(a), r = gpr_of(a, in->a, RDX);
        lea(a, RCX, r, LU_NSMALLNEGINTS);
        alu_ri(a, ALU_CMP, RCX, LU_NSMALLNEGINTS + LU_NSMALLPOSINTS);
        jcc(a, CC_AE, fresh);                   /* unsigned: below the cache too */
        rex(a, 1, RCX, 0, RCX);
        b1(a, 0x69);
        modrm_rr(a, RCX, RCX);
        b4(a, (int32_t)sizeof(PyLongObject));  /* imul rcx, rcx, size */
        mov_ri(a, RAX, (int64_t)(intptr_t)&_PyRuntime.singletons.small_ints[0]);
        alu_rr(a, ALU_ADD, RAX, RCX);
        jmp(a, done);
        bind(a, fresh);
        alloc_count(a, in);
        lea(a, RCX, r, PyLong_MASK);
        alu_ri(a, ALU_CMP, RCX, 2 * PyLong_MASK);
        jcc(a, CC_A, slow);                     /* more than one digit */
    }
    else {
        x = fpr_of(a, in->a, 0);
        alloc_count(a, in);
    }
    mov_ri(a, R11, (int64_t)(intptr_t)&_PyRuntime.allocators.observed);
    cmp_mi(a, 0, R11, 0, 0);
    jcc(a, CC_NE, slow);
    int page = new_label(a), header = new_label(a);
    load(a, RAX, R14, offsetof(struct lu_task, carrier));
    load(a, RAX, RAX, offsetof(struct lu_carrier, current));
    load(a, RCX, RAX, list + (int32_t)offsetof(struct lu_freelist, head));
    test_rr(a, RCX, RCX);
    jcc(a, CC_E, page);
    load(a, R11, RCX, sizeof(PyObject));       /* LU_FREE_NEXT */
    store(a, RAX, list + (int32_t)offsetof(struct lu_freelist, head), R11);
    b1(a, 0x83);
    modrm_mem(a, ALU_SUB, RAX, list + (int32_t)offsetof(struct lu_freelist, n));
    b1(a, 1);                                   /* sub dword [rax+n], 1 */
    jmp(a, header);
    bind(a, page);                              /* a block of lu_object_new's size; its type too */
    pop_block(a, LU_HEAP_OBJECT, is_int ? offsetof(PyLongObject, long_value.ob_digit) + sizeof(digit)
                                        : sizeof(PyFloatObject), RCX, RDX, slow);
    mov_ri(a, R11, (int64_t)(intptr_t)(is_int ? &PyLong_Type : &PyFloat_Type));
    store(a, RCX, offsetof(PyObject, ob_type), R11);
    bind(a, header);
    thread_id(a, R11);
    store(a, RCX, offsetof(PyObject, ob_tid), R11);
    _Static_assert(offsetof(PyObject, ob_flags) == 8 && offsetof(PyObject, ob_gc_bits) == 11
                   && offsetof(PyObject, ob_ref_local) == 12, "flags, mutex and GC bits are one word");
    store32_mi(a, RCX, offsetof(PyObject, ob_flags), 0);
    store32_mi(a, RCX, offsetof(PyObject, ob_ref_local), 1);
    store_mi(a, RCX, offsetof(PyObject, ob_ref_shared), 0);
    if (is_int) {
        int positive = new_label(a);
        to_gpr(a, RAX, in->a);                  /* again: RDX held the page */
        mov_ri(a, R11, ((uintptr_t)1 << _PyLong_NON_SIZE_BITS) | 0);
        test_rr(a, RAX, RAX);
        jcc(a, CC_NS, positive);
        unary(a, 3, RAX);                       /* neg rax */
        mov_ri(a, R11, ((uintptr_t)1 << _PyLong_NON_SIZE_BITS) | 2);
        bind(a, positive);
        store(a, RCX, offsetof(PyLongObject, long_value.lv_tag), R11);
        op_rm(a, 0, 0x89, RAX, RCX, offsetof(PyLongObject, long_value.ob_digit));
    }
    else {
        sse_rm(a, 0xF2, 0x11, x, RCX, offsetof(PyFloatObject, ob_fval));
    }
    mov_rr(a, RAX, RCX);
    jmp(a, done);
    bind(a, slow);
    save_volatile(a);
    if (is_int) {
        to_gpr(a, RDI, in->a);
        call_abs(a, (void *)lu_jit_box_int);
    }
    else {
        to_fpr(a, 0, in->a);
        call_abs(a, (void *)lu_jit_box_float);
    }
    restore_volatile(a);
    bind(a, done);
}

/* BUILD_TUPLE and BUILD_LIST of a few items in line, as PyTuple_New and
   PyList_New make them: the object (and a list's array) popped from the
   carrier's mimalloc pages, counted for the collector, tracked, and filled
   with new references. The helper takes the rest: a page with no block at
   hand, a collection due, a hook that sees allocations, and
   -X lucifer_stress=gc:N. 0 when the build is the helper's alone. */
#define BUILD_INLINE 8

static int
emit_build(struct asm_ *a, lu_v v)
{
    struct lu_ins *in = I(a, v);
    int n = in->nargs, is_list = in->imm == H_build_list;
    if (_PyRuntime.stress.gc != 0 || n > BUILD_INLINE || (!is_list && n == 0)) {
        return 0;
    }
    int slow = new_label(a), undo = new_label(a), done = new_label(a);
    size_t size = is_list ? sizeof(PyListObject) : offsetof(PyTupleObject, ob_item) + (size_t)n * sizeof(PyObject *);
    for (int i = 0; i < n; i++) {
        to_gpr(a, RAX, in->args[i]);
        store(a, RSP, 8 * i, RAX);              /* the helper's argument array, as call_helper makes it */
    }
    /* IF_CALL: nothing live past the build is in a register. Until the
       fast path can no longer fall back to the call it writes only
       caller-saved registers, which the call clobbers anyway. */
    mov_ri(a, R11, (int64_t)(intptr_t)&_PyRuntime.allocators.observed);
    cmp_mi(a, 0, R11, 0, 0);
    jcc(a, CC_NE, slow);
    load(a, RAX, R14, offsetof(struct lu_task, carrier));
    load(a, RAX, RAX, offsetof(struct lu_carrier, current));
    cmp_mi(a, 0, RAX, offsetof(struct lu_entry, gc_alloc_count), LU_GC_ALLOC_FLUSH - 1);
    jcc(a, CC_GE, slow);                        /* lu_gc_record_allocation would flush the count */
    pop_block(a, LU_HEAP_GC, size, RDX, RCX, slow);
    if (is_list && n > 0) {
        pop_block(a, LU_HEAP_MEM, sizeof(struct lu_list_array) + (size_t)n * sizeof(PyObject *), RSI, RDI, undo);
    }
    b1(a, 0x83);
    modrm_mem(a, ALU_ADD, RAX, offsetof(struct lu_entry, gc_alloc_count));
    b1(a, 1);                                   /* add dword [rax+count], 1 */
    thread_id(a, R11);
    store(a, RDX, offsetof(PyObject, ob_tid), R11);
    store32_mi(a, RDX, offsetof(PyObject, ob_flags), (int32_t)((uint32_t)LU_GC_TRACKED << 24));    /* ob_gc_bits */
    store32_mi(a, RDX, offsetof(PyObject, ob_ref_local), 1);
    store_mi(a, RDX, offsetof(PyObject, ob_ref_shared), 0);
    mov_ri(a, R11, (int64_t)(intptr_t)(is_list ? &PyList_Type : &PyTuple_Type));
    store(a, RDX, offsetof(PyObject, ob_type), R11);
    store_mi(a, RDX, offsetof(PyVarObject, ob_size), n);
    int items = RDX;
    int32_t first = offsetof(PyTupleObject, ob_item);
    if (!is_list) {
        store_mi(a, RDX, offsetof(PyTupleObject, ob_hash), -1);
    }
    else {
        store_mi(a, RDX, offsetof(PyListObject, allocated), n);
        if (n > 0) {
            store_mi(a, RSI, offsetof(struct lu_list_array, allocated), n);
            lea(a, R11, RSI, offsetof(struct lu_list_array, ob_item));
            store(a, RDX, offsetof(PyListObject, ob_item), R11);
            items = RSI;
            first = offsetof(struct lu_list_array, ob_item);
        }
        else {
            store_mi(a, RDX, offsetof(PyListObject, ob_item), 0);
        }
    }
    for (int i = 0; i < n; i++) {
        load(a, RBX, RSP, 8 * i);
        incref(a, RBX);                         /* clobbers RAX, RCX */
        store(a, items, first + 8 * i, RBX);
    }
    mov_rr(a, RAX, RDX);
    jmp(a, done);
    bind(a, undo);                              /* the object's block goes back */
    load(a, R11, RCX, offsetof(mi_page_t, free));
    store(a, RDX, 0, R11);
    store(a, RCX, offsetof(mi_page_t, free), RDX);
    b1(a, 0x66);
    b1(a, 0xFF);
    modrm_mem(a, 1, RCX, offsetof(mi_page_t, used));   /* dec word [rcx+used] */
    bind(a, slow);
    mov_rr(a, RDI, RSP);
    mov_ri(a, RSI, n);
    call_abs(a, lu_helpers[in->imm].fn);
    bind(a, done);
    return 1;
}

/* An instance of a class whose instances object.__new__ makes, in line as
   PyType_GenericAlloc makes it: the block (and its preheader) popped from
   the carrier's GC heap, zeroed, counted for the collector, tracked, with
   a reference to its type. The helper takes the rest, as for builds. 0
   when the allocation is the helper's alone (a block too large to pop). */
static int
emit_new_object(struct asm_ *a, lu_v v)
{
    struct lu_ins *in = I(a, v);
    PyTypeObject *tp = (PyTypeObject *)(intptr_t)I(a, in->args[0])->imm;
    size_t pre = (tp->tp_flags & Py_TPFLAGS_PREHEADER) ? 2 * sizeof(PyObject *) : 0;
    Py_ssize_t room = lu_inline_settle_room(tp);         /* every later instance's room */
    size_t size = pre + (size_t)tp->tp_basicsize + lu_inline_room_size(room);
    uint32_t flags = room > 0 ? lu_inline_room_flags(tp, room) : 0;
    if (_PyRuntime.stress.gc != 0 || (size + 7) / 8 > MI_SMALL_WSIZE_MAX) {
        return 0;
    }
    int slow = new_label(a), done = new_label(a);
    mov_ri(a, R11, (int64_t)(intptr_t)&_PyRuntime.allocators.observed);
    cmp_mi(a, 0, R11, 0, 0);
    jcc(a, CC_NE, slow);
    load(a, RAX, R14, offsetof(struct lu_task, carrier));
    load(a, RAX, RAX, offsetof(struct lu_carrier, current));
    cmp_mi(a, 0, RAX, offsetof(struct lu_entry, gc_alloc_count), LU_GC_ALLOC_FLUSH - 1);
    jcc(a, CC_GE, slow);                        /* lu_gc_record_allocation would flush the count */
    pop_block(a, pre ? LU_HEAP_GC_PRE : LU_HEAP_GC, size, RDX, RCX, slow);
    b1(a, 0x83);
    modrm_mem(a, ALU_ADD, RAX, offsetof(struct lu_entry, gc_alloc_count));
    b1(a, 1);                                   /* add dword [rax+count], 1 */
    for (size_t off = 0; off < size; off += 8) {
        if (off < pre || off >= pre + sizeof(PyObject)) {
            store_mi(a, RDX, (int32_t)off, 0);  /* the preheader, slots and inline values */
        }
    }
    if (pre) {
        lea(a, RDX, RDX, (int32_t)pre);
    }
    thread_id(a, R11);
    store(a, RDX, offsetof(PyObject, ob_tid), R11);
    store32_mi(a, RDX, offsetof(PyObject, ob_flags), (int32_t)((uint32_t)LU_GC_TRACKED << 24 | flags));    /* ob_gc_bits, the room */
    store32_mi(a, RDX, offsetof(PyObject, ob_ref_local), 1);
    store_mi(a, RDX, offsetof(PyObject, ob_ref_shared), 0);
    mov_ri(a, RBX, (int64_t)(intptr_t)tp);
    store(a, RDX, offsetof(PyObject, ob_type), RBX);
    incref(a, RBX);                             /* clobbers RAX, RCX */
    mov_rr(a, RAX, RDX);
    jmp(a, done);
    bind(a, slow);
    mov_ri(a, RDI, (int64_t)(intptr_t)tp);
    call_abs(a, lu_helpers[in->imm].fn);
    bind(a, done);
    return 1;
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
    int boxed = new_label(a), done = new_label(a), typed = new_label(a), held = new_label(a);
    int r = gpr_of(a, in->a, RDX);
    if (in->b != LU_NOV) {
        /* The object the word held is the number, and held already. */
        to_gpr(a, RAX, in->b);
        test_rr(a, RAX, RAX);
        jcc(a, CC_NE, typed);
    }
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
    if (in->b != LU_NOV) {
        jmp(a, fail);                           /* unboxed when its object was asked for, and an object now */
    }
    else {
        alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
        jcc(a, CC_BE, fail);                    /* absent, or the other kind unboxed */
    }
    bind(a, typed);
    mov_ri(a, RCX, (int64_t)(intptr_t)(is_int ? &PyLong_Type : &PyFloat_Type));
    cmp_rm(a, RCX, RAX, offsetof(PyObject, ob_type));
    jcc(a, CC_NE, fail);
    if (in->b == LU_NOV) {
        load32(a, RCX, RAX, offsetof(PyObject, ob_ref_local));
        cmp32_ri8(a, RCX, -1);
        jcc(a, CC_E, held);                     /* immortal (a small int) */
        b1(a, 0x64);
        b1(a, 0x4C);
        b1(a, 0x8B);
        b1(a, 0x1C);
        b1(a, 0x25);
        b4(a, 0);                               /* mov r11, fs:[0] */
        cmp_rm(a, R11, RAX, offsetof(PyObject, ob_tid));
        jcc(a, CC_NE, fail);
    }
    bind(a, held);
    if (is_int) {
        /* An int, as UNBOX_INT reads it: one digit here, more in C. */
        int wide = new_label(a);
        load(a, RCX, RAX, offsetof(PyLongObject, long_value.lv_tag));
        alu_ri(a, ALU_CMP, RCX, 2 << _PyLong_NON_SIZE_BITS);
        jcc(a, CC_AE, wide);
        load32(a, RAX, RAX, offsetof(PyLongObject, long_value.ob_digit));
        rex(a, 0, 0, 0, RCX);
        b1(a, 0xF6);
        modrm_rr(a, 0, RCX);
        b1(a, 2);                               /* test cl, 2: negative */
        jcc(a, CC_E, done);
        unary(a, 3, RAX);
        jmp(a, done);
        bind(a, wide);
        save_volatile(a);
        mov_rr(a, RDI, RAX);
        call_abs(a, (void *)lu_jit_unbox_wide);
        restore_volatile(a);
        mov_ri(a, RCX, INT64_MIN);
        alu_rr(a, ALU_CMP, RAX, RCX);
        jcc(a, CC_E, fail);
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

/* PyMutex_LockFast on the lock byte of the object in r (ob_mutex), as
   CPython's LOCK_OBJECT: `busy` when another thread holds it. Clobbers
   RAX, RCX. */
static void
lock_object(struct asm_ *a, int r, int busy)
{
    alu_rr(a, ALU_XOR, RAX, RAX);
    b1(a, 0xB1);
    b1(a, 1);                                   /* mov cl, 1 */
    b1(a, 0xF0);
    rex(a, 0, RCX, 0, r);
    b1(a, 0x0F);
    b1(a, 0xB0);
    modrm_mem(a, RCX, r, offsetof(PyObject, ob_mutex));     /* lock cmpxchg [r+mutex], cl */
    jcc(a, CC_NE, busy);
}

/* Its release: a waiter parked on it is woken in C, a call that clobbers
   the caller-saved registers (RDI among them). */
static void
unlock_object(struct asm_ *a, int r)
{
    int unlocked = new_label(a);
    b1(a, 0xB0);
    b1(a, 1);                                   /* mov al, 1 */
    alu_rr(a, ALU_XOR, RCX, RCX);
    b1(a, 0xF0);
    rex(a, 0, RCX, 0, r);
    b1(a, 0x0F);
    b1(a, 0xB0);
    modrm_mem(a, RCX, r, offsetof(PyObject, ob_mutex));     /* lock cmpxchg [r+mutex], cl */
    jcc(a, CC_E, unlocked);
    rex(a, 1, RDI, 0, r);
    b1(a, 0x8D);
    modrm_mem(a, RDI, r, offsetof(PyObject, ob_mutex));     /* lea rdi, [r+mutex] */
    call_abs(a, (void *)PyMutex_Unlock);
    bind(a, unlocked);
}

/* An inline store's word at off of the object in r holds nothing: the
   store takes it in line only if every later word of the class's room
   (imm2 - 1 of them; imm2 0: unknown) is empty too, so that the words keep
   the instance's insertion order (attrs.c's in_order), and the instance
   keeps no order record, which C appends to; else `slow`. */
static void
fresh_word(struct asm_ *a, struct lu_ins *in, int r, int slow)
{
    if (in->imm2 == 0) {
        jmp(a, slow);
        return;
    }
    rex(a, 0, 0, 0, r);
    b1(a, 0xF6);
    modrm_mem(a, 0, r, offsetof(PyObject, ob_flags) + 1);
    b1(a, LU_ATTRS_UNORDERED >> 8);             /* test byte [r+flags+1], LU_ATTRS_UNORDERED */
    jcc(a, CC_NE, slow);
    for (int k = 1; k < in->imm2; k++) {
        cmp_mi(a, 1, r, (int32_t)in->imm + (int32_t)(LU_INLINE_STRIDE * sizeof(PyObject *)) * k, 0);
        jcc(a, CC_NE, slow);
    }
}

/* INLINE_STORE_F and _I: lu_inline_store_f (_i) in line. Under the
   owner's lock byte, taken with one cmpxchg, a word that already holds a
   value takes the raw value and its marker; the old value, an object, is
   released after the unlock. Anything else is C's, and leaves when it
   declines. */
static void
inline_store_raw(struct asm_ *a, lu_v v)
{
    struct lu_ins *in = I(a, v);
    int is_int = in->op == OP_INLINE_STORE_I;
    struct move m[2] = { { RDI, in->a, 0, 0 }, is_int ? (struct move){ RSI, in->b, 0, 0 }
                                                      : (struct move){ XMM(0), in->b, 0, 1 } };
    int fail = guard_exit(a, in);
    int slow = new_label(a), unlock_slow = new_label(a), done = new_label(a);
    int32_t flags = offsetof(PyObject, ob_flags) + 1;
    int32_t off = (int32_t)in->imm;
    struct lu_deopt *d = lu_publishes(in) ? published(a, in) : NULL;
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
    int fresh = 0;     /* an instance no other thread can have seen takes no lock (S4) */
    if (!fresh) {
        lock_object(a, RDI, slow);
        b1(a, 0xF6);
        modrm_mem(a, 0, RDI, flags);
        b1(a, LU_ATTRS_IN_DICT >> 8);
        jcc(a, CC_NE, unlock_slow);             /* moved to the dict before the lock */
    }
    load(a, RBX, RDI, off);                     /* the old value, kept across calls */
    int have = new_label(a);
    test_rr(a, RBX, RBX);
    jcc(a, CC_NE, have);
    fresh_word(a, in, RDI, unlock_slow);        /* a name new to the instance */
    bind(a, have);
    store(a, RDI, off + 8, R13);                /* the raw value */
    mov_ri(a, RAX, (int64_t)(intptr_t)(is_int ? LU_UNBOXED_INT : LU_UNBOXED));
    store(a, RDI, off, RAX);                    /* after the raw word: x86 stores stay in order */
    if (!fresh) {
        unlock_object(a, RDI);
    }
    alu_ri(a, ALU_CMP, RBX, (int32_t)(intptr_t)LU_UNBOXED_INT);
    jcc(a, CC_BE, done);                        /* no object to release */
    decref(a, RBX, in, d, -1);     /* the old value */
    jmp(a, done);
    bind(a, unlock_slow);
    if (!fresh) {
        unlock_object(a, RDI);
    }
    bind(a, slow);
    mov_rr(a, RDI, R12);
    mov_ri(a, RSI, in->imm);
    if (d != NULL) {
        publish(a, in, d);
    }
    if (is_int) {
        mov_rr(a, RDX, R13);
        call_abs(a, (void *)lu_inline_store_i);
    }
    else {
        movq_xr(a, 0, R13);
        call_abs(a, (void *)lu_inline_store_f);
    }
    if (d != NULL) {
        unpublish(a, REP_NONE);
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
        int wide = new_label(a), done = new_label(a);
        alu_ri(a, ALU_CMP, RCX, 2 << _PyLong_NON_SIZE_BITS);
        jcc(a, CC_AE, wide);
        load32(a, RAX, r, offsetof(PyLongObject, long_value.ob_digit));
        rex(a, 0, 0, 0, RCX);
        b1(a, 0xF6);
        modrm_rr(a, 0, RCX);
        b1(a, 2);                               /* test cl, 2: negative */
        jcc(a, CC_E, done);
        unary(a, 3, RAX);
        jmp(a, done);
        bind(a, wide);                          /* more digits: in C, leaving past 64 bits */
        save_volatile(a);
        mov_rr(a, RDI, r);
        call_abs(a, (void *)lu_jit_unbox_wide);
        restore_volatile(a);
        mov_ri(a, RCX, INT64_MIN);
        alu_rr(a, ALU_CMP, RAX, RCX);
        jcc(a, CC_E, fail);
        bind(a, done);
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
        if (in->b != LU_NOV) {
            /* The object the value came in as, where it did: with a new reference. */
            int made = new_label(a), done = new_label(a);
            int bound = new_label(a);
            r = gpr_of(a, in->b, RDX);
            test_rr(a, r, r);
            jcc(a, CC_E, made);
            mov_ri(a, RCX, (int64_t)(intptr_t)&lu_jit_unbound);
            alu_rr(a, ALU_CMP, r, RCX);
            jcc(a, CC_NE, bound);
            alu_rr(a, ALU_XOR, RAX, RAX);       /* the local is unbound: no object */
            def_gpr(a, v, RAX);
            jmp(a, done);
            bind(a, bound);
            incref(a, r);
            mov_rr(a, RAX, r);
            def_gpr(a, v, RAX);
            jmp(a, done);
            bind(a, made);
            emit_box(a, in);
            def_gpr(a, v, RAX);
            bind(a, done);
            return 0;
        }
        emit_box(a, in);
        def_gpr(a, v, RAX);
        return 0;
    case OP_ADD_I: case OP_SUB_I: case OP_MUL_I: case OP_AND_I: case OP_OR_I: case OP_XOR_I: {
        /* The exit first: under stress it emits a dec, which would clobber OF. */
        int fail = in->op == OP_ADD_I || in->op == OP_SUB_I || in->op == OP_MUL_I ? guard_exit(a, in) : -1;
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
            jcc(a, CC_O, fail);
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
    case OP_POW_F: {
        int fail = guard_exit(a, in);
        to_fpr(a, 0, in->a);
        y = fpr_of(a, in->b, 1);
        if (y != 1) {
            sse_rr(a, 0xF2, 0x10, 1, y, 0);     /* movsd xmm1, y */
        }
        call_abs(a, (void *)lu_jit_float_pow);
        movq_rx(a, RAX, 0);
        mov_ri(a, R11, (int64_t)LU_JIT_POW_FAILED);
        alu_rr(a, ALU_CMP, RAX, R11);
        jcc(a, CC_E, fail);
        def_fpr(a, v, 0);
        return 0;
    }
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
        int other = new_label(a);
        mov_ri(a, RAX, (int64_t)(intptr_t)&PyLong_Type);
        cmp_rm(a, RAX, r, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, other);
        load(a, RCX, r, offsetof(PyLongObject, long_value.lv_tag));
        alu_ri(a, ALU_AND, RCX, _PyLong_SIGN_MASK);
        alu_ri(a, ALU_CMP, RCX, 1);             /* the sign bits of zero */
        jcc(a, CC_E, fz);
        jmp(a, t);
        bind(a, other);
        /* imm 1: only a layout's own truth, which runs no Python; any
           other leaves. */
        int err = in->imm == 1 ? exit_to(a, in->fs, EXIT_GUARD, 0, 0)
                               : exit_to(a, in->fs, EXIT_ERROR, 1, in->drop);
        if (in->imm != 1) {
            publish(a, in, a->exits[a->nexits - 1].d);
        }
        mov_rr(a, RDI, r);
        call_abs(a, in->imm == 1 ? (void *)lu_truth_exact : (void *)lu_jit_truth);
        if (in->imm != 1) {
            unpublish(a, REP_NONE);
        }
        b1(a, 0x83);
        b1(a, 0xF8);
        b1(a, 0xFF);                            /* cmp eax, -1 */
        jcc(a, CC_E, err);
        jmp(a, done);
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
    case OP_GUARD_DEPTH: {
        /* Not a speculation: near the limit the interpreter runs the entry. */
        int fail = exit_to(a, in->fs, EXIT_POLL, 0, 0);
        cmp_mi(a, 0, R14, offsetof(PyThreadState, py_recursion_remaining), (int32_t)in->imm);
        jcc(a, CC_L, fail);
        return 0;
    }
    case OP_GUARD_FRAMES: {
        /* Not a speculation: the root is still compiled code's unless a
           walker made the frames real (runtime/jit/exit.c). */
        int seen = exit_to(a, in->fs, EXIT_FRAME, 0, 0);
        cmp8_mi(a, R15, offsetof(struct lu_frame, owner), LU_FRAME_JIT);
        jcc(a, CC_NE, seen);
        if (f->ndeps > 0 && !(in->imm & 1)) {
            /* Nor did the code the instruction ran take back a fact the
               unit relies on (runtime/jit/facts.c): the same way out. A
               frame that returns relies on nothing more. */
            mov_ri(a, R11, (int64_t)(intptr_t)&a->unit->dead);
            cmp_mi(a, 0, R11, 0, 0);
            jcc(a, CC_NE, seen);
        }
        if (in->imm & 1) {
            /* A frame object takes the locals only if another holds it too
               (lu_frame_retire): one the record alone holds is dropped. */
            int ok = new_label(a);
            load(a, RAX, R15, offsetof(struct lu_frame, frame_obj));
            test_rr(a, RAX, RAX);
            jcc(a, CC_E, ok);
            cmp_mi(a, 0, RAX, offsetof(PyObject, ob_ref_local), 1);
            jcc(a, CC_NE, seen);
            cmp_mi(a, 1, RAX, offsetof(PyObject, ob_ref_shared), 0);
            jcc(a, CC_NE, seen);
            b1(a, 0x64);
            b1(a, 0x48);
            b1(a, 0x8B);
            b1(a, 0x0C);
            b1(a, 0x25);
            b4(a, 0);                           /* mov rcx, fs:[0] */
            cmp_rm(a, RCX, RAX, offsetof(PyObject, ob_tid));
            jcc(a, CC_NE, seen);
            bind(a, ok);
        }
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
    case OP_STORE:
        r = gpr_of(a, in->a, RAX);
        y = gpr_of(a, in->b, RCX);
        store(a, r, (int32_t)in->imm, y);
        return 0;
    case OP_LOAD:
        r = gpr_of(a, in->a, RAX);
        if (in->imm2 == 4) {
            load32(a, RAX, r, (int32_t)in->imm);
        }
        else {
            load(a, RAX, r, (int32_t)in->imm);
        }
        def_gpr(a, v, RAX);
        return 0;
    case OP_LIST_ITEM: {
        /* BINARY_OP_SUBSCR_LIST_INT and _TUPLE_INT in line: an index below
           the size, the item read without a lock (a list's below its
           array's own capacity too, as lu_list_try_item_ref reads it), and
           the reference taken in place for an immortal item or one the
           running thread owns; a tuple's other items count in the shared
           count. Anything else (a negative index, a list resized under the
           read, another thread's list item) is C's, which keeps the
           registers: a list's may wait for its lock. */
        struct move m[2] = { { RDI, in->a, 0, 0 }, { RSI, in->b, 0, 0 } };
        int fail = guard_exit(a, in), slow = new_label(a), done = new_label(a);
        int s = gpr_of(a, in->a, RDX), i = gpr_of(a, in->b, RCX);
        cmp_rm(a, i, s, offsetof(PyVarObject, ob_size));
        jcc(a, CC_AE, slow);                    /* unsigned: a negative index too */
        if (in->imm) {
            int shared = new_label(a);
            load_indexed8(a, RAX, s, i, offsetof(PyTupleObject, ob_item));
            incref_owned_or_exit(a, RAX, shared);
            jmp(a, done);
            bind(a, shared);
            b1(a, 0xF0);
            rex(a, 1, 0, 0, RAX);
            b1(a, 0x83);
            modrm_mem(a, ALU_ADD, RAX, offsetof(PyObject, ob_ref_shared));
            b1(a, 1 << _Py_REF_SHARED_SHIFT);   /* lock add qword [rax+shared], 1 */
            jmp(a, done);
        }
        else {
            load(a, RAX, s, offsetof(PyListObject, ob_item));
            test_rr(a, RAX, RAX);
            jcc(a, CC_E, slow);
            cmp_rm(a, i, RAX, (int32_t)offsetof(struct lu_list_array, allocated) - (int32_t)offsetof(struct lu_list_array, ob_item));
            jcc(a, CC_AE, slow);
            load_indexed8(a, RAX, RAX, i, 0);
            test_rr(a, RAX, RAX);
            jcc(a, CC_E, slow);
            incref_owned_or_exit(a, RAX, slow);
            jmp(a, done);
        }
        bind(a, slow);
        if (in->imm) {
            save_volatile(a);
            parallel_moves(a, m, 2);
            call_abs(a, (void *)lu_jit_tuple_item);
            restore_volatile(a);
        }
        else {
            parked_call(a, v, (void *)lu_jit_list_item, m, 2);
        }
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_STR_ORD: {
        /* PyUnicode_READ of a compact str (any other leaves): the data
           follows a PyASCIIObject when ascii, else a PyCompactUnicodeObject. */
        static const struct { int kind, ascii; int32_t disp; } kinds[] = {
            { 1, 1, sizeof(PyASCIIObject) }, { 1, 0, sizeof(PyCompactUnicodeObject) },
            { 2, 0, sizeof(PyCompactUnicodeObject) }, { 4, 0, sizeof(PyCompactUnicodeObject) },
        };
        int fail = guard_exit(a, in), done = new_label(a);
        int s = gpr_of(a, in->a, RDX), i = gpr_of(a, in->b, RCX);
        cmp_rm(a, i, s, offsetof(PyASCIIObject, length));
        jcc(a, CC_AE, fail);                    /* unsigned: a negative index too */
        load32(a, RAX, s, offsetof(PyASCIIObject, state));
        alu_ri(a, ALU_AND, RAX, str_state(7, 1));
        for (int k = 0; k < 4; k++) {
            int next = new_label(a);
            alu_ri(a, ALU_CMP, RAX, str_state(kinds[k].kind, kinds[k].ascii));
            jcc(a, CC_NE, k < 3 ? next : fail);
            load_indexed(a, RAX, s, i, kinds[k].kind, kinds[k].disp);
            jmp(a, done);
            bind(a, next);
        }
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_BYTES_ITEM: {
        int fail = guard_exit(a, in);
        int b = gpr_of(a, in->a, RDX), i = gpr_of(a, in->b, RCX);
        cmp_rm(a, i, b, offsetof(PyVarObject, ob_size));
        jcc(a, CC_AE, fail);                    /* unsigned: a negative index too */
        load_indexed(a, RAX, b, i, 1, offsetof(PyBytesObject, ob_sval));
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_LIST_STORE: {
        /* STORE_SUBSCR_LIST_INT in line, as CPython locks the list: its
           lock byte taken with one cmpxchg, an index below the size, the
           value's reference taken, and the old value released after the
           unlock. A negative or too large index leaves; a contended lock
           is lu_jit_list_store's. */
        struct move m[3] = { { RDI, in->a, 0, 0 }, { RSI, in->b, 0, 0 }, { RDX, in->c, 0, 0 } };
        int fail = guard_exit(a, in), slow = new_label(a), out = new_label(a), done = new_label(a);
        struct lu_deopt *d = published(a, in);
        parallel_moves(a, m, 3);
        lock_object(a, RDI, slow);
        cmp_rm(a, RSI, RDI, offsetof(PyVarObject, ob_size));
        jcc(a, CC_AE, out);                     /* unsigned: a negative index too */
        load(a, R11, RDI, offsetof(PyListObject, ob_item));
        load_indexed8(a, RBX, R11, RSI, 0);     /* the old value, kept across calls (IF_CALL) */
        incref(a, RDX);
        store_indexed8(a, R11, RSI, 0, RDX);    /* a release on x86: readers take it without the lock */
        unlock_object(a, RDI);
        decref(a, RBX, in, d, -1);     /* the old value */
        jmp(a, done);
        bind(a, out);
        unlock_object(a, RDI);
        jmp(a, fail);
        bind(a, slow);
        publish(a, in, d);
        call_abs(a, (void *)lu_jit_list_store);
        unpublish(a, REP_NONE);
        b1(a, 0x85);
        b1(a, 0xC0);                            /* test eax, eax */
        jcc(a, CC_E, fail);
            bind(a, done);
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
        int done = new_label(a);
        int32_t flags = offsetof(PyObject, ob_flags) + 1;
        int32_t off = (int32_t)in->imm;
        struct lu_deopt *d = lu_publishes(in) ? published(a, in) : NULL;
        parallel_moves(a, m, 2);
        mov_rr(a, R12, RDI);                    /* kept across calls: nothing is live here (IF_CALL) */
        mov_rr(a, R13, RSI);
        b1(a, 0xF6);
        modrm_mem(a, 0, RDI, flags);
        b1(a, LU_ATTRS_IN_DICT >> 8);           /* test byte [rdi+flags+1], LU_ATTRS_IN_DICT */
        jcc(a, CC_NE, slow);
        int fresh = 0;     /* an instance no other thread can have seen takes no lock (S4) */
        if (!fresh) {
            lock_object(a, RDI, slow);
            b1(a, 0xF6);
            modrm_mem(a, 0, RDI, flags);
            b1(a, LU_ATTRS_IN_DICT >> 8);
            jcc(a, CC_NE, unlock_slow);         /* moved to the dict before the lock */
        }
        load(a, RBX, RDI, off);                 /* the old value, kept across calls */
        int have = new_label(a), take = new_label(a);
        test_rr(a, RBX, RBX);
        jcc(a, CC_NE, have);
        fresh_word(a, in, RDI, unlock_slow);    /* a name new to the instance */
        jmp(a, take);
        bind(a, have);
        alu_ri(a, ALU_CMP, RBX, (int32_t)(intptr_t)LU_UNBOXED_INT);
        jcc(a, CC_A, take);
        alu_rr(a, ALU_XOR, RBX, RBX);           /* an unboxed value: no object to release */
        bind(a, take);
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
        if (!fresh) {
            unlock_object(a, RDI);
        }
        test_rr(a, RBX, RBX);
        jcc(a, CC_E, done);                     /* a new name: nothing to release */
        decref(a, RBX, in, d, -1);     /* the old value */
        jmp(a, done);
        bind(a, unlock_slow);
        if (!fresh) {
            unlock_object(a, RDI);
        }
        mov_rr(a, RDI, R12);
        mov_rr(a, RSI, R13);
        bind(a, slow);
        mov_ri(a, RDX, in->imm);
        b1(a, 0x48);
        b1(a, 0x87);
        b1(a, 0xD6);                            /* xchg rsi, rdx: (owner, offset, value) */
        if (d != NULL) {
            publish(a, in, d);
        }
        call_abs(a, (void *)lu_inline_store);
        if (d != NULL) {
            unpublish(a, REP_NONE);
        }
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
        /* An unboxed value: its box, made once in C and kept by the owner;
           making it may wait for the owner's lock. */
        parked_begin(a, in);
        if (r != RDI) {
            mov_rr(a, RDI, r);
        }
        rex(a, 1, RSI, 0, RDI);
        b1(a, 0x8D);
        modrm_mem(a, RSI, RDI, (int32_t)in->imm);      /* lea rsi, [rdi+imm]: the word */
        call_abs(a, (void *)lu_inline_get);
        parked_end(a, in);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_INLINE_LOAD_F: case OP_INLINE_LOAD_I:
        inline_load_raw(a, in, v);
        return 0;
    case OP_INLINE_ORIGIN: {
        /* As INLINE_LOAD, but for an unboxed value, which is no object. */
        int fail = guard_exit(a, in);
        int have = new_label(a), done = new_label(a);
        r = gpr_of(a, in->a, RDX);
        rex(a, 0, 0, 0, r);
        b1(a, 0xF6);
        modrm_mem(a, 0, r, offsetof(PyObject, ob_flags) + 1);
        b1(a, LU_ATTRS_IN_DICT >> 8);           /* test byte [r+flags+1], LU_ATTRS_IN_DICT */
        jcc(a, CC_NE, fail);
        load(a, RAX, r, (int32_t)in->imm);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
        jcc(a, CC_A, have);
        alu_rr(a, ALU_XOR, RAX, RAX);
        jmp(a, done);
        bind(a, have);
        incref_owned_or_exit(a, RAX, fail);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_INLINE_STORE_F: case OP_INLINE_STORE_I:
        inline_store_raw(a, v);
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
    case OP_DECREF:
        decref(a, gpr_of(a, in->a, RDX), in, lu_publishes(in) ? published(a, in) : NULL, -1);
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
        int err = -1;
        if ((h->fx & FX_RAISE) && in->fs != NULL && (h->rep == REP_BIT || h->rep == REP_OBJ)) {
            err = exit_to(a, in->fs, EXIT_ERROR, 1, in->drop);
        }
        struct lu_deopt *pub = NULL;
        if (lu_publishes(in)) {
            pub = err >= 0 ? a->exits[a->nexits - 1].d : published(a, in);
        }
        int inline_ok = in->imm == H_build_tuple || in->imm == H_build_list ? emit_build(a, v)
            : in->imm == H_new_object ? emit_new_object(a, v) : 0;
        if (!inline_ok) {
            call_helper(a, v, pub);
        }
        if (err >= 0 && h->rep == REP_BIT) {
            b1(a, 0x83);
            b1(a, 0xF8);
            b1(a, 0xFF);                        /* cmp eax, -1 */
            jcc(a, CC_E, err);
        }
        else if (err >= 0 && h->rep == REP_OBJ) {
            test_rr(a, RAX, RAX);
            jcc(a, CC_E, err);
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
        /* The poll word is the monitoring version the code was compiled
           under and no event: three instructions. Else the stub below
           merges refcounts other threads queued and processes retired
           storage (LU_POLL_EXPLICIT_MERGE), and runs a scheduled collection
           outside a builtin, with the frames published, as a release's
           destructor would see them; it reports a moved write
           sequence (LU_POLL_QSBR) in place; every other event, or tools
           turned on, leaves. A builtin's frame leaves the visible reasons
           for the poll after it returns too: leaving for one would only
           come back. */
        int poll = exit_to(a, in->fs, EXIT_POLL, 0, 0), done = new_label(a), top = new_label(a);
        int merge = new_label(a);
        int64_t version = (int64_t)a->f->code->_co_instrumentation_version;
        uintptr_t stays = LU_POLL_QSBR | (in->fs->code->co_flags & LU_CO_KERNEL ? LU_POLL_VISIBLE : 0);
        bind(a, top);
        load(a, RAX, R14, offsetof(struct lu_task, carrier));
        if (version <= INT32_MAX) {
            cmp_mi(a, 1, RAX, offsetof(struct lu_carrier, poll), (int32_t)version);
        }
        else {
            mov_ri(a, R11, version);
            cmp_rm(a, R11, RAX, offsetof(struct lu_carrier, poll));
        }
        jcc(a, CC_E, done);
        load(a, RCX, RAX, offsetof(struct lu_carrier, poll));
        alu_ri(a, ALU_AND, RCX, (int32_t)~stays);
        mov_ri(a, R11, version);
        alu_rr(a, ALU_CMP, RCX, R11);
        jcc(a, CC_NE, merge);
        rex(a, 1, 0, 0, RAX);
        b1(a, 0xF7);
        modrm_mem(a, 0, RAX, offsetof(struct lu_carrier, poll));
        b4(a, LU_POLL_QSBR);                    /* test qword [rax+poll], LU_POLL_QSBR */
        jcc(a, CC_E, done);                     /* only a builtin's visible reasons */
        b1(a, 0xF0);
        rex(a, 1, 0, 0, RAX);
        b1(a, 0x81);
        modrm_mem(a, ALU_AND, RAX, offsetof(struct lu_carrier, poll));
        b4(a, (int32_t)~LU_POLL_QSBR);          /* lock and: the bit goes before the report */
        quiescent(a);
        jmp(a, done);
        /* Another event: a merge and a collection are made here, and the
           word read again. */
        bind(a, merge);
        alu_ri(a, ALU_AND, RCX, (int32_t)~(LU_POLL_EXPLICIT_MERGE | LU_POLL_GC_SCHEDULED));
        alu_rr(a, ALU_CMP, RCX, R11);
        jcc(a, CC_NE, poll);
        parked_call(a, v, (void *)lu_poll_in_place, NULL, 0);
        jmp(a, top);
        bind(a, done);
        if (_PyRuntime.handoff.on) {
            /* The handoff scheduler migrates tasks at the interpreter's polls. */
            mov_ri(a, RCX, (int64_t)(intptr_t)&_PyRuntime.sched.active);
            cmp_mi(a, 0, RCX, 0, 0);
            jcc(a, CC_NE, poll);
        }
        return 0;
    }
    case OP_CHECK_LIVE: {
        /* A fact relied on since the last check may have been taken back
           (runtime/jit/deps.c): a dead unit leaves, as at a poll. */
        stress_exit(a, in);
        int dead = exit_to(a, in->fs, EXIT_POLL, 0, 0);
        mov_ri(a, R11, (int64_t)(intptr_t)&a->unit->dead);
        cmp_mi(a, 0, R11, 0, 0);
        jcc(a, CC_NE, dead);
        return 0;
    }
    case OP_DEOPT:
        jmp(a, exit_to(a, in->fs, (int)in->imm, 0, 0));
        return 0;
    case OP_UNLINK: {
        /* A generator's frame stays the current one until lu_gen_finish. */
        int stays = new_label(a);
        cmp8_mi(a, R15, offsetof(struct lu_frame, home), LU_FRAME_GENERATOR);
        jcc(a, CC_E, stays);
        load(a, R11, R15, offsetof(struct lu_frame, previous));
        store(a, R14, offsetof(PyThreadState, current_frame), R11);
        bind(a, stays);
        return 0;
    }
    case OP_RETURN:
        to_gpr(a, RCX, in->a);
        load(a, RAX, RBP, RESULT_SLOT);
        store(a, RAX, 0, RCX);
        op_rm(a, 0, 0x0FB6, RAX, R15, offsetof(struct lu_frame, home));
        op_rm(a, 0, 0x88, RAX, R15, offsetof(struct lu_frame, owner));      /* its owner's again: lu_gen_finish's, a generator's */
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
        moves[nm++] = (struct move){ phi->loc, src, 0, phi->rep == REP_F64 };
    }
    parallel_moves(a, moves, nm);
    free(moves);
}

static void
saves(struct asm_ *a)
{
    b1(a, 0x55);                                /* push rbp */
    mov_rr(a, RBP, RSP);
    b1(a, 0x53);                                /* push rbx */
    b1(a, 0x41); b1(a, 0x54);                   /* push r12 */
    b1(a, 0x41); b1(a, 0x55);                   /* push r13 */
    b1(a, 0x41); b1(a, 0x56);                   /* push r14 */
    b1(a, 0x41); b1(a, 0x57);                   /* push r15 */
    alu_ri(a, ALU_SUB, RSP, a->frame_bytes);
}

static void
prologue(struct asm_ *a)
{
    saves(a);
    mov_rr(a, R14, RDI);
    mov_rr(a, R15, RSI);
    store(a, RBP, RESULT_SLOT, RDX);
    /* Compiled code owns the frame's locals and stack from here: the
       slots hold nothing until an exit or a walker fills them. */
    rex(a, 1, RAX, 0, R15);
    b1(a, 0x8D);
    modrm_mem(a, RAX, R15, offsetof(struct lu_frame, localsplus));
    store(a, R15, offsetof(struct lu_frame, stackpointer), RAX);
    op_rm(a, 0, 0x0FB6, RAX, R15, offsetof(struct lu_frame, owner));    /* movzx eax, byte [r15+owner] */
    op_rm(a, 0, 0x88, RAX, R15, offsetof(struct lu_frame, home));
    store8_mi(a, R15, offsetof(struct lu_frame, owner), LU_FRAME_JIT);
    rex(a, 1, RAX, 0, RBP);
    b1(a, 0x8D);
    modrm_mem(a, RAX, RBP, SPILL_BASE);
    store(a, R15, offsetof(struct lu_frame, jit_spills), RAX);
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
    mov_ri(a, RDX, 0x1122334455667788LL);       /* the exits, patched (pack) */
    rex(a, 1, RAX, 0, RAX);
    b1(a, 0x69);
    modrm_rr(a, RAX, RAX);
    b4(a, (int32_t)sizeof(struct lu_deopt));   /* imul rax, rax, size */
    alu_rr(a, ALU_ADD, RDX, RAX);               /* rdx = &exits[rax] */
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
    a->save_all = new_label(a);
    a->restore_all = new_label(a);
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
    for (int v = 0; v < f->nins; v++) {
        int32_t loc = f->ins[v].loc;
        int saved = 0;
        for (int i = 0; i < lu_target.nsaved; i++) {
            saved |= lu_target.saved[i] == loc;
        }
        if (!(f->ins[v].flags & INS_DEAD) && loc >= 0 && loc < 32 && !saved && loc != LU_LOC_NONE) {
            a->volatiles |= 1u << loc;
        }
    }
    prologue(a);
    int *labels = malloc(sizeof(int) * (size_t)f->nblocks);
    for (int b = 0; b < f->nblocks; b++) {
        labels[b] = new_label(a);
    }
    /* A block runs all its code each time it runs. One larger than the
       core's L2 cache streams from farther out every time, slower than
       the interpreter, whose own code stays near: such code is not
       compiled (measured on pyperformance's unpack_sequence, 400
       unpackings in one block of a loop: 1 MB, twice the interpreter's
       time per unpacking). */
    long l2 = sysconf(_SC_LEVEL2_CACHE_SIZE);
    for (int k = 0; k < f->norder; k++) {
        int b = f->order[k];
        struct lu_block *bl = &f->blocks[b];
        bind(a, labels[b]);
        int next = k + 1 < f->norder ? f->order[k + 1] : -1;
        int start = a->n;
        for (int i = 0; i < bl->nins; i++) {
            int k = unit->counts != NULL ? lu_count_kind(I(a, bl->ins[i])) : -1;
            if (k >= 0) {
                mov_ri(a, R11, (int64_t)(intptr_t)&unit->counts[k]);
                rex(a, 1, 0, 0, R11);
                b1(a, 0xFF);
                modrm_mem(a, 0, R11, 0);        /* inc qword [r11] */
            }
            alloc_count_before(a, bl->ins[i]);
            if (_PyRuntime.jit.dump) {
                fprintf(stderr, "  code +%d v%d\n", a->n, bl->ins[i]);    /* with the unit's address below: a profile by instruction */
            }
            if (emit_ins(a, bl->ins[i], next, labels) < 0) {
                free(labels);
                build_free(a);
                return -1;
            }
        }
        if (l2 > 0 && a->n - start > l2) {
            f->fail = "a block larger than the L2 cache";
            free(labels);
            build_free(a);
            return -1;
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
    if (a->parks) {
        parked_stubs(a);
    }
    exits(a);
    /* Labels. */
    for (int i = 0; i < a->npatches; i++) {
        int target = a->labels[a->patches[i].label];
        int32_t rel = target - (a->patches[i].at + 4);
        memcpy(a->buf + a->patches[i].at, &rel, 4);
    }
    pack(a, unit);
    /* Publication: written through one view, run through the other. */
    void *rw, *mem = lu_code_alloc((size_t)a->n, &rw);
    if (mem == NULL) {
        f->fail = "no memory for code";
        free(a->buf);
        build_free(a);
        return -1;
    }
    memcpy(rw, a->buf, (size_t)a->n);
    unit->mem = mem;
    unit->size = (size_t)a->n;
    unit->code_bytes = (size_t)a->n;
    if (_PyRuntime.jit.dump) {
        fprintf(stderr, "jit: code at %p, %d bytes\n", (void *)mem, a->n);
    }
    unit->fn = (int (*)(PyThreadState *, struct lu_frame *, PyObject **))mem;
    free(a->buf);
    free(a->labels);
    free(a->patches);
    free(a->exits);
    build_free(a);
    return 0;
}

void
lu_target_free(struct lu_unit *unit)
{
    lu_code_free(unit->mem, unit->size);
}
