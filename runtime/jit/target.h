/* The boundary between the optimizing tier's passes and a machine
   (FOUNDATIONS 2.11): the passes (eval.c, opt.c, regalloc.c) include only this.
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
    /* The allocatable general registers every call preserves (the C ABI's
       callee-saved ones, which compiled code saves at its entries too),
       and whether an instruction's own code uses them across its calls. */
    int nsaved;
    const int8_t *saved;
    int (*clobbers_saved)(const struct lu_ins *in);
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

/* An exit's description (runtime/jit/exit.c): where each value of its
   frame state is. A slot or a field is NULL, a constant, an object at a
   location, a raw word at a location (or a constant), or a virtual object,
   named by its index among the exit's so that every slot that names one
   gets the one object. */
enum lu_dv_kind {
    DV_NULL,                    /* an unbound local, or a NULL stack entry */
    DV_CONST,                   /* imm: an object the unit keeps */
    DV_OBJ,                     /* an object at loc */
    DV_RAW,                     /* a raw word at loc, or imm when loc is none */
    DV_VIRT,                    /* imm: the index of a virtual object in the exit's objs */
};

struct lu_dv {
    uint8_t kind;
    int32_t loc;
    int64_t imm;
};

/* A virtual object an exit names: its state id (the same in every exit
   of the unit that names that object), kind (enum lu_vkind) and fields. */
struct lu_dobj {
    uint8_t kind;
    uint8_t neg;
    int32_t id;
    PyTypeObject *type;
    int nfields;
    struct lu_dv *fields;
};

struct lu_deopt_frame {
    PyCodeObject *code;
    PyObject *func;
    int pc;
    int nlocals, nstack;
    struct lu_dv *vals;
    uint8_t ret;                /* how the frame returns when rebuilt (lu_fs) */
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
    int nobjs;
    struct lu_dobj *objs;       /* the virtual objects the frames name */
    int nowned;                 /* the references compiled code holds here, each an object at a location: */
    int32_t *owned;             /* released last, after every slot took its own */
    int error;                  /* leave with the exception set */
    int drop;                   /* then pop this many stack entries of the innermost frame */
    int reason;                 /* enum lu_exit_reason */
    int opcode;                 /* the base opcode at the exit, for the counts */
    struct lu_unit *unit;       /* the unit it leaves */
    PyCodeObject *blame_code;   /* a guard that speculates for another site: the site */
    int blame_pc;
    struct lu_prof *prof;       /* a guard on a learned type, or (tier) the report that more was learned */
    uint8_t tier;
    lu_codeunit *instr;         /* published: the instruction the innermost frame shows as running */
    uint64_t takes;             /* published by a call: its arguments (bits by index) whose stack entries hold
                                   references of their own for the call, which a Python callee's parameters take */
    uint8_t regs;               /* published by a slow path that saved every register at the frame's jit_regs */
    uint8_t visible;            /* an inlined frame is a user's: a walker makes the frames real */
};

/* The activation's forced state (runtime/jit/exit.c), made when a walker
   first needs a frame or an object of it that compiled code kept virtual:
   the objects made, by state id, and the inlined frames' records. Every
   later request, and the exit compiled code then takes, finds them here. */
struct lu_forced;
void lu_jit_forced_free(struct lu_forced *fo);

/* The innermost frame of `f`, a frame compiled code runs, once its
   inlined frames are real: records off the datastack, linked above the
   root. They are made when the call the code is in shows a user's frame
   among them, or always with `all`; else f comes back as it is. NULL with
   an exception when one cannot be made. */
struct lu_frame *lu_jit_force(struct lu_frame *f, int all);

/* A compiled entry into a code object at one bytecode boundary. */
struct lu_unit {
    struct lu_unit *next;
    PyCodeObject *code;         /* whose unit it is (borrowed: the code frees its units) */
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
    unsigned long entries;      /* up to a bound (jit.c) */
    unsigned long entered;      /* every entry, under -X lucifer_jit_stats */
    unsigned long leaves;       /* exits no recompile can remove: side exits of the root frame, errors */
    int loops;                  /* the code has a back edge: an entry may do unbounded work */
    int refused;                /* calls it left out of line for want of budget (jit.c grows it) */
    int dead;
    /* While dead and not yet freed: the list of such units, and whether
       a collection found an activation in it (jit.c). */
    struct lu_unit *dying_next, **dying_pprev;
    int held;
    unsigned long built;        /* sites marked generic before its build began (jit.c) */
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

/* The exits' common path (runtime/jit/exit.c), which the code calls with
   the registers it saved: general registers in gpr[0..15], float registers
   in fpr[0..15], and the spill words below `spills`. Rebuilds the frames
   and returns the enum lu_jit_result the unit returns. */
int lu_jit_deopt(PyThreadState *ts, struct lu_frame *frame, struct lu_deopt *d,
                 const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills);

/* The helpers compiled code calls, by id; runtime/jit/helpers.c gives each
   its result, effects (FX_*) and argument count (0: an array and its
   length). */
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
    X(dict_lacks) X(profile) X(fields_new) \
    X(cell_load) \
    X(compare_ideq) \
    X(binary_op) \
    X(str_concat) \
    X(compare_op) \
    X(setitem) \
    X(delitem) \
    X(list_extend) \
    X(load_attr) \
    X(load_method) \
    X(store_attr) \
    X(load_global) \
    X(store_global) \
    X(build_slice) \
    X(convert_value) \
    X(format) \
    X(build_string) \
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
    X(seqiter_step) \
    X(field_get) \
    X(str_char) \
    X(field_set) \
    X(range_iter) \
    X(listiter_new) \
    X(tupleiter_new) \
    X(rangeiter_new) \
    X(unary) \
    X(list_append) \
    X(list_repeat) \
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
    X(unpack_exact) \
    X(async_gen_wrap) \
    X(send) \
    X(gen_done) \
    X(get_awaitable) \
    X(get_yield_from_iter) \
    X(get_aiter) \
    X(get_anext) \
    X(get_iter_seq) \
    X(binary_slice) \
    X(binary_slice_raw) \
    X(store_slice) \
    X(call_kw) \
    X(call_ex) \
    X(dict_merge) \
    X(dict_update) \
    X(push_exc_info) \
    X(pop_except) \
    X(exc_match) \
    X(raise) \
    X(bytes_concat) \
    X(bind_method) \
    X(float_pow) \
    X(seq_concat) \
    X(reraise) \
    X(load_special) \
    X(unpack)

enum lu_helper_id {
#define LU_HELPER_ENUM(name) H_##name,
    LU_HELPERS(LU_HELPER_ENUM)
#undef LU_HELPER_ENUM
    H_COUNT
};
extern const struct lu_helper lu_helpers[H_COUNT];

/* Whether a walker may see the frames during the instruction's call to
   C: it may run Python, release a value whose type runs code, or park the
   task. It publishes its state for walkers while it does
   (runtime/jit/exit.c): every value that state names is in memory during
   the call. */
static inline int
lu_publishes(const struct lu_ins *in)
{
    if (in->fs == NULL) {
        return 0;
    }
    switch (in->op) {
    case OP_HELPER:
        return (lu_helpers[in->imm].fx & (FX_PY | FX_SEEN)) != 0;
    case OP_TRUTH:
        return in->imm != 1;    /* imm 1: a layout's own truth, which runs no Python */
    case OP_DECREF:
        return in->imm == 1;
    default:
        return (lu_op_flags[in->op] & IF_SEEN) != 0;
    }
}

/* What a root learned of a value where code inlined in it wanted the
   value's type and nothing told it (runtime/jit/jit.c): compiled code
   reports the type there, and the next build of the root guards on it.
   The place is a context: the chain of frames from the root to the frame
   asking, each a code and the instruction it is at, hashed, so that
   kernel code is customized for the place it is inlined at and nowhere
   else. */
static inline uint64_t
lu_jit_ctx_mix(uint64_t h, const void *code, int pc)
{
    h ^= (uint64_t)(uintptr_t)code + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= (uint64_t)(uint32_t)pc * 0xff51afd7ed558ccdULL;
    return h * 0xc4ceb9fe1a85ec53ULL;
}
/* What the object beside a raw value is while the local is unbound: a
   loop that binds a local to a number on every turn carries the number
   raw from its first, where the local has no value yet. */
extern PyObject lu_jit_unbound;

struct lu_prof;
/* The root's record for the context `key`, made if new; NULL without memory. */
struct lu_prof *lu_jit_prof(PyCodeObject *root, uint64_t key);
/* The type it met (a new reference) or NULL, and whether it met more than one. */
PyTypeObject *lu_jit_prof_type(struct lu_prof *p, int *poly);
/* A count that changes whenever a record of the root does. */
unsigned long lu_jit_prof_learned(PyCodeObject *root);
/* Compiled code met `o` there, in a unit built when the count was
   `learned`: 0 when the unit is to leave and be built again. */
int lu_jit_prof_note(struct lu_prof *p, PyObject *o, unsigned long learned);

/* POW_F's call (runtime/jit/helpers.c): a NaN with these bits where the
   power raises or is complex. */
#define LU_JIT_POW_FAILED 0x7ff4dead0000beefULL
double lu_jit_float_pow(double v, double w);

/* Whether the register allocator treats the instruction as a call, which
   clobbers every register but the preserved ones: an IF_CALL instruction,
   but for a release that publishes nothing, whose slow path keeps every
   register (luc-15mf). */
static inline int
lu_alloc_call(const struct lu_ins *in)
{
    return (lu_op_flags[in->op] & IF_CALL) && !(in->op == OP_DECREF && !lu_publishes(in));
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

/* A helper's result that is not an answer: the call needs the interpreter
   (a C stack too deep to recurse on), so compiled code deopts. */
#define LU_JIT_DECLINED ((PyObject *)1)

#endif
