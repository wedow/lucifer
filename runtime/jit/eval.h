/* The evaluator (docs/design/jit-architecture.md 4.1, 4.2): bytecode is
   evaluated over a state that holds, per path, every frame's locals and
   stack and the contents of the objects the code has made. An operation
   the state decides is not emitted; one a virtual object's kind
   understands updates the state; anything else is emitted, with the
   virtual objects it needs made real first. Inlining is the same
   evaluation continued in the callee, whose frame is one more frame of
   the state and nothing in the graph.

   A slot holds a value: an instruction's (>= 0), nothing (LU_NOV), or a
   virtual object (LU_VIRT(id)). The state's binds say what each virtual
   object is on this path (ssa.h struct lu_bind).

   References (ssa.h): compiled code holds one reference for an owned
   value exactly while a slot of the state, or a field of a virtual object
   a slot reaches, names it. Whatever stops being named during an
   instruction waits in `pending` until the instruction ends, where it is
   released if nothing names it then (settle): the bytecode boundary where
   the interpreter drops its last reference. An exit inside the
   instruction releases what is pending and unnamed in its state (held).

   Files: eval_state.c (the state and its snapshots), eval_emit.c (what
   reaches the graph: guards, helpers, making objects real, releases),
   eval_flow.c (blocks, edges, joins, loops, the driver), eval_ops.c and
   eval_arith.c, eval_attr.c, eval_call.c, eval_iter.c (the instructions),
   eval_prims.c (what is known of the kernel's primitives). */
#ifndef LU_JIT_EVAL_H
#define LU_JIT_EVAL_H

#include "jit/target.h"

/* What a path has proved of an object value: its exact type, its raw
   value once it was unboxed, and its type's version once a guard read it. */
struct fact {
    lu_v v;
    PyTypeObject *t;
    lu_v raw;
    uint32_t version;
};

struct state {
    int depth;                  /* the innermost frame's index: frames 0 .. depth */
    lu_v **v;                   /* per frame: locals, then the stack bottom up */
    int *cap;                   /* per frame: slots (locals and the stack's room) */
    int *nlocals;               /* per frame: locals */
    int *sp;                    /* per frame: stack entries */
    struct lu_bind *binds;
    int nbinds, capbinds;
    struct fact *facts;
    int nfacts, capfacts;
    uint32_t version;           /* names what its frames' locals and its binds hold: a copy keeps it, a change
                                   makes a new one (lu_ev_changed) */
};

/* Control reaching a block: the open graph block it comes through (no
   terminator yet, so that the join can convert values in it) and the
   state it arrives with. */
struct edge {
    int block;
    struct state st;
};

/* What a join or a loop header may replace with a phi is a place: a slot
   of the innermost frame (id < 0, `at` the slot), or a field of a virtual
   object that stays virtual across it (id the object's, `at` the field).
   The frame's slots come first, slot i at index i. */
struct place {
    int id, at;
};

/* What was made of a place. */
enum {
    PLACE_SAME,                 /* nothing: it holds what it held */
    PLACE_RAW,                  /* a number that is no object: a raw phi, and beside it (origin) the phi of
                                   the object the number was, where an edge had one */
    PLACE_OBJ,                  /* an object's phi, which holds a reference */
    PLACE_DIRECT,               /* a field that is a raw word (an iterator's index): its phi */
    PLACE_VIRT,                 /* a tuple that is no object on any edge, a different one on each: one virtual
                                   tuple (its reference in place of a phi), whose items are places of the join */
};

struct places {
    struct place *at;
    uint8_t *kind;
    lu_v *phi;                  /* the phi (places that held the same values on every edge share one), or LU_NOV */
    lu_v *origin;
    int *lead;                  /* the first place with that phi: itself, or -1 without one */
    int n, cap;
};

struct bblk {
    int pc;
    int loop;                   /* a back-edge jumps here */
    int loop_end;               /* the last back-edge's pc */
    int built;
    int ir;                     /* a loop header's graph block */
    struct edge *in;            /* forward edges waiting for it to be built */
    int nin, capin;
    struct edge *back;          /* a built loop header: back-edges waiting for the loop to be built to its end */
    int nback, capback;
    int closed;                 /* its back-edges have met it once: later ones fit it or give it a hint */
    int began;                  /* what the unit had spent of its budget when the header was built */
    int gen;                    /* the loop carries a generator run in line: it may be peeled */
    struct places P;            /* a built loop header: its frame's slots, then every field of the virtual
                                   objects it carries, for its back-edges to meet */
    lu_v *ref;                  /* per place: what it holds at the header */
    uint64_t *key;              /* per place: what names it in a hint */
    int *carried;               /* the virtual objects the loop may change that the header left virtual: their ids */
    uint64_t *carried_key;      /* and what names each in a hint */
    int ncarried;
    struct state hdr;           /* the state at the header */
};

struct gctx;

/* One frame being evaluated: the root's, or an inlined call's. */
struct ctx {
    struct gctx *g;
    struct ctx *parent;
    int depth;
    int spent;                  /* what the unit had spent of its budget when this call went in line; 0 for the
                                   root and a generator resumed in line, counted from the unit's start */
    PyCodeObject *code;
    PyFunctionObject *func;     /* NULL for the root when its function is not a function */
    lu_v funcv;                 /* an inlined frame's function as a value, where the call knew it by its code
                                   only (func then stands for it in all but its closure); else LU_NOV */
    PyObject *globals, *builtins;
    int kernel;                 /* kernel code: its module's globals fold */
    int ncode, nlocals;
    struct bblk *bb;
    int nbb;
    struct bblk *loops;         /* while the way in from an entry inside a loop is built: the loops' blocks */
    int lo;                     /* blocks below it are built or not reached yet */
    const int *blkat;           /* code unit -> bblk index, or -1 */
    struct lu_fs *caller_fs;    /* an inlined frame's caller, after the call */
    lu_v gen;                   /* the generator whose frame this is, its body run in line (eval_gen.c): a
                                   virtual one, or with `adopted` the value that is one that exists */
    int adopted;
    lu_v *loaded;               /* an adopted frame: what each slot held when its words were loaded */
    int nloaded;
    int depth_in;               /* and its stack's depth then */
    int ret;                    /* how an inlined frame returns (enum lu_frame_return): a value pushed, an
                                   __init__'s None dropped, the instance already on its caller's stack, or
                                   a FOR_ITER's item pushed unless it is MISSING */
    int missing;                /* LU_RETURN_FOR_ITER: a return joined may answer MISSING */
    int pc, next;               /* the current instruction, the one after its caches */
    int opcode, base, oparg;
    lu_codeunit *cache;
    struct lu_fs *snap;         /* the state before the current instruction, once an exit asked for it */
    int snap_pc, snap_sp;       /* its pc and stack depth */
    struct undo {               /* what the instruction has changed of its frame's slots since: slot, old value */
        int i;
        lu_v old;
    } *undo;
    int nundo, capundo;
    lu_v *kept_locals;          /* the locals of the last such state, for the next to share while none changes */
    const lu_v *kept_of;        /* the slots they were copied from, */
    uint32_t kept_epoch;        /* and the build's epoch then */
    int snap_live;              /* the state's locals are the frame's as they are now */
    struct edge *rets;          /* an inlined frame's returns, each with the value pushed in its caller */
    int nrets, caprets;
};

/* What a rebuild knows from the build before it (eval_flow.c), per place
   of a loop header: a field of a virtual object the loop carries changes
   round it; a place that carries a box wants a raw phi; one whose
   back-edge did not fit gets an object. A hint only grows stronger, in
   that order. Of a carried object itself (lu_ev_object_key) HINT_OBJ says
   that it exists before the loop. */
enum { HINT_VARIES = 1, HINT_INT, HINT_FLOAT, HINT_OBJ };
enum { MISFIT = 1, MISFIT_KIND = 2, MISFIT_FRESH = 4 };
struct hint {
    PyCodeObject *code;
    int pc, kind;
    uint64_t place;             /* a slot's index, or a key (eval_flow.c) */
};

/* What a rebuild knows of generators resumed in line, from the build
   before it (eval_gen.c): the depth of a generator's stack at a code unit
   it resumes at (place 0: `code` is the generator's), and a resume point
   a back-edge brought round to a loop header's carried generator (`code`
   and `at` the header's, `place` the generator's key there). */
struct resume {
    PyCodeObject *code;
    int at;
    uint64_t place;
    int pc, depth;
};

/* One build. */
struct gctx {
    struct lu_func *f;
    struct lu_frame *live;      /* the root frame being compiled */
    int cur;                    /* the graph block being built, -1 once control cannot reach here */
    struct state st;
    lu_v *pending;              /* values and virtual objects that a slot stopped naming, or that were just made */
    int npending, cappending;
    int observed;               /* the instruction made a call during which a walker may see the frames */
    lu_v keep;                  /* a value whose reference leaves with the root's return: not released */
    int leaving;                /* the root returns: its frame is off the stack, and no walker's sight matters */
    int restart;                /* a hint was added: the build starts over */
    int trying;                 /* a back-edge is only tried against its header: a hint it would give is */
    int misfit;                 /* not recorded and restarts nothing, and misfit says there was one (MISFIT, */
                                /* and MISFIT_KIND when a number's phi was brought a number of another kind,
                                   MISFIT_FRESH when a local's object phi an instance the loop made) */
    unsigned long learned;      /* the root's count of what it learned, when the build began */
    int ret_missing;            /* the call just inlined for a FOR_ITER may have answered MISSING */
    uint64_t hot_room;          /* the hot site lu_ev_room let a call in at past the budget, or 0 */
    struct hint *hints;
    int nhints, caphints;
    PyObject **shown;           /* objects the entry frame shows the unit (eval_gen.c), each a reference */
    lu_v *shown_as;             /* and the values they were when the unit was entered */
    int nshown, capshown;
    struct resume *resumes;     /* what a rebuild knows of generators resumed in line (eval_gen.c) */
    int nresumes, capresumes;
    struct resume_phi {         /* a loop header's phi of a carried generator's resume point: its header */
        lu_v phi;
        PyCodeObject *code, *gen;       /* the header's, the generator's */
        int pc;
        uint64_t place;
    } *resume_phis;
    int nresume_phis, capresume_phis;
    struct konst {              /* the build's constants, hashed by kind and bits */
        int op;
        int64_t imm;
        lu_v v;
    } *konsts;
    int nkonsts, capkonsts;
    int budget;                 /* instructions the unit may have and still take a call in line */
    uint32_t *named;            /* per value: the stamp of the last walk that found a slot naming it */
    uint32_t *reached;          /* per virtual object: the same */
    int capnamed, capreached;
    uint32_t stamp;
    /* The same for what changes seldom, kept from walk to walk: what the
       innermost frame's locals and its callers' states name (base_named)
       and what every slot but the innermost stack's reaches
       (base_reached), as long as the frame, its slots and the epoch are
       the ones they were walked for. */
    uint32_t *base_named, *base_reached;
    int capbase_named, capbase_reached;
    uint32_t base_stamp, base_version;
    struct ctx *base_ctx;
    uint32_t epoch;             /* moves when a local, a virtual object or the frames change (lu_ev_changed) */
    uint32_t versions;          /* states' versions handed out */
    int maxdepth;               /* the deepest inlined frame */
    PyTypeObject *relied;       /* the class last made a dependency for what is known of an object's type */
    int generic;                /* the way of lu_ev_kernel_arms for types it has no way for is being built */
};

/* Per-code JIT state the evaluator consults (jit.c). */
int lu_jit_nospec(PyCodeObject *code, int pc);
int lu_jit_catches(PyCodeObject *code, int pc);     /* the kernel handler at pc took what calls raised (jit.c) */
int lu_jit_raises_out(PyCodeObject *code, int pc);  /* what pc raised kept leaving its unit's root (jit.c) */
int lu_jit_next_pc(PyCodeObject *code, int pc);
int lu_jit_noinline(PyCodeObject *code);
/* A call site: pc of code, reached in line from outer's call at outer_pc,
   or a site of the unit's own code (outer NULL). */
uint64_t lu_jit_site_key(PyCodeObject *code, int pc, PyCodeObject *outer, int outer_pc);
/* Whether units of root made calls out of line at the site often: a
   call there goes in line whatever the budget has left. */
int lu_jit_hot_site(PyCodeObject *root, uint64_t key);
/* The evaluator's scan of a code (eval_flow.c), kept with the code's JIT
   state: the one kept, or NULL; and one to keep, malloc'd, which the
   state takes (or frees for one another thread kept first), NULL when the
   code has no state and the caller keeps it. */
struct lu_ev_scan;
struct lu_ev_scan *lu_jit_scan(PyCodeObject *code);
struct lu_ev_scan *lu_jit_keep_scan(PyCodeObject *code, struct lu_ev_scan *scan);

/* --- eval_state.c --- */

static inline struct lu_ins *
I(struct ctx *c, lu_v v)
{
    return &c->g->f->ins[v];
}

static inline lu_v *
slots(struct ctx *c)
{
    return c->g->st.v[c->depth];
}

static inline int *
depth_of(struct ctx *c)
{
    return &c->g->st.sp[c->depth];
}

void lu_ev_state_copy(struct gctx *g, struct state *to, const struct state *from);
/* A frame joins the state above the innermost, with `nslots` unset slots
   of which `nlocals` are locals; and leaves. */
void lu_ev_state_push(struct gctx *g, int nlocals, int nslots);
void lu_ev_state_pop(struct gctx *g);

void lu_ev_push(struct ctx *c, lu_v r);
lu_v lu_ev_pop(struct ctx *c);          /* what it named waits in pending */
lu_v lu_ev_peek(struct ctx *c, int depth);
void lu_ev_set_local(struct ctx *c, int i, lu_v r);
/* v is owned from here (INS_OWNED) and pending; r stopped being named. */
lu_v lu_ev_own(struct ctx *c, lu_v v);
void lu_ev_drop(struct ctx *c, lu_v r);

/* A new virtual object of the kind, its fields unset; the record and
   state of object r now (NULL record: real and changeable), and a new
   record or a real value for it. */
lu_v lu_ev_virt_new(struct ctx *c, int kind, PyTypeObject *type, int n, struct lu_vobj **out);
struct lu_vobj *lu_ev_virt(struct gctx *g, const struct state *st, lu_v r, lu_v *real);
void lu_ev_bind(struct gctx *g, struct state *st, int id, lu_v real, struct lu_vobj *obj);

/* The exact type a value is known to have here, or NULL; a constant's
   object, or NULL; and facts learned. */
PyTypeObject *lu_ev_type(struct ctx *c, lu_v r);
PyObject *lu_ev_const(struct ctx *c, lu_v r);
void lu_ev_set_type(struct ctx *c, lu_v v, PyTypeObject *t);
struct fact *lu_ev_fact(struct ctx *c, lu_v v, int create);
/* The type every use of instruction value v has (its definition gives it). */
void lu_ev_def_type(struct ctx *c, lu_v v, PyTypeObject *t);
/* r's exact type: what is known, else what the root learned of the
   `which`-th value this instruction asks about (guarded), as far as
   lu_ev_type knows it; NULL when it knows none yet, and compiled code then
   reports the type here. */
PyTypeObject *lu_ev_learn_type(struct ctx *c, lu_v r, int which);
/* Whether r's class has the type flags `kind` (a tuple's or a list's of
   any class, LU_PROF_KINDS): its type's, or what every type the root met
   there had (guarded) where it met several. */
int lu_ev_learn_kind(struct ctx *c, lu_v r, int which, unsigned long kind);
/* The same for r's identity: the constant, or NULL. *r is the constant
   from here. Value records are separate by kind and from type records. */
PyObject *lu_ev_learn_const(struct ctx *c, lu_v *r, int which, enum lu_prof_value_kind kind);
/* The root's record of that value, and compiled code that reports r to
   it (and leaves when the unit is to be built again). */
struct lu_prof *lu_ev_prof(struct ctx *c, int which);
void lu_ev_profile(struct ctx *c, struct lu_prof *p, lu_v r);

/* The innermost frame's state at pc as a frame state, with the path's
   binds; and the state before the current instruction as an exit takes
   it now: the binds as they are, and what is pending and unnamed held. */
struct lu_fs *lu_ev_snapshot(struct ctx *c, int pc);
struct lu_fs *lu_ev_exit_fs(struct ctx *c);
/* The instruction at pc begins: the state as it is now is the one its
   exits take. It is copied only if one asks (lu_ev_exit_fs), from the
   slots as they are then and what the instruction changed meanwhile, so
   an instruction that cannot leave costs nothing of the frame's size. */
void lu_ev_begin(struct ctx *c, int pc);
/* What the state names is found by walking it once and stamping what
   the walk meets; any number of values are then looked up. The locals of
   a large frame and its callers' states change seldom, so their walk is
   kept (the base) for the state's version and only the innermost stack is
   walked each time.
   lu_ev_walk stamps what the live state names and reaches; lu_ev_named
   and lu_ev_reached read it, until the next walk. */
void lu_ev_walk(struct ctx *c);
int lu_ev_named(struct gctx *g, lu_v v);
int lu_ev_reached(struct gctx *g, int id);
/* A local, a virtual object's record or the frames changed: what was kept
   of them is stale. */
static inline void
lu_ev_changed(struct gctx *g)
{
    g->epoch++;
    g->st.version = ++g->versions;
}
/* The state is one taken back whole (an edge's, a branch's other arm):
   what was kept of the frame's slots is stale, but a walk of that very
   state, as its version names it, still stands. */
static inline void
lu_ev_replaced(struct gctx *g)
{
    g->epoch++;
}
/* Walks state fs once, after which lu_ev_names says whether it names
   instruction value v (a slot, a virtual object's field, the object a
   virtual one became), other than as held, until the next walk. */
void lu_ev_names_walk(struct gctx *g, struct lu_fs *fs);
int lu_ev_names(struct gctx *g, lu_v v);
/* Whether the state's slots reach virtual object id; the same, not
   looking inside virtual object `past`. */
int lu_ev_reaches(struct gctx *g, const struct state *st, int id);
int lu_ev_reaches_past(struct gctx *g, const struct state *st, int id, int past);
/* Whether r is virtual object id, or one whose fields reach it (eval_flow.c). */
int lu_ev_reaches_id(struct gctx *g, const struct state *st, lu_v r, int id);

/* --- eval_emit.c --- */

lu_v lu_ev_emit(struct ctx *c, int op, int rep, lu_v a, lu_v b, lu_v cc, int64_t imm);
lu_v lu_ev_konst(struct ctx *c, PyObject *o);
lu_v lu_ev_const_i(struct ctx *c, int64_t i);
lu_v lu_ev_const_f(struct ctx *c, double d);
/* A guard that leaves before the current instruction; one after its effect. */
lu_v lu_ev_guard(struct ctx *c, int op, lu_v a, int64_t imm);
void lu_ev_guard_type(struct ctx *c, lu_v r, PyTypeObject *t);
void lu_ev_guard_version(struct ctx *c, lu_v owner, uint32_t version);
/* The object r stands for, as an instruction value: a virtual object is
   made real here. LU_NOV stays (a NULL). */
lu_v lu_ev_val(struct ctx *c, lu_v r);
/* A helper call with its arguments real; `drop` stack entries are what its
   raise consumed. The result (owned when an object). */
lu_v lu_ev_helper(struct ctx *c, int id, int nargs, const lu_v *args, int drop);
lu_v lu_ev_unbox_int(struct ctx *c, lu_v r);
lu_v lu_ev_unbox_float(struct ctx *c, lu_v r);
lu_v lu_ev_box_int(struct ctx *c, lu_v raw);
lu_v lu_ev_box_float(struct ctx *c, lu_v raw);
lu_v lu_ev_bool(struct ctx *c, lu_v bit, int neg);
/* The bit that says whether r is true: folded, read from a virtual bool,
   or tested. */
lu_v lu_ev_truth(struct ctx *c, lu_v r);
/* The instruction ends, the state at pc: what is pending and unnamed is
   released, and a walker's sight of the frames is checked for. */
void lu_ev_settle(struct ctx *c, int pc);
/* Leaves to the interpreter before the current instruction. */
void lu_ev_side_exit(struct ctx *c, int reason);
/* A reference for v's new holder outside the state (a result, a phi). */
void lu_ev_retain(struct ctx *c, lu_v v);

/* --- eval_flow.c --- */

void lu_ev_jump(struct ctx *c, int pc);
void lu_ev_branch(struct ctx *c, lu_v bit, int pc_true, int pc_false);
/* When bit is 1 control goes to pc_true; the evaluation goes on where it
   is 0 (nowhere, if bit is a constant 1). */
void lu_ev_fork(struct ctx *c, lu_v bit, int pc_true);
/* The same, the way taken when bit is 1 built by arm(c, arg) to its end. */
void lu_ev_fork_with(struct ctx *c, lu_v bit, void (*arm)(struct ctx *c, void *arg), void *arg);
/* After helper v, which raises by answering NULL: where a handler of the
   unit takes what it raises, that way goes to the handler (eval_exc.c). */
void lu_ev_caught(struct ctx *c, lu_v v);
/* The current instruction goes two ways on a bit, each way built to its
   end and the two joined after it: lu_ev_split begins the way taken when
   the bit is 1, lu_ev_split_else ends it and begins the other, and
   lu_ev_split_end ends that and joins what reached the instruction's
   end (control goes nowhere when neither did). The instruction has
   changed no slot yet when it splits; a way may split again. */
struct split {
    int other;                  /* the graph block of the way not yet built */
    int pc, sp;                 /* the instruction, and its frame's stack depth before it */
    struct state saved;
    lu_v *pending;
    int npending, observed;
    struct edge arms[2];
    int n;
};
void lu_ev_split(struct ctx *c, lu_v bit, struct split *s);
void lu_ev_split_else(struct ctx *c, struct split *s);
void lu_ev_split_end(struct ctx *c, struct split *s);
/* Joins the edges, which arrive before the instruction at pc of frame c,
   into one block and state (g->cur, g->st). */
void lu_ev_merge(struct ctx *c, struct edge *edges, int n, int pc);
/* Evaluates c's code from pc, the state's innermost frame its own, until
   every path has returned, left or raised. -1 with f->fail set. */
int lu_ev_build(struct ctx *c, int pc);
int lu_ev_decode(struct ctx *c, int pc, int *base, int *oparg, int *opcode);
int lu_ev_jump_target(int op, int oparg, int next);
/* Whether the evaluator compiles every instruction of code a call can
   reach without an exception; and one opcode (eval_ops.c). */
int lu_ev_supported(struct gctx *g, PyCodeObject *code);
int lu_ev_op_supported(int base);
void lu_ev_hint(struct gctx *g, PyCodeObject *code, int pc, uint64_t place, int kind);
int lu_ev_hinted(struct gctx *g, PyCodeObject *code, int pc, uint64_t place);

/* --- the instructions: 0, or -1 when the frame cannot go on (f->fail set) --- */

int lu_ev_instruction(struct ctx *c);           /* eval_ops.c: the dispatch */
/* A site's operand kinds (jit.h), counted: none means the interpreter has
   never run it. */
int lu_ev_feedback(struct ctx *c, lu_codeunit *word);
/* `name` in dict d, else in dict b (when not NULL), as a constant guarded
   where it is read: a new reference, or NULL when it cannot be had so. */
PyObject *lu_ev_dict_const(struct ctx *c, PyObject *d, PyObject *b, PyObject *name);
int lu_ev_binary_op(struct ctx *c);             /* eval_arith.c */
int lu_ev_compare_op(struct ctx *c);
int lu_ev_unary(struct ctx *c);
int lu_ev_load_global(struct ctx *c);           /* eval_attr.c */
int lu_ev_load_attr(struct ctx *c);
int lu_ev_store_attr(struct ctx *c);
int lu_ev_call(struct ctx *c);                  /* eval_call.c */
int lu_ev_call_kw(struct ctx *c);
/* The root frame c ends: its values released, then it returns x (owned,
   the caller's), or with `raised` the exception set (x NULL). */
void lu_ev_leave_root(struct ctx *c, lu_v x, int raised);
int lu_ev_call_ex(struct ctx *c);
/* Function fn called with the n values args, in place of the top `nops`
   stack entries, as a CALL makes it: in line where it can be. */
int lu_ev_call_function(struct ctx *c, PyObject *fn, lu_v *args, int n, int nops);
/* The instruction as the interpreter leaves it to the kernel: kernel[kid]
   of the n args, in line, its result in place of the top `nops` stack
   entries. Only where every operand's type is known or learned: the
   kernel's function folds to what it does for them. `ret` is how the
   interpreter takes the result (LU_RETURN_PUSH; LU_RETURN_FOR_ITER, where
   a result known to be MISSING leaves the loop and g->ret_missing says
   whether the one pushed may still be it). 1 when done, 0 when
   the instruction is still to be built, -1 when the build failed. */
int lu_ev_kernel_op(struct ctx *c, int kid, lu_v *args, int n, int nops, int ret);
/* An operator instruction that has changed nothing yet, whose operands
   args (as lu_ev_kernel_op takes them) are of types not known here but
   learned to be one of a few: one way per type, in which the instruction
   is built again with the type known, so that the operator's kernel
   function folds; and a last way for any other type, which reports it and
   takes the instruction's generic path. 1 when built so, 0 when not, -1
   when the build failed. */
int lu_ev_kernel_arms(struct ctx *c, lu_v *args, int n);
int lu_ev_return(struct ctx *c);
/* eval_gen.c: a generator the unit made and resumes in line never
   exists. RETURN_GENERATOR in an inlined frame makes the frame's slots
   the fields of a virtual generator, the call's value; FOR_ITER on one
   (lu_ev_gen_step: 1 when it built the instruction, 0 when the generator
   was made real for want of room and is stepped as any other) evaluates
   its code from each code unit it may stand at to its next yield, which
   makes the frame's slots its fields again and pushes the value in the
   consumer (lu_ev_gen_yield), or to its return, which ends the
   consumer's loop (lu_ev_gen_finish binds it finished). */
static inline int
lu_ev_in_gen(const struct ctx *c)
{
    return c->adopted || LU_IS_VIRT(c->gen);
}
/* FOR_ITER on a value that is, by what the unit's entry frame shows, a
   generator that exists: its function is guarded, it is marked running,
   its frame's words become a frame of the state and its body runs in
   line as a virtual one's does; a yield stores what changed back. 1 when
   built, 0 when not taken, -1 when the build failed. */
int lu_ev_gen_adopt(struct ctx *c, lu_v it, int exit_pc, int nops);
int lu_ev_gen_takes(PyCodeObject *co);
int lu_ev_gen_make(struct ctx *c);
int lu_ev_gen_step(struct ctx *c, lu_v it, int exit_pc, int nops);
/* A call of the kernel's generator step (next() and the builtins that
   consume an iterator take each item with it) on a generator either of
   the above can run: stepped as a FOR_ITER steps it (exit_pc -1), its
   value the next item or MISSING, the call's nops operands popped. */
int lu_ev_gen_call(struct ctx *c, lu_v it, int nops);
int lu_ev_gen_yield(struct ctx *c);
void lu_ev_gen_finish(struct ctx *c);
lu_v lu_ev_gen_real(struct ctx *c, struct lu_vobj *o);
/* The code units a resume point (a generator's GEN_PC) may be: n of
   them in *pcs. A loop header's phi of one (generator o's) is registered
   with its place;
   a back-edge's resume point the header's phi does not cover is recorded
   for the next build (1 when one was). */
int lu_ev_gen_resumes(struct ctx *c, lu_v v, int **pcs);
void lu_ev_gen_header(struct ctx *c, const struct lu_vobj *o, lu_v phi, int pc, uint64_t place);
int lu_ev_gen_back_edge(struct ctx *c, lu_v phi, lu_v now);
/* A frame's return: value v, pushed on its caller's stack as the frame's
   ret says, goes on after the caller's instruction (eval_call.c). The
   frame has left the state. */
int lu_ev_returned(struct ctx *c, lu_v v);
/* Whether the unit has room for a call at c's instruction in line: its
   budget is not spent, or units of its code called out there often and
   the callee is not in line already (`again`, its frames in the state).
   A call refused is recorded for the unit to count (eval_call.c). */
int lu_ev_room(struct ctx *c, int again);
/* The call lu_ev_room let in for a hot site goes in line: the build
   takes the site past the budget only this once, and the callee's own
   calls have the room the unit began with, from where it begins.
   lu_ev_room_end takes the room back once the callee is built. */
int lu_ev_room_begin(struct ctx *c);
void lu_ev_room_end(struct ctx *c, int room);
/* An instance of tp as object.__new__ makes it: one that does not exist
   when its class allows (VK_INST, eval_attr.c). */
lu_v lu_ev_new_instance(struct ctx *c, PyTypeObject *tp);
/* That instance made, as the interpreter would have made it: object.__new__'s,
   then its attributes stored in order as on a fresh instance. */
lu_v lu_ev_instance_real(struct ctx *c, const struct lu_vobj *o, int id);
/* eval_exc.c: RAISE_VARARGS, taken by a handler of the unit where nothing
   can tell; PUSH_EXC_INFO, POP_EXCEPT and CHECK_EXC_MATCH in it. */
int lu_ev_raise(struct ctx *c);
int lu_ev_push_exc_info(struct ctx *c);
int lu_ev_pop_except(struct ctx *c);
int lu_ev_check_exc_match(struct ctx *c);
int lu_ev_get_iter(struct ctx *c);              /* eval_iter.c */
int lu_ev_for_iter(struct ctx *c);
int lu_ev_rangeiter_next(struct ctx *c, lu_v it, int nops);
/* An exact list's or tuple's iterator that does not exist, at its start;
   the kernel's seqiter_next of one, a call of nops entries, done in place
   (1), or 0 when the iterator exists. */
lu_v lu_ev_seqiter_new(struct ctx *c, lu_v seq);
int lu_ev_seqiter_next(struct ctx *c, lu_v it, int nops);
/* What a class's attributes and bases are is read as of a version
   (eval_prims.c): lu_ev_class_as_of gives the version to read at (0 for a
   static type, which has none to change), or 0 when the class has none
   to rely on; what was read stands once lu_ev_class_relied says the
   version is still the class's (`type` is the class as a constant): it
   is then a dependency of the unit. */
int lu_ev_class_as_of(struct ctx *c, PyTypeObject *t, uint32_t *version);
int lu_ev_class_relied(struct ctx *c, lu_v type, uint32_t version);
/* A call of the primitive `name` answered from the state: the result, or
   LU_NOV - 1 when the call must be made (eval_prims.c). */
#define LU_EV_NONE (INT32_MIN)
lu_v lu_ev_prim(struct ctx *c, const char *name, lu_v *args, int nargs);
PyTypeObject *lu_ev_prim_type(const char *name);
/* The call itself, where the primitive's int operands can go as machine
   words (H_prim_words): its result, else LU_EV_NONE. */
lu_v lu_ev_prim_words(struct ctx *c, const char *name, lu_v *argv, int n, int nops);

static inline PyObject *
name_at(struct ctx *c, int i)
{
    return PyTuple_GET_ITEM(c->code->co_names, i);
}

static inline int
nospec(struct ctx *c, int pc)
{
    return lu_jit_nospec(c->code, pc);
}

#endif
