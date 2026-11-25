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
#include "interp/gen.h"
#include "interp/instrument.h"

#include <cpuid.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
#define XMM(n) (LU_LOC_FPR + (n))

static const int8_t gprs[] = { RBX, R12, R13, RSI, RDI, R8, R9, R10 };
static const int8_t saved_gprs[] = { RBX, R12, R13 };
static int clobbers_saved(const struct lu_ins *in);
struct asm_;
static void frame_end(struct asm_ *a, int r);
static int result_over_a(const struct lu_ins *in);
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
    .result_over_a = result_over_a,
    .trampoline = trampoline, .ntrampoline = sizeof trampoline, .elf_machine = 62,
    .cie = cie, .ncie = sizeof cie, .fp_cfi = fp_cfi, .nfp_cfi = sizeof fp_cfi,
};

#define SAVES 40                /* rbx, r12-r15 below rbp */
#define RESULT_SLOT (-(SAVES + 8))
#define ACT_BYTES 24                    /* sizeof (struct lu_jit_act) */
#define ACT_SLOT (RESULT_SLOT - ACT_BYTES)      /* the unit's activation (target.h struct lu_jit_act) */
#define ACT_AT(field) (ACT_SLOT + (int32_t)offsetof(struct lu_jit_act, field))
#define SPILL_BASE (ACT_SLOT - 8)
static_assert(sizeof(struct lu_jit_act) == ACT_BYTES, "the activation outgrew its slot");

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
    uint32_t *seen;             /* per location (lu_pv_arg_of_loc): the exit that last owned it */
    uint32_t nseen;
    lu_pv *placed;              /* per value: 1 + its place once found (place) */
    int32_t *owns;              /* per value: 1 + the location an exit releases it at, -1 for none, once found */
};

/* The references the frames' slots are owed for an object a published
   call is handed: the value, and how many beyond compiled code's own. */
struct hold {
    lu_v v;
    int n;
};

/* Code is written to one of two streams: the hot one, in emission order,
   and the cold one, which the unit's code ends with: slow paths, so that
   the fast path falls through instead of jumping over them. A position in
   the cold stream is COLD plus its offset there until the streams join. */
#define COLD (1 << 30)

struct stream {
    uint8_t *buf;
    int n, cap;
};

struct asm_ {
    uint8_t *buf;
    int n, cap;
    struct stream other;        /* the stream not being written */
    int cold;                   /* cold_begin depth */
    int fuse_at, fuse_end;      /* the last flag-setting instruction a jcc may fuse with (here positions) */
    int last_bind;              /* where the last label was bound (here position) */
    int skips[16];              /* per depth past the first: the label after a nested slow path (emitters
                                   nest a few deep) */
    int *labels;
    int nlabels, caplabels;
    struct patch *patches;
    int npatches, cappatches;
    struct exit_site *exits;
    int nexits, capexits;
    int deopts_patch;           /* the imm64 that holds the exits' table */
    int epilogue;               /* the activation's end, then the return */
    int restore;                /* the return alone */
    int decline;                /* an entry's decline, before its link */
    int end;                    /* a return whose frame C ends (lu_jit_left) */
    int entered;                /* past the activation's link, where the direct entry joins the prologue */
    struct lu_func *f;
    struct lu_unit *unit;
    int serial;                 /* it stores without locks while serial: keeps the task's unlocked_jit */
    int frame_bytes;
    int save_all, restore_all;  /* the shared stubs of parked_call, emitted when used */
    int release;                /* 1 + the label of the release stub (release_in_line), or 0 */
    int parks;
    int parking;                /* the publish in hand is a park's: no code of this thread runs to count holds */
    int saved;                  /* the published call in hand saved every register at jit_regs (kept_begin) */
    lu_v next;                  /* the instruction after the one in hand in its block, or LU_NOV */
    lu_v flags_of;              /* the compare whose condition the flags hold, flags_cc, or LU_NOV */
    int flags_cc;
    int *uses;                  /* per value: how many instructions and states read it */
    uint32_t volatiles;         /* the caller-saved registers the unit allocates, by location */
    struct hold *held;          /* what the published call in hand holds of what it is handed */
    int nheld;
    uint32_t *walked, walk;     /* per virtual object, the holds_of walk that went through it last */
    uint32_t *handed;           /* per value, the holds_of walk whose call is handed it */
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

/* The instructions whose result may take operand a's register (target.h):
   an overflow undoes the operation before the exit reads a. */
static int
result_over_a(const struct lu_ins *in)
{
    switch (in->op) {
    case OP_ADD_I: case OP_SUB_I: case OP_AND_I: case OP_OR_I: case OP_XOR_I:
    case OP_ADD_F: case OP_SUB_F: case OP_MUL_F: case OP_DIV_F:
        return in->a != in->b;
    case OP_LOAD:
        return 1;
    default:
        return 0;
    }
}

/* --- the buffer --- */

static void
grow_buf(struct asm_ *a, int n)
{
    a->cap = a->cap ? a->cap * 2 : 4096;
    while (a->n + n > a->cap) {
        a->cap *= 2;
    }
    a->buf = realloc(a->buf, (size_t)a->cap);
}

/* In line: a call to store a byte cost more than the store. */
static inline Py_ALWAYS_INLINE void
put(struct asm_ *a, const void *p, int n)
{
    if (a->n + n > a->cap) {
        grow_buf(a, n);
    }
    memcpy(a->buf + a->n, p, (size_t)n);
    a->n += n;
}

static inline Py_ALWAYS_INLINE void
b1(struct asm_ *a, int x)
{
    uint8_t c = (uint8_t)x;
    put(a, &c, 1);
}

static inline Py_ALWAYS_INLINE void
b4(struct asm_ *a, int32_t x)
{
    put(a, &x, 4);
}

static void
b8(struct asm_ *a, int64_t x)
{
    put(a, &x, 8);
}

/* The position the next byte goes to. */
static int
here(struct asm_ *a)
{
    return a->n + (a->cold ? COLD : 0);
}

static void
swap_streams(struct asm_ *a)
{
    struct stream s = a->other;
    a->other = (struct stream){ a->buf, a->n, a->cap };
    a->buf = s.buf;
    a->n = s.n;
    a->cap = s.cap;
}

static void jmp(struct asm_ *a, int label);
static int new_label(struct asm_ *a);
static void bind(struct asm_ *a, int label);

/* Code from here to cold_end goes to the cold stream, after the code
   before it, which does not fall into it. A slow path inside a slow path
   is already cold: the code before it jumps over it. */
static void
cold_begin(struct asm_ *a)
{
    if (a->cold++ == 0) {
        swap_streams(a);
        return;
    }
    a->skips[a->cold - 2] = new_label(a);
    jmp(a, a->skips[a->cold - 2]);
}

static void
cold_end(struct asm_ *a)
{
    if (--a->cold == 0) {
        swap_streams(a);
        return;
    }
    bind(a, a->skips[a->cold - 1]);
}

/* The cold stream after the hot one: positions become offsets. */
static int
joined(struct asm_ *a, int pos)
{
    return pos >= COLD ? pos - COLD + a->n : pos;
}

static void
join_streams(struct asm_ *a)
{
    if (a->n + a->other.n > a->cap) {
        grow_buf(a, a->other.n);
    }
    memcpy(a->buf + a->n, a->other.buf, (size_t)a->other.n);
    if (_PyRuntime.jit.dump) {
        fprintf(stderr, "  cold +%d\n", a->n);     /* where the slow paths begin */
    }
    for (int i = 0; i < a->nlabels; i++) {
        a->labels[i] = joined(a, a->labels[i]);
    }
    for (int i = 0; i < a->npatches; i++) {
        a->patches[i].at = joined(a, a->patches[i].at);
    }
    for (uint32_t i = 0; i < a->npubs; i++) {
        a->pubs[i].at = joined(a, a->pubs[i].at);
    }
    a->deopts_patch = joined(a, a->deopts_patch);
    a->n += a->other.n;
    free(a->other.buf);
    a->other = (struct stream){ 0 };
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
    a->labels[label] = here(a);
    a->last_bind = here(a);
}

/* An instruction that sets the flags, which a jcc right after it may
   fuse with, began at `start`. */
static void
sets_flags(struct asm_ *a, int start)
{
    a->fuse_at = start;
    a->fuse_end = here(a);
}

/* Whether this is one of the cores whose JCC erratum microcode fix
   keeps no branch that crosses or ends at a 32-byte boundary in the
   decoded-uop cache: Intel family 6, Skylake through Comet Lake (the
   models Intel lists for the erratum). Asked once; every thread gets the
   same answer. */
static int
jcc_erratum(void)
{
    static int known;           /* 0: not asked; 1: no; 2: yes */
    if (__atomic_load_n(&known, __ATOMIC_RELAXED) == 0) {
        unsigned eax, ebx, ecx, edx, vendor[3];
        int yes = 0;
        if (__get_cpuid(0, &eax, &vendor[0], &vendor[2], &vendor[1]) && memcmp(vendor, "GenuineIntel", 12) == 0
            && __get_cpuid(1, &eax, &ebx, &ecx, &edx) && ((eax >> 8) & 0xF) == 6) {
            unsigned model = ((eax >> 4) & 0xF) | ((eax >> 12) & 0xF0);
            static const unsigned models[] = { 0x4E, 0x5E, 0x55, 0x8E, 0x9E, 0xA5, 0xA6 };
            for (size_t i = 0; i < sizeof models / sizeof models[0]; i++) {
                yes |= model == models[i];
            }
        }
        __atomic_store_n(&known, yes ? 2 : 1, __ATOMIC_RELAXED);
    }
    return __atomic_load_n(&known, __ATOMIC_RELAXED) == 2;
}

/* A branch of `len` bytes is about to be written in the hot stream. On
   those cores a loop of branches that cross or end at a 32-byte boundary
   runs from the legacy decoders: NOPs move such a branch, with the
   compare it fuses with, to the next window. Units start 64-byte aligned
   (codemem.c), so offsets say where windows are. */
static void
window_branch(struct asm_ *a, int len)
{
    static const uint8_t nops[][11] = {         /* the longest a decoder takes without a stall */
        { 0x90 }, { 0x66, 0x90 }, { 0x0F, 0x1F, 0x00 }, { 0x0F, 0x1F, 0x40, 0x00 },
        { 0x0F, 0x1F, 0x44, 0x00, 0x00 }, { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 },
        { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 }, { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
        { 0x66, 0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    };
    if (a->cold || !jcc_erratum()) {
        return;
    }
    int fused = a->fuse_end == a->n && a->last_bind <= a->fuse_at;
    int start = fused ? a->fuse_at : a->n, end = a->n + len;
    if (start / 32 == (end - 1) / 32 && end % 32 != 0) {
        return;
    }
    int pad = 32 - start % 32, moved = a->n - start;
    if (a->n + pad > a->cap) {
        grow_buf(a, pad);
    }
    memmove(a->buf + start + pad, a->buf + start, (size_t)moved);
    for (int at = start, left = pad; left > 0;) {
        int k = left < 11 ? left : 11;
        memcpy(a->buf + at, nops[k - 1], (size_t)k);
        at += k;
        left -= k;
    }
    a->n += pad;
    a->fuse_at = a->fuse_end = -1;
}

static void
rel32_to(struct asm_ *a, int label)
{
    if (a->npatches == a->cappatches) {
        a->cappatches = a->cappatches ? a->cappatches * 2 : 64;
        a->patches = realloc(a->patches, sizeof(struct patch) * (size_t)a->cappatches);
    }
    a->patches[a->npatches++] = (struct patch){ here(a), label };
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
    int at = here(a);
    op_rr(a, 1, 0x01 + alu * 8, src, dst);
    sets_flags(a, at);
}

static void
alu_ri(struct asm_ *a, int alu, int dst, int32_t imm)
{
    int at = here(a);
    rex(a, 1, 0, 0, dst);
    b1(a, 0x81);
    modrm_rr(a, alu, dst);
    b4(a, imm);
    sets_flags(a, at);
}

/* cmp r32, imm8 (sign-extended) */
static void
cmp32_ri8(struct asm_ *a, int r, int imm)
{
    int at = here(a);
    rex(a, 0, 0, 0, r);
    b1(a, 0x83);
    modrm_rr(a, ALU_CMP, r);
    b1(a, imm);
    sets_flags(a, at);
}

/* op qword/dword [base+disp], imm32 */
static void
alu_mi(struct asm_ *a, int w, int alu, int base, int32_t disp, int32_t imm)
{
    int at = here(a);
    rex(a, w, 0, 0, base);
    b1(a, 0x81);
    modrm_mem(a, alu, base, disp);
    b4(a, imm);
    sets_flags(a, at);
}

static void
cmp_mi(struct asm_ *a, int w, int base, int32_t disp, int32_t imm)
{
    alu_mi(a, w, ALU_CMP, base, disp, imm);
}

/* cmp byte [base+disp], imm8 and mov byte [base+disp], imm8 */
static void
cmp8_mi(struct asm_ *a, int base, int32_t disp, int imm)
{
    int at = here(a);
    rex(a, 0, 0, 0, base);
    b1(a, 0x80);
    modrm_mem(a, ALU_CMP, base, disp);
    b1(a, imm);
    sets_flags(a, at);
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
    int at = here(a);
    op_rm(a, 1, 0x3B, reg, base, disp);
    sets_flags(a, at);
}

static void
test_rr(struct asm_ *a, int x, int y)
{
    int at = here(a);
    op_rr(a, 1, 0x85, y, x);
    sets_flags(a, at);
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
    window_branch(a, 6);
    b1(a, 0x0F);
    b1(a, 0x80 + cc);
    rel32_to(a, label);
}

static void
jmp(struct asm_ *a, int label)
{
    window_branch(a, 5);
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

/* The same test without the reference: r's object is immortal or this
   thread's, which no other thread frees. Clobbers RCX, R11. */
static void
owned_or_exit(struct asm_ *a, int r, int fail)
{
    int done = new_label(a);
    load32(a, RCX, r, offsetof(PyObject, ob_ref_local));
    cmp32_ri8(a, RCX, -1);
    jcc(a, CC_E, done);                         /* immortal */
    b1(a, 0x64);
    b1(a, 0x4C);
    b1(a, 0x8B);
    b1(a, 0x1C);
    b1(a, 0x25);
    b4(a, 0);                                   /* mov r11, fs:[0] */
    cmp_rm(a, R11, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, fail);
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
find_place(struct asm_ *a, lu_v v)
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

/* The same, found once per value: exits name the same values again and again. */
static lu_pv
place(struct asm_ *a, lu_v v)
{
    if (a->b.placed == NULL) {
        a->b.placed = calloc((size_t)a->f->nins, sizeof(lu_pv));
    }
    if (a->b.placed[v] == 0) {
        a->b.placed[v] = find_place(a, v) + 1;
    }
    return a->b.placed[v] - 1;
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
   row of owned locations, the last in the unit's while it is built. A
   location named twice is one value, entered once: seen says which
   location d has (b->seen[...] == stamp). */
struct owning {
    struct build *b;
    struct lu_deopt *d;
    uint32_t stamp;
};

static void
own_loc(struct owning *w, int32_t loc)
{
    struct build *b = w->b;
    uint32_t *seen = &b->seen[lu_pv_arg_of_loc(loc)];
    if (*seen == w->stamp) {
        return;
    }
    *seen = w->stamp;
    b->owned = grow(b->owned, &b->capowned, b->nowned + 1, sizeof *b->owned);
    b->owned[b->nowned++] = loc;
    w->d->nowned++;
}

static void
note_owned(struct lu_func *f, lu_v v, void *arg)
{
    struct build *b = ((struct owning *)arg)->b;
    if (b->owns[v] == 0) {
        struct lu_ins *in = &f->ins[v];
        b->owns[v] = (in->flags & INS_OWNED) && in->loc != LU_LOC_NONE ? (int32_t)lu_pv_arg_of_loc(in->loc) + 1 : -1;
    }
    if (b->owns[v] > 0) {
        own_loc(arg, lu_pv_loc(LU_PV(0, (uint32_t)b->owns[v] - 1)));
    }
}

/* What a frame that names no virtual object owns (struct lu_fs record). */
static void
note_frame_owned(struct lu_func *f, lu_v v, void *arg)
{
    struct lu_fs *p = arg;
    struct lu_ins *in = &f->ins[v];
    if ((in->flags & INS_OWNED) && in->loc != LU_LOC_NONE) {
        p->owned[p->nowned++] = in->loc;
    }
}

static void
record_owned(struct lu_func *f, struct lu_fs *p)
{
    p->owned = lu_ir_alloc(f, sizeof(int32_t) * (size_t)(2 + p->nlocals + p->nstack));
    lu_fs_each_frame(f, p, p, note_frame_owned, p);
}

static int
names_virtual(const struct lu_fs *p)
{
    for (int i = 0; i < p->nlocals + p->nstack; i++) {
        if (LU_IS_VIRT(*lu_fs_slot(p, i))) {
            return 1;
        }
    }
    return 0;
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
    h = mix(h, (uintptr_t)fr->func ^ (uint64_t)fr->funcv << 40);
    h = mix(h, (uint64_t)(uint32_t)fr->pc << 32 | (uint32_t)fr->caller);
    h = mix(h, (uint64_t)(uint32_t)fr->nlocals << 32 | (uint64_t)(uint32_t)fr->nstack << 8 | fr->ret);
    h = mix(h, fr->gen);
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
    return x->code == y->code && x->func == y->func && x->funcv == y->funcv && x->pc == y->pc && x->ret == y->ret
        && x->gen == y->gen
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
    struct pending *x = lu_ir_alloc(a->f, sizeof *x);         /* freed with the build */
    x->index = a->nexits;
    struct lu_deopt *d = &x->d;
    d->unit = a->unit;
    d->objs = b->nobjs;
    d->owned = b->nowned;
    /* The frames outermost first, and room for their slots in that order. */
    uint32_t n = 0, nvals = 0;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        n++;
        nvals += (uint32_t)(p->nlocals + p->nstack);
        d->rest += p->evaluated != NULL && *p->evaluated > p->seq ? (uint32_t)(*p->evaluated - p->seq) : 0;
    }
    b->chain = grow(b->chain, &b->capchain, n, sizeof *b->chain);
    b->room = grow(b->room, &b->caproom, nvals, sizeof *b->room);
    uint32_t k = n;
    for (struct lu_fs *p = fs; p != NULL; p = p->caller) {
        b->chain[--k] = p;
        d->visible |= k > 0 && !(p->code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN));
        if (p->record == 0 && (p->gen > 0 || names_virtual(p))) {
            p->record = -1;
        }
    }
    /* The slots of frames that name virtual objects, innermost first (the
       order those are entered in). A frame that names none is described
       once per caller record (struct lu_fs record), below. */
    uint32_t end = nvals;
    for (k = n; k-- > 0;) {
        struct lu_fs *p = b->chain[k];
        int m = p->nlocals + p->nstack;
        end -= (uint32_t)m;
        for (int i = 0; p->record < 0 && i < m; i++) {
            b->room[end + (uint32_t)i] = describe(a, d, fs, *lu_fs_slot(p, i));
        }
    }
    int caller = -1;
    for (k = 0; k < n; end += (uint32_t)(b->chain[k]->nlocals + b->chain[k]->nstack), k++) {
        struct lu_fs *p = b->chain[k];
        if (p->record > 0 && p->under == caller) {
            caller = p->record - 1;
            continue;
        }
        for (int i = 0; p->record >= 0 && i < p->nlocals + p->nstack; i++) {
            b->room[end + (uint32_t)i] = describe(a, d, fs, *lu_fs_slot(p, i));
        }
        struct frame_key key = { .fr = { .code = p->code, .func = p->func, .pc = p->pc, .ret = p->ret,
                                         .funcv = p->funcv >= 0 ? place(a, p->funcv) : LU_PV(DV_NULL, 0),
                                         .gen = p->gen > 0 ? describe(a, d, fs, LU_VIRT(p->gen - 1))
                                             : p->gen < 0 ? place(a, -1 - p->gen) : LU_PV(DV_NULL, 0),
                                         .nlocals = p->nlocals, .nstack = p->nstack, .caller = caller },
                                 .vals = b->room + end };
        int under = caller;
        caller = frame_record(b, &key);
        if (p->record >= 0) {
            if (p->owned == NULL) {
                record_owned(a->f, p);
            }
            p->record = caller + 1;
            p->under = under;
        }
    }
    d->nframes = (int32_t)n;
    d->frame = caller;
    /* What it owns: what it holds and its frames' slots, a recorded
       frame's from its list. */
    uint32_t places = 32 + (uint32_t)a->f->nspill;
    if (b->nseen < places) {
        b->seen = realloc(b->seen, sizeof(uint32_t) * places);
        memset(b->seen + b->nseen, 0, sizeof(uint32_t) * (places - b->nseen));
        b->nseen = places;
    }
    if (b->owns == NULL) {
        b->owns = calloc((size_t)a->f->nins, sizeof(int32_t));
    }
    struct owning w = { b, d, (uint32_t)a->nexits + 1 };
    for (int i = 0; i < fs->nheld; i++) {
        note_owned(a->f, fs->held[i], &w);
    }
    for (k = 0; k < n; k++) {
        struct lu_fs *p = b->chain[k];
        if (p->record < 0) {
            lu_fs_each_frame(a->f, fs, p, note_owned, &w);
        }
        for (int i = 0; p->record > 0 && i < p->nowned; i++) {
            own_loc(&w, p->owned[i]);
        }
    }
    d->reason = (uint8_t)reason;
    d->error = (uint8_t)error;
    d->drop = drop;
    d->opcode = (uint8_t)lu_base_opcode(fs->code, fs->pc);
    return d;
}

/* The unit's descriptions in one block, from the exits on (struct
   lu_unit); and the addresses of the exits and of each published state
   written into the code that reads them. */
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
    }
    a->nexits = 0;
    int64_t p = (int64_t)(intptr_t)u->exits;
    memcpy(a->buf + a->deopts_patch, &p, 8);
    for (uint32_t i = 0; i < a->npubs; i++) {
        p = (int64_t)(intptr_t)&u->exits[a->pubs[i].exit];
        memcpy(a->buf + a->pubs[i].at, &p, 8);
    }
}

/* What building the descriptions holds. */
static void
build_free(struct asm_ *a)
{
    struct build *b = &a->b;
    free(b->frames);
    free(b->objs);
    free(b->vals);
    free(b->imms);
    free(b->owned);
    free(b->frame_table.slots);
    free(b->imm_table.slots);
    free(b->room);
    free(b->chain);
    free(b->seen);
    free(b->placed);
    free(b->owns);
    free(a->pubs);
    free(a->other.buf);
    free(a->uses);
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
static void increfs(struct asm_ *a, int r, int n);
static void decrefs(struct asm_ *a, int r, int n);
static void lea(struct asm_ *a, int dst, int base, int32_t disp);

/* Whether v is an object a hold may be owed for: one the call is handed
   (a->handed, stamped by holds_of). */
static int
holdable(struct asm_ *a, lu_v v)
{
    return v >= 0 && a->handed[v] == a->walk && I(a, v)->rep == REP_OBJ && I(a, v)->op != OP_CONST
        && I(a, v)->op != OP_BOOL;
}

/* One more name of v, when it is an object the call is handed. */
static void
hold_add(struct asm_ *a, struct lu_ins *in, struct hold *h, int *nh, lu_v v)
{
    if (!holdable(a, v)) {
        return;
    }
    int k = 0;
    while (k < *nh && h[k].v != v) {
        k++;
    }
    if (k < *nh) {
        h[k].n++;
    }
    else {
        h[(*nh)++] = (struct hold){ v, (I(a, v)->flags & INS_OWNED) ? 0 : 1 };
    }
}

/* The names a slot's value v gives: v itself, or the object it became;
   for an object not yet made, what that object would hold a reference
   to, once per object however many slots reach it. */
static void
hold_names(struct asm_ *a, struct lu_ins *in, lu_v v, struct hold *h, int *nh)
{
    if (!LU_IS_VIRT(v)) {
        hold_add(a, in, h, nh, v);
        return;
    }
    int id = LU_VIRT_ID(v);
    struct lu_vobj *o = lu_fs_vobj(a->f, in->fs, id, &v);
    if (v != LU_NOV) {
        hold_add(a, in, h, nh, v);
        return;
    }
    if (a->walked[id] == a->walk) {
        return;
    }
    a->walked[id] = a->walk;
    if (o->kind == VK_BOX_INT || o->kind == VK_BOX_FLOAT || o->kind == VK_BOOL) {
        return;                 /* raw values, and the object a number came in as */
    }
    for (int i = 0; i < o->n; i++) {
        hold_names(a, in, o->f[i], h, nh);
    }
}

/* What the frames' slots hold of the objects a published call is handed,
   beyond compiled code's one reference per object
   (docs/design/jit-architecture.md 10): the interpreter's frames hold a
   reference per slot, and the C the call runs may count them. One hold
   per further slot that names an object compiled code owns, one per slot
   for an object it only borrows. Every frame of the state counts, an
   inlined one as the frame the interpreter would have pushed; an object
   not yet made holds nothing itself, but one per field that names an
   object, as the object would. A constant is the unit's, held as long as
   the unit is. */
static int
holds_of(struct asm_ *a, struct lu_ins *in, struct hold **out)
{
    struct lu_fs *fs = in->fs;
    int nh = 0;
    struct hold *h = lu_ir_alloc(a->f, sizeof(struct hold) * (size_t)(in->nargs + 3));
    if (a->walked == NULL) {
        a->walked = lu_ir_alloc(a->f, sizeof(uint32_t) * (size_t)(a->f->nvobjs + 1));
        a->handed = lu_ir_alloc(a->f, sizeof(uint32_t) * (size_t)a->f->nins);
    }
    a->walk++;
    int any = 0;
    for (int j = -3; j < in->nargs; j++) {
        lu_v v = j == -3 ? in->a : j == -2 ? in->b : j == -1 ? in->c : in->args[j];
        if (v >= 0) {
            a->handed[v] = a->walk;
            any |= holdable(a, v);
        }
    }
    for (struct lu_fs *p = fs; any && p != NULL; p = p->caller) {
        for (int i = 0; i < p->nlocals + p->nstack; i++) {
            hold_names(a, in, *lu_fs_slot(p, i), h, &nh);
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
    if (in->op != OP_HELPER || (in->imm != H_call && in->imm != H_call_any && in->imm != H_call_kw
                                && in->imm != H_call_bound)) {
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
    alu_mi(a, 0, ALU_ADD, R14, offsetof(PyThreadState, py_recursion_remaining), n);
}

/* The task's unlocked_jit in a unit that stores without locks while serial
   (runtime/thread/task.c): 0 while C the unit called may run Python, 1 again
   after, before any check reads the unit. */
static void
unlocked_pause(struct asm_ *a)
{
    if (a->serial) {
        store_mi(a, R14, (int32_t)offsetof(struct lu_task, unlocked_jit), 0);
    }
}

static void
unlocked_again(struct asm_ *a)
{
    if (a->serial) {
        store_mi(a, R14, (int32_t)offsetof(struct lu_task, unlocked_jit), 1);
    }
}

/* One more of kind k in the unit's census, while it keeps one (-X
   lucifer_jit_stats), with r as scratch. */
static void
counted(struct asm_ *a, int k, int r)
{
    if (a->unit->counts != NULL && k >= 0) {
        mov_ri(a, r, (int64_t)(intptr_t)&a->unit->counts[k]);
        rex(a, 1, 0, 0, r);
        b1(a, 0xFF);
        modrm_mem(a, 0, r, 0);                  /* inc qword [r] */
    }
}

static void
publish(struct asm_ *a, struct lu_ins *in, struct lu_deopt *d)
{
    struct lu_fs *fs = in->fs;
    a->inlined = 0;
    for (struct lu_fs *p = fs; p->caller != NULL; p = p->caller) {
        a->inlined += !lu_frame_is_kernel(p->code);        /* kernel frames count against no limit while inlined */
    }
    if (a->inlined > 0) {
        depth_add(a, -a->inlined);
    }
    d->instr = in->op != OP_DECREF ? lu_jit_next_pc(fs->code, fs->pc) : fs->pc;
    a->pubs = grow(a->pubs, &a->cappubs, a->npubs + 1, sizeof *a->pubs);
    a->pubs[a->npubs].at = here(a) + 2;
    a->pubs[a->npubs++].exit = ((struct pending *)d)->index;
    mov_ri(a, RAX, 0x1122334455667788LL);       /* d's address, patched (pack) */
    store(a, R15, offsetof(struct lu_frame, jit_desc), RAX);
    unlocked_pause(a);          /* in the call: what returns checks before an unlocked store */
    a->nheld = a->parking ? 0 : holds_of(a, in, &a->held);
    d->takes = takes_of(a, in, a->held, a->nheld);
    if (a->nheld > 0) {
        b1(a, 0x51);                            /* push rcx: an argument may be in it already */
        for (int k = 0; k < a->nheld; k++) {
            counted(a, CNT_hold, RAX);
            to_gpr(a, R11, a->held[k].v);
            increfs(a, R11, a->held[k].n);
        }
        b1(a, 0x59);                            /* pop rcx */
    }
}

/* After the call: the holds go, with the call's result (RAX, or XMM0 for
   `rep` REP_F64) kept. A held value is where the published state says it
   is: in memory, where the allocator puts what a call's state names, or,
   after a slow path that saved the registers (kept_begin), in a register
   whose word at jit_regs holds it while the registers C used are not yet
   back. */
static void
unpublish(struct asm_ *a, int rep)
{
    if (a->inlined > 0) {
        depth_add(a, a->inlined);
        a->inlined = 0;
    }
    store_mi(a, R15, offsetof(struct lu_frame, jit_desc), 0);
    if (a->nheld == 0) {
        unlocked_again(a);
        return;
    }
    alu_ri(a, ALU_SUB, RSP, 16);
    store(a, RSP, 0, RAX);
    if (rep == REP_F64) {
        sse_rm(a, 0xF2, 0x11, 0, RSP, 8);
    }
    for (int k = 0; k < a->nheld; k++) {
        int loc = I(a, a->held[k].v)->loc;
        if (a->saved && loc >= 0 && loc < LU_LOC_FPR && I(a, a->held[k].v)->op != OP_CONST) {
            load(a, RDX, R15, offsetof(struct lu_frame, jit_regs));
            load(a, RDX, RDX, 8 * loc);
        }
        else {
            to_gpr(a, RDX, a->held[k].v);
        }
        if (a->held[k].n == 1) {
            decref(a, RDX, NULL, NULL, -1);
        }
        else {
            decrefs(a, RDX, a->held[k].n);
        }
    }
    if (rep == REP_F64) {
        sse_rm(a, 0xF2, 0x10, 0, RSP, 8);
    }
    load(a, RAX, RSP, 0);
    alu_ri(a, ALU_ADD, RSP, 16);
    a->nheld = 0;
    unlocked_again(a);          /* after the releases, whose destructors may run Python */
}

/* inc dword [base+disp] (digit 0) or dec (digit 1) */
static void
incdec32(struct asm_ *a, int digit, int base, int32_t disp)
{
    rex(a, 0, 0, 0, base);
    b1(a, 0xFF);
    modrm_mem(a, digit, base, disp);
}

/* The increments the parameters of a call took from its argument words
   (bits by index at rsp: helpers.c call_function's `takes`), given back
   once the callee's frame released them. Clobbers RAX, RCX, R10. */
static void
takes_back(struct asm_ *a, uint64_t takes)
{
    for (; takes != 0; takes &= takes - 1) {
        load(a, R10, RSP, 8 * __builtin_ctzll(takes));
        incref(a, R10);
    }
}

/* After a direct entry ran its unit (EAX the enum lu_jit_result, the
   result word at [rsp]): a return ended the frame and left the value
   there; anything else names the frame there and goes to lu_jit_finish.
   Then `done` with the value or NULL in RAX. */
static void
direct_done(struct asm_ *a, uint64_t takes, int done)
{
    int exited = new_label(a);
    b1(a, 0x83);
    b1(a, 0xF8);
    b1(a, LU_JIT_RETURN);                       /* cmp eax, LU_JIT_RETURN */
    jcc(a, CC_NE, exited);
    takes_back(a, takes);
    load(a, RAX, RSP, 0);
    jmp(a, done);
    cold_begin(a);
    bind(a, exited);
    mov_rr(a, RDI, R14);
    load(a, RSI, RSP, 0);                       /* the frame, for DEOPT and ERROR */
    op_rr(a, 0, 0x89, RAX, RDX);                /* mov edx, eax */
    mov_rr(a, RCX, RSI);
    mov_ri(a, R8, 0);                           /* no marker: the caller's frame is under it */
    call_abs(a, (void *)lu_jit_finish);
    if (takes != 0) {
        store(a, RSP, 0, RAX);
        takes_back(a, takes);
        load(a, RAX, RSP, 0);
    }
    jmp(a, done);
    cold_end(a);
}

/* The direct call of the function in RAX with `nargs` positional
   arguments at rsp + `off` (target.h struct lu_jit_direct), the bits of
   `takes` by argument word at rsp: then `done` with the value or NULL in
   RAX, or `slow` with nothing done. */
static void
direct_call(struct asm_ *a, int nargs, int off, uint64_t takes, int slow, int done)
{
    int entered = new_label(a);
    takes &= ~(((uint64_t)1 << (off / 8)) - 1);         /* the arguments': not the callable's, nor an empty self */
    load(a, RCX, RAX, offsetof(PyFunctionObject, func_code));
    load(a, RDX, RCX, offsetof(PyCodeObject, co_executors));
    test_rr(a, RDX, RDX);
    jcc(a, CC_E, slow);
    b1(a, 0xF6);
    modrm_rr(a, 0, RDX);
    b1(a, 1);                                   /* test dl, 1: a call count, not JIT state */
    jcc(a, CC_NE, slow);
    cmp_mi(a, 0, RDX, offsetof(struct lu_jit_direct, nargs), nargs);
    jcc(a, CC_NE, slow);
    load(a, R11, RDX, offsetof(struct lu_jit_direct, entry));
    test_rr(a, R11, R11);
    jcc(a, CC_E, slow);
    mov_rr(a, RDI, R14);
    mov_rr(a, RSI, RAX);
    lea(a, RDX, RSP, off);
    mov_rr(a, RCX, RSP);                        /* the value goes over the callable's word */
    mov_ri(a, R8, (int64_t)(takes >> (off / 8)));
    rex(a, 0, 0, 0, R11);
    b1(a, 0xFF);
    modrm_rr(a, 2, R11);                        /* call r11 */
    op_rr(a, 0, 0x85, RAX, RAX);                /* test eax, eax: an int */
    jcc(a, CC_NE, entered);
    jmp(a, slow);                               /* declined: nothing was done */
    bind(a, entered);
    direct_done(a, takes, done);
}

/* An H_call with its argument array at rsp (the callable, self or NULL,
   the arguments): a Python function, or a bound method of one, whose
   code's unit has a direct entry for this many arguments is called
   there. Anything else falls through to the helper. */
static void
call_direct(struct asm_ *a, lu_v v, uint64_t takes, int done)
{
    struct lu_ins *in = I(a, v);
    struct lu_ins *self = I(a, in->args[1]);
    int none = self->op == OP_CONST && self->imm == 0;
    int n = in->nargs - 2;
    int slow = new_label(a), function = new_label(a);
    load(a, RAX, RSP, 0);
    mov_ri(a, R11, (int64_t)(intptr_t)&PyFunction_Type);
    cmp_rm(a, R11, RAX, offsetof(PyObject, ob_type));
    if (none) {
        /* A bound method: its self goes in the free word before the
           arguments, as h_call puts it, and the word is empty again if the
           call is not made here. */
        int unbind = new_label(a);
        jcc(a, CC_E, function);
        mov_ri(a, R11, (int64_t)(intptr_t)&PyMethod_Type);
        cmp_rm(a, R11, RAX, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, slow);
        load(a, RCX, RAX, offsetof(PyMethodObject, im_self));
        load(a, RAX, RAX, offsetof(PyMethodObject, im_func));
        mov_ri(a, R11, (int64_t)(intptr_t)&PyFunction_Type);
        cmp_rm(a, R11, RAX, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, slow);
        store(a, RSP, 8, RCX);
        direct_call(a, n + 1, 8, takes, unbind, done);
        bind(a, unbind);
        store_mi(a, RSP, 8, 0);
        jmp(a, slow);
        bind(a, function);
        direct_call(a, n, 16, takes, slow, done);
    }
    else {
        jcc(a, CC_NE, slow);
        direct_call(a, n + 1, 8, takes, slow, done);
    }
    bind(a, slow);
}

/* `d`, when the helper publishes: the state it publishes. */
static void
call_helper(struct asm_ *a, lu_v v, struct lu_deopt *d)
{
    struct lu_ins *in = I(a, v);
    const struct lu_helper *h = &lu_helpers[in->imm];
    int bare = d == NULL && (h->fx & (FX_PY | FX_SEEN));    /* no state to publish (facts.c unsettles) */
    if (d != NULL) {
        publish(a, in, d);
    }
    else if (bare) {
        unlocked_pause(a);      /* before a direct call too: its callee runs Python */
    }
    static const int argregs[] = { RDI, RSI, RDX, RCX, R8, R9 };
    struct move moves[8];
    int nm = 0;
    int done = -1;
    if (h->nargs == 0) {
        /* Variadic: the values in the argument array at rsp. */
        for (int i = 0; i < in->nargs; i++) {
            to_gpr(a, RAX, in->args[i]);
            store(a, RSP, 8 * i, RAX);
        }
        if (in->imm == H_call || in->imm == H_call_any) {
            done = new_label(a);
            call_direct(a, v, d != NULL ? d->takes : 0, done);
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
    if (done >= 0) {
        bind(a, done);
    }
    if (d != NULL) {
        unpublish(a, h->rep);
    }
    else if (bare) {
        unlocked_again(a);
    }
}

/* Leaves at `dead` once the unit is dead (runtime/jit/deps.c). Clobbers R11. */
static void
jump_if_dead(struct asm_ *a, int dead)
{
    mov_ri(a, R11, (int64_t)(intptr_t)&a->unit->dead);
    cmp_mi(a, 0, R11, 0, 0);
    jcc(a, CC_NE, dead);
}

/* After a call that published: a walker that made the frames real during
   it leaves at `left`, when the instruction has such an exit of its own. */
static void
settle(struct asm_ *a, int left)
{
    if (left >= 0) {
        cmp8_mi(a, R15, offsetof(struct lu_frame, owner), LU_FRAME_JIT);
        jcc(a, CC_NE, left);
        if (a->f->ndeps > 0) {
            jump_if_dead(a, left);     /* as GUARD_FRAMES: nor did the code it ran take back a fact */
        }
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
static void kept_begin(struct asm_ *a, struct lu_ins *in, struct lu_deopt *d);

static void
parked_begin(struct asm_ *a, struct lu_ins *in)
{
    a->parking = 1;
    kept_begin(a, in, published(a, in));
    a->parking = 0;
}

/* A call on the slow path of an instruction that is not a call to the
   allocator (lu_alloc_call): every register it allocates is kept across.
   With `d`, the state the call publishes, every register goes where that
   state reads them (jit_regs), and back after, but RAX; else the
   caller-saved ones go to the stack. Arguments are set up after
   kept_begin; a walker's exit is taken after kept_end, with the
   registers back. */
static void
kept_begin(struct asm_ *a, struct lu_ins *in, struct lu_deopt *d)
{
    if (d == NULL) {
        save_volatile(a);
        return;
    }
    a->parks = 1;
    alu_ri(a, ALU_SUB, RSP, 256);
    b1(a, 0xE8);
    rel32_to(a, a->save_all);                   /* call save_all */
    store(a, R15, offsetof(struct lu_frame, jit_regs), RSP);
    d->regs = 1;
    publish(a, in, d);
    a->saved = 1;
}

static void
kept_end(struct asm_ *a, struct lu_deopt *d)
{
    if (d == NULL) {
        restore_volatile(a);
        return;
    }
    unpublish(a, REP_NONE);
    a->saved = 0;
    b1(a, 0xE8);
    rel32_to(a, a->restore_all);                /* call restore_all */
    alu_ri(a, ALU_ADD, RSP, 256);
}

/* Leaves from a parked slow path to `exit`: the reference the call took
   (RAX) released, the registers back. */
static void
parked_leave(struct asm_ *a, struct lu_ins *in, int exit)
{
    if (in->op != OP_INLINE_LOAD || (in->flags & INS_OWNED)) {
        mov_rr(a, RDI, RAX);    /* a borrowed read (opt.c) took none */
        call_abs(a, (void *)lu_jit_decref);
    }
    b1(a, 0xE8);
    rel32_to(a, a->restore_all);
    alu_ri(a, ALU_ADD, RSP, 256);
    jmp(a, exit);
}

/* After the park: a walker that made the frames real leaves, and so does
   a poll word that changed meanwhile (a tool turned on while the world
   stopped), so that a park is a poll and a call's poll after it may go
   (facts.c). */
static void
parked_end(struct asm_ *a, struct lu_ins *in)
{
    int kept = new_label(a), seen = new_label(a);
    int left = exit_to(a, in->fs, EXIT_FRAME, 0, 0);
    unpublish(a, REP_NONE);
    a->saved = 0;
    if (a->f->ndeps > 0) {
        jump_if_dead(a, seen);     /* nor did the code it ran take back a fact */
    }
    cmp8_mi(a, R15, offsetof(struct lu_frame, owner), LU_FRAME_JIT);
    if (in->op == OP_POLL) {
        jcc(a, CC_E, kept);     /* the poll reads the word again itself */
    }
    else {
        int polled = exit_to(a, in->fs, EXIT_POLL, 0, 0);
        jcc(a, CC_NE, seen);
        load(a, RCX, R14, offsetof(struct lu_task, carrier));
        mov_ri(a, R11, (int64_t)a->f->unit->version);
        cmp_rm(a, R11, RCX, offsetof(struct lu_carrier, poll));
        jcc(a, CC_E, kept);
        parked_leave(a, in, polled);
    }
    bind(a, seen);
    parked_leave(a, in, left);
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

/* Py_INCREF n times on the free-threaded build, in one step: immortal
   objects stay; the owning thread counts locally, others in the shared
   count. */
static void
increfs(struct asm_ *a, int r, int n)
{
    int done = new_label(a), shared = new_label(a);
    test_rr(a, r, r);
    jcc(a, CC_E, done);                         /* Py_XINCREF: a phi may carry NULL */
    load32(a, RCX, r, offsetof(PyObject, ob_ref_local));
    if (n == 1) {
        rex(a, 0, 0, 0, RCX);
        b1(a, 0x83);
        modrm_rr(a, ALU_ADD, RCX);
        b1(a, 1);                               /* add ecx, 1 */
        jcc(a, CC_E, done);                     /* immortal: the count wraps to 0 */
    }
    else {
        int below = new_label(a);
        cmp32_ri8(a, RCX, -1);
        jcc(a, CC_E, done);                     /* immortal */
        rex(a, 0, 0, 0, RCX);
        b1(a, n < 128 ? 0x83 : 0x81);
        modrm_rr(a, ALU_ADD, RCX);
        n < 128 ? b1(a, n) : b4(a, n);          /* add ecx, n */
        jcc(a, CC_AE, below);
        rex(a, 0, 0, 0, RCX);
        b1(a, 0x83);
        modrm_rr(a, ALU_OR, RCX);
        b1(a, 0xFF);                            /* or ecx, -1: immortal, where n steps of one end */
        bind(a, below);
    }
    b1(a, 0x64);
    b1(a, 0x48);
    b1(a, 0x8B);
    b1(a, 0x04);
    b1(a, 0x25);
    b4(a, 0);                                   /* mov rax, fs:[0] */
    cmp_rm(a, RAX, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, shared);
    op_rm(a, 0, 0x89, RCX, r, offsetof(PyObject, ob_ref_local));
    cold_begin(a);
    bind(a, shared);
    /* lock add qword [r+shared], n: no flags noted, so no padding goes
       between the prefix and its instruction */
    b1(a, 0xF0);
    rex(a, 1, 0, 0, r);
    b1(a, n << _Py_REF_SHARED_SHIFT < 128 ? 0x83 : 0x81);
    modrm_mem(a, ALU_ADD, r, offsetof(PyObject, ob_ref_shared));
    n << _Py_REF_SHARED_SHIFT < 128 ? b1(a, n << _Py_REF_SHARED_SHIFT) : b4(a, n << _Py_REF_SHARED_SHIFT);
    jmp(a, done);
    cold_end(a);
    bind(a, done);
}

static void
incref(struct asm_ *a, int r)
{
    increfs(a, r, 1);
}


/* Py_DECREF of r: the owning thread's count above one drops in place;
   anything else (immortal, last reference, another thread's object) goes
   to C, which keeps the registers for a DECREF (kept_begin) and clobbers
   them for the stores that release an old value. When the release may
   run a finalizer, C runs with `d`, the state of `in`, published, and a
   walker's write then leaves at `left` if it is a label. Clobbers RAX,
   RCX, RDX and R11. */
static void settle(struct asm_ *a, int left);

static void store32_mi(struct asm_ *a, int base, int32_t disp, int32_t imm);
static void thread_id(struct asm_ *a, int r);

extern void lu_jit_decrefs(PyObject *o, int64_t n);

/* Py_DECREF n times (n > 1), in one step, of the object in r: the owning
   thread's count above n drops in place; anything else (immortal aside)
   goes to C, which drops them one by one. Clobbers RAX, RCX, RDX, R11. */
static void
decrefs(struct asm_ *a, int r, int n)
{
    int done = new_label(a), slow = new_label(a);
    test_rr(a, r, r);
    jcc(a, CC_E, done);
    load32(a, RCX, r, offsetof(PyObject, ob_ref_local));
    cmp32_ri8(a, RCX, -1);
    jcc(a, CC_E, done);                         /* immortal */
    alu_ri(a, ALU_CMP, RCX, n);
    jcc(a, CC_BE, slow);                        /* the last local references, or not as many */
    thread_id(a, RAX);
    cmp_rm(a, RAX, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, slow);
    alu_ri(a, ALU_SUB, RCX, n);
    op_rm(a, 0, 0x89, RCX, r, offsetof(PyObject, ob_ref_local));
    cold_begin(a);
    bind(a, slow);
    mov_rr(a, RDI, r);
    mov_ri(a, RSI, n);
    call_abs(a, (void *)lu_jit_decrefs);
    jmp(a, done);
    cold_end(a);
    bind(a, done);
}

/* The carrier's entry (struct lu_entry) into dst: the freelists. */
static void
entry_of(struct asm_ *a, int dst)
{
    load(a, dst, R14, offsetof(struct lu_task, carrier));
    load(a, dst, dst, offsetof(struct lu_carrier, current));
}

/* Keeps the dead object in r on the freelist at disp from `base`, as
   lu_freelist_push does (no owner, no count), then `done`; `full` when
   the list holds LU_FREE_MAX (-1: known to have room). Clobbers RCX. */
static void
freelist_push(struct asm_ *a, int r, int base, int32_t disp, int full, int done)
{
    int32_t head = disp + (int32_t)offsetof(struct lu_freelist, head), n = disp + (int32_t)offsetof(struct lu_freelist, n);
    _Static_assert(sizeof(struct lu_freelist) == 16, "a tuple's freelist is 16 bytes after the one before");
    if (full >= 0) {
        cmp_mi(a, 0, base, n, LU_FREE_MAX);
        jcc(a, CC_GE, full);
    }
    load(a, RCX, base, head);
    store(a, r, sizeof(PyObject), RCX);         /* LU_FREE_NEXT */
    store(a, base, head, r);
    alu_mi(a, 0, ALU_ADD, base, n, 1);
    store_mi(a, r, offsetof(PyObject, ob_tid), 0);
    store32_mi(a, r, offsetof(PyObject, ob_ref_local), 0);
    jmp(a, done);
}

/* The release of the object in r whose local count (in ECX) is about to
   reach zero, in line for the kinds whose dealloc only returns the block
   to a freelist or the allocator: an exact compact int, an exact float,
   an exact tuple of 1 to LU_FREE_TUPLE_SIZES items none of which this
   release frees, an exact compact ascii str, and a plain instance
   (release_instance).
   What _Py_DecRef, _Py_MergeZeroLocalRefcount and _Py_Dealloc do on the
   way there is not needed: the count is the owner's and the shared count
   is zero, so the object dies now and no other thread holds or queues it;
   no finalizer, weakref or ref tracer can see it (the types have none,
   and a tracer makes the allocators observed); no item dies, so no dealloc
   nests (the trash queue's depth). A tuple leaves the collector's view
   first, as _Py_Dealloc does. The code is one stub the unit's releases
   share, the object in RDX: it returns with ZF set when it released the
   object, clear when not. Clobbers RAX, RCX, RDX, R11. */
static void
release_in_line(struct asm_ *a, int r, int done)
{
    if (a->release == 0) {
        a->release = 1 + new_label(a);
    }
    mov_rr(a, RDX, r);
    b1(a, 0xE8);
    rel32_to(a, a->release - 1);                /* call the stub */
    jcc(a, CC_E, done);
}

/* mi_free's fast path for the block in RAX, which nothing else holds:
   a block of a page of this thread's segment that is neither full nor
   holds aligned blocks goes on the page's local free list, and its used
   count drops; then `done`. Anything else, or the page's last block
   (whose page retires), goes on to `other` with RAX as it was. Clobbers
   RCX, R11. */
/* The free above is mimalloc's fast path for this configuration only: a
   build that encodes free lists, pads or guards blocks, tracks them for a
   sanitizer or keeps statistics frees another way. */
#if (defined(MI_ENCODE_FREELIST) && MI_ENCODE_FREELIST) || (defined(MI_PADDING) && MI_PADDING) \
    || (defined(MI_GUARDED) && MI_GUARDED) || (defined(MI_TRACK_ENABLED) && MI_TRACK_ENABLED) \
    || (defined(MI_STAT) && MI_STAT > 0) || (defined(MI_DEBUG) && MI_DEBUG != 0) \
    || (defined(MI_SECURE) && MI_SECURE != 0)
#error "free_in_line pushes plain next pointers: this mimalloc configuration frees blocks another way"
#endif

static void
free_in_line(struct asm_ *a, int other, int done)
{
    _Static_assert(sizeof(mi_slice_t) == sizeof(mi_page_t), "a page is its first slice");
    mov_rr(a, RCX, RAX);
    mov_ri(a, R11, (int64_t)~(uint64_t)MI_SEGMENT_MASK);
    alu_rr(a, ALU_AND, RCX, R11);               /* the segment */
    thread_id(a, R11);
    cmp_rm(a, R11, RCX, offsetof(mi_segment_t, thread_id));
    jcc(a, CC_NE, other);                       /* another thread's: mi_free sorts it */
    mov_rr(a, R11, RAX);
    alu_rr(a, ALU_SUB, R11, RCX);
    rex(a, 1, 0, 0, R11);
    b1(a, 0xC1);
    modrm_rr(a, 5, R11);
    b1(a, MI_SEGMENT_SLICE_SHIFT);              /* shr r11: the slice's index */
    rex(a, 1, R11, 0, R11);
    b1(a, 0x69);
    modrm_rr(a, R11, R11);
    b4(a, (int32_t)sizeof(mi_slice_t));         /* imul r11, r11, size */
    alu_rr(a, ALU_ADD, R11, RCX);
    alu_ri(a, ALU_ADD, R11, offsetof(mi_segment_t, slices));
    load32(a, RCX, R11, offsetof(mi_slice_t, slice_offset));
    alu_rr(a, ALU_SUB, R11, RCX);               /* the page: the area's first slice */
    cmp8_mi(a, R11, offsetof(mi_page_t, flags), 0);
    jcc(a, CC_NE, other);                       /* full, or aligned blocks */
    b1(a, 0x66);
    rex(a, 0, 0, 0, R11);
    b1(a, 0x83);
    modrm_mem(a, ALU_CMP, R11, offsetof(mi_page_t, used));
    b1(a, 1);                                   /* cmp word [r11+used], 1 */
    jcc(a, CC_E, other);                        /* the last block: the page retires */
    load(a, RCX, R11, offsetof(mi_page_t, local_free));
    store(a, RAX, 0, RCX);                      /* the block's next, unencoded in this build */
    store(a, R11, offsetof(mi_page_t, local_free), RAX);
    b1(a, 0x66);
    rex(a, 0, 0, 0, R11);
    b1(a, 0xFF);
    modrm_mem(a, 1, R11, offsetof(mi_page_t, used));   /* dec word [r11+used] */
    jmp(a, done);
}

/* To `label` when the type in RAX has LU_TPFLAGS_PLAIN_RELEASE. */
static void
plain_jump(struct asm_ *a, int label)
{
    rex(a, 0, 0, 0, RAX);
    b1(a, 0xF6);
    modrm_mem(a, 0, RAX, offsetof(PyTypeObject, tp_flags) + 5);
    b1(a, (uint8_t)(LU_TPFLAGS_PLAIN_RELEASE >> 40));     /* test byte [rax+flags+5], plain */
    jcc(a, CC_NE, label);
}

/* The release of an instance of a Python class (type in RAX, object in
   r, RDX; its class plain, as the caller saw: plain_jump) whose
   dealloc would only release its attributes, its type and its block: a
   class with LU_TPFLAGS_PLAIN_RELEASE (no __del__, no __slots__, object
   its base, inline values), and an instance with no dict and its values
   in line (a weak reference to it the caller ruled out, owner_dies).
   Nothing it holds dies with it: each inline value is unboxed, immortal
   or this thread's with a count above the references this release drops,
   and so is its type; then the counts drop, the collector's count of
   objects with it (lu_gc_record_deallocation, short of a flush), and
   mi_free takes the block (this thread's heap or another's, as mimalloc
   sorts it), the registers the unit allocates kept around the call.
   Else `fail`. Clobbers RAX, RCX, R11; R10 kept.

   The words read are the class's names, not the instance's room: no
   instance holds a word at or past the count (attrs.c), and one that came
   by __class__ assignment, with another class's room, has its values in a
   dict, which fails here. The count bounds the references dropped too. */
static void
release_instance(struct asm_ *a, int r, int fail, int done)
{
    int failed = new_label(a);
    int scan = new_label(a), scanned = new_label(a), drop = new_label(a), dropped = new_label(a);
    int untyped = new_label(a);
    /* What C can write straight into a type behind the bit (a spec's
       slots, a metaclass's code): the bit then never decides alone. */
    load(a, R11, RAX, offsetof(PyTypeObject, tp_dealloc));
    mov_ri(a, RCX, (int64_t)(intptr_t)lu_subtype_dealloc);
    alu_rr(a, ALU_CMP, R11, RCX);
    jcc(a, CC_NE, fail);
    cmp_mi(a, 1, RAX, offsetof(PyTypeObject, tp_finalize), 0);
    jcc(a, CC_NE, fail);                        /* lu_subtype_dealloc runs it (tp_del it does not) */
    cmp_mi(a, 1, r, LU_MANAGED_DICT_OFFSET, 0);
    jcc(a, CC_NE, fail);                        /* a dict, which inline values moved to make too */
    rex(a, 0, 0, 0, r);
    b1(a, 0xF6);
    modrm_mem(a, 0, r, offsetof(PyObject, ob_flags) + 1);
    b1(a, LU_ATTRS_IN_DICT >> 8);               /* test byte [r+flags+1], LU_ATTRS_IN_DICT */
    jcc(a, CC_NE, fail);
    load(a, RCX, R14, offsetof(struct lu_task, carrier));
    load(a, RCX, RCX, offsetof(struct lu_carrier, current));
    cmp_mi(a, 0, RCX, offsetof(struct lu_entry, gc_alloc_count), -LU_GC_ALLOC_FLUSH + 1);
    jcc(a, CC_LE, fail);                        /* the decrement would flush */
    /* [rsp] R10, [rsp+8] the end of the inline values, [rsp+16] whether a
       word holds a counted object; 24 bytes keep the stack aligned for a
       call (the stub's return address below). */
    alu_ri(a, ALU_SUB, RSP, 24);
    store(a, RSP, 0, R10);
    store32_mi(a, RSP, 16, 0);
    thread_id(a, R10);
    load(a, RCX, RAX, offsetof(PyHeapTypeObject, ht_cached_keys));
    load(a, RCX, RCX, LU_SHARED_NAMES_COUNT);  /* the names */
    load(a, R11, RAX, offsetof(PyTypeObject, tp_basicsize));
    alu_rr(a, ALU_ADD, R11, r);                 /* the values */
    store(a, RSP, 8, RCX);
    rex(a, 1, 0, 0, RSP);
    b1(a, 0xC1);
    modrm_mem(a, 4, RSP, 8);
    b1(a, 4);                                   /* shl qword [rsp+8], 4: LU_INLINE_STRIDE words a name */
    op_rm(a, 1, 0x01, R11, RSP, 8);             /* add [rsp+8], r11: the end */
    /* Nothing it holds may die with it: the type and each value are
       immortal, or this thread's with a local count above the references
       this release drops (at most one per name, one for the type). However
       the same object repeats among them, it stays above zero. */
    alu_ri(a, ALU_ADD, RCX, 1);
    int typed = new_label(a);
    cmp_mi(a, 0, RAX, offsetof(PyObject, ob_ref_local), -1);
    jcc(a, CC_E, typed);
    op_rm(a, 0, 0x39, RCX, RAX, offsetof(PyObject, ob_ref_local));  /* cmp [rax+local], ecx */
    jcc(a, CC_BE, failed);
    cmp_rm(a, R10, RAX, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, failed);
    bind(a, typed);
    bind(a, scan);
    cmp_rm(a, R11, RSP, 8);
    jcc(a, CC_AE, scanned);
    load(a, RAX, R11, 0);
    alu_ri(a, ALU_ADD, R11, LU_INLINE_STRIDE * sizeof(PyObject *));
    alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
    jcc(a, CC_BE, scan);                        /* absent, or unboxed */
    cmp_mi(a, 0, RAX, offsetof(PyObject, ob_ref_local), -1);
    jcc(a, CC_E, scan);
    op_rm(a, 0, 0x39, RCX, RAX, offsetof(PyObject, ob_ref_local));  /* cmp [rax+local], ecx */
    jcc(a, CC_BE, failed);
    cmp_rm(a, R10, RAX, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, failed);
    store32_mi(a, RSP, 16, 1);                  /* one to drop */
    jmp(a, scan);
    bind(a, scanned);
    /* It dies: the values' counts (when any word holds a counted object),
       then the type's, then the block. */
    cmp_mi(a, 0, RSP, 16, 0);
    jcc(a, CC_E, dropped);
    load(a, R11, r, offsetof(PyObject, ob_type));
    load(a, R11, R11, offsetof(PyTypeObject, tp_basicsize));
    alu_rr(a, ALU_ADD, R11, r);
    bind(a, drop);
    cmp_rm(a, R11, RSP, 8);
    jcc(a, CC_AE, dropped);
    load(a, RAX, R11, 0);
    alu_ri(a, ALU_ADD, R11, LU_INLINE_STRIDE * sizeof(PyObject *));
    alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
    jcc(a, CC_BE, drop);
    cmp_mi(a, 0, RAX, offsetof(PyObject, ob_ref_local), -1);
    jcc(a, CC_E, drop);
    alu_mi(a, 0, ALU_SUB, RAX, offsetof(PyObject, ob_ref_local), 1);
    jmp(a, drop);
    bind(a, dropped);
    load(a, RAX, r, offsetof(PyObject, ob_type));
    cmp_mi(a, 0, RAX, offsetof(PyObject, ob_ref_local), -1);
    jcc(a, CC_E, untyped);
    alu_mi(a, 0, ALU_SUB, RAX, offsetof(PyObject, ob_ref_local), 1);
    bind(a, untyped);
    load(a, RCX, R14, offsetof(struct lu_task, carrier));
    load(a, RCX, RCX, offsetof(struct lu_carrier, current));
    alu_mi(a, 0, ALU_SUB, RCX, offsetof(struct lu_entry, gc_alloc_count), 1);
    rex(a, 0, 0, 0, r);
    b1(a, 0x80);
    modrm_mem(a, 4, r, offsetof(PyObject, ob_gc_bits));
    b1(a, (uint8_t)~LU_GC_TRACKED);             /* and byte [r+gc_bits]: the collector's no longer */
    store32_mi(a, r, offsetof(PyObject, ob_ref_local), 0);     /* dead, as on every other path */
    mov_rr(a, RAX, r);
    alu_ri(a, ALU_SUB, RAX, 2 * sizeof(PyObject *));  /* the block starts at the preheader (a managed dict) */
    int freed = new_label(a), call = new_label(a);
    free_in_line(a, call, freed);
    bind(a, call);
    save_volatile(a);                           /* aligned: the 24 bytes and the call to the stub */
    mov_rr(a, RDI, RAX);
    call_abs(a, (void *)mi_free);
    restore_volatile(a);
    bind(a, freed);
    load(a, R10, RSP, 0);
    alu_ri(a, ALU_ADD, RSP, 24);
    jmp(a, done);
    bind(a, failed);
    load(a, R10, RSP, 0);
    alu_ri(a, ALU_ADD, RSP, 24);
    jmp(a, fail);
}

/* The object in r dies now if its count is the owner's last (local one,
   this thread's, nothing shared) and no hook observes the allocators;
   else `other`. Clobbers RCX, R11; RAX kept. */
static void
owner_dies(struct asm_ *a, int r, int other)
{
    cmp_mi(a, 0, r, offsetof(PyObject, ob_ref_local), 1);
    jcc(a, CC_NE, other);                       /* merged, or another thread's */
    thread_id(a, R11);
    cmp_rm(a, R11, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, other);
    /* The whole shared word: no other thread's references, and no flag
       either. The weakref tests below rely on it: making a weak reference
       sets the shared word's maybe-weakref bit, so an object anyone ever
       weakly referenced never gets here. Do not narrow this to the count. */
    cmp_mi(a, 1, r, offsetof(PyObject, ob_ref_shared), 0);
    jcc(a, CC_NE, other);
    mov_ri(a, R11, (int64_t)(intptr_t)&_PyRuntime.allocators.observed);
    cmp_mi(a, 0, R11, 0, 0);
    jcc(a, CC_NE, other);
}

static void
release_stub(struct asm_ *a)
{
    const int r = RDX;
    int other = new_label(a), done = new_label(a);
    bind(a, a->release - 1);
    int not_int = new_label(a), not_float = new_label(a), scan = new_label(a), scanned = new_label(a);
    int drop = new_label(a), dropped = new_label(a), back = new_label(a);
    int instance = new_label(a), not_str = new_label(a);
    /* The kind first, each to its own code: most objects released another
       way leave here. The type stays in RAX. */
    int labels[4] = { new_label(a), not_int, not_float, not_str };
    load(a, RAX, r, offsetof(PyObject, ob_type));
    plain_jump(a, instance);
    static PyTypeObject *const kinds[] = { &PyLong_Type, &PyFloat_Type, &PyUnicode_Type, &PyTuple_Type };
    for (int k = 0; k < 4; k++) {
        mov_ri(a, R11, (int64_t)(intptr_t)kinds[k]);
        alu_rr(a, ALU_CMP, RAX, R11);
        jcc(a, CC_E, labels[k]);
    }
    jmp(a, other);
    bind(a, labels[0]);
    owner_dies(a, r, other);
    cmp_mi(a, 1, r, offsetof(PyLongObject, long_value.lv_tag), 2 << _PyLong_NON_SIZE_BITS);
    jcc(a, CC_AE, back);                        /* not compact */
    entry_of(a, RAX);
    freelist_push(a, r, RAX, offsetof(struct lu_entry, free_ints), back, done);
    bind(a, not_int);                           /* a float */
    owner_dies(a, r, other);
    entry_of(a, RAX);
    freelist_push(a, r, RAX, offsetof(struct lu_entry, free_floats), back, done);
    bind(a, not_float);                         /* a str */
    owner_dies(a, r, other);
    /* An exact compact ascii str holds nothing but its block (str_dealloc). */
    load32(a, RAX, r, offsetof(PyASCIIObject, state));
    alu_ri(a, ALU_AND, RAX, str_state(0, 1));
    alu_ri(a, ALU_CMP, RAX, str_state(0, 1));
    jcc(a, CC_NE, back);
    store32_mi(a, r, offsetof(PyObject, ob_ref_local), 0);     /* dead, as on every other path */
    mov_rr(a, RAX, r);
    int str_call = new_label(a);
    free_in_line(a, str_call, done);
    bind(a, str_call);
    alu_ri(a, ALU_SUB, RSP, 8);                 /* aligned for the call (the stub's own return address) */
    save_volatile(a);
    mov_rr(a, RDI, r);
    call_abs(a, (void *)mi_free);
    restore_volatile(a);
    alu_ri(a, ALU_ADD, RSP, 8);
    jmp(a, done);
    bind(a, not_str);                           /* a tuple */
    owner_dies(a, r, other);
    load(a, RCX, r, offsetof(PyVarObject, ob_size));
    alu_ri(a, ALU_SUB, RCX, 1);
    alu_ri(a, ALU_CMP, RCX, LU_FREE_TUPLE_SIZES - 1);
    jcc(a, CC_A, back);                         /* unsigned: 1 to LU_FREE_TUPLE_SIZES items */
    entry_of(a, RAX);
    rex(a, 1, 0, 0, RCX);
    b1(a, 0xC1);
    modrm_rr(a, 4, RCX);
    b1(a, 4);                                   /* shl rcx, 4: the list's offset */
    alu_rr(a, ALU_ADD, RAX, RCX);
    int32_t lists = (int32_t)offsetof(struct lu_entry, free_tuples);
    cmp_mi(a, 0, RAX, lists + (int32_t)offsetof(struct lu_freelist, n), LU_FREE_MAX);
    jcc(a, CC_GE, back);
    /* No item may die here: each is immortal, or this thread's with a
       local count above the tuple's size. An item may fill several slots,
       but never more than all of them, so dropping one reference per slot
       leaves it above zero however the slots repeat. */
    load(a, RCX, r, offsetof(PyVarObject, ob_size));
    bind(a, scan);
    alu_ri(a, ALU_SUB, RCX, 1);
    jcc(a, CC_S, scanned);
    load_indexed8(a, R11, r, RCX, offsetof(PyTupleObject, ob_item));
    test_rr(a, R11, R11);
    jcc(a, CC_E, scan);
    load32(a, RAX, R11, offsetof(PyObject, ob_ref_local));
    cmp32_ri8(a, RAX, -1);
    jcc(a, CC_E, scan);
    op_rm(a, 0, 0x3B, RAX, r, offsetof(PyVarObject, ob_size));  /* cmp eax, [r+size]: at most 20 */
    jcc(a, CC_BE, back);
    thread_id(a, RAX);
    cmp_rm(a, RAX, R11, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, back);
    jmp(a, scan);
    bind(a, scanned);
    load(a, RCX, r, offsetof(PyVarObject, ob_size));
    bind(a, drop);
    alu_ri(a, ALU_SUB, RCX, 1);
    jcc(a, CC_S, dropped);
    load_indexed8(a, R11, r, RCX, offsetof(PyTupleObject, ob_item));
    test_rr(a, R11, R11);
    jcc(a, CC_E, drop);
    load32(a, RAX, R11, offsetof(PyObject, ob_ref_local));
    cmp32_ri8(a, RAX, -1);
    jcc(a, CC_E, drop);
    alu_mi(a, 0, ALU_SUB, R11, offsetof(PyObject, ob_ref_local), 1);
    jmp(a, drop);
    bind(a, dropped);
    rex(a, 0, 0, 0, r);
    b1(a, 0x80);
    modrm_mem(a, 4, r, offsetof(PyObject, ob_gc_bits));
    b1(a, (uint8_t)~LU_GC_TRACKED);             /* and byte [r+gc_bits]: the collector's no longer */
    load(a, R11, r, offsetof(PyVarObject, ob_size));
    entry_of(a, RAX);
    rex(a, 1, 0, 0, R11);
    b1(a, 0xC1);
    modrm_rr(a, 4, R11);
    b1(a, 4);                                   /* shl r11, 4 */
    alu_rr(a, ALU_ADD, RAX, R11);
    freelist_push(a, r, RAX, lists - (int32_t)sizeof(struct lu_freelist), -1, done);   /* room: seen above */
    bind(a, instance);
    owner_dies(a, r, other);
    release_instance(a, r, back, done);
    bind(a, back);
    bind(a, other);
    test_rr(a, RSP, RSP);                       /* ZF clear: not released */
    b1(a, 0xC3);
    bind(a, done);
    alu_rr(a, ALU_XOR, RAX, RAX);               /* ZF set: released */
    b1(a, 0xC3);
}

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
    cold_begin(a);
    bind(a, slow);
    release_in_line(a, r, done);
    int keep = in != NULL && !lu_alloc_call(in);   /* a release values stay in registers across */
    if (keep) {
        kept_begin(a, in, d);
        mov_rr(a, RDI, r);
    }
    else {
        mov_rr(a, RDI, r);
        if (d != NULL) {
            publish(a, in, d);
        }
    }
    int bare = d == NULL && in != NULL && in->op == OP_DECREF && in->imm == 1;   /* a return's (facts.c) */
    if (bare) {
        unlocked_pause(a);
    }
    call_abs(a, (void *)lu_jit_decref);
    if (bare) {
        unlocked_again(a);
    }
    if (keep) {
        kept_end(a, d);
    }
    else if (d != NULL) {
        unpublish(a, REP_NONE);
    }
    if (d != NULL) {
        settle(a, left);
    }
    jmp(a, done);
    cold_end(a);
    bind(a, done);
}
extern PyObject *lu_jit_box_int(int64_t v);
extern int64_t lu_jit_unbox_wide(PyObject *o);
extern PyObject *lu_jit_box_float(double v);
extern PyObject *lu_jit_list_item(PyObject *list, int64_t i);
extern PyObject *lu_jit_tuple_item(PyObject *t, int64_t i);
extern PyObject *lu_jit_slot_load(PyObject **slot);
extern int lu_jit_list_store(PyObject *list, int64_t i, PyObject *v);
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

/* A box of in->a's raw value, in RCX, as PyLong_FromLongLong and
   PyFloat_FromDouble make it: a small int from the cache, a one-digit int
   or a float from the carrier's freelist (its type stays), else from its
   mimalloc object heap, with the header _Py_NewReference gives it. Ints
   and floats are none of the collector's: nothing counts them. Anything
   else is C's, which keeps the registers: a wider int, a page with no
   block at hand, or a hook that sees allocations or births. */
/* The cached int whose value is in r, into RCX (RAX clobbered), or to
   `other` for a value the cache does not hold. */
static void
small_int_box(struct asm_ *a, int r, int other)
{
    lea(a, RCX, r, LU_NSMALLNEGINTS);
    alu_ri(a, ALU_CMP, RCX, LU_NSMALLNEGINTS + LU_NSMALLPOSINTS);
    jcc(a, CC_AE, other);                       /* unsigned: below the cache too */
    rex(a, 1, RCX, 0, RCX);
    b1(a, 0x69);
    modrm_rr(a, RCX, RCX);
    b4(a, (int32_t)sizeof(PyLongObject));      /* imul rcx, rcx, size */
    mov_ri(a, RAX, (int64_t)(intptr_t)&_PyRuntime.singletons.small_ints[0]);
    alu_rr(a, ALU_ADD, RCX, RAX);
}

static void
emit_box(struct asm_ *a, struct lu_ins *in)
{
    int is_int = in->op == OP_BOX_INT, slow = new_label(a), done = new_label(a);
    int32_t list = is_int ? (int32_t)offsetof(struct lu_entry, free_ints) : (int32_t)offsetof(struct lu_entry, free_floats);
    int x = 0, r = -1;
    if (is_int) {
        int fresh = new_label(a);
        r = gpr_of(a, in->a, RDX);
        small_int_box(a, r, fresh);
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
    cold_begin(a);
    bind(a, page);                              /* a block of lu_object_new's size; its type too */
    pop_block(a, LU_HEAP_OBJECT, is_int ? offsetof(PyLongObject, long_value.ob_digit) + sizeof(digit)
                                        : sizeof(PyFloatObject), RCX, RDX, slow);
    mov_ri(a, R11, (int64_t)(intptr_t)(is_int ? &PyLong_Type : &PyFloat_Type));
    store(a, RCX, offsetof(PyObject, ob_type), R11);
    if (r == RDX) {
        to_gpr(a, RDX, in->a);                  /* again: RDX held the page */
    }
    jmp(a, header);
    cold_end(a);
    bind(a, header);
    thread_id(a, R11);
    store(a, RCX, offsetof(PyObject, ob_tid), R11);
    _Static_assert(offsetof(PyObject, ob_flags) == 8 && offsetof(PyObject, ob_gc_bits) == 11
                   && offsetof(PyObject, ob_ref_local) == 12, "flags, mutex and GC bits are one word");
    store32_mi(a, RCX, offsetof(PyObject, ob_flags), 0);
    store32_mi(a, RCX, offsetof(PyObject, ob_ref_local), 1);
    store_mi(a, RCX, offsetof(PyObject, ob_ref_shared), 0);
    if (is_int) {
        /* One digit and the sign: a non-negative value is its own digit. */
        int negative = new_label(a), tagged = new_label(a);
        test_rr(a, r, r);
        jcc(a, CC_S, negative);
        store_mi(a, RCX, offsetof(PyLongObject, long_value.lv_tag), (int32_t)(((uintptr_t)1 << _PyLong_NON_SIZE_BITS) | 0));
        op_rm(a, 0, 0x89, r, RCX, offsetof(PyLongObject, long_value.ob_digit));
        bind(a, tagged);
        cold_begin(a);
        bind(a, negative);
        mov_rr(a, RAX, r);
        unary(a, 3, RAX);                       /* neg rax */
        store_mi(a, RCX, offsetof(PyLongObject, long_value.lv_tag), (int32_t)(((uintptr_t)1 << _PyLong_NON_SIZE_BITS) | 2));
        op_rm(a, 0, 0x89, RAX, RCX, offsetof(PyLongObject, long_value.ob_digit));
        jmp(a, tagged);
        cold_end(a);
    }
    else {
        sse_rm(a, 0xF2, 0x11, x, RCX, offsetof(PyFloatObject, ob_fval));
    }
    cold_begin(a);
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
    mov_rr(a, RCX, RAX);
    jmp(a, done);
    cold_end(a);
    bind(a, done);
}

/* BUILD_TUPLE and BUILD_LIST of a few items in line, as PyTuple_New and
   PyList_New make them: a tuple from the carrier's freed ones, else the
   object (and a list's array) popped from the carrier's mimalloc pages and
   counted for the collector; tracked, and filled with new references. The helper takes the rest: a page with no block at
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
    int header = new_label(a);
    if (!is_list && n <= LU_FREE_TUPLE_SIZES) {
        /* A freed tuple of n items first, as PyTuple_New takes one: reuse,
           not an allocation, for the collector's count. */
        int page = new_label(a);
        int32_t list = (int32_t)(offsetof(struct lu_entry, free_tuples) + (size_t)(n - 1) * sizeof(struct lu_freelist));
        load(a, RDX, RAX, list + (int32_t)offsetof(struct lu_freelist, head));
        test_rr(a, RDX, RDX);
        jcc(a, CC_E, page);
        load(a, R11, RDX, sizeof(PyObject));   /* LU_FREE_NEXT */
        store(a, RAX, list + (int32_t)offsetof(struct lu_freelist, head), R11);
        b1(a, 0x83);
        modrm_mem(a, ALU_SUB, RAX, list + (int32_t)offsetof(struct lu_freelist, n));
        b1(a, 1);                               /* sub dword [rax+n], 1 */
        jmp(a, header);
        bind(a, page);
    }
    cmp_mi(a, 0, RAX, offsetof(struct lu_entry, gc_alloc_count), LU_GC_ALLOC_FLUSH - 1);
    jcc(a, CC_GE, slow);                        /* lu_gc_record_allocation would flush the count */
    pop_block(a, LU_HEAP_GC, size, RDX, RCX, slow);
    if (is_list && n > 0) {
        pop_block(a, LU_HEAP_MEM, sizeof(struct lu_list_array) + (size_t)n * sizeof(PyObject *), RSI, RDI, undo);
    }
    b1(a, 0x83);
    modrm_mem(a, ALU_ADD, RAX, offsetof(struct lu_entry, gc_alloc_count));
    b1(a, 1);                                   /* add dword [rax+count], 1 */
    bind(a, header);
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
    cold_begin(a);
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
    jmp(a, done);
    cold_end(a);
    bind(a, done);
    return 1;
}

/* An instance of a class whose instances object.__new__ makes, in line as
   PyType_GenericAlloc makes it: the block (and its preheader) popped from
   the carrier's GC heap, zeroed, counted for the collector, tracked, with
   a reference to its type. The helper takes the rest, as for builds. 0
   when the allocation is the helper's alone (a block too large to pop). */
static int
emit_new_object(struct asm_ *a, lu_v v, int err)
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
    /* Zero: the preheader, the slots, each inline value word (a raw word
       is read only under its value word's marker, which a store writes
       after it) and the order record. */
    size_t values = pre + (size_t)tp->tp_basicsize, raw = values + (size_t)room * LU_INLINE_STRIDE * sizeof(PyObject *);
    for (size_t off = 0; off < size; off += 8) {
        int raw_word = off >= values && off < raw && (off - values) % (LU_INLINE_STRIDE * sizeof(PyObject *)) != 0;
        if ((off < pre || off >= pre + sizeof(PyObject)) && !raw_word) {
            store_mi(a, RDX, (int32_t)off, 0);
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
    /* The instance's reference to its type, as incref with R11 still the
       thread's id: the type is no NULL. */
    int typed = new_label(a), shared = new_label(a);
    load32(a, RCX, RBX, offsetof(PyObject, ob_ref_local));
    rex(a, 0, 0, 0, RCX);
    b1(a, 0x83);
    modrm_rr(a, ALU_ADD, RCX);
    b1(a, 1);                                   /* add ecx, 1 */
    jcc(a, CC_E, typed);                        /* immortal */
    cmp_rm(a, R11, RBX, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, shared);
    op_rm(a, 0, 0x89, RCX, RBX, offsetof(PyObject, ob_ref_local));
    bind(a, typed);
    mov_rr(a, RAX, RDX);
    cold_begin(a);
    bind(a, shared);
    b1(a, 0xF0);
    rex(a, 1, 0, 0, RBX);
    b1(a, 0x83);
    modrm_mem(a, ALU_ADD, RBX, offsetof(PyObject, ob_ref_shared));
    b1(a, 1 << _Py_REF_SHARED_SHIFT);
    jmp(a, typed);
    bind(a, slow);
    mov_ri(a, RDI, (int64_t)(intptr_t)tp);
    call_abs(a, lu_helpers[in->imm].fn);
    if (err >= 0) {
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, err);                      /* only C's object can be missing */
    }
    jmp(a, done);
    cold_end(a);
    bind(a, done);
    return 2;
}

static int
cond_of(int op, int swap)
{
    static const int cc[] = { CC_L, CC_LE, CC_E, CC_NE, CC_G, CC_GE };
    static const int sw[] = { CC_G, CC_GE, CC_E, CC_NE, CC_L, CC_LE };
    return swap ? sw[op] : cc[op];
}

/* How many operands and frame states read each value (a state once per
   instruction), for a compare whose one reader is the next instruction. */
static void
count_use(struct lu_func *f, lu_v v, void *arg)
{
    if (v >= 0) {
        ((int *)arg)[v]++;
    }
}

static int *
count_uses(struct lu_func *f)
{
    int *uses = calloc((size_t)f->nins + 1, sizeof(int));
    for (int k = 0; k < f->norder; k++) {
        struct lu_block *bl = &f->blocks[f->order[k]];
        for (int i = 0; i < bl->nins; i++) {
            struct lu_ins *in = &f->ins[bl->ins[i]];
            lu_v ops[3] = { in->a, in->b, in->c };
            for (int j = 0; j < 3; j++) {
                count_use(f, ops[j], uses);
            }
            for (int j = 0; j < in->nargs; j++) {
                count_use(f, in->args[j], uses);
            }
            if (in->fs != NULL) {
                lu_fs_each_once(f, in->fs, ++f->marks, count_use, uses);
            }
        }
    }
    return uses;
}

/* Whether the compare v's result goes to the flags only: its one reader
   is the instruction after it, a BRANCH or a GUARD_BIT, which then tests
   the condition itself. The flags must survive to it: no count or stress
   code runs before it. */
static int
flags_only(struct asm_ *a, lu_v v)
{
    if (a->next == LU_NOV || a->uses[v] != 1 || a->unit->counts != NULL) {
        return 0;
    }
    struct lu_ins *n = I(a, a->next);
    if (n->a != v) {
        return 0;
    }
    return n->op == OP_BRANCH
        || (n->op == OP_GUARD_BIT && !(a->f->stress && !(n->flags & INS_NOSTRESS)));
}

/* After a compare that set the flags to condition cc: the bit, unless the
   next instruction reads the flags instead (flags_only). */
static void
def_cond(struct asm_ *a, lu_v v, int cc)
{
    if (flags_only(a, v)) {
        a->flags_of = v;
        a->flags_cc = cc;
        return;
    }
    int loc = I(a, v)->loc;
    setcc(a, cc, RAX);
    movzx8(a, loc >= 0 && loc < LU_LOC_FPR ? loc : RAX, RAX);
    if (!(loc >= 0 && loc < LU_LOC_FPR)) {
        def_gpr(a, v, RAX);
    }
}

/* A constant operand that fits a sign-extended imm32. */
static int
imm32_of(struct asm_ *a, lu_v v, int32_t *imm)
{
    struct lu_ins *in = I(a, v);
    if ((in->op == OP_CONST_I || in->op == OP_CONST) && in->imm >= INT32_MIN && in->imm <= INT32_MAX) {
        *imm = (int32_t)in->imm;
        return 1;
    }
    return 0;
}

/* The register v's result is made in: its own, unless it is spilled or
   is the register of `avoid` (an operand read after the result is
   written), else RAX. */
static int
result_gpr(struct asm_ *a, lu_v v, lu_v avoid)
{
    int loc = I(a, v)->loc;
    if (loc >= 0 && loc < LU_LOC_FPR
        && (avoid == LU_NOV || I(a, avoid)->op == OP_CONST_I || I(a, avoid)->op == OP_CONST || I(a, avoid)->loc != loc)) {
        return loc;
    }
    return RAX;
}

/* cmp a, b, with b an immediate when it can be: the condition code of
   `op` (cond_of) as the flags then say it. */
static int
cmp_operands(struct asm_ *a, lu_v x, lu_v y, int op)
{
    int32_t imm;
    if (imm32_of(a, y, &imm)) {
        alu_ri(a, ALU_CMP, gpr_of(a, x, RAX), imm);
        return cond_of(op, 0);
    }
    if (imm32_of(a, x, &imm)) {
        alu_ri(a, ALU_CMP, gpr_of(a, y, RAX), imm);
        return cond_of(op, 1);
    }
    int r = gpr_of(a, x, RAX);
    alu_rr(a, ALU_CMP, r, gpr_of(a, y, RCX));
    return cond_of(op, 0);
}

/* To `label` when the object in r keeps its attributes in a dict. Only a
   word found absent needs this: values that move to a dict leave every
   word NULL for good (attrs.c move_to_dict), and an index below the
   class's count is in every room the class gave, so a word that holds a
   value is in line, for a store under the object's lock (or serial)
   always, for a load as of its read. An instance that came by __class__
   assignment has another class's room, and its values in a dict: code
   for a class that may have one tests first (LU_LOAD_FLAG_FIRST,
   LU_STORE_FLAG_FIRST), and so does every unit built while threads run
   (skips_flag): one built before the class was marked could be handed
   such an instance by another thread before it notices it is dead. */
static void
in_dict_jump(struct asm_ *a, int r, int label)
{
    rex(a, 0, 0, 0, r);
    b1(a, 0xF6);
    modrm_mem(a, 0, r, offsetof(PyObject, ob_flags) + 1);
    b1(a, LU_ATTRS_IN_DICT >> 8);               /* test byte [r+flags+1], LU_ATTRS_IN_DICT */
    jcc(a, CC_NE, label);
}

/* Whether an inline load may read its word without the in-dict test
   first: built while serial (INS_UNLOCKED, facts.c), and for a class no
   instance has come to by assignment (first_bit clear). One the unit may
   have died before checks that it lives first, and leaves at `fail`. */
static int
skips_flag(struct asm_ *a, struct lu_ins *in, int fail, int64_t first_bit)
{
    if (in->flags & INS_RECHECK) {
        jump_if_dead(a, exit_to(a, in->fs, EXIT_POLL, 0, 0));      /* a dead unit is no failed guard (store_locks) */
    }
    return (in->flags & (INS_FRESH | INS_UNLOCKED)) && !(in->imm2 & first_bit);
}

/* The raw word of the word at in->imm of the object in r, into v. */
static void
raw_into(struct asm_ *a, struct lu_ins *in, lu_v v, int r, int is_int)
{
    int loc = in->loc;                          /* read straight into the value's register */
    if (is_int) {
        int t = loc >= 0 && loc < LU_LOC_FPR ? loc : RAX;
        load(a, t, r, (int32_t)in->imm + 8);
        def_gpr(a, v, t);
    }
    else {
        int x = loc >= LU_LOC_FPR ? loc - LU_LOC_FPR : 0;
        sse_rm(a, 0xF2, 0x10, x, r, (int32_t)in->imm + 8);     /* movsd, the binary64 */
        def_fpr(a, v, x);
    }
}

/* INLINE_LOAD_F and _I: an inline value as binary64 (int64): an unboxed
   value's raw word, or an exact float's (a compact int's) value, read only
   when the owning thread holds the object (as INLINE_LOAD takes a
   reference only then).

   Under a name whose first number is the load's (lucifer.h), the word
   decides, whatever the name holds now: the marker, or an exact float
   (an exact int of up to two digits), has its value in the raw word; nothing
   leaves (absent); any other object leaves, at a guard whose failures
   the site counts; a wider int is read from the object as below. The
   object's type and tag are read without a reference: an object freed
   meanwhile stays readable until this thread's next quiescent point
   (qsbr), and whatever it reads as only picks the raw word, which holds
   a value stored under the name. */
static void
inline_load_raw(struct asm_ *a, struct lu_ins *in, lu_v v)
{
    int is_int = in->op == OP_INLINE_LOAD_I;
    int fail = guard_exit(a, in);
    int boxed = new_label(a), done = new_label(a), typed = new_label(a), held = new_label(a);
    int r = gpr_of(a, in->a, RDX);
    int after = -1;
    if (!skips_flag(a, in, fail, LU_LOAD_FLAG_FIRST)) {
        in_dict_jump(a, r, fail);               /* maybe another class's room (lucifer.h lu_inline_foreign) */
    }
    if (in->b == LU_NOV && lu_field_first(LU_LOAD_KIND(in->imm2)) == (is_int ? LU_FIELD_INT : LU_FIELD_FLOAT)) {
        int other = new_label(a), wide = new_label(a);
        after = new_label(a);
        cmp_mi(a, 1, r, (int32_t)in->imm, (int32_t)(intptr_t)(is_int ? LU_UNBOXED_INT : LU_UNBOXED));
        jcc(a, CC_NE, other);
        raw_into(a, in, v, r, is_int);
        cold_begin(a);
        bind(a, other);
        /* The word again, which a store on another thread may have made
           the marker since. */
        int take = new_label(a);
        load(a, RAX, r, (int32_t)in->imm);
        alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)(is_int ? LU_UNBOXED_INT : LU_UNBOXED));
        jcc(a, CC_E, take);
        alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
        jcc(a, CC_BE, fail);                    /* absent, or no object */
        mov_ri(a, RCX, (int64_t)(intptr_t)(is_int ? &PyLong_Type : &PyFloat_Type));
        cmp_rm(a, RCX, RAX, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, fail);                    /* another kind */
        if (is_int) {
            cmp_mi(a, 1, RAX, offsetof(PyLongObject, long_value.lv_tag), 3 << _PyLong_NON_SIZE_BITS);
            jcc(a, CC_AE, wide);                /* two digits fit int64, so their raw word was written too */
        }
        bind(a, take);
        raw_into(a, in, v, r, is_int);
        jmp(a, after);
        if (!is_int) {
            cold_end(a);
            bind(a, after);
            return;
        }
        bind(a, wide);                          /* the general way below, in the cold code */
    }
    if (in->b != LU_NOV) {
        /* The object the word held is the number, and held already. */
        to_gpr(a, RAX, in->b);
        test_rr(a, RAX, RAX);
        jcc(a, CC_NE, typed);
    }
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
        cold_begin(a);
        bind(a, wide);
        save_volatile(a);
        mov_rr(a, RDI, RAX);
        call_abs(a, (void *)lu_jit_unbox_wide);
        restore_volatile(a);
        mov_ri(a, RCX, INT64_MIN);
        alu_rr(a, ALU_CMP, RAX, RCX);
        jcc(a, CC_E, fail);
        jmp(a, done);
        cold_end(a);
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
    if (after >= 0) {
        jmp(a, after);
        cold_end(a);
        bind(a, after);
    }
}

/* PyMutex_LockFast on the lock byte of the object in r (ob_mutex), as
   CPython's LOCK_OBJECT: `busy` when another thread holds it. Clobbers
   RAX and tmp. */
static void
lock_object(struct asm_ *a, int r, int tmp, int busy)
{
    alu_rr(a, ALU_XOR, RAX, RAX);
    rex(a, 0, 0, 0, tmp);
    b1(a, 0xB0 + (tmp & 7));
    b1(a, 1);                                   /* mov tmp8, 1 */
    b1(a, 0xF0);
    rex(a, 0, tmp, 0, r);
    b1(a, 0x0F);
    b1(a, 0xB0);
    modrm_mem(a, tmp, r, offsetof(PyObject, ob_mutex));     /* lock cmpxchg [r+mutex], tmp8 */
    jcc(a, CC_NE, busy);
}

/* Its release, clobbering RAX and tmp: a waiter parked on it is woken in
   C, a call that clobbers the caller-saved registers (RDI among them), or
   with `keep` every allocated register and RCX and XMM0 kept. */
static void
unlock_object(struct asm_ *a, int r, int tmp, int keep)
{
    int unlocked = new_label(a);
    b1(a, 0xB0);
    b1(a, 1);                                   /* mov al, 1 */
    alu_rr(a, ALU_XOR, tmp, tmp);
    b1(a, 0xF0);
    rex(a, 0, tmp, 0, r);
    b1(a, 0x0F);
    b1(a, 0xB0);
    modrm_mem(a, tmp, r, offsetof(PyObject, ob_mutex));     /* lock cmpxchg [r+mutex], tmp8 */
    jcc(a, CC_E, unlocked);
    if (keep) {
        alu_ri(a, ALU_SUB, RSP, 16);
        store(a, RSP, 0, RCX);
        sse_rm(a, 0xF2, 0x11, 0, RSP, 8);       /* movsd [rsp+8], xmm0 */
        save_volatile(a);
    }
    rex(a, 1, RDI, 0, r);
    b1(a, 0x8D);
    modrm_mem(a, RDI, r, offsetof(PyObject, ob_mutex));     /* lea rdi, [r+mutex] */
    call_abs(a, (void *)PyMutex_Unlock);
    if (keep) {
        restore_volatile(a);
        sse_rm(a, 0xF2, 0x10, 0, RSP, 8);
        load(a, RCX, RSP, 0);
        alu_ri(a, ALU_ADD, RSP, 16);
    }
    bind(a, unlocked);
}

/* Whether a store that locks in line takes the lock: not into an instance
   no other thread can see (INS_FRESH), nor while serial (INS_UNLOCKED,
   runtime/jit/facts.c), when a store or read the unit may have died before
   checks that it lives first and leaves before it, as at a poll: no guard
   failed, and the site must not count one (a unit that dies often would
   make its site generic). */
static int
store_locks(struct asm_ *a, struct lu_ins *in)
{
    if (in->flags & INS_RECHECK) {
        jump_if_dead(a, exit_to(a, in->fs, EXIT_POLL, 0, 0));
    }
    return !(in->flags & (INS_FRESH | INS_UNLOCKED));
}

/* TYPED_LOAD and TYPED_STORE: item b of the bytearray in a, read as native
   items of array typecode imm (runtime/prim/memoryview.c typed_get and
   typed_set), in line under the bytearray's lock, which every resize takes
   too: the storage and the size read under it are the ones the item is
   in. The lock byte is taken with one cmpxchg, the index (negative from
   the end) is tested against the size in items, the item moved and the
   lock released. A lock another thread holds, an index out of range, a
   stored int out of the typecode's range or a 'Q' item past 2**63 leaves
   before anything is stored, for the primitive to do the operation; a
   waiter that parked on the lock meanwhile is woken in C, every register
   kept. Scratch registers only: RAX, RCX, RDX, R11, XMM0, XMM1. */
static void
typed_item(struct asm_ *a, struct lu_ins *in, lu_v v)
{
    int code = (int)in->imm, store_ = in->op == OP_TYPED_STORE, f = code == 'f' || code == 'd';
    int width = code == 'b' || code == 'B' ? 1 : code == 'h' || code == 'H' ? 2
        : code == 'i' || code == 'I' || code == 'f' ? 4 : 8;
    int fail = guard_exit(a, in), out = new_label(a), done = new_label(a);
    int locks = store_locks(a, in);
    int o = gpr_of(a, in->a, RDX), i = gpr_of(a, in->b, RCX);
    int x = store_ && f ? fpr_of(a, in->c, 0) : -1;     /* before RAX holds anything: a constant passes through it */
    if (store_ && !f && code != 'q' && code != 'l') {
        x = gpr_of(a, in->c, R11);
        if (code == 'Q' || code == 'L') {
            test_rr(a, x, x);
            jcc(a, CC_S, fail);                 /* unsigned: below 2**63 as an unboxed int is */
        }
        else if (code == 'B' || code == 'H' || code == 'I') {
            mov_rr(a, RAX, x);
            rex(a, 1, 0, 0, RAX);
            b1(a, 0xC1);
            modrm_rr(a, 5, RAX);
            b1(a, 8 * width);                   /* shr rax, bits */
            jcc(a, CC_NE, fail);
        }
        else {
            op_rr(a, 1, width == 1 ? 0x0FBE : width == 2 ? 0x0FBF : 0x63, RAX, x);  /* movsx rax, x's low bits */
            alu_rr(a, ALU_CMP, RAX, x);
            jcc(a, CC_NE, fail);
        }
    }
    if (locks) {
        lock_object(a, o, R11, fail);
    }
    load(a, RAX, o, offsetof(PyVarObject, ob_size));
    if (width > 1) {
        rex(a, 1, 0, 0, RAX);
        b1(a, 0xC1);
        modrm_rr(a, 5, RAX);
        b1(a, __builtin_ctz(width));            /* shr rax, log2(width): the size in items */
    }
    int positive = new_label(a);
    mov_rr(a, R11, i);
    test_rr(a, R11, R11);
    jcc(a, CC_NS, positive);
    alu_rr(a, ALU_ADD, R11, RAX);               /* negative: from the end */
    bind(a, positive);
    alu_rr(a, ALU_CMP, R11, RAX);
    jcc(a, CC_AE, out);                         /* unsigned: still negative too */
    load(a, RAX, o, offsetof(PyByteArrayObject, ob_start));
    rex(a, 1, RAX, R11, RAX);
    b1(a, 0x8D);
    b1(a, 0x44);
    b1(a, (__builtin_ctz(width) << 6) | ((R11 & 7) << 3) | RAX);
    b1(a, 0);                                   /* lea rax, [rax + r11 * width] */
    if (!store_ && f) {
        sse_rm(a, code == 'd' ? 0xF2 : 0xF3, 0x10, 0, RAX, 0);   /* movsd / movss xmm0, [rax] */
        if (code == 'f') {
            sse_rr(a, 0xF3, 0x5A, 0, 0, 0);     /* cvtss2sd xmm0, xmm0 */
        }
    }
    else if (!store_) {
        static const struct { char code; int w, op; } loads[] = {
            { 'b', 1, 0x0FBE }, { 'B', 0, 0x0FB6 }, { 'h', 1, 0x0FBF }, { 'H', 0, 0x0FB7 }, { 'i', 1, 0x63 },
            { 'I', 0, 0x8B }, { 'l', 1, 0x8B }, { 'L', 1, 0x8B }, { 'q', 1, 0x8B }, { 'Q', 1, 0x8B },
        };
        for (size_t k = 0; k < sizeof loads / sizeof loads[0]; k++) {
            if (loads[k].code == code) {
                op_rm(a, loads[k].w, loads[k].op, RCX, RAX, 0);    /* sign- or zero-extended to 64 bits */
            }
        }
        if (code == 'L' || code == 'Q') {
            test_rr(a, RCX, RCX);
            jcc(a, CC_S, out);                  /* past 2**63: no unboxed int */
        }
    }
    else if (f) {
        if (code == 'f') {
            sse_rr(a, 0xF2, 0x5A, 1, x, 0);     /* cvtsd2ss xmm1, x */
            sse_rm(a, 0xF3, 0x11, 1, RAX, 0);   /* movss [rax], xmm1 */
        }
        else {
            sse_rm(a, 0xF2, 0x11, x, RAX, 0);   /* movsd [rax], x */
        }
    }
    else {
        to_gpr(a, R11, in->c);
        if (width == 2) {
            b1(a, 0x66);
        }
        op_rm(a, width == 8, width == 1 ? 0x88 : 0x89, R11, RAX, 0);    /* mov [rax], r11 (its low bytes) */
    }
    if (locks) {
        unlock_object(a, o, R11, 1);
    }
    jmp(a, done);
    bind(a, out);
    if (locks) {
        unlock_object(a, o, R11, 1);
    }
    jmp(a, fail);
    bind(a, done);
    if (!store_ && f) {
        def_fpr(a, v, 0);
    }
    else if (!store_) {
        def_gpr(a, v, RCX);
    }
}

/* An inline store's word at off of the object in r holds nothing: the
   store takes it in line only if every later word of the class's room
   (imm2 - 1 of them; imm2 0: unknown) is empty too, so that the words keep
   the instance's insertion order (attrs.c's in_order), and the instance
   keeps no order record, which C appends to; else `slow`. */
static void
fresh_word(struct asm_ *a, struct lu_ins *in, int r, int slow)
{
    int64_t words = LU_STORE_WORDS(in->imm2);
    if (words == 0) {
        jmp(a, slow);
        return;
    }
    rex(a, 0, 0, 0, r);
    b1(a, 0xF6);
    modrm_mem(a, 0, r, offsetof(PyObject, ob_flags) + 1);
    b1(a, LU_ATTRS_UNORDERED >> 8);             /* test byte [r+flags+1], LU_ATTRS_UNORDERED */
    jcc(a, CC_NE, slow);
    for (int k = 1; k < words; k++) {
        cmp_mi(a, 1, r, (int32_t)in->imm + (int32_t)(LU_INLINE_STRIDE * sizeof(PyObject *)) * k, 0);
        jcc(a, CC_NE, slow);
    }
}

/* INLINE_STORE_F and _I: lu_inline_store_f (_i) in line. Unlocked, a
   word that holds the marker takes the raw value alone. Otherwise, under
   the owner's lock byte, taken with one cmpxchg, a word that already holds
   a value takes the raw value and its marker; the old value, an object,
   is released after the unlock. Anything else is C's, and leaves when it
   declines. */
static void
inline_store_raw(struct asm_ *a, lu_v v)
{
    struct lu_ins *in = I(a, v);
    int is_int = in->op == OP_INLINE_STORE_I;
    struct move m[2] = { { RDI, in->a, 0, 0 }, is_int ? (struct move){ RSI, in->b, 0, 0 }
                                                      : (struct move){ XMM(0), in->b, 0, 1 } };
    int fail = guard_exit(a, in);
    int fresh = !store_locks(a, in);
    int slow = new_label(a), unlock_slow = new_label(a), done = new_label(a);
    int32_t off = (int32_t)in->imm;
    struct lu_deopt *d = lu_publishes(in) ? published(a, in) : NULL;
    int general = -1;
    if (fresh && !(in->imm2 & (LU_STORE_BY_C | LU_STORE_FLAG_FIRST))) {
        /* Under its own kind (eval_attr.c store_kind), unlocked: a word
           that holds the marker takes the raw value alone; one that holds
           the kind's object takes the raw value and the marker, and the
           object is released. */
        int32_t marker = (int32_t)(intptr_t)(is_int ? LU_UNBOXED_INT : LU_UNBOXED);
        int boxed = new_label(a);
        general = new_label(a);
        int r = gpr_of(a, in->a, RDI);
        load(a, RBX, r, off);                   /* the old value: nothing is live here (IF_CALL) */
        alu_ri(a, ALU_CMP, RBX, marker);
        jcc(a, CC_NE, boxed);
        for (int k = 0; k < 2; k++) {
            if (is_int) {
                store(a, r, off + 8, gpr_of(a, in->b, RSI));
            }
            else {
                sse_rm(a, 0xF2, 0x11, fpr_of(a, in->b, 0), r, off + 8);     /* movsd [r+off+8], the binary64 */
            }
            if (k == 1) {
                rex(a, 1, 0, 0, r);
                b1(a, 0xC7);
                modrm_mem(a, 0, r, off);
                b4(a, marker);                  /* mov qword [r+off], marker: after the raw word */
                decref(a, RBX, in, d, -1);      /* the old value, an object of the kind */
                jmp(a, done);
            }
            if (k == 0) {                       /* the hot stream goes on at done, after the cold code */
                cold_begin(a);
                bind(a, boxed);
                test_rr(a, RBX, RBX);
                jcc(a, CC_E, general);          /* a name new to the instance */
            }
        }
        bind(a, general);
    }
    parallel_moves(a, m, 2);
    mov_rr(a, R12, RDI);                        /* kept across calls: nothing is live here (IF_CALL) */
    if (is_int) {
        mov_rr(a, R13, RSI);
    }
    else {
        movq_rx(a, R13, 0);
    }
    if (in->imm2 & LU_STORE_BY_C) {
        jmp(a, slow);                           /* the name's kind is C's to keep (attrs.c) */
    }
    if (!fresh || (in->imm2 & LU_STORE_FLAG_FIRST)) {
        in_dict_jump(a, RDI, slow);             /* maybe another class's room (lucifer.h lu_inline_foreign) */
    }
    if (!fresh) {
        lock_object(a, RDI, RCX, slow);
    }
    load(a, RBX, RDI, off);                     /* the old value, kept across calls */
    int have = new_label(a);
    test_rr(a, RBX, RBX);
    jcc(a, CC_NE, have);
    in_dict_jump(a, RDI, unlock_slow);
    fresh_word(a, in, RDI, unlock_slow);        /* a name new to the instance */
    bind(a, have);
    store(a, RDI, off + 8, R13);                /* the raw value */
    mov_ri(a, RAX, (int64_t)(intptr_t)(is_int ? LU_UNBOXED_INT : LU_UNBOXED));
    store(a, RDI, off, RAX);                    /* after the raw word: x86 stores stay in order */
    if (!fresh) {
        unlock_object(a, RDI, RCX, 0);
    }
    alu_ri(a, ALU_CMP, RBX, (int32_t)(intptr_t)LU_UNBOXED_INT);
    jcc(a, CC_BE, done);                        /* no object to release */
    decref(a, RBX, in, d, -1);     /* the old value */
    jmp(a, done);
    bind(a, unlock_slow);
    if (!fresh) {
        unlock_object(a, RDI, RCX, 0);
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
    if (general >= 0) {
        jmp(a, done);
        cold_end(a);
    }
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
        cold_begin(a);
        bind(a, wide);                          /* more digits: in C, leaving past 64 bits */
        save_volatile(a);
        mov_rr(a, RDI, r);
        call_abs(a, (void *)lu_jit_unbox_wide);
        restore_volatile(a);
        mov_ri(a, RCX, INT64_MIN);
        alu_rr(a, ALU_CMP, RAX, RCX);
        jcc(a, CC_E, fail);
        jmp(a, done);
        cold_end(a);
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
            def_gpr(a, v, RCX);
            bind(a, done);
            return 0;
        }
        emit_box(a, in);
        def_gpr(a, v, RCX);
        return 0;
    case OP_ADD_I: case OP_SUB_I: case OP_MUL_I: case OP_AND_I: case OP_OR_I: case OP_XOR_I: {
        /* The exit first: under stress it emits a dec, which would clobber OF.
           The result is made in its own register, which no value the exit
           reads holds (the allocator gives it to nothing live here), with a
           constant operand as an immediate. */
        int fail = in->op == OP_ADD_I || in->op == OP_SUB_I || in->op == OP_MUL_I ? guard_exit(a, in) : -1;
        lu_v xv = in->a, yv = in->b;
        int32_t imm;
        if (in->op != OP_SUB_I && !imm32_of(a, yv, &imm) && imm32_of(a, xv, &imm)) {
            xv = in->b;
            yv = in->a;
        }
        int d = result_gpr(a, v, yv), konst = imm32_of(a, yv, &imm);
        to_gpr(a, d, xv);
        r = konst ? -1 : gpr_of(a, yv, RCX);
        if (in->op == OP_MUL_I && konst) {
            rex(a, 1, d, 0, d);
            b1(a, 0x69);
            modrm_rr(a, d, d);
            b4(a, imm);                         /* imul d, d, imm32 */
        }
        else if (in->op == OP_MUL_I) {
            op_rr(a, 1, 0x0FAF, d, r);          /* imul d, r */
        }
        else {
            int alu = in->op == OP_ADD_I ? ALU_ADD : in->op == OP_SUB_I ? ALU_SUB
                    : in->op == OP_AND_I ? ALU_AND : in->op == OP_OR_I ? ALU_OR : ALU_XOR;
            if (konst) {
                alu_ri(a, alu, d, imm);
            }
            else {
                alu_rr(a, alu, d, r);
            }
        }
        if ((in->op == OP_ADD_I || in->op == OP_SUB_I) && d == I(a, xv)->loc) {
            /* The result took x's register: an overflow takes x back for the exit. */
            int undo = new_label(a);
            jcc(a, CC_O, undo);
            cold_begin(a);
            bind(a, undo);
            int back = in->op == OP_ADD_I ? ALU_SUB : ALU_ADD;
            if (konst) {
                alu_ri(a, back, d, imm);
            }
            else {
                alu_rr(a, back, d, r);
            }
            jmp(a, fail);
            cold_end(a);
        }
        else if (in->op == OP_ADD_I || in->op == OP_SUB_I || in->op == OP_MUL_I) {
            jcc(a, CC_O, fail);
        }
        def_gpr(a, v, d);
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
        int loc = in->loc;
        x = loc >= LU_LOC_FPR ? loc - LU_LOC_FPR : 0;  /* made in its own register, as the ints are */
        to_fpr(a, x, in->a);
        y = fpr_of(a, in->b, 1);
        if (in->op == OP_DIV_F) {
            int fail = guard_exit(a, in), ok = new_label(a);
            sse_rr(a, 0x66, 0x57, 15, 15, 0);   /* xorpd xmm15, xmm15 */
            sse_rr(a, 0x66, 0x2E, y, 15, 0);    /* ucomisd y, 0 */
            jcc(a, CC_P, ok);
            jcc(a, CC_E, fail);
            bind(a, ok);
        }
        sse_rr(a, 0xF2, ops[in->op], x, y, 0);
        def_fpr(a, v, x);
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
        def_cond(a, v, cmp_operands(a, in->a, in->b, (int)in->imm));
        return 0;
    case OP_CMP_F: {
        int op = (int)in->imm;
        x = fpr_of(a, in->a, 0);
        y = fpr_of(a, in->b, 1);
        if (op == Py_LT || op == Py_LE) {
            sse_rr(a, 0x66, 0x2E, y, x, 0);
            def_cond(a, v, op == Py_LT ? CC_A : CC_AE);
            return 0;
        }
        else if (op == Py_GT || op == Py_GE) {
            sse_rr(a, 0x66, 0x2E, x, y, 0);
            def_cond(a, v, op == Py_GT ? CC_A : CC_AE);
            return 0;
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
        def_cond(a, v, cmp_operands(a, in->a, in->b, Py_EQ));
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
        cold_begin(a);
        bind(a, other);
        /* imm 1: only a layout's own truth, which runs no Python; any
           other leaves. */
        int err = in->imm == 1 ? exit_to(a, in->fs, EXIT_GUARD, 0, 0)
                               : exit_to(a, in->fs, EXIT_ERROR, 1, in->drop);
        struct lu_deopt *d = in->imm != 1 ? a->exits[a->nexits - 1].d : NULL;
        kept_begin(a, in, d);
        mov_rr(a, RDI, r);
        call_abs(a, in->imm == 1 ? (void *)lu_truth_exact : (void *)lu_jit_truth);
        kept_end(a, d);
        b1(a, 0x83);
        b1(a, 0xF8);
        b1(a, 0xFF);                            /* cmp eax, -1 */
        jcc(a, CC_E, err);
        jmp(a, done);
        cold_end(a);
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
    case OP_GUARD_KIND: {
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        test_rr(a, r, r);
        jcc(a, CC_E, fail);
        load(a, RAX, r, offsetof(PyObject, ob_type));
        load(a, RAX, RAX, offsetof(PyTypeObject, tp_flags));
        mov_ri(a, RCX, in->imm);
        alu_rr(a, ALU_AND, RAX, RCX);
        alu_rr(a, ALU_CMP, RAX, RCX);
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
        if (a->flags_of == in->a) {
            jcc(a, in->imm ? a->flags_cc ^ 1 : a->flags_cc, fail);     /* the compare's condition */
            return 0;
        }
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
            jump_if_dead(a, seen);
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
        x = result_gpr(a, v, LU_NOV);
        if (in->imm2 == 4) {
            load32(a, x, r, (int32_t)in->imm);
        }
        else {
            load(a, x, r, (int32_t)in->imm);
        }
        def_gpr(a, v, x);
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
        if (!(in->flags & INS_OWNED)) {
            /* Borrowed, while serial (opt.c borrow_items): no other task
               changes the list, so an index below its size finds the array
               and an item in it, which the list keeps; nothing is counted.
               A unit that may have died checks first (facts.c). */
            int fail = guard_exit(a, in);
            store_locks(a, in);
            int s = gpr_of(a, in->a, RDX), i = gpr_of(a, in->b, RCX);
            cmp_rm(a, i, s, offsetof(PyVarObject, ob_size));
            jcc(a, CC_AE, fail);                /* unsigned: a negative index too */
            if (in->imm) {
                load_indexed8(a, RAX, s, i, offsetof(PyTupleObject, ob_item));
                test_rr(a, RAX, RAX);
                jcc(a, CC_E, fail);             /* a tuple not filled yet: the interpreter's */
            }
            else {
                load(a, RAX, s, offsetof(PyListObject, ob_item));
                load_indexed8(a, RAX, RAX, i, 0);
            }
            def_gpr(a, v, RAX);
            return 0;
        }
        struct move m[2] = { { RDI, in->a, 0, 0 }, { RSI, in->b, 0, 0 } };
        int fail = guard_exit(a, in), slow = new_label(a), done = new_label(a);
        int s = gpr_of(a, in->a, RDX), i = gpr_of(a, in->b, RCX);
        cmp_rm(a, i, s, offsetof(PyVarObject, ob_size));
        jcc(a, CC_AE, slow);                    /* unsigned: a negative index too */
        if (in->imm) {
            int shared = new_label(a);
            load_indexed8(a, RAX, s, i, offsetof(PyTupleObject, ob_item));
            test_rr(a, RAX, RAX);
            jcc(a, CC_E, fail);                 /* a tuple not filled yet: the interpreter's */
            incref_owned_or_exit(a, RAX, shared);
            jmp(a, done);
            bind(a, shared);
            b1(a, 0xF0);
            rex(a, 1, 0, 0, RAX);
            b1(a, 0x83);
            modrm_mem(a, ALU_ADD, RAX, offsetof(PyObject, ob_ref_shared));
            b1(a, 1 << _Py_REF_SHARED_SHIFT);   /* lock add qword [rax+shared], 1 */
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
        }
        cold_begin(a);
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
        jmp(a, done);
        cold_end(a);
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
    case OP_SLOT_LOAD: {
        /* LOAD_ATTR_SLOT in line: the word read without a lock, and the
           reference taken in place for an immortal value or one the running
           thread owns, which no other thread frees; another thread's value
           is C's (lu_try_xget_ref), every register kept. An empty slot
           leaves, for the interpreter to raise. */
        int fail = guard_exit(a, in), slow = new_label(a), done = new_label(a);
        r = gpr_of(a, in->a, RDX);
        load(a, RAX, r, (int32_t)in->imm);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        incref_owned_or_exit(a, RAX, slow);
        cold_begin(a);
        bind(a, slow);
        save_volatile(a);
        rex(a, 1, RDI, 0, r);
        b1(a, 0x8D);
        modrm_mem(a, RDI, r, (int32_t)in->imm);     /* lea rdi, [r+imm] */
        call_abs(a, (void *)lu_jit_slot_load);
        restore_volatile(a);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        jmp(a, done);
        cold_end(a);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_STR_CMP1: {
        /* str_cmp against the str of one code point: an empty str is
           below it; else its first character decides, and on a tie its
           length. Read as STR_ORD reads, from a compact str's data. */
        static const struct { int kind, ascii; int32_t disp; } kinds[] = {
            { 1, 1, sizeof(PyASCIIObject) }, { 1, 0, sizeof(PyCompactUnicodeObject) },
            { 2, 0, sizeof(PyCompactUnicodeObject) }, { 4, 0, sizeof(PyCompactUnicodeObject) },
        };
        int fail = guard_exit(a, in), got = new_label(a), less = new_label(a), more = new_label(a);
        int done = new_label(a);
        int s = gpr_of(a, in->a, RDX);
        load(a, R11, s, offsetof(PyASCIIObject, length));
        test_rr(a, R11, R11);
        jcc(a, CC_E, in->imm2 ? more : less);
        load32(a, RAX, s, offsetof(PyASCIIObject, state));
        alu_ri(a, ALU_AND, RAX, str_state(7, 1));
        for (int k = 0; k < 4; k++) {
            int next = new_label(a);
            alu_ri(a, ALU_CMP, RAX, str_state(kinds[k].kind, kinds[k].ascii));
            jcc(a, CC_NE, k < 3 ? next : fail);
            if (kinds[k].kind == 4) {
                load32(a, RCX, s, kinds[k].disp);
            }
            else {
                op_rm(a, 0, kinds[k].kind == 1 ? 0x0FB6 : 0x0FB7, RCX, s, kinds[k].disp);   /* movzx ecx, [s+data] */
            }
            jmp(a, got);
            bind(a, next);
        }
        bind(a, got);
        alu_ri(a, ALU_CMP, RCX, (int32_t)in->imm);
        jcc(a, CC_B, in->imm2 ? more : less);
        jcc(a, CC_A, in->imm2 ? less : more);
        alu_ri(a, ALU_CMP, R11, 1);
        jcc(a, CC_A, in->imm2 ? less : more);  /* the same character, then more of them */
        alu_rr(a, ALU_XOR, RAX, RAX);
        jmp(a, done);
        bind(a, less);
        mov_ri(a, RAX, -1);
        jmp(a, done);
        bind(a, more);
        mov_ri(a, RAX, 1);
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
    case OP_TYPED_LOAD: case OP_TYPED_STORE:
        typed_item(a, in, v);
        return 0;
    case OP_LIST_STORE: {
        /* STORE_SUBSCR_LIST_INT in line, as CPython locks the list: its
           lock byte taken with one cmpxchg, an index below the size, the
           value's reference taken, and the old value released after the
           unlock. A negative or too large index leaves; a contended lock
           is lu_jit_list_store's. */
        struct move m[3] = { { RDI, in->a, 0, 0 }, { RSI, in->b, 0, 0 }, { RDX, in->c, 0, 0 } };
        int fail = guard_exit(a, in), slow = new_label(a), out = new_label(a), done = new_label(a);
        int locks = store_locks(a, in);
        struct lu_deopt *d = published(a, in);
        parallel_moves(a, m, 3);
        if (locks) {
            lock_object(a, RDI, RCX, slow);
        }
        cmp_rm(a, RSI, RDI, offsetof(PyVarObject, ob_size));
        jcc(a, CC_AE, out);                     /* unsigned: a negative index too */
        load(a, R11, RDI, offsetof(PyListObject, ob_item));
        load_indexed8(a, RBX, R11, RSI, 0);     /* the old value, kept across calls (IF_CALL) */
        incref(a, RDX);
        store_indexed8(a, R11, RSI, 0, RDX);    /* a release on x86: readers take it without the lock */
        if (locks) {
            unlock_object(a, RDI, RCX, 0);
        }
        decref(a, RBX, in, d, -1);     /* the old value */
        jmp(a, done);
        bind(a, out);
        if (locks) {
            unlock_object(a, RDI, RCX, 0);
        }
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
        int fresh = !store_locks(a, in);
        int slow = new_label(a), unlock_slow = new_label(a), immortal = new_label(a);
        int done = new_label(a);
        int32_t off = (int32_t)in->imm;
        struct lu_deopt *d = lu_publishes(in) ? published(a, in) : NULL;
        parallel_moves(a, m, 2);
        mov_rr(a, R12, RDI);                    /* kept across calls: nothing is live here (IF_CALL) */
        mov_rr(a, R13, RSI);
        if (in->imm2 & LU_STORE_BY_C) {
            jmp(a, slow);                       /* the name's kind is C's to keep (attrs.c) */
        }
        if (!fresh || (in->imm2 & LU_STORE_FLAG_FIRST)) {
            in_dict_jump(a, RDI, slow);         /* maybe another class's room (lucifer.h lu_inline_foreign) */
        }
        if (!fresh) {
            lock_object(a, RDI, RCX, slow);
        }
        /* Under a name whose raw words hold ints (floats), the value's
           raw word into RAX, stored with it: an int of one digit (an exact
           float) in line, a wider int C's; any other value is C's while
           the name is of that kind, and under one that takes anything is
           stored with the raw word as it was (RAX reloaded). */
        int plain = new_label(a);
        int other = in->imm2 & LU_STORE_RAW_OR_PLAIN ? plain : unlock_slow;
        if (in->imm2 & LU_STORE_INT_RAW) {
            int positive = new_label(a);
            mov_ri(a, R11, (int64_t)(intptr_t)&PyLong_Type);
            cmp_rm(a, R11, RSI, offsetof(PyObject, ob_type));
            jcc(a, CC_NE, other);
            load(a, RCX, RSI, offsetof(PyLongObject, long_value.lv_tag));
            alu_ri(a, ALU_CMP, RCX, 2 << _PyLong_NON_SIZE_BITS);
            jcc(a, CC_AE, unlock_slow);         /* more than one digit: C's */
            load32(a, RAX, RSI, offsetof(PyLongObject, long_value.ob_digit));
            rex(a, 0, 0, 0, RCX);
            b1(a, 0xF6);
            modrm_rr(a, 0, RCX);
            b1(a, 2);                           /* test cl, 2: negative */
            jcc(a, CC_E, positive);
            unary(a, 3, RAX);
            bind(a, positive);
        }
        else if (in->imm2 & LU_STORE_FLOAT_RAW) {
            mov_ri(a, R11, (int64_t)(intptr_t)&PyFloat_Type);
            cmp_rm(a, R11, RSI, offsetof(PyObject, ob_type));
            jcc(a, CC_NE, other);
            load(a, RAX, RSI, offsetof(PyFloatObject, ob_fval));   /* the bits */
        }
        if (in->imm2 & LU_STORE_RAW_OR_PLAIN) {
            int raw_done = new_label(a);
            jmp(a, raw_done);
            bind(a, plain);
            load(a, RAX, RDI, off + 8);         /* the raw word, unchanged */
            bind(a, raw_done);
        }
        load(a, RBX, RDI, off);                 /* the old value, kept across calls */
        int have = new_label(a), take = new_label(a);
        test_rr(a, RBX, RBX);
        jcc(a, CC_NE, have);
        in_dict_jump(a, RDI, unlock_slow);
        fresh_word(a, in, RDI, unlock_slow);    /* a name new to the instance */
        jmp(a, take);
        bind(a, have);
        if (!(in->imm2 & LU_STORE_OBJECTS)) {
            alu_ri(a, ALU_CMP, RBX, (int32_t)(intptr_t)LU_UNBOXED_INT);
            jcc(a, CC_A, take);
            alu_rr(a, ALU_XOR, RBX, RBX);       /* an unboxed value: no object to release */
        }
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
        if (in->imm2 & (LU_STORE_INT_RAW | LU_STORE_FLOAT_RAW)) {
            store(a, RDI, off + 8, RAX);        /* the raw word, before the value word (attrs.c's order) */
        }
        store(a, RDI, off, RSI);
        if (!fresh) {
            unlock_object(a, RDI, RCX, 0);
        }
        test_rr(a, RBX, RBX);
        jcc(a, CC_E, done);                     /* a new name: nothing to release */
        decref(a, RBX, in, d, -1);     /* the old value */
        jmp(a, done);
        bind(a, unlock_slow);
        if (!fresh) {
            unlock_object(a, RDI, RCX, 0);
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
           word at imm holds a value (in line, in_dict_jump), and the owning
           thread takes the reference in place. */
        int fail = guard_exit(a, in);
        r = gpr_of(a, in->a, RDX);
        if (!skips_flag(a, in, fail, LU_LOAD_FLAG_FIRST)) {
            in_dict_jump(a, r, fail);           /* maybe another class's room (lucifer.h lu_inline_foreign) */
        }
        int unboxed = new_label(a), done = new_label(a);
        int64_t class_value = LU_LOAD_CLASS_VALUE(in->imm2);
        int objects = LU_LOAD_KIND(in->imm2) == LU_FIELD_OBJECT;  /* no marker under an object name (attrs.c) */
        load(a, RAX, r, (int32_t)in->imm);
        test_rr(a, RAX, RAX);
        if (class_value != 0) {
            /* None of its own, while its values are still in line (a move to
               the dict sets the flag before it clears the words, attrs.c):
               the class's value, immortal (eval_attr.c). */
            int own = new_label(a);
            jcc(a, CC_NE, own);
            in_dict_jump(a, r, fail);
            mov_ri(a, RAX, class_value);
            jmp(a, done);
            bind(a, own);
        }
        else {
            jcc(a, CC_E, fail);
        }
        if (!objects) {
            alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
            jcc(a, CC_BE, unboxed);
        }
        /* The reference, a step of its own. */
        if (in->flags & INS_OWNED) {
            incref_owned_or_exit(a, RAX, fail);
        }
        else {
            owned_or_exit(a, RAX, fail);        /* borrowed (opt.c): only this thread frees it */
        }
        if (objects) {
            bind(a, done);
            def_gpr(a, v, RAX);
            return 0;
        }
        cold_begin(a);
        bind(a, unboxed);
        /* An unboxed value: a cached int is its own box (attrs.c stores one
           as the marker); any other's box is made once in C and kept by the
           owner, and making it may wait for the owner's lock. */
        int made = new_label(a);
        alu_ri(a, ALU_CMP, RAX, (int32_t)(intptr_t)LU_UNBOXED_INT);
        jcc(a, CC_NE, made);
        load(a, RAX, r, (int32_t)in->imm + 8);
        small_int_box(a, RAX, made);
        mov_rr(a, RAX, RCX);
        jmp(a, done);
        bind(a, made);
        if (in->flags & INS_LEAVES) {
            jmp(a, fail);       /* values held without a count live across: no park (opt.c borrow_items) */
        }
        parked_begin(a, in);
        if (r != RDI) {
            mov_rr(a, RDI, r);
        }
        rex(a, 1, RSI, 0, RDI);
        b1(a, 0x8D);
        modrm_mem(a, RSI, RDI, (int32_t)in->imm);      /* lea rsi, [rdi+imm]: the word */
        call_abs(a, (in->flags & INS_OWNED) ? (void *)lu_inline_get : (void *)lu_inline_peek);
        parked_end(a, in);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);
        jmp(a, done);
        cold_end(a);
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
        if (!skips_flag(a, in, fail, LU_LOAD_FLAG_FIRST)) {
            in_dict_jump(a, r, fail);
        }
        load(a, RAX, r, (int32_t)in->imm);
        test_rr(a, RAX, RAX);
        jcc(a, CC_E, fail);                     /* absent, or in a dict (in_dict_jump) */
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
        int64_t index = in->imm2 & 0xffffffff, entry = 16 * index;
        r = gpr_of(a, in->a, RDX);
        test_rr(a, r, r);
        jcc(a, CC_E, fail);
        load(a, RAX, r, offsetof(PyObject, ob_type));
        cmp_mi(a, 0, RAX, offsetof(PyTypeObject, tp_version_tag), (int32_t)in->imm);
        jcc(a, CC_NE, fail);
        load(a, R11, r, (int32_t)(in->imm2 >> 32));     /* the dict, where the version's class keeps it */
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
        cmp_mi(a, 1, R11, offsetof(PyDictKeysObject, dk_nentries), (int32_t)index);
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
    case OP_ENTRY_BORROW: {
        /* opt.c borrow_items: an OSR entry's value goes uncounted where it
           is its holder's item, which keeps it: the container's item before
           index c (its last item without c), or that item's tuple item at
           byte imm2. Anything else is a failed guard whose failures make
           the entry count the value (jit.c NOBORROW). */
        int fail = guard_exit(a, in);
        a->exits[a->nexits - 1].d->borrow = 1;
        store_locks(a, in);
        int e = gpr_of(a, in->a, RDX), done = new_label(a);
        alu_rr(a, ALU_XOR, RAX, RAX);
        test_rr(a, e, e);
        jcc(a, CC_E, done);                     /* an unbound local: nothing to hold */
        int c = gpr_of(a, in->b, RCX);
        mov_ri(a, R11, (int64_t)(intptr_t)(in->imm ? &PyTuple_Type : &PyList_Type));
        cmp_rm(a, R11, c, offsetof(PyObject, ob_type));
        jcc(a, CC_NE, fail);
        if (in->c == LU_NOV) {
            load(a, RAX, c, offsetof(PyVarObject, ob_size));
        }
        else {
            int i = gpr_of(a, in->c, RAX);
            if (i != RAX) {
                mov_rr(a, RAX, i);
            }
        }
        alu_ri(a, ALU_SUB, RAX, 1);
        cmp_rm(a, RAX, c, offsetof(PyVarObject, ob_size));
        jcc(a, CC_AE, fail);                    /* unsigned: below 0 too */
        if (in->imm) {
            load_indexed8(a, RAX, c, RAX, offsetof(PyTupleObject, ob_item));
        }
        else {
            load(a, R11, c, offsetof(PyListObject, ob_item));
            load_indexed8(a, RAX, R11, RAX, 0);
        }
        if (in->imm2 >= 0) {
            test_rr(a, RAX, RAX);
            jcc(a, CC_E, fail);
            mov_ri(a, R11, (int64_t)(intptr_t)&PyTuple_Type);
            cmp_rm(a, R11, RAX, offsetof(PyObject, ob_type));
            jcc(a, CC_NE, fail);
            mov_ri(a, R11, (int64_t)((in->imm2 - (int64_t)offsetof(PyTupleObject, ob_item)) / 8));
            cmp_rm(a, R11, RAX, offsetof(PyVarObject, ob_size));
            jcc(a, CC_AE, fail);
            load(a, RAX, RAX, (int32_t)in->imm2);
        }
        alu_rr(a, ALU_CMP, RAX, e);
        jcc(a, CC_NE, fail);
        bind(a, done);
        def_gpr(a, v, RAX);
        return 0;
    }
    case OP_DECREF:
        if (lu_publishes(in)) {
            /* A release that ran C checks the frames there, so the check
               after it (GUARD_FRAMES) may go (facts.c). */
            struct lu_deopt *d = published(a, in);
            decref(a, gpr_of(a, in->a, RDX), in, d, exit_to(a, in->fs, EXIT_FRAME, 0, 0));
        }
        else {
            decref(a, gpr_of(a, in->a, RDX), in, NULL, -1);
        }
        return 0;
    case OP_RITER_DONE:
        r = gpr_of(a, in->a, RDX);
        cmp_mi(a, 1, r, offsetof(rangeiterobject, len), 0);
        def_cond(a, v, CC_LE);
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
        if ((h->fx & FX_RAISE) && in->fs != NULL && (h->rep == REP_BIT || h->rep == REP_OBJ) && !(in->flags & INS_CAUGHT)) {
            err = exit_to(a, in->fs, EXIT_ERROR, 1, in->drop);
            a->exits[a->nexits - 1].d->catchable = 1;
        }
        struct lu_deopt *pub = NULL;
        if (lu_publishes(in)) {
            pub = err >= 0 ? a->exits[a->nexits - 1].d : published(a, in);
        }
        int inline_ok = in->imm == H_build_tuple || in->imm == H_build_list ? emit_build(a, v)
            : in->imm == H_new_object ? emit_new_object(a, v, err) : 0;
        int lacks = in->imm == H_dict_lacks || in->imm == H_str_char ? new_label(a) : -1;
        if (in->imm == H_str_char) {
            /* A latin-1 code point's str is the runtime's singleton, which
               is immortal: no call, and no reference to take. */
            int r = gpr_of(a, in->args[0], RDX);
            mov_rr(a, RAX, r);
            op_rr(a, 1, 0x69, RAX, RAX);
            b4(a, (int32_t)sizeof(_PyRuntime.singletons.latin1[0]));     /* imul rax, rax, size */
            mov_ri(a, R11, (int64_t)(intptr_t)&_PyRuntime.singletons.latin1[0]);
            alu_rr(a, ALU_ADD, RAX, R11);
            alu_ri(a, ALU_CMP, r, 255);
            jcc(a, CC_BE, lacks);               /* unsigned */
        }
        else if (lacks >= 0) {
            /* An instance with no dict of its own lacks every name: 1
               without the call. */
            load(a, RAX, gpr_of(a, in->args[0], RDX), LU_MANAGED_DICT_OFFSET);
            test_rr(a, RAX, RAX);
            b1(a, 0xB8);
            b4(a, 1);                           /* mov eax, 1: the flags stay */
            jcc(a, CC_E, lacks);
        }
        if (!inline_ok) {
            call_helper(a, v, pub);
        }
        if (lacks >= 0) {
            bind(a, lacks);
        }
        if (err >= 0 && h->rep == REP_BIT) {
            b1(a, 0x83);
            b1(a, 0xF8);
            b1(a, 0xFF);                        /* cmp eax, -1 */
            jcc(a, CC_E, err);
        }
        else if (err >= 0 && h->rep == REP_OBJ && inline_ok != 2) {   /* 2: the result checked */
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
        int merge = new_label(a), slow = new_label(a);
        int64_t version = (int64_t)a->f->unit->version;
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
        jcc(a, CC_NE, slow);
        cold_begin(a);
        bind(a, slow);
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
        if (in->flags & INS_LEAVES) {
            jmp(a, poll);       /* values held without a count live across: the interpreter does it */
        }
        else {
            alu_ri(a, ALU_AND, RCX, (int32_t)~(LU_POLL_EXPLICIT_MERGE | LU_POLL_GC_SCHEDULED));
            alu_rr(a, ALU_CMP, RCX, R11);
            jcc(a, CC_NE, poll);
            parked_call(a, v, (void *)lu_poll_in_place, NULL, 0);
            jmp(a, top);
        }
        cold_end(a);
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
        jump_if_dead(a, exit_to(a, in->fs, EXIT_POLL, 0, 0));
        return 0;
    }
    case OP_COUNT_OUT: {
        /* A call the budget left out of line, counted: the one its site
           was armed for leaves, for jit.c to look at the unit's sites. */
        stress_exit(a, in);
        int hot = exit_to(a, in->fs, EXIT_HOT, 0, 0);
        mov_ri(a, R11, (int64_t)(intptr_t)&a->unit->outs[in->imm].left);
        rex(a, 1, 0, 0, R11);
        b1(a, 0xFF);
        modrm_mem(a, 0, R11, 0);                    /* inc qword [r11] */
        jcc(a, CC_E, hot);
        return 0;
    }
    case OP_DEOPT:
        jmp(a, exit_to(a, in->fs, (int)in->imm, 0, 0));
        return 0;
    case OP_UNLINK:
        /* lu_frame_unlink; for a generator lu_gen_finish's state as well,
           so that the releases see it cleared (clear_gen_frame), and
           lu_gen_finish then releases only. */
        load(a, R11, R15, offsetof(struct lu_frame, previous));
        store(a, R14, offsetof(PyThreadState, current_frame), R11);
        if (f->code->co_flags & LU_CO_GENERATORS) {
            const int32_t gen = -(int32_t)offsetof(struct lu_gen, frame);
            const int32_t prev = gen + (int32_t)offsetof(struct lu_gen, exc_state.previous_item);
            store8_mi(a, R15, gen + (int32_t)offsetof(struct lu_gen, frame_state), LU_GEN_CLEARED);
            load(a, R11, R15, prev);
            store(a, R14, offsetof(PyThreadState, exc_info), R11);
            store_mi(a, R15, prev, 0);
        }
        /* lu_depth_leave, before the releases, as lu_frame_drop: their finalizers get the depth back. */
        if (lu_frame_is_kernel(f->code)) {
            alu_mi(a, 1, ALU_ADD, R14, offsetof(struct lu_task, kernel_room), f->code->co_framesize);
        }
        else {
            incdec32(a, 0, R14, offsetof(PyThreadState, py_recursion_remaining));
        }
        return 0;
    case OP_FRAME_STORE:
        r = gpr_of(a, in->a, RAX);
        store(a, R15, (int32_t)(offsetof(struct lu_frame, localsplus) + 8 * in->imm), r);
        return 0;
    case OP_YIELD: {
        lu_codeunit *start = lu_code_start(f->code);
        to_gpr(a, RCX, in->a);
        load(a, RAX, RBP, RESULT_SLOT);
        store(a, RAX, 0, RCX);
        rex(a, 1, RAX, 0, R15);
        b1(a, 0x8D);
        modrm_mem(a, RAX, R15, (int32_t)(offsetof(struct lu_frame, localsplus) + 8 * in->imm));
        store(a, R15, offsetof(struct lu_frame, stackpointer), RAX);
        mov_ri(a, RAX, (int64_t)(intptr_t)(start + in->imm2));
        store(a, R15, offsetof(struct lu_frame, instr), RAX);
        mov_ri(a, RAX, (int64_t)(intptr_t)(start + in->src_pc));
        store(a, R15, offsetof(struct lu_frame, last), RAX);
        op_rm(a, 0, 0x0FB6, RAX, R15, offsetof(struct lu_frame, home));
        op_rm(a, 0, 0x88, RAX, R15, offsetof(struct lu_frame, owner));
        mov_ri(a, RAX, LU_JIT_YIELD);
        jmp(a, a->epilogue);
        return 0;
    }
    case OP_RETURN:
        to_gpr(a, RCX, in->a);
        load(a, RAX, RBP, RESULT_SLOT);
        store(a, RAX, 0, RCX);
        op_rm(a, 0, 0x0FB6, RAX, R15, offsetof(struct lu_frame, home));
        op_rm(a, 0, 0x88, RAX, R15, offsetof(struct lu_frame, owner));      /* its owner's again: lu_gen_finish's, a generator's */
        if (!(f->code->co_flags & LU_CO_GENERATORS)) {
            frame_end(a, in->imm ? LU_JIT_RAISED : LU_JIT_RETURN);           /* imm: the exception set, no value */
            jmp(a, a->epilogue);
        }
        else {
            mov_ri(a, RAX, in->imm ? LU_JIT_RAISED : LU_JIT_RETURN);
            jmp(a, a->end);
        }
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

/* Whether the object in r may lose a reference here without being freed
   (immortal, or this thread's with a local count above one, the count
   left in `count`; RAX the thread's id): else `end`, or `other` for
   another thread's object. Clobbers the flags. */
static void
spare_ref(struct asm_ *a, int r, int count, int end, int other)
{
    int ok = new_label(a);
    load32(a, count, r, offsetof(PyObject, ob_ref_local));
    cmp32_ri8(a, count, -1);
    jcc(a, CC_E, ok);                           /* immortal */
    cmp_rm(a, RAX, r, offsetof(PyObject, ob_tid));
    jcc(a, CC_NE, other);
    cmp32_ri8(a, count, 1);
    jcc(a, CC_BE, end);
    bind(a, ok);
}

/* As spare_ref, and another thread's object with deferred reference
   counting (code objects, top-level functions and methods): its shared
   count carries LU_REF_DEFERRED, so dropping one reference from it can
   reach neither zero nor the merge, and only a collection, with the world
   stopped, takes the bias off. `count` is 0 for such an object. */
static void
spare_shared(struct asm_ *a, int r, int count, int end)
{
    int ok = new_label(a), other = new_label(a);
    spare_ref(a, r, count, end, other);
    int past = new_label(a);
    jmp(a, past);
    bind(a, other);
    rex(a, 0, 0, 0, r);
    b1(a, 0xF6);
    modrm_mem(a, 0, r, offsetof(PyObject, ob_gc_bits));
    b1(a, LU_GC_DEFERRED);                      /* test byte [r+ob_gc_bits], LU_GC_DEFERRED */
    jcc(a, CC_E, end);
    mov_ri(a, count, 0);
    bind(a, ok);
    bind(a, past);
}

/* The reference spare_ref found, given back. */
static void
drop_spare(struct asm_ *a, int r, int count)
{
    int immortal = new_label(a);
    cmp32_ri8(a, count, -1);
    jcc(a, CC_E, immortal);
    incdec32(a, 1, r, offsetof(PyObject, ob_ref_local));
    bind(a, immortal);
}

/* The reference spare_shared found, given back: from the shared count
   with a locked subtraction when it is another thread's. */
static void
drop_shared(struct asm_ *a, int r, int count)
{
    int done = new_label(a), mine = new_label(a);
    test_rr(a, count, count);
    jcc(a, CC_NE, mine);
    b1(a, 0xF0);
    rex(a, 1, 0, 0, r);
    b1(a, 0x83);
    modrm_mem(a, 5, r, offsetof(PyObject, ob_ref_shared));
    b1(a, 1 << _Py_REF_SHARED_SHIFT);           /* lock sub qword [r+ob_ref_shared], one reference */
    jmp(a, done);
    bind(a, mine);
    drop_spare(a, r, count);
    bind(a, done);
}

/* A root's return ends its frame (lu_frame_end) when nothing it gives
   back can be freed: no frame object, the frame not its datastack
   chunk's first, the function's and code's references spare (another
   thread's deferred ones on a cold path). Everything is tested before
   anything moves; else `end`, where C ends the frame once the unit has
   left (epilogue). RAX is the result r after. Clobbers RCX, RDX, R8,
   R11 and the flags. */
static void
frame_end(struct asm_ *a, int r)
{
    int slow = new_label(a), other = new_label(a), ended = new_label(a);
    cmp_mi(a, 1, R15, offsetof(struct lu_frame, frame_obj), 0);
    jcc(a, CC_NE, slow);
    load(a, R11, R14, offsetof(PyThreadState, datastack_chunk));
    lea(a, R11, R11, offsetof(_PyStackChunk, data));
    alu_rr(a, ALU_CMP, R11, R15);
    jcc(a, CC_E, slow);
    thread_id(a, RAX);
    load(a, RDX, R15, offsetof(struct lu_frame, func));
    spare_ref(a, RDX, RCX, slow, other);
    load(a, R11, R15, offsetof(struct lu_frame, code));
    spare_ref(a, R11, R8, slow, other);
    drop_spare(a, RDX, RCX);
    drop_spare(a, R11, R8);
    bind(a, ended);
    store(a, R14, offsetof(PyThreadState, datastack_top), R15);
    mov_ri(a, RAX, r);
    cold_begin(a);
    bind(a, other);
    load(a, RDX, R15, offsetof(struct lu_frame, func));
    spare_shared(a, RDX, RCX, slow);
    load(a, R11, R15, offsetof(struct lu_frame, code));
    spare_shared(a, R11, R8, slow);
    drop_shared(a, RDX, RCX);
    drop_shared(a, R11, R8);
    jmp(a, ended);
    bind(a, slow);
    mov_ri(a, RAX, r);
    jmp(a, a->end);
    cold_end(a);
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

/* The gate, the first thing either entry does (RSP as the saves left it,
   R14 the task): a serial unit stores the task's unlocked word, kept in
   the activation for its end, before it reads its death (the switch to
   parallel either sees the word or is seen: runtime/thread/task.c); the
   poll word is the version the unit was built under with no event
   pending, a kernel unit's visible ones excepted as at its polls; the C
   stack is above the floor. Else it returns 0 with nothing done
   (a->decline). Clobbers RAX and R11 only. */
static void
gate(struct asm_ *a)
{
    int decline = a->decline;
    int64_t version = (int64_t)a->unit->version;
    if (a->serial) {
        load(a, R11, R14, (int32_t)offsetof(struct lu_task, unlocked_jit));
        store(a, RBP, ACT_AT(unlocked_jit), R11);
        store_mi(a, R14, (int32_t)offsetof(struct lu_task, unlocked_jit), 1);
        jump_if_dead(a, decline);
    }
    load(a, RAX, R14, offsetof(struct lu_task, carrier));
    cmp_rm(a, RSP, RAX, offsetof(struct lu_carrier, jit_stack_floor));
    jcc(a, CC_B, decline);
    if (a->f->code->co_flags & LU_CO_KERNEL) {
        load(a, RAX, RAX, offsetof(struct lu_carrier, poll));
        alu_ri(a, ALU_AND, RAX, (int32_t)~LU_POLL_VISIBLE);
        mov_ri(a, R11, version);
        alu_rr(a, ALU_CMP, RAX, R11);
    }
    else if (version <= INT32_MAX) {
        cmp_mi(a, 1, RAX, offsetof(struct lu_carrier, poll), (int32_t)version);
    }
    else {
        mov_ri(a, R11, version);
        cmp_rm(a, R11, RAX, offsetof(struct lu_carrier, poll));
    }
    jcc(a, CC_NE, decline);
}

/* The activation linked from the task, past the gate and an entry's
   other declines: from here to the epilogue's unlink the collector sees
   it (jit.c "Freeing dead units"). Clobbers R11. */
static void
link_activation(struct asm_ *a)
{
    mov_ri(a, R11, (int64_t)(intptr_t)a->unit);
    store(a, RBP, ACT_AT(unit), R11);
    load(a, R11, R14, offsetof(struct lu_task, jit_act));
    store(a, RBP, ACT_AT(outer), R11);
    lea(a, R11, RBP, ACT_SLOT);
    store(a, R14, offsetof(struct lu_task, jit_act), R11);
}

static void
prologue(struct asm_ *a)
{
    saves(a);
    mov_rr(a, R14, RDI);
    mov_rr(a, R15, RSI);
    store(a, RBP, RESULT_SLOT, RDX);
    gate(a);
    link_activation(a);
    bind(a, a->entered);
    /* Entries, while the unit counts them (runtime/jit/jit.c), and every
       entry under -X lucifer_jit_stats. */
    int counted = new_label(a);
    mov_ri(a, R11, (int64_t)(intptr_t)&a->unit->entries);
    cmp_mi(a, 1, R11, 0, (int32_t)ENTRIES_SEEN);
    jcc(a, CC_AE, counted);
    rex(a, 1, 0, 0, R11);
    b1(a, 0xFF);
    modrm_mem(a, 0, R11, 0);                    /* inc qword [r11] */
    bind(a, counted);
    if (_PyRuntime.jit.stats) {
        mov_ri(a, R11, (int64_t)(intptr_t)&a->unit->entered);
        b1(a, 0xF0);
        rex(a, 1, 0, 0, R11);
        b1(a, 0xFF);
        modrm_mem(a, 0, R11, 0);                /* lock inc qword [r11] */
    }
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

/* The machine frame popped: the caller's registers back, RSP at the
   return address. */
static void
pops(struct asm_ *a)
{
    rex(a, 1, RSP, 0, RBP);
    b1(a, 0x8D);
    modrm_mem(a, RSP, RBP, -SAVES);             /* lea rsp, [rbp-40] */
    b1(a, 0x41); b1(a, 0x5F);
    b1(a, 0x41); b1(a, 0x5E);
    b1(a, 0x41); b1(a, 0x5D);
    b1(a, 0x41); b1(a, 0x5C);
    b1(a, 0x5B);
    b1(a, 0x5D);
}

/* EAX the result. The activation ends (a serial unit's unlocked word as
   its gate found it), then the return. From the unlink on, the unit's code
   runs in the window where nothing polls, as before its link (jit.c
   "Freeing dead units"). A stale unit's last activation may free it in
   `left`, which stops the world: it leaves by a jump to lu_jit_left with
   its machine frame popped, so that no return address into it is on the
   stack then; lu_jit_left returns EAX to the unit's caller. A decline
   restores the serial word and returns with no activation to end. */
static void
epilogue(struct asm_ *a)
{
    int stale = new_label(a);
    bind(a, a->epilogue);
    load(a, R11, RBP, ACT_AT(outer));
    store(a, R14, offsetof(struct lu_task, jit_act), R11);
    if (a->serial) {
        load(a, R11, RBP, ACT_AT(unlocked_jit));
        store(a, R14, (int32_t)offsetof(struct lu_task, unlocked_jit), R11);
    }
    mov_ri(a, R11, (int64_t)(intptr_t)&a->unit->stale);
    cmp_mi(a, 0, R11, 0, 0);
    jcc(a, CC_NE, stale);
    bind(a, a->restore);
    pops(a);
    b1(a, 0xC3);
    cold_begin(a);
    int tail = new_label(a);
    bind(a, stale);
    mov_ri(a, RCX, 0);                          /* its frame ended */
    jmp(a, tail);
    bind(a, a->end);
    load(a, R11, RBP, ACT_AT(outer));
    store(a, R14, offsetof(struct lu_task, jit_act), R11);
    if (a->serial) {
        load(a, R11, RBP, ACT_AT(unlocked_jit));
        store(a, R14, (int32_t)offsetof(struct lu_task, unlocked_jit), R11);
    }
    mov_rr(a, RCX, R15);                        /* the frame C ends */
    bind(a, tail);
    mov_ri(a, RDI, (int64_t)(intptr_t)a->unit);
    op_rr(a, 0, 0x89, RAX, RSI);                /* mov esi, eax */
    mov_rr(a, RDX, R14);
    pops(a);
    mov_ri(a, R11, (int64_t)(intptr_t)lu_jit_left);
    rex(a, 0, 0, 0, R11);
    b1(a, 0xFF);
    modrm_rr(a, 4, R11);                        /* jmp r11 */
    bind(a, a->decline);
    if (a->serial) {
        load(a, R11, RBP, ACT_AT(unlocked_jit));
        store(a, R14, (int32_t)offsetof(struct lu_task, unlocked_jit), R11);
    }
    mov_ri(a, RAX, LU_JIT_NONE);
    jmp(a, a->restore);
    cold_end(a);
}

/* DEOPT and ERROR name the unit's frame in the result word (jit.h enum
   lu_jit_result). Clobbers RCX. */
static void
name_frame(struct asm_ *a)
{
    load(a, RCX, RBP, RESULT_SLOT);
    store(a, RCX, 0, R15);
}

/* The direct entry (target.h struct lu_jit_direct): RDI the task, RSI
   the function, RDX its arguments, RCX the result's word, R8 the
   arguments that give their references. The gate, then what making the frame needs (a live unit for the
   function's globals and builtins, recursion and datastack room, as
   lu_frame_push), decline with 0 and nothing done. Then the activation is
   linked, the frame pushed and bound as lu_frame_push makes it, the
   prologue run (in line for COPY_FREE_VARS alone, else in C), and the
   unit run from where its own entry has set up the same registers. Its
   offset in the code. */
static int
direct_entry(struct asm_ *a)
{
    struct lu_func *f = a->f;
    PyCodeObject *code = f->code;
    int at = a->n, decline = a->decline;
    saves(a);
    mov_rr(a, R14, RDI);
    gate(a);
    /* A function of the same code with other globals or builtins has units of its own. */
    load(a, R11, RSI, offsetof(PyFunctionObject, func_globals));
    mov_ri(a, RAX, (int64_t)(intptr_t)a->unit->globals);
    alu_rr(a, ALU_CMP, R11, RAX);
    jcc(a, CC_NE, decline);
    load(a, R11, RSI, offsetof(PyFunctionObject, func_builtins));
    mov_ri(a, RAX, (int64_t)(intptr_t)a->unit->builtins);
    alu_rr(a, ALU_CMP, R11, RAX);
    jcc(a, CC_NE, decline);
    int words = code->co_framesize, kernel = lu_frame_is_kernel(code);
    if (kernel) {
        cmp_mi(a, 1, R14, offsetof(struct lu_task, kernel_room), words);
        jcc(a, CC_L, decline);
    }
    else {
        cmp_mi(a, 0, R14, offsetof(PyThreadState, py_recursion_remaining), 0);
        jcc(a, CC_LE, decline);
    }
    load(a, R10, R14, offsetof(PyThreadState, datastack_top));
    test_rr(a, R10, R10);
    jcc(a, CC_E, decline);
    load(a, R11, R14, offsetof(PyThreadState, datastack_limit));
    alu_rr(a, ALU_SUB, R11, R10);
    alu_ri(a, ALU_CMP, R11, 8 * words);
    jcc(a, CC_B, decline);
    link_activation(a);
    /* The frame. */
    lea(a, R11, R10, 8 * words);
    store(a, R14, offsetof(PyThreadState, datastack_top), R11);
    if (kernel) {
        alu_mi(a, 1, ALU_SUB, R14, offsetof(struct lu_task, kernel_room), words);
    }
    else {
        incdec32(a, 1, R14, offsetof(PyThreadState, py_recursion_remaining));
    }
    mov_rr(a, R15, R10);
    store(a, RBP, RESULT_SLOT, RCX);
    mov_ri(a, R11, (int64_t)(intptr_t)code);
    store(a, R15, offsetof(struct lu_frame, code), R11);
    incref(a, R11);                             /* clobbers RAX and RCX only */
    store(a, R15, offsetof(struct lu_frame, func), RSI);
    incref(a, RSI);
    load(a, R11, RSI, offsetof(PyFunctionObject, func_globals));
    store(a, R15, offsetof(struct lu_frame, globals), R11);
    load(a, R11, RSI, offsetof(PyFunctionObject, func_builtins));
    store(a, R15, offsetof(struct lu_frame, builtins), R11);
    store_mi(a, R15, offsetof(struct lu_frame, locals), 0);
    store_mi(a, R15, offsetof(struct lu_frame, frame_obj), 0);
    store_mi(a, R15, offsetof(struct lu_frame, jit_desc), 0);
    store_mi(a, R15, offsetof(struct lu_frame, jit_forced), 0);
    load(a, R11, R14, offsetof(PyThreadState, current_frame));
    store(a, R15, offsetof(struct lu_frame, previous), R11);
    lu_codeunit *instr = lu_code_start(code) + f->entry_pc;
    mov_ri(a, R11, (int64_t)(intptr_t)instr);
    store(a, R15, offsetof(struct lu_frame, instr), R11);
    mov_ri(a, R11, (int64_t)(intptr_t)(instr - 1 - lu_opcode_caches[RESUME]));
    store(a, R15, offsetof(struct lu_frame, last), R11);
    store8_mi(a, R15, offsetof(struct lu_frame, owner), lu_frame_owner_for(code));
    store8_mi(a, R15, offsetof(struct lu_frame, ret), LU_RETURN_PUSH);
    /* The parameters: what an argument gives (R8's bit) is moved, the rest counted. */
    for (int i = 0; i < code->co_argcount; i++) {
        int given = new_label(a);
        load(a, R10, RDX, 8 * i);
        if (i < 64) {
            rex(a, 1, 0, 0, R8);
            b1(a, 0x0F);
            b1(a, 0xBA);
            modrm_rr(a, 4, R8);
            b1(a, i);                           /* bt r8, i */
            jcc(a, CC_B, given);
        }
        incref(a, R10);
        bind(a, given);
        store(a, R15, offsetof(struct lu_frame, localsplus) + 8 * i, R10);
    }
    int nfree = code->co_nfreevars, cells = 0;
    for (int i = 0; code->co_localspluskinds != NULL && i < code->co_nlocalsplus; i++) {
        cells |= (((const unsigned char *)PyBytes_AS_STRING(code->co_localspluskinds))[i] & CO_FAST_CELL) != 0;
    }
    if (cells) {
        /* MAKE_CELL may raise: the prologue runs in C on a frame the
           interpreter can take, every local unbound and the stack at its
           base, and its error is the interpreter's to unwind. */
        int made = new_label(a);
        for (int i = code->co_argcount; i < code->co_nlocalsplus - nfree; i++) {
            store_mi(a, R15, offsetof(struct lu_frame, localsplus) + 8 * i, 0);
        }
        lea(a, R11, R15, offsetof(struct lu_frame, localsplus) + 8 * code->co_nlocalsplus);
        store(a, R15, offsetof(struct lu_frame, stackpointer), R11);
        store(a, R14, offsetof(PyThreadState, current_frame), R15);
        mov_rr(a, RDI, R15);
        call_abs(a, (void *)lu_jit_prologue);
        op_rr(a, 0, 0x85, RAX, RAX);            /* test eax, eax */
        jcc(a, CC_E, made);
        name_frame(a);
        mov_ri(a, RAX, LU_JIT_ERROR);
        jmp(a, a->epilogue);
        bind(a, made);
    }
    else {
        /* The other locals the unit reads at entry are unbound (a loop's
           header at the call's entry reads them all). */
        for (int v = 0; v < f->nins; v++) {
            struct lu_ins *in = &f->ins[v];
            int i = (int)in->imm;
            if (in->op == OP_ENTRY_LOCAL && !(in->flags & INS_DEAD) && i >= code->co_argcount
                && i < code->co_nlocalsplus - nfree) {
                store_mi(a, R15, offsetof(struct lu_frame, localsplus) + 8 * i, 0);
            }
        }
        if (nfree > 0) {
            /* COPY_FREE_VARS, the prologue's only instruction. */
            load(a, R11, RSI, offsetof(PyFunctionObject, func_closure));
            for (int i = 0; i < nfree; i++) {
                load(a, R10, R11, offsetof(PyTupleObject, ob_item) + 8 * i);
                incref(a, R10);
                store(a, R15, offsetof(struct lu_frame, localsplus) + 8 * (code->co_nlocalsplus - nfree + i), R10);
            }
        }
        store(a, R14, offsetof(PyThreadState, current_frame), R15);
    }
    unsigned long *stat = lu_jit_direct_count();
    if (stat != NULL) {
        mov_ri(a, R11, (int64_t)(intptr_t)stat);
        b1(a, 0xF0);
        rex(a, 1, 0, 0, R11);
        b1(a, 0xFF);
        modrm_mem(a, 0, R11, 0);                /* lock inc qword [r11] */
    }
    jmp(a, a->entered);
    return at;
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
    a->deopts_patch = here(a) + 2;
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
    name_frame(a);
    jmp(a, a->epilogue);
}

int
lu_target_emit(struct lu_func *f, struct lu_unit *unit)
{
    struct asm_ A = { 0 }, *a = &A;
    a->f = f;
    a->unit = unit;
    for (int i = 0; i < f->ndeps; i++) {
        a->serial |= f->deps[i].kind == LU_DEP_SERIAL;
    }
    unit->serial = a->serial;
    a->epilogue = new_label(a);
    a->restore = new_label(a);
    a->decline = new_label(a);
    a->end = new_label(a);
    a->entered = new_label(a);
    a->save_all = new_label(a);
    a->restore_all = new_label(a);
    a->flags_of = LU_NOV;
    a->uses = count_uses(f);
    int maxargs = 0;
    for (int v = 0; v < f->nins; v++) {
        if (f->ins[v].op == OP_HELPER && f->ins[v].nargs > maxargs) {
            maxargs = f->ins[v].nargs;
        }
    }
    /* After push rbp the stack is 16-aligned and the five saves leave it
       8 off, so the frame is 8 more than a multiple of 16. */
    int bytes = 8 + ACT_BYTES + 8 * f->nspill + 8 * maxargs;      /* RESULT_SLOT, ACT_SLOT, spills, arguments */
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
            counted(a, lu_count_kind(I(a, bl->ins[i])), R11);
            alloc_count_before(a, bl->ins[i]);
            if (_PyRuntime.jit.dump) {
                fprintf(stderr, "  code +%d v%d\n", a->n, bl->ins[i]);    /* with the unit's address below: a profile by instruction */
            }
            a->next = i + 1 < bl->nins ? bl->ins[i + 1] : LU_NOV;
            if (emit_ins(a, bl->ins[i], next, labels) < 0) {
                free(labels);
                build_free(a);
                return -1;
            }
            if (a->flags_of != bl->ins[i] && I(a, bl->ins[i])->op != OP_BRANCH) {
                a->flags_of = LU_NOV;           /* only the next instruction reads them */
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
        lu_v flags = a->flags_of;
        a->flags_of = LU_NOV;
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
            int cc = CC_NE;
            if (flags == term->a) {
                cc = a->flags_cc;               /* the compare's condition, its bit never made */
            }
            else {
                int r = gpr_of(a, term->a, RAX);
                test_rr(a, r, r);
            }
            if (bl->succ[1] == next) {
                jcc(a, cc, labels[bl->succ[0]]);
            }
            else if (bl->succ[0] == next) {
                jcc(a, cc ^ 1, labels[bl->succ[1]]);
            }
            else {
                jcc(a, cc, labels[bl->succ[0]]);
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
    int direct = lu_jit_call_entry(f->code) == f->entry_pc && lu_jit_direct_nargs(f->code) >= 0
        ? direct_entry(a) : -1;
    if (a->release) {
        release_stub(a);                /* last: every release has been written */
    }
    join_streams(a);
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
    unit->direct = direct >= 0 ? (char *)mem + direct : NULL;
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
