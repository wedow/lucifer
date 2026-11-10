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
#define IF_SEEN     0x80        /* may release a value whose finalizer runs Python, or park the task:
                                   a walker may see the frame, which publishes its state (lu_publishes) */

/* X(name, result rep, flags) */
#define LU_IR_OPS(X) \
    X(CONST, REP_OBJ, IF_PURE)          /* imm: a PyObject * the unit keeps alive */ \
    X(CONST_I, REP_I64, IF_PURE)        /* imm */ \
    X(CONST_F, REP_F64, IF_PURE)        /* imm: the bits */ \
    X(ENTRY_LOCAL, REP_OBJ, 0)          /* imm: frame->localsplus[imm], borrowed from the slot */ \
    X(ENTRY_STACK, REP_OBJ, 0)          /* imm: the stack entry, owned */ \
    X(ENTRY_PC, REP_I64, 0)             /* the code unit frame->instr names: a resume unit's entry */ \
    X(PHI, REP_NONE, 0)                 /* args: one input per predecessor; rep set by the builder */ \
    X(UNBOX_INT, REP_I64, IF_GUARD | IF_EFFECT)     /* a: an exact int that fits one digit; kept, since */ \
    X(UNBOX_FLOAT, REP_F64, IF_GUARD | IF_EFFECT)   /* a's known type lets later code fold */ \
    X(BOX_INT, REP_OBJ, 0)              /* a: i64; a new reference; b: the object it was unboxed from; c: a bit, NULL when 0 */ \
    X(BOX_FLOAT, REP_OBJ, 0)            /* a: f64; a new reference; b: the same */ \
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
    X(POW_F, REP_F64, IF_GUARD | IF_EFFECT | IF_CALL)   /* a ** b as float.__pow__; deopts where that raises or is complex */ \
    X(CMP_I, REP_BIT, IF_PURE)          /* imm: Py_LT .. Py_GE */ \
    X(CMP_F, REP_BIT, IF_PURE)          /* imm: Py_LT .. Py_GE, false on NaN except != */ \
    X(IS, REP_BIT, IF_PURE)             /* a is b */ \
    X(NOT, REP_BIT, IF_PURE) \
    X(BOOL, REP_OBJ, IF_PURE)           /* a: bit; Py_True or Py_False, immortal; imm 1 (virtual): of NOT a */ \
    X(TRUTH, REP_BIT, IF_RAISE | IF_CALL | IF_EFFECT)       /* a: any object, as TO_BOOL decides */ \
    X(GUARD_TYPE, REP_NONE, IF_GUARD)   /* Py_TYPE(a) == imm */ \
    X(GUARD_VERSION, REP_NONE, IF_GUARD)        /* a version == imm; imm2 says which (GV_*) */ \
    X(GUARD_IS, REP_NONE, IF_GUARD)     /* a == imm, or a field of a (GI_*) */ \
    X(GUARD_BIT, REP_NONE, IF_GUARD)    /* a == imm */ \
    X(GUARD_NOT_NULL, REP_NONE, IF_GUARD)       /* a != NULL */ \
    X(GUARD_DEPTH, REP_NONE, IF_GUARD | IF_EFFECT)  /* the task's py_recursion_remaining >= imm (at entry) */ \
    X(GUARD_FRAMES, REP_NONE, IF_GUARD | IF_EFFECT) /* a call left no inlined frame pushed and the root     \
                                                       compiled code's (runtime/jit/deopt.c); imm 1: and    \
                                                       without a frame object another holds; imm2: the      \
                                                       instruction's first IR instruction */ \
    X(LOAD, REP_NONE, 0)                /* *(a + imm), rep set by the builder; a borrowed object; imm2 4: 32 bits */ \
    X(STORE, REP_NONE, IF_EFFECT)       /* *(a + imm) = b, raw */ \
    X(LIST_ITEM, REP_OBJ, IF_GUARD)    /* a: an exact list (imm 1: tuple), b: i64 index; deopts out of range; publishes on its slow path (parked_call) */ \
    X(LIST_STORE, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN)   /* a[b] = c, borrowing c */ \
    X(STR_ORD, REP_I64, IF_GUARD)       /* a: an exact str, b: i64 index; the code point; deopts out of range */ \
    X(BYTES_ITEM, REP_I64, IF_GUARD)    /* a: an exact bytes, b: i64 index; the byte; deopts out of range */ \
    X(ATTR_LOAD, REP_OBJ, IF_GUARD | IF_CALL)   /* a.name from its managed dict: imm version, imm2 entry; c: the name */ \
    X(ATTR_STORE, REP_NONE, IF_RAISE | IF_EFFECT | IF_CALL)     /* a.(name c) = b, borrowing b */ \
    X(INLINE_STORE, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN) /* a's inline word at byte imm = b, borrowing b;   \
                                                       imm2: 1 + the words after it in the class's room (0: unknown) */ \
    X(INLINE_LOAD, REP_OBJ, IF_GUARD)   /* a's inline value at byte imm, while it keeps them; scratch registers only */ \
    X(INLINE_LOAD_F, REP_F64, IF_GUARD) /* the same as binary64: an unboxed float's, or an exact float's */ \
    X(INLINE_LOAD_I, REP_I64, IF_GUARD) /* the same as int64: an unboxed int's, or a compact int's */ \
    X(INLINE_STORE_F, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN)   /* a's inline word at byte imm = the float b, unboxed */ \
    X(INLINE_STORE_I, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN)   /* the same for the int b */ \
    X(GLOBAL_LOAD, REP_OBJ, IF_GUARD | IF_CALL) /* imm: the dict, imm2: keys version << 32 | entry */ \
    X(INCREF, REP_NONE, IF_EFFECT) \
    X(DECREF, REP_NONE, IF_EFFECT | IF_CALL | IF_SEEN)  /* imm 1: may run a finalizer, published at fs when it has one */ \
    X(RITER_NEXT, REP_I64, 0)           /* a: an exact range_iterator; the next value, stepping it */ \
    X(RITER_DONE, REP_BIT, 0)           /* a: an exact range_iterator: exhausted */ \
    X(HELPER, REP_NONE, IF_EFFECT | IF_CALL)    /* imm: the helper, args; rep and flags from it */ \
    X(JUMP, REP_NONE, IF_TERM) \
    X(BRANCH, REP_NONE, IF_TERM)        /* a: bit; to succ[0] when 1 */ \
    X(RETURN, REP_NONE, IF_TERM)        /* a: the frame's result, owned */ \
    X(UNLINK, REP_NONE, IF_EFFECT)      /* the root frame leaves the stack before its locals go */ \
    X(DEOPT, REP_NONE, IF_TERM)         /* leaves unconditionally at fs; imm: the reason */ \
    X(POLL, REP_NONE, IF_GUARD)         /* a back-edge: leaves at fs if the poll word is set */ \
    X(CHECK_LIVE, REP_NONE, IF_GUARD | IF_EFFECT)   /* leaves at fs, as a poll does, once the unit is dead */

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
    uint8_t ret;                /* how an inlined frame returns when rebuilt: LU_RETURN_FOR_ITER for
                                   the step of a FOR_ITER (ssa_build.c iter_step), else 0 (a push) */
    uint8_t gen;                /* a generator's frame its caller resumed in line: the generator is
                                   the caller's top stack entry (ssa_build.c inline_resume), and this
                                   says how it returns (LU_RETURN_FOR_ITER or _NEXT; 0: not one) */
};

/* Instruction flags kept per instruction. */
enum {
    INS_DEAD = 1,               /* removed */
    INS_VIRTUAL = 2,            /* a box nothing observes: built only at an exit */
    INS_NOSTRESS = 4,           /* a guard after its instruction's effect: deopt:N never fails it; a
                                   Python-running helper whose frames deopt:N never keeps */
    INS_CAUGHT = 8,             /* a raising helper whose NULL result the code branches on: a handler takes it */
    INS_FRESH = 16,             /* an inline store into an instance this code made that no other thread
                                   can have seen yet, of a name it lacks: it takes no lock, releases
                                   nothing and publishes nothing (ssa_opt.c 3c) */
};
/* A virtual object (ssa_opt.c step 3c) is a tuple build, an instance
   allocation, a loop iterator (lu_iter_type) or a phi of such objects,
   flagged INS_VIRTUAL: never made in compiled code, but by an exit that
   names it, once per exit, with the fields it has there
   (lu_object_fields). A tuple's or an iterator's fields are its
   arguments, a phi's the phis of its inputs' fields (its `fields`; its c
   names an input whose shape it has). An instance's fields are the stores
   into it, which stay in place, virtual, so that where they are says
   which fields an exit sees. */

struct lu_ins {
    uint8_t op;
    uint8_t rep;
    uint8_t flags;
    uint8_t drop;               /* IF_RAISE: stack entries of fs the raising instruction consumed */
    int32_t block;
    lu_v a, b, c;
    int64_t imm, imm2;
    struct lu_fs *fs;           /* where a guard or a raise leaves */
    struct lu_fs *origin;       /* a guard moved out of a loop: where it was, which its failure blames */
    PyCodeObject *src;          /* the bytecode it was built for (NULL: the optimizer's), for the counts */
    int src_pc;
    int nfields;                /* a virtual instance: its stores */
    lu_v *fields;
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
    struct lu_fs *entry_fs;     /* a join's state at its start, its phis as values: a loop's guards move before it */
};

/* Helpers compiled code calls (runtime/jit/helpers.c). */
struct lu_helper {
    void *fn;
    const char *name;
    uint8_t rep;                /* result */
    uint8_t flags;              /* IF_RAISE: a NULL (or -1 for REP_I64/BIT) result raised */
    uint8_t nargs;
};

/* A value that is a generator, made by a call of a generator function
   whose code (and function, when it is a constant) the builder knows. */
struct lu_gen_made {
    lu_v v;
    PyCodeObject *code;
    PyFunctionObject *func;
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
    int direct;                 /* the call entry of a code compiled code may call directly */
    int resume;                 /* a generator's resume unit: an entry per resume point (frame->instr picks) */
    int nborrowed;              /* leading parameters the root borrows (ssa_build.c) */
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
    int room;                   /* the recursion budget the deepest inlined frame needs */
    int loops;                  /* back edges built: one entry may run unbounded work */
    int sites, cold;            /* sites with feedback, and those the interpreter never ran */
    struct lu_fdep *deps;       /* the facts the code relies on between polls */
    int ndeps, capdeps;
    int *idom;                  /* per block, once optimized: its immediate dominator (-1: unreached) */
    struct lu_gen_made *gens;   /* values that are generators of a known code (ssa_build.c) */
    int ngens, capgens;
};

/* Building and editing. */
void *lu_ir_alloc(struct lu_func *f, size_t size);
int lu_ir_block(struct lu_func *f);
lu_v lu_ir_emit(struct lu_func *f, int block, int op, int rep, lu_v a, lu_v b, lu_v c, int64_t imm);
void lu_ir_edge(struct lu_func *f, int from, int to);
lu_v lu_ir_const(struct lu_func *f, int block, PyObject *o);
void lu_ir_free(struct lu_func *f);
/* The unit relies on dep between polls (runtime/jit/deps.c). */
void lu_ir_dep(struct lu_func *f, struct lu_fdep *dep);
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

/* Whether instruction s runs before x on every path to x. */
int lu_ssa_before(struct lu_func *f, lu_v s, lu_v x);
/* The fields virtual object v has where instruction `at` runs: their
   values into vals (room for every field) and their places into offs (an
   instance's or an iterator's byte offsets, a tuple's indexes); the count. */
int lu_object_fields(struct lu_func *f, lu_v v, lu_v at, lu_v *vals, int32_t *offs);
/* The instruction whose shape virtual object v has (the build, or a phi's
   input), or LU_NOV when v is not a virtual object. */
lu_v lu_object_shape(struct lu_func *f, lu_v v);
/* The place of field k of an object of shape's kind whose fields are its
   arguments: a tuple's index, an iterator's byte offset. */
int32_t lu_object_place(const struct lu_ins *shape, int k);

/* The type of the loop iterator a listiter_new, tupleiter_new or
   rangeiter_new helper makes (ssa_build.c for_step), else NULL. */
PyTypeObject *lu_iter_type(const struct lu_ins *in);

/* JIT build statistics, for -X lucifer_jit_stats. */
extern unsigned long lu_jit_bailout_counts[256];

#endif
