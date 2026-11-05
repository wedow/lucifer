/* Block SSA for the optimizing tier (DESIGN 6.2), after the donor's block
   SSA: a function is blocks of instructions, each instruction defines at
   most one value, phis start blocks, and every instruction that can leave
   compiled code carries the frame state to leave with.

   Frame states follow the donor's BEFORE rule: a state names the values of
   every local and stack entry of each frame at a bytecode boundary that has
   not executed, so a guard that fails hands the interpreter an instruction
   to run as if compiled code had never started it. Nothing it did is
   replayed: guards come before an instruction's first effect, and a pure
   island (an operator whose kernel method folded into unboxed arithmetic)
   restarts from its operator's boundary. A state is values, never
   locations; the backend says where each value lives at each exit.

   Values are objects (REP_OBJ, a PyObject * the code holds a reference to
   or borrows), raw int64 or binary64 (REP_I64, REP_F64), or a condition
   (REP_BIT, 0 or 1). A raw value reaches the interpreter or the heap only
   through a box (BOX_INT, BOX_FLOAT); a box whose every use is a frame
   state, an unbox or a release is virtual and allocates only if a deopt
   needs it, once per exit, so aliases stay one object (DESIGN 4.5). */
#ifndef LU_JIT_SSA_H
#define LU_JIT_SSA_H

#include "interp/frame.h"
#include "jit/jit.h"
#include "interp/code.h"
#include "interp/instrument.h"
#include "interp/specialize.h"
#include "object/dict.h"
#include "opcode.h"

#include <stdio.h>

typedef int32_t lu_v;           /* a value: the index of the instruction that defines it */
#define LU_NOV (-1)

enum lu_rep { REP_NONE, REP_OBJ, REP_I64, REP_F64, REP_BIT };

/* Instruction flags in the table below. */
#define IF_GUARD    0x01        /* may deopt to its frame state */
#define IF_RAISE    0x02        /* may raise: leaves through its frame state with an exception */
#define IF_EFFECT   0x04        /* not removable when unused (a guard that stands for a raise is one) */
#define IF_CALL     0x08        /* calls C: every register is dead across it */
#define IF_PURE     0x10        /* a value of its operands alone */
#define IF_TERM     0x20        /* ends its block */
#define IF_PY       0x40        /* may run Python: the frame must be observable */

/* X(name, result rep, flags) */
#define LU_IR_OPS(X) \
    X(CONST, REP_OBJ, IF_PURE)          /* imm: a PyObject * the unit keeps alive */ \
    X(CONST_I, REP_I64, IF_PURE)        /* imm */ \
    X(CONST_F, REP_F64, IF_PURE)        /* imm: the bits */ \
    X(ENTRY_LOCAL, REP_OBJ, 0)          /* imm: frame->localsplus[imm], borrowed from the slot */ \
    X(ENTRY_STACK, REP_OBJ, 0)          /* imm: the stack entry, owned */ \
    X(PHI, REP_NONE, 0)                 /* args: one input per predecessor; rep set by the builder */ \
    X(UNBOX_INT, REP_I64, IF_GUARD | IF_EFFECT)     /* a: an exact int that fits one digit; kept, since */ \
    X(UNBOX_FLOAT, REP_F64, IF_GUARD | IF_EFFECT)   /* a's known type lets later code fold */ \
    X(BOX_INT, REP_OBJ, IF_CALL)        /* a: i64; a new reference; b: the object it was unboxed from */ \
    X(BOX_FLOAT, REP_OBJ, IF_CALL)      /* a: f64; a new reference; b: the same */ \
    X(I2F, REP_F64, IF_PURE)            /* a: i64, rounded as int.__float__ */ \
    X(ADD_I, REP_I64, IF_GUARD)         /* overflow deopts */ \
    X(SUB_I, REP_I64, IF_GUARD) \
    X(MUL_I, REP_I64, IF_GUARD) \
    X(NEG_I, REP_I64, IF_GUARD) \
    X(ABS_I, REP_I64, IF_GUARD) \
    X(AND_I, REP_I64, IF_PURE) \
    X(OR_I, REP_I64, IF_PURE) \
    X(XOR_I, REP_I64, IF_PURE) \
    X(SHL_I, REP_I64, IF_GUARD | IF_EFFECT)         /* a negative count or a lost bit deopts */ \
    X(SAR_I, REP_I64, IF_GUARD | IF_EFFECT)         /* a negative count deopts */ \
    X(FLOORDIV_I, REP_I64, IF_GUARD | IF_EFFECT)    /* a zero divisor deopts; the interpreter raises */ \
    X(MOD_I, REP_I64, IF_GUARD | IF_EFFECT) \
    X(ADD_F, REP_F64, IF_PURE) \
    X(SUB_F, REP_F64, IF_PURE) \
    X(MUL_F, REP_F64, IF_PURE) \
    X(DIV_F, REP_F64, IF_GUARD | IF_EFFECT)         /* a zero divisor deopts */ \
    X(NEG_F, REP_F64, IF_PURE) \
    X(ABS_F, REP_F64, IF_PURE) \
    X(SQRT_F, REP_F64, IF_GUARD | IF_EFFECT)        /* a negative operand deopts (math.sqrt raises) */ \
    X(CMP_I, REP_BIT, IF_PURE)          /* imm: Py_LT .. Py_GE */ \
    X(CMP_F, REP_BIT, IF_PURE)          /* imm: Py_LT .. Py_GE, false on NaN except != */ \
    X(IS, REP_BIT, IF_PURE)             /* a is b */ \
    X(NOT, REP_BIT, IF_PURE) \
    X(BOOL, REP_OBJ, IF_PURE)           /* a: bit; Py_True or Py_False, immortal */ \
    X(TRUTH, REP_BIT, IF_RAISE | IF_CALL | IF_EFFECT)       /* a: any object, as TO_BOOL decides */ \
    X(GUARD_TYPE, REP_NONE, IF_GUARD)   /* Py_TYPE(a) == imm */ \
    X(GUARD_VERSION, REP_NONE, IF_GUARD)        /* a version == imm; imm2 says which (GV_*) */ \
    X(GUARD_IS, REP_NONE, IF_GUARD)     /* a == imm, or a field of a (GI_*) */ \
    X(GUARD_BIT, REP_NONE, IF_GUARD)    /* a == imm */ \
    X(GUARD_NOT_NULL, REP_NONE, IF_GUARD)       /* a != NULL */ \
    X(LOAD, REP_NONE, 0)                /* *(a + imm), rep set by the builder; a borrowed object */ \
    X(STORE, REP_NONE, IF_EFFECT)       /* *(a + imm) = b, raw */ \
    X(LIST_ITEM, REP_OBJ, IF_GUARD | IF_CALL)   /* a: an exact list, b: i64 index; deopts out of range */ \
    X(LIST_STORE, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL)     /* a[b] = c, borrowing c */ \
    X(ATTR_LOAD, REP_OBJ, IF_GUARD | IF_CALL)   /* a.name from its managed dict: imm version, imm2 entry; c: the name */ \
    X(ATTR_STORE, REP_NONE, IF_RAISE | IF_EFFECT | IF_CALL)     /* a.(name c) = b, borrowing b */ \
    X(GLOBAL_LOAD, REP_OBJ, IF_GUARD | IF_CALL) /* imm: the dict, imm2: keys version << 32 | entry */ \
    X(INCREF, REP_NONE, IF_EFFECT) \
    X(DECREF, REP_NONE, IF_EFFECT | IF_CALL) \
    X(STORE_LOCAL, REP_NONE, IF_EFFECT | IF_CALL)       /* root slot imm takes a's reference */ \
    X(RITER_NEXT, REP_I64, 0)           /* a: an exact range_iterator; the next value, stepping it */ \
    X(RITER_DONE, REP_BIT, 0)           /* a: an exact range_iterator: exhausted */ \
    X(HELPER, REP_NONE, IF_EFFECT | IF_CALL)    /* imm: the helper, args; rep and flags from it */ \
    X(JUMP, REP_NONE, IF_TERM) \
    X(BRANCH, REP_NONE, IF_TERM)        /* a: bit; to succ[0] when 1 */ \
    X(RETURN, REP_NONE, IF_TERM)        /* a: the frame's result, owned */ \
    X(DEOPT, REP_NONE, IF_TERM)         /* leaves unconditionally at fs; imm: the reason */ \
    X(POLL, REP_NONE, IF_GUARD)         /* a back-edge: leaves at fs if the poll word is set */

/* GUARD_VERSION's imm2: whose version. */
enum { GV_TYPE_OF, GV_TYPE_ITSELF, GV_DICT_KEYS, GV_FUNC };
/* GUARD_IS's imm2: the word compared, a itself, or at an offset (low 32
   bits) into a or into a dict's keys table. */
#define GI_FIELD ((int64_t)1 << 40)
#define GI_KEYS_FIELD ((int64_t)2 << 40)
#define GI_MODE(imm2) ((imm2) >> 40)
#define GI_OFFSET(imm2) ((int32_t)((imm2) & 0xffffffff))

enum lu_op {
#define LU_OP_ENUM(name, rep, flags) OP_##name,
    LU_IR_OPS(LU_OP_ENUM)
#undef LU_OP_ENUM
    OP_COUNT
};

extern const char *const lu_op_names[OP_COUNT];
extern const uint8_t lu_op_flags[OP_COUNT];

/* A frame state: the frame `code` at the boundary before the instruction
   at pc (code units), and for an inlined frame, its caller's state, which
   is at the boundary after the call with the call's operands gone. */
struct lu_fs {
    struct lu_fs *caller;
    PyCodeObject *code;
    PyObject *func;
    int pc;
    int nlocals, nstack;
    lu_v *vals;                 /* locals, then the stack bottom up; LU_NOV is NULL */
    uint8_t *owned;             /* per entry: compiled code holds a reference for it */
};

/* Instruction flags kept per instruction. */
enum {
    INS_DEAD = 1,               /* removed */
    INS_VIRTUAL = 2,            /* a box nothing observes: built only at an exit */
    INS_NOSTRESS = 4,           /* a guard after its instruction's effect: deopt:N never fails it */
};

struct lu_ins {
    uint8_t op;
    uint8_t rep;
    uint8_t flags;
    uint8_t drop;               /* IF_RAISE: stack entries of fs the raising instruction consumed */
    int32_t block;
    lu_v a, b, c;
    int64_t imm, imm2;
    struct lu_fs *fs;           /* where a guard or a raise leaves */
    int nargs;
    lu_v *args;                 /* PHI: per predecessor; HELPER: the arguments */
    /* The backend's: the position in the linear order, and the location. */
    int32_t pos;
    int32_t loc;
};

struct lu_block {
    int *ins;                   /* instruction indexes in order, phis first */
    int nins, cap;
    int *preds;
    int npreds, cappreds;
    int succ[2];
    int nsucc;
    int order;                  /* position in the emission order */
    int start, end;             /* linear positions of its first and last instruction */
    int loop_end;               /* for a loop header: the last position of its loop, else -1 */
};

/* Helpers compiled code calls (runtime/jit/helpers.c). */
struct lu_helper {
    void *fn;
    const char *name;
    uint8_t rep;                /* result */
    uint8_t flags;              /* IF_RAISE: a NULL (or -1 for REP_I64/BIT) result raised */
    uint8_t nargs;
};

struct lu_func {
    struct lu_ins *ins;
    int nins, capins;
    struct lu_block *blocks;
    int nblocks, capblocks;
    int *order;                 /* blocks in emission order */
    int norder;
    PyCodeObject *code;
    int entry_pc;
    int stress;                 /* deopt:N is on: guards count */
    PyObject **consts;          /* strong references the unit keeps */
    int nconsts, capconsts;
    char *arena;                /* chunks for states and argument arrays */
    size_t arena_used, arena_cap;
    char **chunks;
    int nchunks;
    const char *fail;           /* why the compile gave up */
    int bailouts[256];          /* side exits planted per base opcode */
    int nspill;                 /* spill words the allocator used */
    PyTypeObject **known;       /* per value: the exact type a guard proved, or NULL */
    int *known_block;           /* and the block of that guard */
    int capknown;
    int ninlined;
    int loops;                  /* back edges built: one entry may run unbounded work */
};

/* Building and editing. */
void *lu_ir_alloc(struct lu_func *f, size_t size);
int lu_ir_block(struct lu_func *f);
lu_v lu_ir_emit(struct lu_func *f, int block, int op, int rep, lu_v a, lu_v b, lu_v c, int64_t imm);
void lu_ir_edge(struct lu_func *f, int from, int to);
lu_v lu_ir_const(struct lu_func *f, int block, PyObject *o);
void lu_ir_free(struct lu_func *f);
void lu_ir_dump(struct lu_func *f, FILE *out);
/* The value v forwards to after simplification. */
static inline struct lu_ins *
lu_ins(struct lu_func *f, lu_v v)
{
    return &f->ins[v];
}

/* The passes, in order (runtime/jit/ssa_*.c). Each returns 0, or -1 with
   f->fail set. */
int lu_ssa_build(struct lu_func *f, struct lu_frame *frame);
int lu_ssa_optimize(struct lu_func *f);
int lu_regalloc(struct lu_func *f);

/* JIT build statistics, for -X lucifer_jit_stats. */
extern unsigned long lu_jit_bailout_counts[256];

#endif
