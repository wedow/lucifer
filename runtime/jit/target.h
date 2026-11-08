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
    DV_BOX_I64,                 /* a virtual int: its raw value at loc, or imm when loc is none; fields: the
                                   objects (at their offsets) whose attribute it was stored into raw */
    DV_BOX_F64,                 /* a virtual float: the same */
    DV_NEW_DICT,                /* a virtual empty dict (a **kwargs nothing filled) */
    DV_BOOL,                    /* a virtual bool: True when the bit at loc (or imm), xor bitimm, is 1 */
    DV_TUPLE,                   /* a virtual tuple: its items are fields[i] */
    DV_INSTANCE,                /* a virtual instance of the type imm: fields[i] in the inline word at byte fields[i].off */
    DV_ITER,                    /* a virtual loop iterator of the type imm: fields[i] in the word at byte fields[i].off */
    DV_RAW,                     /* a field's raw word: at loc, or imm when loc is none */
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
    int32_t off;                /* a field's place */
    int32_t nfields;            /* DV_TUPLE, DV_INSTANCE, DV_ITER: the fields, each a raw word or a reference the */
    struct lu_dv *fields;       /* object holds (DV_OBJ: owned), which moves into the object made */
};
/* Frees the fields n values describe. */
void lu_jit_dv_free(struct lu_dv *vals, int n);

struct lu_deopt_frame {
    PyCodeObject *code;
    PyObject *func;
    int pc;
    int nlocals, nstack;
    struct lu_dv *vals;
    uint8_t gen;                /* the frame of the generator its caller's top stack entry holds, which
                                   returns to it as this enum lu_frame_return says (0: not one) */
};

/* Why compiled code left, for the counts. */
enum lu_exit_reason {
    EXIT_GUARD,                 /* a guard failed */
    EXIT_STRESS,                /* -X lucifer_stress=deopt:N failed a guard */
    EXIT_POLL,                  /* the poll word asked */
    EXIT_SIDE,                  /* an instruction the tier does not compile */
    EXIT_ERROR,                 /* an exception */
    EXIT_FRAME,                 /* a walker wrote the frame's locals, or keeps its frame object */
    EXIT_YIELD,                 /* a generator yields: its frame is left at the YIELD_VALUE */
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
    int nrelease;               /* references compiled code still holds that no frame takes: released last */
    struct lu_dv *release;
    lu_codeunit *instr;         /* published: the instruction the root frame shows as running (lu_jit_instr) */
    uint8_t regs;               /* published by a slow path that saved every register at the frame's jit_regs */
};

/* A code object's entry for calls from compiled code, the first field of
   its JIT state (co_executors, runtime/jit/jit.c). `entry` is the live
   call-entry unit's direct entry, or NULL:
     int entry(PyThreadState *ts, PyFunctionObject *func, PyObject **args, PyObject **result,
               int given, PyObject *kwnames)
   takes `given` positional arguments (up to nargs), borrowed, pushes and
   binds the frame above the task's current one as the interpreter would
   (defaults, keyword-only parameters, star parameters, the prologue's
   cells and free variables) and runs it. The keyword entry, the
   LU_DIRECT_KW bytes before entry, takes as well one value per name in
   kwnames (a sixth argument; positional callers pay nothing for it). 1: it returned *result and was popped; 0:
   nothing was done (the interpreter's checks at a call failed: a poll is
   pending, a tool or hook is on, no room; or the binding would raise,
   which the slow way then does); else the enum lu_jit_result of an exit,
   with the frame, in *result, left to the interpreter
   (lu_jit_call_finish). */
struct lu_jit_direct {
    void *entry;
    int nargs;                  /* the most positional arguments it takes (LU_DIRECT_ANY), or -1: never called directly */
    int call_pc;                /* where the call entry's unit starts */
};
#define LU_DIRECT_ANY 0x7fffffff
#define LU_DIRECT_KW 8

/* What the direct entry does not bind in line (runtime/jit/helpers.c): a
   frame for func bound to the call's arguments, not yet current, holding
   its first nborrowed parameters borrowed, or NULL with nothing changed. */
struct lu_frame *lu_jit_bind(PyThreadState *ts, PyFunctionObject *func, PyObject *const *args, int given,
                             PyObject *kwnames, int nborrowed);

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
    X(attr_load) X(attr_store) X(global_load) X(item) X(helper) X(incref) X(decref) X(box) \
    X(call_unknown) X(call_poly) X(call_refused) X(call_c) X(call_kw)
enum lu_count {
#define LU_COUNT_ENUM(name) CNT_##name,
    LU_COUNT_KINDS(LU_COUNT_ENUM)
#undef LU_COUNT_ENUM
    CNT_COUNT
};
extern const char *const lu_count_names[CNT_COUNT];
/* The kind an instruction counts as, or -1. */
int lu_count_kind(const struct lu_ins *in);
/* A call compiled code makes instead of inlining it, by why: a callee
   the builder could not name, one of several a site met, a Python function
   it would not or could not inline, a callable that is not Python code, or
   a call with keywords or unpacked arguments. A call helper's imm2 holds
   the kind plus one. */
#define LU_COUNT_IS_CALL(k) ((k) >= CNT_call_unknown)
/* Kinds that are guards: every guard_* kind and unbox (not polls and checks that the unit lives). */
#define LU_COUNT_IS_GUARD(k) ((k) <= CNT_unbox)

/* Dependencies (runtime/jit/deps.c): a compile's ticket, taken before it
   reads anything, and its end; publishing a unit's (-1: a change since the
   ticket may have been missed, and the unit is not to be entered); then the
   policy's side (jit.c): a dependency's unit dies, and its site keeps its
   guard in place. */
uint64_t lu_jit_deps_begin(void);
void lu_jit_deps_end(void);
int lu_jit_deps_publish(struct lu_unit *u, const struct lu_fdep *want, int n, uint64_t ticket);
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
   run Python; IF_SEEN: may release a value whose finalizer does, or park)
   and argument count (0: an array and its length). */
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
    X(prim_call) \
    X(make_function) \
    X(set_function_attribute) \
    X(cell_store) \
    X(get_iter) \
    X(for_iter) \
    X(seqiter_next) \
    X(range_iter) \
    X(listiter_new) \
    X(tupleiter_new) \
    X(rangeiter_new) \
    X(unary) \
    X(list_append) \
    X(list_repeat) \
    X(list_repeat_fresh) \
    X(build_list) \
    X(build_tuple) \
    X(new_object) \
    X(new_dict) \
    X(contains) \
    X(list_push) \
    X(build_set) \
    X(build_map) \
    X(set_add) \
    X(list_to_tuple) \
    X(async_gen_wrap) \
    X(gen_send) \
    X(send) \
    X(gen_done) \
    X(get_awaitable) \
    X(get_yield_from_iter) \
    X(get_aiter) \
    X(get_anext) \
    X(gen_enter) \
    X(gen_yielded) \
    X(gen_finish) \
    X(get_iter_seq) \
    X(binary_slice) \
    X(slice_native) \
    X(store_slice) \
    X(call_kw) \
    X(call_ex) \
    X(call_forward) \
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

/* Whether a walker may see the root frame during the instruction's call
   to C: the call may run Python (IF_PY), or release a value whose
   finalizer does, or park the task (IF_SEEN). It publishes the root
   frame's state for walkers while it does (runtime/jit/deopt.c): every
   root local that state names is in memory during the call. */
static inline int
lu_publishes(const struct lu_ins *in)
{
    if (in->fs == NULL || (in->flags & (INS_VIRTUAL | INS_FRESH))) {
        return 0;
    }
    switch (in->op) {
    case OP_HELPER:
        return (lu_helpers[in->imm].flags & (IF_PY | IF_SEEN)) != 0;
    case OP_TRUTH:
        return in->imm != 1;    /* imm 1: a layout's own truth, which runs no Python */
    default:
        return (lu_op_flags[in->op] & IF_SEEN) != 0;
    }
}

/* -X lucifer_jit_stats: how often compiled code takes the C path of an
   operation it otherwise runs in line (runtime/jit/jit.c reports them). */
#define LU_C_PATHS(X) X(list_item) X(tuple_item) X(list_store) X(box_int) X(box_float) X(build_list) X(build_tuple) \
    X(new_object)
enum lu_c_path {
#define LU_C_PATH_ENUM(name) C_PATH_##name,
    LU_C_PATHS(LU_C_PATH_ENUM)
#undef LU_C_PATH_ENUM
    C_PATHS
};
void lu_jit_c_path_count(enum lu_c_path k);

/* -X lucifer_jit_stats: the objects compiled code allocates, counted per
   site (the bytecode instruction) and kind; a class call counts what the
   class makes. jit.c reports the busiest. */
#define LU_ALLOC_KINDS(X) X(int) X(float) X(tuple) X(list) X(dict) X(set) X(instance) X(iterator) \
    X(function) X(class_call)
enum lu_alloc_kind {
#define LU_ALLOC_ENUM(name) ALLOC_##name,
    LU_ALLOC_KINDS(LU_ALLOC_ENUM)
#undef LU_ALLOC_ENUM
    ALLOC_KINDS
};
/* The kind an instruction allocates, or -1. */
int lu_alloc_kind(const struct lu_ins *in);
/* The count of a site and kind, kept for the life of the process. */
unsigned long *lu_jit_alloc_site(PyCodeObject *code, int pc, int kind);
#define LU_JIT_C_PATH(name) do { \
        if (_PyRuntime.jit.stats) { \
            lu_jit_c_path_count(C_PATH_##name); \
        } \
    } while (0)

/* -X lucifer_jit_stats: why a call compiled code did not inline missed
   the callee's direct entry (runtime/jit/jit.c classifies a call reaching
   a C helper): a keyword call of what is not a Python function, or a
   star call; a callable that is a type, a C function or something else;
   a generator's code; a prologue that is not cells and free variables;
   more arguments than parameters; a code that never got hot, is
   disabled, retired, or has no live unit at its call entry; or a live
   entry, which the helper enters itself. */
#define LU_WHY_NOT(X) X(kw) X(star) X(type) X(builtin) X(object) X(generator) \
    X(prologue) X(toomany) X(cold) X(disabled) X(retired) X(nounit) X(entry)
enum lu_why_not {
#define LU_WHY_NOT_ENUM(name) WHY_NOT_##name,
    LU_WHY_NOT(LU_WHY_NOT_ENUM)
#undef LU_WHY_NOT_ENUM
    WHY_NOTS
};
void lu_jit_why_not_count(enum lu_why_not k, PyObject *callable);
/* A call of callable with nargs positional arguments: its reason. */
void lu_jit_why_not(PyObject *callable, Py_ssize_t nargs);

/* A helper's result that is not an answer: the call needs the interpreter
   (a C stack too deep to recurse on), so compiled code deopts. */
#define LU_JIT_DECLINED ((PyObject *)1)

#endif
