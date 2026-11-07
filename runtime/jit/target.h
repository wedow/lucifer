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
    /* For perf (runtime/interp/perf.c): the stack trampoline, which calls
       its fourth argument with its first three inside a frame of its own
       (NULL: none); the ELF machine; and the DWARF call frame information
       of a CIE (from its id on) and of a function that starts by pushing
       and setting the frame pointer, as compiled units and the trampoline
       do. */
    const uint8_t *trampoline;
    int ntrampoline;
    int elf_machine;
    const uint8_t *cie, *fp_cfi;
    int ncie, nfp_cfi;
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
    DV_NEW_DICT,                /* a virtual empty dict (a **kwargs nothing filled) */
};

struct lu_dv {
    uint8_t kind;
    uint8_t owned;              /* DV_OBJ: compiled code holds a reference for this entry */
    uint8_t maybe_null;         /* DV_BOX_*: NULL when the bit at bitloc (or bitimm) is 0 */
    uint8_t bitimm;
    int32_t loc;
    int32_t bitloc;
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
    EXIT_FRAME,                 /* a walker wrote the frame's locals, or keeps its frame object */
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
    PyCodeObject *blame_code;   /* a guard moved out of a loop: the site it came from */
    int blame_pc;
    int call_pc;                /* lu_jit_frames_push: the innermost frame's instruction during the call */
};

/* A code object's entry for calls from compiled code, the first field of
   its JIT state (co_executors, runtime/jit/jit.c). `entry` is the live
   call-entry unit's direct entry, or NULL:
     int entry(PyThreadState *ts, PyFunctionObject *func, PyObject **args, PyObject **result,
               int given)
   takes `given` positional arguments (borrowed; up to nargs, the rest
   from the function's defaults), pushes and binds the frame
   above the task's current one and runs it. 1: it returned *result and
   was popped; 0: nothing was done (the interpreter's checks at a call
   failed: a poll is pending, a tool or hook is on, no room); else the
   enum lu_jit_result of an exit, with the frame, in *result, left to the
   interpreter (lu_jit_call_finish). */
struct lu_jit_direct {
    void *entry;
    int nargs;                  /* the positional parameters, or -1: never called directly */
};

/* The rest of a direct call whose callee left compiled code with `r`
   (runtime/jit/helpers.c): the interpreter runs the callee's frame and
   any above it, and its value or NULL with an exception comes back. */
PyObject *lu_jit_call_finish(PyThreadState *ts, struct lu_frame *callee, int r);

/* -X lucifer_jit_stats' count of calls compiled code makes into compiled
   code (direct entries), or NULL when the counts are off. */
unsigned long *lu_jit_direct_count(void);

/* A compiled entry into a code object at one bytecode boundary. */
struct lu_unit {
    struct lu_unit *next;
    PyCodeObject *code;         /* whose unit it is (borrowed: the code frees its units) */
    int entry_pc;
    int (*fn)(PyThreadState *ts, struct lu_frame *frame, PyObject **result);
    void *direct;               /* the call entry's direct entry (struct lu_jit_direct), or NULL */
    void *mem;
    size_t size;
    size_t code_bytes;
    struct lu_deopt **deopts;
    struct lu_deopt ***deopt_table;     /* what the exits' common path reads */
    int ndeopts;
    PyObject **consts;
    int nconsts;
    long *stress_counter;       /* -X lucifer_stress=deopt:N: guards fail when it reaches 0 */
    unsigned long entries;      /* up to a bound (jit.c) */
    unsigned long leaves;       /* exits no recompile can remove: side exits of the root frame, errors */
    int loops;                  /* the code has a back edge: an entry may do unbounded work */
    int dead;
    struct lu_dep *deps;        /* what it relies on between polls (runtime/jit/deps.c) */
    int ndeps;
    /* -X lucifer_jit_stats: what the code executed, by kind (lu_count_kind),
       and the next unit on the report's list. */
    unsigned long *counts;
    char *stats_name;
    struct lu_unit *stats_next;
};

/* What -X lucifer_jit_stats counts as compiled code runs it: the census of
   guards, loads and calls per unit. */
#define LU_COUNT_KINDS(X) \
    X(guard_type) X(guard_type_of) X(guard_type_itself) X(guard_keys) X(guard_func) \
    X(guard_is) X(guard_cell) X(guard_global) X(guard_bit) X(guard_not_null) X(guard_other) \
    X(unbox) X(live) X(poll) X(load_type) X(load_version) X(load_size) X(load_other) \
    X(attr_load) X(attr_store) X(global_load) X(item) X(helper) X(incref) X(decref) X(box)
enum lu_count {
#define LU_COUNT_ENUM(name) CNT_##name,
    LU_COUNT_KINDS(LU_COUNT_ENUM)
#undef LU_COUNT_ENUM
    CNT_COUNT
};
extern const char *const lu_count_names[CNT_COUNT];
/* The kind an instruction counts as, or -1. */
int lu_count_kind(const struct lu_ins *in);
/* Kinds that are guards: every guard_* kind and unbox (not polls and checks that the unit lives). */
#define LU_COUNT_IS_GUARD(k) ((k) <= CNT_unbox)

/* Dependencies (runtime/jit/deps.c): the epoch a compile starts at, and
   publishing a unit's (-1: a change since the epoch may have been missed,
   and the unit is not to be entered); then the policy's side (jit.c): a
   dependency's unit dies, and its site keeps its guard in place. */
uint64_t lu_jit_deps_epoch(void);
int lu_jit_deps_publish(struct lu_unit *u, const struct lu_fdep *want, int n, uint64_t epoch0);
void lu_jit_deps_free(struct lu_unit *u);
void lu_jit_unit_kill(struct lu_unit *u);
void lu_jit_nohoist_set(PyCodeObject *code, int pc);
int lu_jit_nohoist(PyCodeObject *code, int pc);

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

/* Around a Python-running helper called from an inlined frame
   (runtime/jit/deopt.c): pushes the inlined frames d names as the
   interpreter would have them during the call, with their locals, read as
   lu_jit_deopt reads them; then pops them again, unless the callee made a
   frame object of one. Kept frames are the ones the next exit (the call's
   error exit or the frames guard after it) hands the interpreter. */
void lu_jit_frames_push(PyThreadState *ts, struct lu_frame *root, struct lu_deopt *d,
                        const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills);
void lu_jit_frames_pop(PyThreadState *ts, struct lu_frame *root);

/* The helpers compiled code calls, by id; runtime/jit/helpers.c gives each
   its result, flags (IF_RAISE: NULL, or -1 for a bit, raised; IF_PY: may
   run Python) and argument count (0: an array and its length). */
#define LU_HELPERS(X) \
    X(box_int) \
    X(box_float) \
    X(decref) \
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
    X(str_concat) \
    X(compare_op) \
    X(setitem) \
    X(delitem) \
    X(list_extend) \
    X(list_extend_seq) \
    X(load_attr) \
    X(load_method) \
    X(store_attr) \
    X(call) \
    X(call_special) \
    X(super_attr) \
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
    X(list_repeat) \
    X(build_list) \
    X(build_tuple) \
    X(new_dict) \
    X(contains) \
    X(list_push) \
    X(build_set) \
    X(build_map) \
    X(set_add) \
    X(list_to_tuple) \
    X(get_iter_seq) \
    X(binary_slice) \
    X(slice_native) \
    X(store_slice) \
    X(call_kw) \
    X(call_ex) \
    X(dict_merge) \
    X(catch) \
    X(push_exc_info) \
    X(pop_except) \
    X(exc_match) \
    X(raise)

enum lu_helper_id {
#define LU_HELPER_ENUM(name) H_##name,
    LU_HELPERS(LU_HELPER_ENUM)
#undef LU_HELPER_ENUM
    H_COUNT
};
extern const struct lu_helper lu_helpers[H_COUNT];

/* Whether the instruction calls C that may run Python, and so publishes
   the root frame's state for walkers while it does (runtime/jit/deopt.c):
   every root local that state names is in memory during the call. */
static inline int
lu_publishes(const struct lu_ins *in)
{
    return in->fs != NULL && !(in->flags & INS_VIRTUAL)
        && ((in->op == OP_HELPER && (lu_helpers[in->imm].flags & IF_PY)) || (in->op == OP_TRUTH && in->imm != 1));
}

/* -X lucifer_jit_stats: how often compiled code takes the C path of an
   operation it otherwise runs in line (runtime/jit/jit.c reports them). */
#define LU_C_PATHS(X) X(list_item) X(tuple_item) X(list_store) X(box_int) X(box_float) X(build_list) X(build_tuple)
enum lu_c_path {
#define LU_C_PATH_ENUM(name) C_PATH_##name,
    LU_C_PATHS(LU_C_PATH_ENUM)
#undef LU_C_PATH_ENUM
    C_PATHS
};
void lu_jit_c_path_count(enum lu_c_path k);
#define LU_JIT_C_PATH(name) do { \
        if (_PyRuntime.jit.stats) { \
            lu_jit_c_path_count(C_PATH_##name); \
        } \
    } while (0)

/* A helper's result that is not an answer: the call needs the interpreter
   (a C stack too deep to recurse on), so compiled code deopts. */
#define LU_JIT_DECLINED ((PyObject *)1)

#endif
