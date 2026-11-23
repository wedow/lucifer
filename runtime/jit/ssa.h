/* Block SSA for the optimizing tier (docs/design/jit-architecture.md): a
   function is blocks of instructions, each instruction defines at most one
   value, phis start blocks, and every instruction that can leave compiled
   code carries the frame state to leave with.

   Values are objects (REP_OBJ), raw int64 or binary64 (REP_I64, REP_F64),
   or a condition (REP_BIT). The graph holds only what runs: an object the
   evaluator kept virtual (runtime/jit/eval.c) is no instruction. It is a
   record (struct lu_vobj) that frame states name, with its fields, and an
   exit that names it makes it, once (runtime/jit/exit.c).

   A frame state names, per frame, every local and stack entry at a
   bytecode boundary that has not executed, innermost frame first: a guard
   that fails hands the interpreter an instruction to run as if compiled
   code had never started it. An inlined frame is nothing but its state.

   References: an object value flagged INS_OWNED carries one reference of
   compiled code's from its definition until the evaluator releases it
   (DECREF), which it does where the last slot of the state stops naming
   it. A slot holds no reference of its own; an exit gives every slot it
   fills a new one and then drops compiled code's. */
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
/* In a frame state or a virtual object's field, a value may instead name
   a virtual object by its id. Instruction operands never do. */
#define LU_VIRT(id) (-2 - (id))
#define LU_IS_VIRT(r) ((r) <= -2)
#define LU_VIRT_ID(r) (-2 - (r))

enum lu_rep { REP_NONE, REP_OBJ, REP_I64, REP_F64, REP_BIT };

/* Instruction flags in the table below. */
#define IF_GUARD    0x01        /* may deopt to its frame state */
#define IF_RAISE    0x02        /* may raise: leaves through its frame state with an exception */
#define IF_EFFECT   0x04        /* not removable when unused (a guard that stands for a raise is one) */
#define IF_CALL     0x08        /* calls C on its fast path: every register but the preserved is dead across it */
#define IF_PURE     0x10        /* a value of its operands alone */
#define IF_TERM     0x20        /* ends its block */
#define IF_SEEN     0x40        /* its C may let a walker see the frames: it publishes its state */

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
    X(BOX_INT, REP_OBJ, 0)              /* a: i64; a new reference; b: the object it is when not NULL */ \
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
    X(BOOL, REP_OBJ, IF_PURE)           /* a: bit; Py_True or Py_False, immortal */ \
    X(TRUTH, REP_BIT, IF_RAISE | IF_EFFECT)       /* a: any object, as TO_BOOL decides */ \
    X(GUARD_TYPE, REP_NONE, IF_GUARD)   /* Py_TYPE(a) == imm */ \
    X(GUARD_KIND, REP_NONE, IF_GUARD)   /* Py_TYPE(a)->tp_flags has the flags imm */ \
    X(GUARD_VERSION, REP_NONE, IF_GUARD)        /* a version == imm; imm2 says which (GV_*) */ \
    X(GUARD_IS, REP_NONE, IF_GUARD)     /* a == imm, or a field of a (GI_*) */ \
    X(GUARD_BIT, REP_NONE, IF_GUARD)    /* a == imm */ \
    X(GUARD_NOT_NULL, REP_NONE, IF_GUARD)       /* a != NULL */ \
    X(GUARD_DEPTH, REP_NONE, IF_GUARD | IF_EFFECT)  /* the task's py_recursion_remaining >= imm (at entry) */ \
    X(GUARD_FRAMES, REP_NONE, IF_GUARD | IF_EFFECT) /* no walker made the frames real during the instruction's   \
                                                       calls (runtime/jit/exit.c); imm 1: nor holds the root's  \
                                                       frame object, which would take its locals at a return */ \
    X(LOAD, REP_NONE, 0)                /* *(a + imm), rep set by the builder; a borrowed object; imm2 4: 32 bits */ \
    X(STORE, REP_NONE, IF_EFFECT)       /* *(a + imm) = b, raw */ \
    X(LIST_ITEM, REP_OBJ, IF_GUARD)    /* a: an exact list (imm 1: tuple), b: i64 index; deopts out of range; publishes on its slow path (parked_call) */ \
    X(LIST_STORE, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN)   /* a[b] = c, borrowing c */ \
    X(STR_ORD, REP_I64, IF_GUARD)       /* a: an exact str, b: i64 index; the code point; deopts out of range */ \
    X(BYTES_ITEM, REP_I64, IF_GUARD)    /* a: an exact bytes, b: i64 index; the byte; deopts out of range */ \
    X(SLOT_LOAD, REP_OBJ, IF_GUARD)     /* a's __slots__ member at byte imm, a new reference; deopts when it is \
                                           empty */ \
    X(STR_CMP1, REP_I64, IF_GUARD)      /* a: an exact str; -1, 0 or 1 as a compares with the str of code point imm \
                                           (imm2 1: that str with a); deopts for a str not compact */ \
    X(TYPED_LOAD, REP_NONE, IF_GUARD)   /* a: a bytearray, b: i64 index (negative from the end); its item read as \
                                           array typecode imm, i64 or f64 (rep set by the builder); deopts out of \
                                           range, past 2**63, or when another thread holds a's lock */ \
    X(TYPED_STORE, REP_NONE, IF_GUARD | IF_EFFECT)     /* a[b] = c (i64, f64) as typecode imm; deopts as TYPED_LOAD \
                                                          and for a c out of the typecode's range */ \
    X(ATTR_LOAD, REP_OBJ, IF_GUARD)   /* a.name from its managed dict: imm version, imm2 entry; c: the name */ \
    X(ATTR_STORE, REP_NONE, IF_RAISE | IF_EFFECT | IF_CALL)     /* a.(name c) = b, borrowing b */ \
    X(INLINE_STORE, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN) /* a's inline word at byte imm = b, borrowing b;   \
                                                       imm2: 1 + the words after it in the class's room (0: unknown), \
                                                       LU_STORE_BY_C when the name's kind does not take it */ \
    X(INLINE_LOAD, REP_OBJ, IF_GUARD)   /* a's inline value at byte imm, while it keeps them; scratch registers only; \
                                           imm2 (and _F's, _I's): the class value or 0, and LU_LOAD_KIND */ \
    X(INLINE_LOAD_F, REP_F64, IF_GUARD) /* the same as binary64: an unboxed float's, or an exact float's */ \
    X(INLINE_LOAD_I, REP_I64, IF_GUARD) /* the same as int64: an unboxed int's, or an int's; b (both): the word's object as INLINE_ORIGIN read it, whose number it is when not NULL */ \
    X(INLINE_ORIGIN, REP_OBJ, IF_GUARD) /* a's inline value at byte imm as the object it is, a new reference; NULL when it is unboxed */ \
    X(INLINE_STORE_F, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN)   /* a's inline word at byte imm = the float b, unboxed */ \
    X(INLINE_STORE_I, REP_NONE, IF_GUARD | IF_EFFECT | IF_CALL | IF_SEEN)   /* the same for the int b */ \
    X(GLOBAL_LOAD, REP_OBJ, IF_GUARD | IF_CALL) /* imm: the dict, imm2: keys version << 32 | entry */ \
    X(INCREF, REP_NONE, IF_EFFECT) \
    X(DECREF, REP_NONE, IF_EFFECT)    /* imm 1: its type may run code when freed: publishes fs */ \
    X(RITER_NEXT, REP_I64, IF_EFFECT)   /* a: an exact range_iterator; the next value, stepping it */ \
    X(RITER_DONE, REP_BIT, 0)           /* a: an exact range_iterator: exhausted */ \
    X(HELPER, REP_NONE, IF_EFFECT | IF_CALL)    /* imm: the helper, args; rep and effects from it */ \
    X(JUMP, REP_NONE, IF_TERM) \
    X(BRANCH, REP_NONE, IF_TERM)        /* a: bit; to succ[0] when 1 */ \
    X(RETURN, REP_NONE, IF_TERM)        /* a: the frame's result, owned; imm 1: none, the exception set raised it */ \
    X(FRAME_STORE, REP_NONE, IF_EFFECT) /* the root frame's localsplus[imm] = a, with no reference taken */ \
    X(YIELD, REP_NONE, IF_TERM)         /* a generator's root frame yields a (owned): its slots were stored; imm: the  \
                                           slots it holds, imm2: the instruction it resumes at */ \
    X(UNLINK, REP_NONE, IF_EFFECT)      /* the root frame leaves the stack before its locals go */ \
    X(DEOPT, REP_NONE, IF_TERM)         /* leaves unconditionally at fs; imm: the reason */ \
    X(POLL, REP_NONE, IF_GUARD)         /* a back-edge: leaves at fs if the poll word is set */ \
    X(CHECK_LIVE, REP_NONE, IF_GUARD | IF_EFFECT)   /* leaves at fs, as a poll does, once the unit is dead */ \
    X(COUNT_OUT, REP_NONE, IF_GUARD | IF_EFFECT)    /* counts a call the budget left out of line, the unit's   \
                                                       outs[imm]: leaves at fs at the unit's hot_calls (jit.c) */

/* INLINE_STORE[_I/_F]'s imm2 bit: the value is not of a kind the name's
   (attrs.c LU_FIELD_*) takes in line, so C stores it and keeps the kind. */
#define LU_STORE_BY_C ((int64_t)1 << 32)
/* An object store's imm2 bits under an int (float) name: the store takes
   only an exact int that fits (an exact float) in line, and writes its raw
   word beside it; any other value is C's. */
#define LU_STORE_INT_RAW ((int64_t)1 << 33)
#define LU_STORE_FLOAT_RAW ((int64_t)1 << 34)
/* An object store's imm2 bit under an object name: the word it replaces
   holds no marker. */
#define LU_STORE_OBJECTS ((int64_t)1 << 35)
/* With LU_STORE_INT_RAW (_FLOAT_RAW), under a name that takes anything:
   a value of another kind is stored in line, its raw word left as it is. */
#define LU_STORE_RAW_OR_PLAIN ((int64_t)1 << 36)
/* As LU_LOAD_FLAG_FIRST, for a store. */
#define LU_STORE_FLAG_FIRST ((int64_t)1 << 37)
#define LU_STORE_WORDS(imm2) ((imm2) & 0xffffffff)

/* INLINE_LOAD[_I/_F]'s imm2: the class value that stands behind a
   missing word (a pointer, below bit 60), and the kind (LU_FIELD_*) the
   name had when the load was built: which shape of word to expect first.
   The load checks the word it reads whatever the kind is now. */
#define LU_LOAD_KIND_SHIFT 60
#define LU_LOAD_KIND(imm2) ((int)(((uint64_t)(imm2) >> LU_LOAD_KIND_SHIFT) & 7))
/* The class may have instances that came by __class__ assignment
   (lucifer.h lu_inline_foreign): the in-dict flag is tested first. */
#define LU_LOAD_FLAG_FIRST ((int64_t)((uint64_t)1 << 63))
#define LU_LOAD_CLASS_VALUE(imm2) ((imm2) & (((int64_t)1 << LU_LOAD_KIND_SHIFT) - 1))

/* POLL's imm: a loop's back-edge, or a call's start (facts.c may drop it). */
enum { POLL_LOOP, POLL_CALL };
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

/* A virtual object: an object the code made that nothing has yet needed
   to exist. Its kind says how to make it and which operations the
   evaluator answers from its fields. Records are immutable: a change
   makes a new record under the same id (struct lu_bind). */
enum lu_vkind {
    VK_BOX_INT,                 /* f[0]: its value, an i64; f[1] (n 2, 4): the object it is where it came in as one, else NULL;
                                   f[2], f[3] (n 4): the instance and the byte of the inline value it was read from */
    VK_BOX_FLOAT,               /* f[0]: its value, an f64; f[1]: the same */
    VK_BOOL,                    /* f[0]: a bit; neg: True when it is 0 */
    VK_TUPLE,                   /* f[i]: item i */
    VK_SEQITER,                 /* type: the list or tuple iterator's; f[0]: the sequence, f[1]: the index, an i64 */
    VK_DICT,                    /* an exact dict of str keys in order: f[2i] the key (a constant), f[2i+1] its value */
    VK_LIST,                    /* an exact list: f[i] item i */
    VK_METHOD,                  /* a bound method: f[0] its function, f[1] its self */
    VK_FIELDS,                  /* an instance of a kernel type with fields (lu_field_offset): f[i] field i, the
                                   constant NULL while unset */
    VK_INST,                    /* an instance of a class whose attributes are its own (__slots__, or values
                                   inline): f[2i] a name (a constant str), f[2i+1] its value, in the order set */
    VK_EXC,                     /* an exception the unit raised and its handler dropped: type its class, no fields */
    VK_GEN,                     /* a generator: f[GEN_FUNCV] its function where the unit knows it by its code only
                                   (else nothing), f[GEN_FN] the function, or the one that stands for it, a
                                   constant; f[GEN_PC] the code unit its frame resumes at, an i64 (GEN_FINISHED;
                                   GEN_RUNNING while a frame of the state is its frame, and holds what it
                                   holds); f[GEN_DEPTH] its stack's depth there, an i64; then its frame's
                                   locals and its stack's room, nothing where a slot holds nothing */
    VK_SPLICE,                  /* type: an exact list or tuple; f[i] its items, the last an exact tuple whose
                                   items follow (a call's f(a, *args), CALL_FUNCTION_EX's operand) */
    VK_KINDS
};

enum { GEN_FUNCV, GEN_FN, GEN_PC, GEN_DEPTH, GEN_SLOTS };
#define GEN_FINISHED (-1)
#define GEN_RUNNING (-2)

/* Kinds whose object may change once it exists: a record of one says
   nothing after that. */
#define LU_VKIND_MUTABLE(kind) \
    ((kind) == VK_SEQITER || (kind) == VK_DICT || (kind) == VK_LIST || (kind) == VK_FIELDS || (kind) == VK_INST || (kind) == VK_GEN \
     || (kind) == VK_SPLICE)

struct lu_vobj {
    uint8_t kind;               /* enum lu_vkind */
    uint8_t neg;
    int n;
    PyTypeObject *type;         /* the exact type of the object it makes */
    lu_v *f;                    /* values, or other virtual objects */
};

/* What a path knows of virtual object `id` beyond its first record: the
   record it has now, and the value that is the object once the path made
   it (LU_NOV while it is virtual). A real object keeps its record while
   its kind cannot change (a tuple's items stay known); one that can has
   none (obj NULL). */
struct lu_bind {
    int id;
    lu_v real;
    struct lu_vobj *obj;
};

/* A frame state: the frame `code` at the boundary before the instruction
   at pc (code units), and for an inlined frame, its caller's state, which
   is at the boundary after the call with the call's operands gone. The
   state an instruction holds (the innermost frame's) also carries the
   path's virtual objects and the references compiled code holds that no
   slot names (held), which an exit there releases. */
struct lu_fs {
    struct lu_fs *caller;
    PyCodeObject *code;
    PyObject *func;
    lu_v funcv;                 /* the function called, where the unit knows it by its code only (func is then
                                   one of that code's, made for the build); LU_NOV where func is the function */
    int pc;
    int nlocals, nstack;
    lu_v *locals;               /* LU_NOV is NULL. States of one frame whose locals are the same share them */
    lu_v *stack;                /* bottom up */
    uint8_t ret;                /* how an inlined frame returns when rebuilt (enum lu_frame_return) */
    int gen;                    /* the generator whose frame this is, whose body the unit runs in line: 1 + the
                                   id of a virtual one, -1 - the value that is one that exists; 0 for a
                                   call's frame */
    struct lu_bind *binds;
    int nbinds;
    lu_v *held;
    int nheld;
    uint32_t mark;              /* the walk that last saw it as a caller (lu_fs_each_once) */
    uint8_t virt;               /* and whether a slot of it names a virtual object then */
    /* The backend's, for a frame that names no virtual object (so that
       every exit describes it alike): 1 + the unit's record of it under
       the caller record `under` (-1: none), and the locations of the
       owned values it names; -1 for a frame that names one. */
    int32_t record, under;
    int32_t nowned;
    int32_t *owned;
};

/* Slot i of the frame: its locals, then its stack. */
static inline lu_v *
lu_fs_slot(const struct lu_fs *p, int i)
{
    return i < p->nlocals ? &p->locals[i] : &p->stack[i - p->nlocals];
}

/* Instruction flags kept per instruction. */
enum {
    INS_DEAD = 1,               /* removed */
    INS_OWNED = 2,              /* an object value that carries a reference of compiled code's */
    INS_NOSTRESS = 4,           /* a guard after its instruction's effect: deopt:N never fails it */
    INS_CAUGHT = 8,             /* a helper whose raise a handler of the unit takes: NULL is its result */
    INS_PROVEN = 16,            /* a guard of what its path proved already: only a writer takes it back (facts.c) */
    INS_FRESH = 32,             /* an inline store into an instance the code just made, which no other thread can
                                   have seen, of a name it lacks: no lock, nothing released, nothing published */
    INS_UNLOCKED = 64,          /* a store that locks in line, built while serial: no lock; an inline load or
                                   store: no in-dict test before the word (facts.c) */
    INS_RECHECK = 128,          /* such an instruction where the unit may have died: it checks first, and leaves */
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
    PyCodeObject *blame;        /* a guard that speculates for another site (a loop header's): its failure counts there */
    int blame_pc;
    struct lu_prof *prof;       /* a guard on a learned type, or on a report of one (target.h) */
    PyCodeObject *src;          /* the bytecode it was built for (NULL: the optimizer's), for the counts */
    int src_pc;
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

/* What a helper's C may do, which decides what the evaluator must have
   real and what it forgets across the call. */
enum {
    FX_RAISE = 1,               /* a NULL (or -1 for REP_I64/BIT) result raised */
    FX_PY = 2,                  /* may run Python: frames are observable and anything reachable may change */
    FX_SEEN = 4,                /* may release a value of any type, or park: frames are observable */
    FX_ALLOC = 8,               /* allocates */
    FX_DECLINES = 16,           /* may answer LU_JIT_DECLINED with nothing done: the interpreter runs the instruction */
};

/* Helpers compiled code calls (runtime/jit/helpers.c). None keeps a
   reference it is given; an object result is a new reference. */
struct lu_helper {
    void *fn;
    const char *name;
    uint8_t rep;                /* result */
    uint8_t fx;
    uint8_t nargs;              /* 0: an array and its length */
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
    PyTypeObject **known;       /* per value: the exact type its definition gives it, or NULL */
    int capknown;
    struct lu_vobj **vobjs;     /* virtual objects by id: their first records */
    int nvobjs, capvobjs;
    int ninlined;
    uint32_t marks;             /* walks begun with lu_fs_each_once */
    const lu_v *last_locals;    /* the locals of the state that walk saw last, and the walk */
    uint32_t last_mark;
    int loops;                  /* back edges built: one entry may run unbounded work */
    int sites, cold;            /* sites with feedback, and those the interpreter never ran */
    int refused;                /* calls left out of line for want of budget */
    uint64_t *outs;             /* their sites, each once, by lu_jit_site_key: the unit counts the calls made there */
    int nouts, capouts;
    uint64_t *hots;             /* hot sites taken in line past the budget, each once */
    int nhots, caphots;
    int budget;                 /* instructions past which the evaluator inlines no more (jit.c) */
    struct lu_fdep *deps;       /* the facts the code relies on between polls */
    int ndeps, capdeps;
    int *idom;                  /* per block, once optimized: its immediate dominator (-1: unreached) */
};

/* Building and editing. */
/* Zeroed memory that lives as long as f: the common case in line, a new
   chunk out of line. */
void *lu_ir_alloc_chunk(struct lu_func *f, size_t size);
static inline void *
lu_ir_alloc(struct lu_func *f, size_t size)
{
    size = (size + 15) & ~(size_t)15;
    if (f->arena == NULL || f->arena_used + size > f->arena_cap) {
        return lu_ir_alloc_chunk(f, size);
    }
    void *p = f->arena + f->arena_used;
    f->arena_used += size;
    return p;
}
int lu_ir_block(struct lu_func *f);
/* A block of -1 is none: a constant, which runs nowhere. */
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

/* The passes, in order (runtime/jit/eval.c, opt.c, regalloc.c). Each returns 0, or -1 with
   f->fail set. */
int lu_ssa_build(struct lu_func *f, struct lu_frame *frame);
int lu_ssa_optimize(struct lu_func *f);
/* Guards a dependency stands for go (runtime/jit/facts.c). After the order is set. */
int lu_ssa_facts(struct lu_func *f);
int lu_regalloc(struct lu_func *f);

/* A new virtual object record of n fields (zeroed), and a new id for it. */
struct lu_vobj *lu_ir_vobj(struct lu_func *f, int kind, PyTypeObject *type, int n);
int lu_ir_vobj_id(struct lu_func *f, struct lu_vobj *o);

/* What state fs knows of virtual object id: its record (NULL: none, it is
   real and may have changed) and, in *real, the value that is the object
   (LU_NOV while virtual). */
struct lu_vobj *lu_fs_vobj(struct lu_func *f, const struct lu_fs *fs, int id, lu_v *real);

/* Calls fn(f, v, arg) for every instruction value an exit at fs reads:
   its slots, the fields of the virtual objects they reach, the objects
   virtual ones became, and what it holds. A value may come more than once. */
void lu_fs_each(struct lu_func *f, const struct lu_fs *fs, void (*fn)(struct lu_func *, lu_v, void *), void *arg);
/* The same for one of its frames, p, alone: not what fs holds. */
void lu_fs_each_frame(struct lu_func *f, const struct lu_fs *fs, const struct lu_fs *p,
                      void (*fn)(struct lu_func *, lu_v, void *), void *arg);
/* lu_fs_each, for a walk that needs each value once: a caller's frame
   seen before under the same mark (++f->marks begins a walk), and the
   locals of a state whose locals are the last state's of the walk, give
   only what their virtual objects stand for at fs. Inlined code's states
   share their callers' frames, and states of one frame often its locals. */
void lu_fs_each_once(struct lu_func *f, const struct lu_fs *fs, uint32_t mark,
                     void (*fn)(struct lu_func *, lu_v, void *), void *arg);
/* The same for the slots and fields themselves, to rewrite them with fn,
   which gives what it gave before for a value it returned: a caller's
   frame or locals rewritten under the same mark are not again. */
void lu_fs_rewrite(struct lu_func *f, struct lu_fs *fs, uint32_t mark, lu_v (*fn)(lu_v));

/* JIT build statistics, for -X lucifer_jit_stats. */
extern unsigned long lu_jit_bailout_counts[256];

#endif
