/* The boundary between the optimizing tier's passes and a machine
   (FOUNDATIONS 2.11): runtime/jit/ssa*.c and regalloc.c include only this.
   A target names its allocatable registers and lowers an allocated
   function to code; the unit it fills is arch-neutral, and so is the
   deopt description each exit carries. runtime/jit/x86_64/ is the one
   backend; runtime/jit/stub/ has none, and the tier then runs nothing. */
#ifndef LU_JIT_TARGET_H
#define LU_JIT_TARGET_H

#include "jit/ssa.h"

/* A location: a general register (0 .. 15), a float register (16 .. 31),
   a spill word (LU_LOC_SPILL(k), negative), or none (constants, virtual
   values, values without a use). */
#define LU_LOC_NONE INT32_MIN
#define LU_LOC_FPR 16
#define LU_LOC_SPILL(k) (-1 - (k))
#define LU_LOC_IS_SPILL(loc) ((loc) < 0 && (loc) != LU_LOC_NONE)
#define LU_LOC_SPILL_INDEX(loc) (-1 - (loc))

struct lu_target {
    const char *name;           /* NULL: no backend */
    int ngpr, nfpr;
    const int8_t *gpr;          /* allocatable, in preference order */
    const int8_t *fpr;
};
extern const struct lu_target lu_target;

/* One value of an exit's frame state: where it is and what the frame
   holds for it. */
enum lu_dv_kind {
    DV_NULL,                    /* an unbound local, or a NULL stack entry */
    DV_CONST,                   /* imm: an object the unit keeps */
    DV_OBJ,                     /* an object at loc */
    DV_BOX_I64,                 /* a virtual int: its raw value at loc, or imm when loc is none */
    DV_BOX_F64,                 /* a virtual float: the same */
};

struct lu_dv {
    uint8_t kind;
    uint8_t owned;              /* DV_OBJ: compiled code holds a reference for this entry */
    int32_t loc;
    int32_t vid;                /* the SSA value: one virtual box is built once per exit */
    int64_t imm;
};

struct lu_deopt_frame {
    PyCodeObject *code;
    PyObject *func;
    int pc;
    int nlocals, nstack;
    struct lu_dv *vals;
};

/* Why compiled code left, for the counts. */
enum lu_exit_reason {
    EXIT_GUARD,                 /* a guard failed */
    EXIT_STRESS,                /* -X lucifer_stress=deopt:N failed a guard */
    EXIT_POLL,                  /* the poll word asked */
    EXIT_SIDE,                  /* an instruction the tier does not compile */
    EXIT_ERROR,                 /* an exception */
    EXIT_REASONS
};

struct lu_deopt {
    long *stress;               /* EXIT_STRESS: the counter to reset */
    int nframes;
    struct lu_deopt_frame *frames;      /* outermost first */
    int error;                  /* leave with the exception set */
    int drop;                   /* then pop this many stack entries of the innermost frame */
    int reason;                 /* enum lu_exit_reason */
    int opcode;                 /* the base opcode at the exit, for the counts */
    struct lu_unit *unit;       /* the unit it leaves */
};

/* A compiled entry into a code object at one bytecode boundary. */
struct lu_unit {
    struct lu_unit *next;
    int entry_pc;
    int (*fn)(PyThreadState *ts, struct lu_frame *frame, PyObject **result);
    void *mem;
    size_t size;
    size_t code_bytes;
    struct lu_deopt **deopts;
    struct lu_deopt ***deopt_table;     /* what the exits' common path reads */
    int ndeopts;
    PyObject **consts;
    int nconsts;
    long *stress_counter;       /* -X lucifer_stress=deopt:N: guards fail when it reaches 0 */
    unsigned long entries;
    unsigned long leaves;       /* exits no recompile can remove: side exits of the root frame, errors */
    int loops;                  /* the code has a back edge: an entry may do unbounded work */
    int dead;
};

/* Lowers an allocated function into unit: code, exits, constants. 0, or
   -1 with f->fail set. */
int lu_target_emit(struct lu_func *f, struct lu_unit *unit);
void lu_target_free(struct lu_unit *unit);

/* The exits' common path (runtime/jit/deopt.c), which the code calls with
   the registers it saved: general registers in gpr[0..15], float registers
   in fpr[0..15], and the spill words below `spills`. Rebuilds the frames
   and returns the enum lu_jit_result the unit returns. */
int lu_jit_deopt(PyThreadState *ts, struct lu_frame *frame, struct lu_deopt *d,
                 const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills);

/* The helpers compiled code calls, by id; runtime/jit/helpers.c gives each
   its result, flags (IF_RAISE: NULL, or -1 for a bit, raised; IF_PY: may
   run Python) and argument count (0: an array and its length). */
#define LU_HELPERS(X) \
    X(box_int) \
    X(box_float) \
    X(decref) \
    X(store_local) \
    X(truth) \
    X(list_item) \
    X(list_store) \
    X(attr_load) \
    X(attr_store) \
    X(slot_load) \
    X(slot_store) \
    X(dict_lacks) \
    X(cell_load) \
    X(compare_ideq) \
    X(binary_op) \
    X(compare_op) \
    X(setitem) \
    X(load_attr) \
    X(load_method) \
    X(store_attr) \
    X(call) \
    X(prim) \
    X(make_function) \
    X(set_function_attribute) \
    X(cell_store) \
    X(get_iter) \
    X(for_iter) \
    X(seqiter_next) \
    X(range_iter) \
    X(unary) \
    X(list_append) \
    X(build_list) \
    X(build_tuple) \
    X(contains)

enum lu_helper_id {
#define LU_HELPER_ENUM(name) H_##name,
    LU_HELPERS(LU_HELPER_ENUM)
#undef LU_HELPER_ENUM
    H_COUNT
};
extern const struct lu_helper lu_helpers[H_COUNT];

/* A helper's result that is not an answer: the call needs the interpreter
   (a C stack too deep to recurse on), so compiled code deopts. */
#define LU_JIT_DECLINED ((PyObject *)1)

#endif
