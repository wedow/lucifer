/* Bytecode to block SSA (DESIGN 6.2 steps 1-3).

   The builder interprets bytecode abstractly over SSA values: a state is
   the value of every local and stack entry, plus, per entry, whether
   compiled code owns a reference for it. Blocks start at jump targets;
   a block with more than one predecessor gets a phi per local and stack
   entry, and phis that turn out trivial are removed by the optimizer.

   References follow the interpreter's, with compiled code as every
   frame's owner: a local or a stack entry is owned when compiled code
   holds a reference for it, and the root frame's slots hold nothing while
   it runs (runtime/jit/deopt.c). An instruction that consumes an operand
   it borrows releases it afterwards; one that keeps it takes a reference
   first. The optimizer deletes the pairs that a virtual box makes moot.

   A walker may write a frame's locals from inside a call that runs
   Python, or while the task is parked in one: every instruction that makes
   such a call (a release that may run a finalizer, a truth test included)
   is followed by a guard (GUARD_FRAMES) that leaves after it when one did,
   and so is the root's return when a frame object would take its
   locals.

   Speculation comes from the interpreter's caches: LOAD_GLOBAL and
   LOAD_ATTR shapes, and the operand kinds BINARY_OP, COMPARE_OP and
   STORE_SUBSCR record (runtime/jit/jit.h). Every guard leaves at the state
   before the instruction it belongs to.

   Calls of Python functions whose identity is known (a global, a method
   found through a type's version, a kernel function) inline: the callee's
   bytecode becomes blocks of the same function, its frame state names its
   caller's, and its returns join at a continuation block. Kernel code's
   module globals are constants: the kernel binds its names once at boot.
   Primitives the kernel calls become IR (float_add is ADD_F). Anything
   the builder does not compile leaves the root frame to the interpreter
   (DEOPT, a side exit), and an inlined call that meets one is not
   inlined. An exception leaves through its instruction's frame state,
   unless a handler of the unit takes it: then the instruction branches
   to the handler, whose blocks are reachable for that (catch_here). */
#include "jit/target.h"
#include "interp/exceptions.h"
#include "interp/gen.h"

#include <stdlib.h>
#include <string.h>

/* Inlining policy: code units an inlined callee compiles (one larger than
   that compiles only the branch directions it took), and inlined frames
   deep. */
#define INLINE_MAX_UNITS 400
#define INLINE_MAX_DEPTH 4
#define INLINE_MAX_TOTAL 3000

#define INTRINSIC_ASYNC_GEN_WRAP 4  /* pycore_intrinsics.h */
#define INTRINSIC_LIST_TO_TUPLE 6

struct bstate {
    lu_v *v;
    uint8_t *own;
    int sp;
};

struct bblk {
    int id;                     /* the IR block */
    int pc;
    int npreds;                 /* static predecessors among reachable code */
    int succ[2], nsucc;         /* the blocks it can jump or fall to */
    int phis;                   /* the entry state is phis */
    int exc;                    /* an exception handler: edges come from raising instructions */
    int has;                    /* the entry state is set */
    int folded;                 /* built taking the edges it had as all it gets */
    int done;
    struct bstate in;
};

/* An exception landing whose handler is a caller's (catch_here): its
   block, ended once the inline that made it holds (wire_pending), with the
   handler's frame state at the handler. */
struct pending {
    struct ctx *k;
    int block, handler;
    struct bstate st;
};

struct ctx {
    struct lu_func *f;
    struct lu_frame *live;      /* the root frame being compiled */
    struct ctx *parent;
    PyCodeObject *code;
    PyFunctionObject *func;
    PyObject *globals, *builtins;
    int kernel;                 /* kernel code: its module's globals fold */
    int ncode, nlocals, nvars;
    int *blkat;                 /* code unit -> bblk, or -1 */
    struct bblk *bb;
    int nbb;
    struct lu_fs *caller;       /* an inlined frame's caller, after the call */
    int depth;
    int cont;                   /* an inlined frame's continuation block */
    lu_v *rets;
    int nrets, caprets;
    int cur;                    /* the block being built, -1 once control cannot reach here */
    struct bstate st;
    struct lu_fs *snap;         /* the state before the current instruction */
    int pc, next;               /* the current instruction, the one after its caches */
    int opcode, base, oparg;
    lu_codeunit *cache;
    int failed;                 /* an inlined frame met what it cannot compile */
    const char *why;            /* why, for LUCIFER_JIT_DEBUG */
    int ncalls;                 /* calls it cannot inline (opaque_call) */
    int observed;               /* the instruction makes a call during which a walker may see the frame */
    uint8_t *fixed;             /* per local: a parameter the code never rebinds, whose reference
                                   stays as the frame was entered with it */
    struct pending *pend;       /* landings for a caller's handlers, wired once the inline holds */
    int npend, cappend;
    int units;                  /* code units compiled */
    struct lu_fs *island;       /* inlined for an operator: where its folded arithmetic restarts */
    int generic_call;           /* the end of a call on a polymorphic site's generic answer: no feedback */
    int cells;                  /* an inlined closure's caller set its cells (inline_call) */
    PyFunctionObject *code_fn;  /* a callee test proved code_value a function of this one's code and globals */
    lu_v code_value;
    int made;                   /* a frame compiled code never ran, made to be an inlined call's
                                   caller (class_call): nothing leaves at its own boundaries */
    int *entries;               /* a resume unit's: each resume point, and its stack depth */
    int *entry_depth;
    int nentries;
    int gen;                    /* a generator's frame resumed in line (inline_resume): how it returns */
    lu_v genv;                  /* that generator */
};

/* Per-code JIT state the builder consults: pcs not to speculate at. */
int lu_jit_nospec(PyCodeObject *code, int pc);
int lu_jit_noinline(PyCodeObject *code);
int lu_jit_next_pc(PyCodeObject *code, int pc);

static int build_block(struct ctx *c, int bi);
static int step(struct ctx *c, int pc, struct lu_block *entry);
static int call_dispatch(struct ctx *c, int n);
static int call(struct ctx *c);

/* A site's operand kinds (runtime/jit/jit.h), counted: none means the
   interpreter has never run it. Attribute sites count in cached_site. */
static int
feedback(struct ctx *c, lu_codeunit *word)
{
    int kinds = __atomic_load_n(&word->cache, __ATOMIC_RELAXED);
    c->f->sites++;
    c->f->cold += kinds == 0;
    return kinds;
}
static PyTypeObject *known_type(struct ctx *c, lu_v v);
static void set_known(struct ctx *c, lu_v v, PyTypeObject *t);

static int decode(struct ctx *c, int pc, int *base, int *oparg, int *opcode);
static int jump_target(int op, int oparg, int next);

/* The parameters the code never rebinds: per local, 1. */
static uint8_t *
fixed_params(struct ctx *c)
{
    PyCodeObject *co = c->code;
    uint8_t *fixed = lu_ir_alloc(c->f, (size_t)c->nlocals + 1);
    for (int i = 0; i < c->nlocals; i++) {
        fixed[i] = i < co->co_argcount;
    }
    for (int pc = 0; pc < c->ncode;) {
        int base, oparg, op;
        int next = decode(c, pc, &base, &oparg, &op);
        if (base == STORE_FAST || base == DELETE_FAST || base == LOAD_FAST_AND_CLEAR) {
            fixed[oparg] = 0;
        }
        else if (base == STORE_FAST_LOAD_FAST) {
            fixed[oparg >> 4] = 0;
        }
        else if (base == STORE_FAST_STORE_FAST) {
            fixed[oparg >> 4] = 0;
            fixed[oparg & 15] = 0;
        }
        else if (base == MAKE_CELL) {
            fixed[oparg] = 0;
        }
        pc = next;
    }
    return fixed;
}

/* --- states --- */

static void
state_init(struct ctx *c, struct bstate *s)
{
    s->v = lu_ir_alloc(c->f, sizeof(lu_v) * (size_t)c->nvars);
    s->own = lu_ir_alloc(c->f, (size_t)c->nvars);
    for (int i = 0; i < c->nvars; i++) {
        s->v[i] = LU_NOV;
    }
    s->sp = 0;
}

static void
state_copy(struct ctx *c, struct bstate *to, const struct bstate *from)
{
    state_init(c, to);
    memcpy(to->v, from->v, sizeof(lu_v) * (size_t)c->nvars);
    memcpy(to->own, from->own, (size_t)c->nvars);
    to->sp = from->sp;
}

static struct lu_fs *
snapshot(struct ctx *c, int pc)
{
    struct lu_fs *fs = lu_ir_alloc(c->f, sizeof(struct lu_fs));
    int n = c->nlocals + c->st.sp;
    fs->caller = c->caller;
    fs->gen = (uint8_t)c->gen;
    fs->code = c->code;
    fs->func = (PyObject *)c->func;
    fs->pc = pc;
    fs->nlocals = c->nlocals;
    fs->nstack = c->st.sp;
    fs->vals = lu_ir_alloc(c->f, sizeof(lu_v) * (size_t)(n ? n : 1));
    fs->owned = lu_ir_alloc(c->f, (size_t)(n ? n : 1));
    memcpy(fs->vals, c->st.v, sizeof(lu_v) * (size_t)c->nlocals);
    memcpy(fs->vals + c->nlocals, c->st.v + c->nlocals, sizeof(lu_v) * (size_t)c->st.sp);
    memcpy(fs->owned, c->st.own, (size_t)c->nlocals);
    memcpy(fs->owned + c->nlocals, c->st.own + c->nlocals, (size_t)c->st.sp);
    return fs;
}

/* The state before the instruction being built. */
static struct lu_fs *
before(struct ctx *c)
{
    if (c->snap == NULL) {
        c->snap = snapshot(c, c->pc);
    }
    return c->snap;
}

/* --- emitting --- */

static lu_v
emit(struct ctx *c, int op, int rep, lu_v a, lu_v b, lu_v cc, int64_t imm)
{
    if (c->cur < 0) {
        Py_FatalError("lucifer jit: an instruction where control cannot reach");
    }
    lu_v v = lu_ir_emit(c->f, c->cur, op, rep, a, b, cc, imm);
    c->f->ins[v].src = c->code;
    c->f->ins[v].src_pc = c->pc;
    if (lu_op_flags[op] & IF_GUARD) {
        c->f->ins[v].fs = c->snap;      /* it leaves before the instruction it belongs to */
    }
    c->observed |= op != OP_DECREF && lu_publishes(&c->f->ins[v]);
    return v;
}

static lu_v
guard(struct ctx *c, int op, lu_v a, int64_t imm)
{
    lu_v g = emit(c, op, REP_NONE, a, LU_NOV, LU_NOV, imm);
    c->f->ins[g].fs = before(c);
    return g;
}

/* a's exact type is t from here on. */
static void
guard_type(struct ctx *c, lu_v a, PyTypeObject *t)
{
    if (known_type(c, a) != t) {
        guard(c, OP_GUARD_TYPE, a, (int64_t)(intptr_t)t);
        set_known(c, a, t);
    }
}

/* A guard checking what its instruction's effect returned: stress never fails it. */
static lu_v
guard_after(struct ctx *c, int op, lu_v a, int64_t imm)
{
    lu_v g = guard(c, op, a, imm);
    c->f->ins[g].flags |= INS_NOSTRESS;
    return g;
}

static lu_v
konst(struct ctx *c, PyObject *o)
{
    return lu_ir_const(c->f, c->cur, o);
}

static lu_v
const_i(struct ctx *c, int64_t i)
{
    return emit(c, OP_CONST_I, REP_I64, LU_NOV, LU_NOV, LU_NOV, i);
}

static struct lu_ins *
I(struct ctx *c, lu_v v)
{
    return &c->f->ins[v];
}

/* fn's code and defaults are the ones compiled. */
static void
guard_func(struct ctx *c, PyFunctionObject *fn)
{
    lu_v g = guard(c, OP_GUARD_VERSION, konst(c, (PyObject *)fn), fn->func_version);
    I(c, g)->imm2 = GV_FUNC;
}

static PyTypeObject *known_type(struct ctx *c, lu_v v);

/* owner's type has version `version`: by its exact type when that is
   known (an instance this code made), a fact every guard of the type then
   shares, and not a use of the instance. */
static void
guard_version(struct ctx *c, lu_v owner, uint32_t version)
{
    PyTypeObject *t = known_type(c, owner);
    lu_v g = guard(c, OP_GUARD_VERSION, t != NULL ? konst(c, (PyObject *)t) : owner, version);
    I(c, g)->imm2 = t != NULL ? GV_TYPE_ITSELF : GV_TYPE_OF;
}

/* The object a value is known to be, or NULL. */
static PyObject *
known(struct ctx *c, lu_v v)
{
    return v != LU_NOV && I(c, v)->op == OP_CONST ? (PyObject *)(intptr_t)I(c, v)->imm : NULL;
}

static void
set_known(struct ctx *c, lu_v v, PyTypeObject *t)
{
    struct lu_func *f = c->f;
    if (v >= f->capknown) {
        int n = f->capknown ? f->capknown : 256;
        while (n <= v) {
            n *= 2;
        }
        f->known = realloc(f->known, sizeof(PyTypeObject *) * (size_t)n);
        f->known_block = realloc(f->known_block, sizeof(int) * (size_t)n);
        memset(f->known + f->capknown, 0, sizeof(PyTypeObject *) * (size_t)(n - f->capknown));
        f->capknown = n;
    }
    f->known[v] = t;
    f->known_block[v] = c->cur;
}

/* v is an object of exact type t wherever it is used: one an instruction
   made, not one a guard proved. */
static void
set_made(struct ctx *c, lu_v v, PyTypeObject *t)
{
    set_known(c, v, t);
    c->f->known_block[v] = -1;
}

/* Whether block `from` dominates the current block: it is reached by
   following single predecessors back from it. */
static int
dominates(struct ctx *c, int from)
{
    int b = c->cur;
    for (int steps = 0; b >= 0 && steps < 64; steps++) {
        if (b == from) {
            return 1;
        }
        if (c->f->blocks[b].npreds != 1) {
            return 0;
        }
        b = c->f->blocks[b].preds[0];
    }
    return 0;
}

/* The exact type a value is known to have, or NULL. */
static PyTypeObject *
known_type(struct ctx *c, lu_v v)
{
    if (v == LU_NOV) {
        return NULL;
    }
    if (v < c->f->capknown && c->f->known[v] != NULL
        && (c->f->known_block[v] < 0 || dominates(c, c->f->known_block[v]))) {
        return c->f->known[v];
    }
    switch (I(c, v)->op) {
    case OP_BOX_INT: return &PyLong_Type;
    case OP_BOX_FLOAT: return &PyFloat_Type;
    case OP_BOOL: return &PyBool_Type;
    case OP_CONST: return known(c, v) != NULL ? Py_TYPE(known(c, v)) : NULL;
    case OP_HELPER: return I(c, v)->imm == H_range_iter ? &PyRangeIter_Type : lu_iter_type(I(c, v));
    default: return NULL;
    }
}

/* Whether releasing a value can be skipped: immortal constants and bools. */
static int
immortal(struct ctx *c, lu_v v)
{
    PyObject *o = known(c, v);
    return v == LU_NOV || I(c, v)->op == OP_BOOL || (o != NULL && _Py_IsImmortal(o))
        || (I(c, v)->op == OP_CONST && o == NULL);
}

static void
incref(struct ctx *c, lu_v v)
{
    if (!immortal(c, v)) {
        emit(c, OP_INCREF, REP_NONE, v, LU_NOV, LU_NOV, 0);
    }
}

/* Whether releasing v cannot run a finalizer: its type makes no
   reference cycle, has no __del__ and no weak references. */
static int
plain_release(struct ctx *c, lu_v v)
{
    PyTypeObject *t = known_type(c, v);
    return t == &PyLong_Type || t == &PyFloat_Type || t == &PyBool_Type || t == &PyUnicode_Type
        || t == &PyBytes_Type || t == &PyRangeIter_Type;
}

/* A release that may run a finalizer is marked (imm 1); frames_guard
   gives it the state after its instruction. */
static void
release(struct ctx *c, lu_v v, int owned)
{
    if (owned && !immortal(c, v)) {
        int seen = !plain_release(c, v);
        emit(c, OP_DECREF, REP_NONE, v, LU_NOV, LU_NOV, seen);
        c->observed |= seen;
    }
}

static void
push(struct ctx *c, lu_v v, int owned)
{
    int i = c->nlocals + c->st.sp++;
    c->st.v[i] = v;
    c->st.own[i] = (uint8_t)owned;
}

static lu_v
pop(struct ctx *c, int *owned)
{
    int i = c->nlocals + --c->st.sp;
    if (owned != NULL) {
        *owned = c->st.own[i];
    }
    return c->st.v[i];
}

static lu_v
peek(struct ctx *c, int depth)
{
    return c->st.v[c->nlocals + c->st.sp - depth];
}

static int
peek_owned(struct ctx *c, int depth)
{
    return c->st.own[c->nlocals + c->st.sp - depth];
}

/* A value the consumer keeps: a reference of its own. */
static lu_v
take(struct ctx *c, lu_v v, int owned)
{
    if (!owned) {
        incref(c, v);
    }
    return v;
}

/* Stack entries borrowed from local i become owned before i changes. */
static void
own_borrowers(struct ctx *c, int i)
{
    lu_v old = c->st.v[i];
    if (old == LU_NOV) {
        return;
    }
    for (int k = c->nlocals; k < c->nlocals + c->st.sp; k++) {
        if (c->st.v[k] == old && !c->st.own[k]) {
            incref(c, old);
            c->st.own[k] = 1;
        }
    }
}

static void
store_local(struct ctx *c, int i, lu_v v, int owned)
{
    own_borrowers(c, i);
    v = take(c, v, owned);
    release(c, c->st.v[i], c->st.own[i]);
    c->st.v[i] = v;
    c->st.own[i] = 1;
}

static lu_v
helper(struct ctx *c, int id, int nargs, const lu_v *args)
{
    const struct lu_helper *h = &lu_helpers[id];
    c->observed |= (h->flags & (IF_PY | IF_SEEN)) != 0;
    lu_v v = emit(c, OP_HELPER, h->rep, LU_NOV, LU_NOV, LU_NOV, id);
    struct lu_ins *in = I(c, v);
    if (h->flags & IF_SEEN) {
        in->fs = before(c);     /* the state it publishes */
    }
    in->nargs = nargs;
    in->args = lu_ir_alloc(c->f, sizeof(lu_v) * (size_t)(nargs ? nargs : 1));
    memcpy(in->args, args, sizeof(lu_v) * (size_t)nargs);
    return v;
}

static int catcher(struct ctx *c, struct lu_fs *fs, struct ctx **k, int *handler, int *level, int *lasti,
                   int *at);
static void catch_here(struct ctx *c, struct lu_fs *fs, int drop, struct ctx *k, int handler, int level,
                       int lasti, int at);

/* A helper that may raise: it leaves at the state before the instruction,
   minus the `drop` stack entries the instruction consumed. With
   helper_raising, an object result raised where a handler of this unit
   takes the exception (catcher) branches there instead (catch_here). */
static lu_v
helper_leaving(struct ctx *c, int id, int nargs, const lu_v *args, int drop)
{
    lu_v v = helper(c, id, nargs, args);
    I(c, v)->fs = before(c);
    I(c, v)->drop = (uint8_t)drop;
    return v;
}

/* A helper that may raise, as the instruction at fs's pc raises: an
   exception leaves (or is caught) there, `drop` stack entries consumed. */
static lu_v
helper_raising_at(struct ctx *c, int id, int nargs, const lu_v *args, int drop, struct lu_fs *fs)
{
    lu_v v = helper(c, id, nargs, args);
    I(c, v)->fs = fs;
    I(c, v)->drop = (uint8_t)drop;
    struct ctx *k;
    int handler, level, lasti, at;
    if (lu_helpers[id].rep == REP_OBJ && catcher(c, fs, &k, &handler, &level, &lasti, &at)) {
        I(c, v)->flags |= INS_CAUGHT;
        lu_v raised = emit(c, OP_IS, REP_BIT, v, konst(c, NULL), LU_NOV, 0);
        int landing = lu_ir_block(c->f), cont = lu_ir_block(c->f);
        lu_ir_edge(c->f, c->cur, landing);
        lu_ir_edge(c->f, c->cur, cont);
        emit(c, OP_BRANCH, REP_NONE, raised, LU_NOV, LU_NOV, 0);
        c->cur = landing;
        catch_here(c, fs, drop, k, handler, level, lasti, at);
        c->cur = cont;
    }
    return v;
}

static lu_v
helper_raising(struct ctx *c, int id, int nargs, const lu_v *args, int drop)
{
    return helper_raising_at(c, id, nargs, args, drop, before(c));
}

/* Whether the loop whose FOR_ITER is at pc may step a new iterator each
   time (for_iter): only the stack holds the iterator, so only an exit sees
   it, and the optimizer keeps every one of them virtual. A generator's
   stack goes back to its frame at a yield, which would make each one: a
   loop of a generator's that yields keeps its iterator. The step
   speculates nothing, so the site's failures do not matter. */
static int
steps_fresh(struct ctx *c, int pc)
{
    int base, oparg, opcode;
    if (pc >= c->ncode || (decode(c, pc, &base, &oparg, &opcode), base != FOR_ITER)) {
        return 0;
    }
    if (!(c->code->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_ITERABLE_COROUTINE))) {
        return 1;
    }
    int end = jump_target(FOR_ITER, oparg, decode(c, pc, &base, &oparg, &opcode));
    for (int at = pc; at < end && at < c->ncode;) {
        at = decode(c, at, &base, &oparg, &opcode);
        if (base == YIELD_VALUE || base == SEND) {
            return 0;
        }
    }
    return 1;
}

/* --- leaving --- */

/* The block ends here: the rest of the instruction stream is not reached. */
static void
side_exit(struct ctx *c, int reason)
{
    lu_v d = emit(c, OP_DEOPT, REP_NONE, LU_NOV, LU_NOV, LU_NOV, reason);
    I(c, d)->fs = before(c);
    c->cur = -1;
}


/* What the tier does not compile here leaves to the interpreter: a side
   exit, which in an inlined frame rebuilds that frame and its callers. */
static void
unsupported(struct ctx *c)
{
    c->f->bailouts[c->base]++;
    side_exit(c, EXIT_SIDE);
}

/* Python-running helpers other than calls need a real frame for
   everything below them, so inlined frames leave instead. */
static int
root_only(struct ctx *c)
{
    return c->parent == NULL;
}

/* After an instruction that made a call during which a walker may see the
   frames: they are as compiled code left them. A call from an inlined
   frame runs with the inlined frames pushed as real ones (the backend's),
   which go again when it returns unless the callee kept a frame object of
   one; a walker that wrote the root's locals made its slots real. Either
   way compiled code leaves here, after the instruction, for the
   interpreter.

   The instruction's releases since `first` that may run a finalizer
   publish the locals of this state: a local stored over already names its
   new value, as the interpreter's does when it releases the old one. The
   stack's entries may not exist yet, and a release that follows a value it
   would name publishes nothing (one in a block that is not the last).
   imm2: `first`, the instruction's first IR instruction, for the backend. */
static void
frames_guard(struct ctx *c, lu_v first)
{
    lu_v g = emit(c, OP_GUARD_FRAMES, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    struct lu_fs *after = snapshot(c, c->next), *locals = NULL;
    I(c, g)->fs = after;
    I(c, g)->imm2 = first;
    for (lu_v v = first; v < g; v++) {
        struct lu_ins *in = I(c, v);
        if (in->op != OP_DECREF || !in->imm || in->fs != NULL || in->block != c->cur) {
            continue;
        }
        int defined = 1;
        for (int i = 0; i < after->nlocals; i++) {
            lu_v l = after->vals[i];
            defined &= l == LU_NOV || l < v || I(c, l)->op == OP_CONST;
        }
        if (!defined) {
            continue;
        }
        if (locals == NULL) {
            int sp = c->st.sp;
            c->st.sp = 0;
            locals = snapshot(c, c->next);
            c->st.sp = sp;
        }
        in->fs = locals;
    }
}

/* --- edges and blocks --- */

/* Every stack entry and local is owned at a block's end, so that a join's
   inputs agree, but for a parameter the code never rebinds, which is the
   same on every path. */
static void
normalize(struct ctx *c)
{
    for (int k = 0; k < c->nlocals + c->st.sp; k++) {
        if (!c->st.own[k] && c->st.v[k] != LU_NOV && !(k < c->nlocals && c->fixed[k])) {
            if (k < c->nlocals || !immortal(c, c->st.v[k])) {
                incref(c, c->st.v[k]);
            }
            c->st.own[k] = 1;
        }
    }
}

static lu_v
phi_input(struct ctx *c, lu_v v)
{
    return v != LU_NOV ? v : konst(c, NULL);
}

/* An edge from the current block, whose state is c->st, to bytecode pc. */
static void
edge_to(struct ctx *c, int pc)
{
    int bi = c->blkat[pc];
    struct bblk *b = &c->bb[bi];
    if (b->id < 0) {
        b->id = lu_ir_block(c->f);
    }
    if (!b->has) {
        state_copy(c, &b->in, &c->st);
        b->has = 1;
        if (b->npreds > 1) {
            b->phis = 1;
            for (int k = 0; k < c->nlocals + c->st.sp; k++) {
                lu_v phi = lu_ir_emit(c->f, b->id, OP_PHI, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, k);
                I(c, phi)->args = NULL;
                b->in.v[k] = phi;
                b->in.own[k] = k < c->nlocals && c->fixed[k] ? c->st.own[k] : 1;  /* as normalize left them */
                /* A root (1) or an inlined frame's (2) local's value. */
                I(c, phi)->imm2 = k < c->nlocals ? (c->parent == NULL ? 1 : 2) : 0;
            }
        }
    }
    else if (b->in.sp != c->st.sp) {
        c->f->fail = "stack depths differ at a join";
        c->failed = 1;
    }
    if (b->phis) {
        for (int k = 0; k < c->nlocals + c->st.sp; k++) {
            struct lu_ins *phi = I(c, b->in.v[k]);
            lu_v in = phi_input(c, c->st.v[k]);
            phi = I(c, b->in.v[k]);
            phi->args = realloc(phi->args, sizeof(lu_v) * (size_t)(phi->nargs + 1));
            phi->args[phi->nargs++] = in;
        }
    }
    lu_ir_edge(c->f, c->cur, b->id);
}

static void
jump_to(struct ctx *c, int pc)
{
    normalize(c);
    edge_to(c, pc);
    emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    c->cur = -1;
}

/* Branches on bit: to pc_true when set. */
static void
branch_to(struct ctx *c, lu_v bit, int pc_true, int pc_false)
{
    normalize(c);
    if (c->blkat[pc_true] == c->blkat[pc_false]) {
        edge_to(c, pc_true);
        emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    }
    else {
        edge_to(c, pc_true);
        edge_to(c, pc_false);
        emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
    }
    c->cur = -1;
}

/* --- exceptions --- */

static int
hidden(PyCodeObject *co)
{
    return (co->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN)) != 0;
}

/* Whether a block the handler bi leads to was built taking the edges it
   had as all it gets (build_block): a first edge into the handler now
   would come too late for it. */
static int
folded_after(struct ctx *c, int bi)
{
    char *seen = calloc((size_t)c->nbb, 1);
    int *work = malloc(sizeof(int) * (size_t)c->nbb), nw = 0, found = 0;
    seen[bi] = 1;
    work[nw++] = bi;
    while (nw > 0 && !found) {
        struct bblk *b = &c->bb[work[--nw]];
        for (int i = 0; i < b->nsucc; i++) {
            int s = b->succ[i];
            found |= c->bb[s].folded;
            if (!seen[s]) {
                seen[s] = 1;
                work[nw++] = s;
            }
        }
    }
    free(seen);
    free(work);
    return found;
}

/* Where an exception raised by the current instruction is handled in this
   unit, as the interpreter would unwind it: the frame k whose exception
   table covers the instruction (the call, in a caller), the handler with
   its stack level and lasti flag, and the instruction there (at). Only
   through frames that make no traceback entry (kernel code) and the root,
   whose frame is real; and only to a handler not built yet. fs is c's
   state before the instruction. */
static int
catcher(struct ctx *c, struct lu_fs *fs, struct ctx **k, int *handler, int *level, int *lasti, int *at)
{
    for (struct ctx *x = c; x != NULL; x = x->parent, fs = fs->caller) {
        if (x->parent != NULL && !hidden(x->code)) {
            return 0;
        }
        lu_codeunit *start = lu_code_start(x->code);
        int pc = (int)(lu_instr_before(x->code, start + x->next) - start);
        if (lu_exception_handler(x->code, pc, level, handler, lasti)) {
            int bi = *handler < x->ncode ? x->blkat[*handler] : -1;
            if (bi < 0 || !x->bb[bi].exc || x->bb[bi].done || *level > fs->nstack
                || (!x->bb[bi].has && folded_after(x, bi))) {
                return 0;
            }
            *k = x;
            *at = pc;
            return 1;
        }
    }
    return 0;
}

/* The landing of an exception raised by the instruction whose state is
   fs, which frame k's handler takes: the exception, with the root's
   traceback entry when the root is the frame it is caught in (kernel
   frames make none), then the frames it leaves released, then k's state at
   the handler, its stack cut to the handler's level with lasti and the
   exception pushed. Where the interpreter must unwind (a tool is watching
   exceptions, or a call kept inlined frames pushed), the landing leaves as
   the instruction's error exit. */
static void
catch_here(struct ctx *c, struct lu_fs *fs, int drop, struct ctx *k, int handler, int level,
           int lasti, int at)
{
    struct ctx *root = c;
    while (root->parent != NULL) {
        root = root->parent;
    }
    lu_v args[2] = { konst(c, (PyObject *)root->code), const_i(c, k == root && !hidden(root->code) ? at : -1) };
    lu_v exc = helper(c, H_catch, 2, args);
    I(c, exc)->fs = fs;
    I(c, exc)->drop = (uint8_t)drop;
    struct lu_fs *kfs = fs;
    for (struct ctx *x = c; x != k; x = x->parent) {
        kfs = kfs->caller;
    }
    struct bstate saved = k->st;
    int saved_cur = k->cur;
    state_init(k, &k->st);
    memcpy(k->st.v, kfs->vals, sizeof(lu_v) * (size_t)(kfs->nlocals + level));
    memcpy(k->st.own, kfs->owned, (size_t)(kfs->nlocals + level));
    k->st.sp = level;
    k->cur = c->cur;
    if (lasti) {
        PyObject *n = PyLong_FromLong(at);
        push(k, konst(k, n), 0);
        Py_DECREF(n);
    }
    push(k, exc, 1);
    normalize(k);
    /* What the frames the exception leaves held, after the handler's state took its references. */
    struct lu_fs *x = fs;
    for (; x != kfs; x = x->caller) {
        for (int i = 0; i < x->nlocals + x->nstack; i++) {
            release(c, x->vals[i], x->owned[i]);
        }
    }
    for (int i = x->nlocals + level; i < x->nlocals + x->nstack; i++) {
        release(c, x->vals[i], x->owned[i]);
    }
    if (k == c) {
        jump_to(k, handler);
    }
    else {
        /* A caller's handler: its blocks change only once the inline holds,
           so that an inline undone leaves them as they were. */
        if (c->npend == c->cappend) {
            c->cappend = c->cappend ? 2 * c->cappend : 4;
            c->pend = realloc(c->pend, sizeof(struct pending) * (size_t)c->cappend);
        }
        c->pend[c->npend++] = (struct pending){ k, c->cur, handler, k->st };
    }
    k->st = saved;
    k->cur = saved_cur;
}

/* The landings an inline that holds made for the handlers of c and its callers. */
static void
wire_pending(struct ctx *c, struct ctx *callee)
{
    for (int i = 0; i < callee->npend; i++) {
        struct pending *p = &callee->pend[i];
        if (p->k != c) {
            if (c->npend == c->cappend) {
                c->cappend = c->cappend ? 2 * c->cappend : 4;
                c->pend = realloc(c->pend, sizeof(struct pending) * (size_t)c->cappend);
            }
            c->pend[c->npend++] = *p;
            continue;
        }
        struct bstate saved = c->st;
        int saved_cur = c->cur;
        c->st = p->st;
        c->cur = p->block;
        jump_to(c, p->handler);
        c->st = saved;
        c->cur = saved_cur;
    }
}

/* --- bytecode --- */

static int
opcode_at(struct ctx *c, int pc)
{
    return __atomic_load_n(&lu_code_start(c->code)[pc].op.code, __ATOMIC_RELAXED);
}

/* Decodes the instruction at pc (EXTENDED_ARG prefixes included): its
   base opcode and oparg, and the pc after its caches. */
static int
decode(struct ctx *c, int pc, int *base, int *oparg, int *opcode)
{
    lu_codeunit *start = lu_code_start(c->code);
    int arg = 0;
    while (lu_base_opcode(c->code, pc) == EXTENDED_ARG) {
        arg = arg << 8 | start[pc].op.arg;
        pc++;
    }
    *base = lu_base_opcode(c->code, pc);
    *opcode = opcode_at(c, pc);
    *oparg = arg << 8 | start[pc].op.arg;
    return pc + 1 + lu_opcode_caches[*base];
}

/* The first cache word of the instruction at pc, past its EXTENDED_ARGs. */
static lu_codeunit *
cache_of(struct ctx *c, int pc)
{
    while (lu_base_opcode(c->code, pc) == EXTENDED_ARG) {
        pc++;
    }
    return lu_code_start(c->code) + pc + 1;
}

/* The opcodes the builder compiles at all; anything else is a side exit. */
static int
supported(int op)
{
    switch (op) {
    case NOP: case NOT_TAKEN: case RESUME: case LOAD_FAST: case LOAD_FAST_BORROW:
    case LOAD_FAST_CHECK: case LOAD_FAST_LOAD_FAST: case LOAD_FAST_BORROW_LOAD_FAST_BORROW:
    case STORE_FAST: case STORE_FAST_LOAD_FAST: case STORE_FAST_STORE_FAST:
    case LOAD_CONST: case LOAD_SMALL_INT: case POP_TOP: case PUSH_NULL: case COPY: case SWAP:
    case END_FOR: case POP_ITER: case LOAD_GLOBAL: case LOAD_ATTR: case STORE_ATTR:
    case BINARY_OP: case COMPARE_OP: case TO_BOOL: case UNARY_NOT: case UNARY_NEGATIVE:
    case IS_OP: case POP_JUMP_IF_FALSE: case POP_JUMP_IF_TRUE: case POP_JUMP_IF_NONE:
    case POP_JUMP_IF_NOT_NONE: case JUMP_FORWARD: case JUMP_BACKWARD:
    case JUMP_BACKWARD_NO_INTERRUPT: case GET_ITER: case FOR_ITER: case CALL:
    case RETURN_VALUE: case STORE_SUBSCR: case COPY_FREE_VARS: case LOAD_DEREF:
    case BUILD_LIST: case BUILD_TUPLE: case CONTAINS_OP: case UNPACK_SEQUENCE:
    case MAKE_FUNCTION: case SET_FUNCTION_ATTRIBUTE: case STORE_DEREF: case LIST_EXTEND:
    case DELETE_SUBSCR: case LOAD_FAST_AND_CLEAR: case LIST_APPEND: case SET_ADD: case MAP_ADD:
    case BUILD_SET: case BUILD_MAP: case CALL_INTRINSIC_1: case BINARY_SLICE: case STORE_SLICE:
    case LOAD_COMMON_CONSTANT: case CALL_KW: case CALL_FUNCTION_EX: case DICT_MERGE: case DICT_UPDATE:
    case PUSH_EXC_INFO: case POP_EXCEPT: case CHECK_EXC_MATCH: case RAISE_VARARGS: case RERAISE: case DELETE_FAST:
    case LOAD_SUPER_ATTR: case STORE_GLOBAL: case BUILD_SLICE: case CONVERT_VALUE: case FORMAT_SIMPLE:
    case FORMAT_WITH_SPEC: case BUILD_STRING: case LOAD_SPECIAL:
    case YIELD_VALUE: case SEND: case END_SEND: case GET_AWAITABLE:
    case GET_YIELD_FROM_ITER: case GET_AITER: case GET_ANEXT:
        return 1;
    default:
        return 0;
    }
}

static int
is_jump(int op)
{
    return op == JUMP_FORWARD || op == JUMP_BACKWARD || op == JUMP_BACKWARD_NO_INTERRUPT;
}

static int
is_branch(int op)
{
    return op == POP_JUMP_IF_FALSE || op == POP_JUMP_IF_TRUE || op == POP_JUMP_IF_NONE
        || op == POP_JUMP_IF_NOT_NONE || op == FOR_ITER || op == SEND;
}

/* Instructions after which control never falls through (in compiled
   code: a yield leaves it, and the frame goes on at a resume point). */
static int
terminal(int op)
{
    return op == RETURN_VALUE || op == RAISE_VARARGS || op == RERAISE || op == YIELD_VALUE;
}

static int
jump_target(int op, int oparg, int next)
{
    return op == JUMP_FORWARD ? next + oparg
        : op == FOR_ITER ? next + oparg + 1
        : (op == JUMP_BACKWARD || op == JUMP_BACKWARD_NO_INTERRUPT) ? next - oparg
        : next + oparg;
}

/* Finds the blocks reachable from entry and their static predecessor
   counts. Returns -1 on bytecode it cannot follow. */
static int
scan(struct ctx *c, int entry)
{
    int n = c->ncode;
    char *reach = calloc((size_t)n + 1, 1), *start = calloc((size_t)n + 1, 1);
    char *ins = calloc((size_t)n + 1, 1), *exc = calloc((size_t)n + 1, 1);
    int *preds = calloc((size_t)n + 1, sizeof(int));
    int *work = malloc(sizeof(int) * (((size_t)n + 1) * 3 + (size_t)c->nentries)), nw = 0;
    c->blkat = lu_ir_alloc(c->f, sizeof(int) * ((size_t)n + 1));
    start[entry] = 1;
    preds[entry] = 1;
    work[nw++] = entry;
    for (int i = 0; i < c->nentries; i++) {
        start[c->entries[i]] = 1;
        work[nw++] = c->entries[i];
    }
    for (;;) {
        while (nw > 0) {
            int pc = work[--nw];
            while (pc < n && !reach[pc]) {
                int base, oparg, opcode;
                reach[pc] = 1;
                ins[pc] = 1;
                int next = decode(c, pc, &base, &oparg, &opcode);
                int ends = terminal(base) || !supported(base) || is_jump(base);
                if (is_jump(base) || is_branch(base)) {
                    int t = jump_target(base, oparg, next);
                    if (t < 0 || t >= n) {
                        free(reach); free(start); free(ins); free(exc); free(preds); free(work);
                        return -1;
                    }
                    start[t] = 1;
                    preds[t]++;
                    work[nw++] = t;
                }
                if (is_branch(base)) {
                    start[next] = 1;
                    preds[next]++;
                    work[nw++] = next;
                    break;
                }
                if (ends) {
                    break;
                }
                for (int k = pc + 1; k < next; k++) {
                    reach[k] = 1;   /* EXTENDED_ARG's instruction and caches */
                }
                if (next < n && start[next]) {
                    preds[next]++;
                    work[nw++] = next;
                    break;
                }
                pc = next;
            }
            if (pc < n && reach[pc] && start[pc] && nw >= 0) {
                /* reached an already scanned block start by fall-through: counted above */
            }
        }
        /* The handlers of what is reachable are reachable: raising
           instructions may branch to them (catch_here). */
        int found = 0;
        for (int pc = 0; pc < n; pc++) {
            int level, handler, lasti;
            if (ins[pc] && lu_exception_handler(c->code, pc, &level, &handler, &lasti)
                && handler >= 0 && handler < n && !exc[handler]) {
                exc[handler] = start[handler] = 1;
                work[nw++] = handler;
                found = 1;
            }
        }
        if (!found) {
            break;
        }
    }
    /* A fall-through into a block start found later is a predecessor too. */
    for (int pc = 0; pc < n; pc++) {
        c->blkat[pc] = -1;
    }
    c->nbb = 0;
    for (int pc = 0; pc < n; pc++) {
        if (start[pc] && reach[pc]) {
            c->nbb++;
        }
    }
    c->bb = lu_ir_alloc(c->f, sizeof(struct bblk) * (size_t)(c->nbb ? c->nbb : 1));
    int k = 0;
    for (int pc = 0; pc < n; pc++) {
        if (start[pc] && reach[pc]) {
            c->bb[k].pc = pc;
            c->bb[k].npreds = preds[pc];
            c->bb[k].exc = exc[pc];
            c->bb[k].id = -1;
            c->blkat[pc] = k++;
        }
    }
    free(reach);
    free(start);
    free(ins);
    free(exc);
    free(preds);
    free(work);
    return 0;
}

/* Recounts fall-through predecessors exactly: a block start reached by
   falling out of the instruction before it. */
static void
count_fallthrough(struct ctx *c)
{
    for (int k = 0; k < c->nbb; k++) {
        c->bb[k].npreds = 0;
        c->bb[k].nsucc = 0;
    }
    for (int k = 0; k < c->nbb; k++) {
        int pc = c->bb[k].pc;
        for (;;) {
            int base, oparg, opcode;
            int next = decode(c, pc, &base, &oparg, &opcode);
            if (is_jump(base) || is_branch(base)) {
                int t = jump_target(base, oparg, next);
                if (t >= 0 && t < c->ncode && c->blkat[t] >= 0) {
                    c->bb[c->blkat[t]].npreds++;
                    if (c->bb[k].nsucc < 2) {
                        c->bb[k].succ[c->bb[k].nsucc++] = c->blkat[t];
                    }
                }
            }
            if (terminal(base) || !supported(base) || is_jump(base)) {
                break;
            }
            if (next >= c->ncode) {
                break;
            }
            if (c->blkat[next] >= 0) {
                c->bb[c->blkat[next]].npreds++;
                if (c->bb[k].nsucc < 2) {
                    c->bb[k].succ[c->bb[k].nsucc++] = c->blkat[next];
                }
                break;
            }
            pc = next;
        }
    }
    for (int k = 0; k < c->nbb; k++) {
        if (c->bb[k].exc) {
            c->bb[k].npreds += 2;       /* any number of raising instructions: a join */
        }
    }
}

/* --- kernel lookups the builder folds --- */

/* A kernel global's value, a new reference, or NULL. */
static PyObject *
kernel_global(struct ctx *c, PyObject *name)
{
    PyObject *v = lu_ns_get(c->globals, name);
    if (v == NULL) {
        v = lu_ns_get(c->builtins, name);
    }
    return v;
}

/* The primitive a callable is, by name, or NULL. */
static const char *
primitive_name(PyObject *o)
{
    if (o == NULL || !PyCFunction_Check(o) || lu_kernel_def_of(((PyCFunctionObject *)o)->m_ml) != NULL) {
        return NULL;
    }
    PyObject *m = ((PyCFunctionObject *)o)->m_self;
    if (m == NULL || !PyModule_Check(m)) {
        return NULL;
    }
    const char *mod = PyModule_GetName(m);
    if (mod == NULL) {
        PyErr_Clear();
        return NULL;
    }
    return strcmp(mod, "_lucifer") == 0 ? ((PyCFunctionObject *)o)->m_ml->ml_name : NULL;
}

/* The Python function a kernel callable runs, or NULL. */
static PyFunctionObject *
kernel_function(PyObject *o, PyObject **self)
{
    *self = NULL;
    PyObject *fn = o != NULL ? lu_kernel_function_of(o, self) : NULL;
    return fn != NULL && PyFunction_Check(fn) ? (PyFunctionObject *)fn : NULL;
}

/* A value's kind for speculation when its site saw nothing yet: its known
   type, or one every current input of its phi has. */
static int
guess_kind(struct ctx *c, lu_v v)
{
    PyTypeObject *t = known_type(c, v);
    if (t == NULL && v != LU_NOV && I(c, v)->op == OP_PHI && I(c, v)->nargs > 0) {
        t = known_type(c, I(c, v)->args[0]);
        for (int i = 1; i < I(c, v)->nargs; i++) {
            if (known_type(c, I(c, v)->args[i]) != t) {
                t = NULL;
            }
        }
    }
    return t == &PyLong_Type ? LU_KIND_INT : t == &PyFloat_Type ? LU_KIND_FLOAT
        : t == &PyList_Type ? LU_KIND_LIST : t == &PyTuple_Type ? LU_KIND_TUPLE
        : t == &PyUnicode_Type ? LU_KIND_STR : t == &PyBytes_Type ? LU_KIND_BYTES : 0;
}

/* --- arithmetic --- */

/* An int as a machine word. A constant that fits 64 bits is itself, one
   digit or not: unboxing a constant past one digit would always leave
   (the kernel's range checks against _SSIZE_MAX). */
static lu_v
unbox_int(struct ctx *c, lu_v v)
{
    if (I(c, v)->op == OP_BOX_INT) {
        return I(c, v)->a;
    }
    PyObject *o = known(c, v);
    if (o != NULL && PyLong_CheckExact(o)) {
        int overflow;
        long long x = PyLong_AsLongLongAndOverflow(o, &overflow);
        if (!overflow) {
            return const_i(c, x);
        }
    }
    lu_v u = emit(c, OP_UNBOX_INT, REP_I64, v, LU_NOV, LU_NOV, 0);
    I(c, u)->fs = before(c);
    return u;
}

static lu_v
unbox_float(struct ctx *c, lu_v v)
{
    if (I(c, v)->op == OP_BOX_FLOAT) {
        return I(c, v)->a;
    }
    PyObject *o = known(c, v);
    if (o != NULL && PyFloat_CheckExact(o)) {
        double d = PyFloat_AS_DOUBLE(o);
        int64_t bits;
        memcpy(&bits, &d, 8);
        return emit(c, OP_CONST_F, REP_F64, LU_NOV, LU_NOV, LU_NOV, bits);
    }
    lu_v u = emit(c, OP_UNBOX_FLOAT, REP_F64, v, LU_NOV, LU_NOV, 0);
    I(c, u)->fs = before(c);
    return u;
}

/* A slice bound as a machine int: a box's raw value or a constant that
   fits 64 bits, or None (`bit` set in *none). 0 for anything else. */
static int
raw_bound(struct ctx *c, lu_v v, lu_v *raw, int *none, int bit)
{
    PyObject *o = known(c, v);
    if (o == Py_None) {
        *none |= bit;
        *raw = const_i(c, 0);
        return 1;
    }
    if (I(c, v)->op != OP_BOX_INT && (o == NULL || !PyLong_CheckExact(o))) {
        return 0;
    }
    if (I(c, v)->op != OP_BOX_INT) {
        int overflow;
        PyLong_AsLongLongAndOverflow(o, &overflow);
        if (overflow) {
            return 0;
        }
    }
    *raw = unbox_int(c, v);
    return 1;
}

static lu_v
box_int(struct ctx *c, lu_v i)
{
    return emit(c, OP_BOX_INT, REP_OBJ, i, LU_NOV, LU_NOV, 0);
}

static lu_v
box_float(struct ctx *c, lu_v d)
{
    return emit(c, OP_BOX_FLOAT, REP_OBJ, d, LU_NOV, LU_NOV, 0);
}

/* The object o as a box of its raw value: o itself when built, so that
   identity holds (a guard proved o is that exact type with that value). */
static lu_v
rebox(struct ctx *c, lu_v o, PyTypeObject *t)
{
    lu_v b = t == &PyLong_Type ? box_int(c, unbox_int(c, o)) : box_float(c, unbox_float(c, o));
    I(c, b)->b = o;
    return b;
}

/* True or False for a bit, folded when the bit is constant. */
static lu_v
bool_of(struct ctx *c, lu_v bit)
{
    if (I(c, bit)->op == OP_CONST_I) {
        return konst(c, I(c, bit)->imm ? Py_True : Py_False);
    }
    return emit(c, OP_BOOL, REP_OBJ, bit, LU_NOV, LU_NOV, 0);
}

/* a is b, folded when the answer is known: two constants, or a value of
   a known type against a constant of another type. */
static lu_v
is_of(struct ctx *c, lu_v a, lu_v b)
{
    PyObject *ka = known(c, a), *kb = known(c, b);
    PyTypeObject *ta = known_type(c, a), *tb = known_type(c, b);
    if ((ka != NULL || I(c, a)->op == OP_CONST) && (kb != NULL || I(c, b)->op == OP_CONST)) {
        return const_i(c, ka == kb);
    }
    if ((kb != NULL && ta != NULL && ta != Py_TYPE(kb)) || (ka != NULL && tb != NULL && tb != Py_TYPE(ka))) {
        return const_i(c, 0);
    }
    return emit(c, OP_IS, REP_BIT, a, b, LU_NOV, 0);
}

static lu_v
not_of(struct ctx *c, lu_v bit)
{
    if (I(c, bit)->op == OP_CONST_I) {
        return const_i(c, !I(c, bit)->imm);
    }
    return emit(c, OP_NOT, REP_BIT, bit, LU_NOV, LU_NOV, 0);
}

/* Whether v is a list display built in this block that nothing since
   could have shown another thread: no instruction after it published a
   state or ran Python, and no local holds it. */
static int
unseen_display(struct ctx *c, lu_v v)
{
    struct lu_ins *in = I(c, v);
    if (in->op != OP_HELPER || in->imm != H_build_list || in->block != c->cur) {
        return 0;
    }
    for (int i = 0; i < c->nlocals; i++) {
        if (c->st.v[i] == v) {
            return 0;
        }
    }
    struct lu_block *bl = &c->f->blocks[c->cur];
    int k = bl->nins - 1;
    for (; k >= 0 && bl->ins[k] != v; k--) {
        struct lu_ins *u = I(c, bl->ins[k]);
        if (lu_publishes(u) || (u->op == OP_HELPER && (lu_helpers[u->imm].flags & IF_PY))) {
            return 0;
        }
    }
    return k >= 0;
}

static lu_v
int_op(struct ctx *c, int op, lu_v a, lu_v b)
{
    static const int ops[] = {
        [NB_ADD] = OP_ADD_I, [NB_SUBTRACT] = OP_SUB_I, [NB_MULTIPLY] = OP_MUL_I,
        [NB_AND] = OP_AND_I, [NB_OR] = OP_OR_I, [NB_XOR] = OP_XOR_I,
        [NB_LSHIFT] = OP_SHL_I, [NB_RSHIFT] = OP_SAR_I, [NB_FLOOR_DIVIDE] = OP_FLOORDIV_I,
        [NB_REMAINDER] = OP_MOD_I,
    };
    if (op >= (int)(sizeof ops / sizeof ops[0]) || ops[op] == 0) {
        return LU_NOV;
    }
    lu_v r = emit(c, ops[op], REP_I64, a, b, LU_NOV, 0);
    if (lu_op_flags[ops[op]] & IF_GUARD) {
        I(c, r)->fs = before(c);
    }
    return r;
}

/* The names of BINARY_OP's operators, for the kernel lookups. */
static const char *const binary_names[] = {
    [NB_ADD] = "__add__", [NB_SUBTRACT] = "__sub__", [NB_MULTIPLY] = "__mul__",
    [NB_TRUE_DIVIDE] = "__truediv__",
};

static const char *const compare_names[] = {
    "__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__",
};

static int inline_call(struct ctx *c, PyFunctionObject *fn, lu_v fnv, int fnv_owned, lu_v *args, uint8_t *owned,
                       int nargs, int nkwonly, lu_v *result, int *result_owned);

/* A kernel method of a static type, inlined as the interpreter would run
   it for operands of that exact type. */
static int
kernel_method(struct ctx *c, PyTypeObject *tp, const char *name, lu_v *args, int nargs,
              lu_v *result, int *owned)
{
    PyObject *key = PyUnicode_InternFromString(name);
    PyObject *descr = key != NULL ? _PyType_Lookup(tp, key) : NULL;
    Py_XDECREF(key);
    PyObject *self;
    PyFunctionObject *fn = kernel_function(descr, &self);
    uint8_t own[4] = { 0, 0, 0, 0 };
    if (fn == NULL || self != NULL) {
        return 0;
    }
    return inline_call(c, fn, LU_NOV, 0, args, own, nargs, 0, result, owned);
}

/* BINARY_OP on floats, or a float and an int: the operator's method of
   float, inlined (binary_op's dispatch picks it for two exact floats). */
static int
float_binary(struct ctx *c, int base, lu_v a, lu_v b, int lk, int rk, lu_v *result, int *owned)
{
    if (base >= (int)(sizeof binary_names / sizeof binary_names[0]) || binary_names[base] == NULL) {
        return 0;
    }
    lu_v fa = lk == LU_KIND_INT ? emit(c, OP_I2F, REP_F64, unbox_int(c, a), LU_NOV, LU_NOV, 0) : unbox_float(c, a);
    lu_v fb = rk == LU_KIND_INT ? emit(c, OP_I2F, REP_F64, unbox_int(c, b), LU_NOV, LU_NOV, 0) : unbox_float(c, b);
    lu_v args[2] = { box_float(c, fa), box_float(c, fb) };
    return kernel_method(c, &PyFloat_Type, binary_names[base], args, 2, result, owned);
}

static int str_item(struct ctx *c, lu_v a, lu_v b, int owna, int ownb);

static int
binary_op(struct ctx *c)
{
    int owna, ownb;
    lu_v b = peek(c, 1), a = peek(c, 2);
    owna = peek_owned(c, 2);
    ownb = peek_owned(c, 1);
    int kinds = feedback(c, c->cache + 1);
    int lk = kinds & 0xff, rk = kinds >> 8;
    int op = c->oparg;
    int base = op >= NB_INPLACE_ADD && op != NB_SUBSCR ? op - NB_INPLACE_ADD : op;
    lu_v r = LU_NOV;
    int rown = 1;
    if (lk == 0) lk = guess_kind(c, a);
    if (rk == 0) rk = guess_kind(c, b);
    if (lk == 0 && (rk == LU_KIND_INT || rk == LU_KIND_FLOAT)) lk = rk;
    if (rk == 0 && (lk == LU_KIND_INT || lk == LU_KIND_FLOAT) && op != NB_SUBSCR) rk = lk;
    if (known_type(c, a) == &PyTuple_Type) lk = LU_KIND_TUPLE;
    if (known_type(c, a) == &PyList_Type) lk = LU_KIND_LIST;
    if (lu_jit_nospec(c->code, c->pc)) {
        lk = rk = LU_KIND_OTHER;
    }
    if (known_type(c, a) == &PyLong_Type) lk = LU_KIND_INT;
    if (known_type(c, a) == &PyFloat_Type) lk = LU_KIND_FLOAT;
    if (known_type(c, b) == &PyLong_Type) rk = LU_KIND_INT;
    if (known_type(c, b) == &PyFloat_Type) rk = LU_KIND_FLOAT;
    if (op == NB_SUBSCR) {
        if ((lk == LU_KIND_LIST || lk == LU_KIND_TUPLE) && rk == LU_KIND_INT) {
            guard_type(c, a, lk == LU_KIND_LIST ? &PyList_Type : &PyTuple_Type);
            lu_v i = unbox_int(c, b);
            r = emit(c, OP_LIST_ITEM, REP_OBJ, a, i, LU_NOV, lk == LU_KIND_TUPLE);
            I(c, r)->fs = before(c);
        }
        else if (lk == LU_KIND_BYTES && rk == LU_KIND_INT) {
            guard_type(c, a, &PyBytes_Type);
            lu_v byte = emit(c, OP_BYTES_ITEM, REP_I64, a, unbox_int(c, b), LU_NOV, 0);
            I(c, byte)->fs = before(c);
            r = box_int(c, byte);
        }
        else if (lk == LU_KIND_STR && rk == LU_KIND_INT && str_item(c, a, b, owna, ownb)) {
            return 0;
        }
    }
    else if (lk == LU_KIND_INT && rk == LU_KIND_INT && base == NB_TRUE_DIVIDE) {
        lu_v x = emit(c, OP_I2F, REP_F64, unbox_int(c, a), LU_NOV, LU_NOV, 0);
        lu_v y = emit(c, OP_I2F, REP_F64, unbox_int(c, b), LU_NOV, LU_NOV, 0);
        I(c, x)->fs = I(c, y)->fs = before(c);
        I(c, x)->imm = I(c, y)->imm = 1;      /* exact only below 2**53: guarded */
        lu_v args[2] = { box_float(c, x), box_float(c, y) };
        c->island = before(c);
        if (!kernel_method(c, &PyFloat_Type, "__truediv__", args, 2, &r, &rown)) {
            r = LU_NOV;
        }
        c->island = NULL;
    }
    else if (lk == LU_KIND_INT && rk == LU_KIND_INT) {
        lu_v x = unbox_int(c, a), y = unbox_int(c, b);
        lu_v z = int_op(c, base, x, y);
        r = z != LU_NOV ? box_int(c, z) : LU_NOV;
    }
    else if ((lk == LU_KIND_FLOAT || lk == LU_KIND_INT) && (rk == LU_KIND_FLOAT || rk == LU_KIND_INT)) {
        /* The float method's arithmetic leaves at the operator, whose
           operands are still on the stack: a state inside the method
           would resume after the operator with them there. */
        c->island = before(c);
        if (!float_binary(c, base, a, b, lk, rk, &r, &rown)) {
            r = LU_NOV;
        }
        c->island = NULL;
    }
    else if (op == NB_MULTIPLY && ((lk == LU_KIND_LIST && rk == LU_KIND_INT) || (lk == LU_KIND_INT && rk == LU_KIND_LIST))) {
        /* list.__mul__ and __rmul__ of an exact list and an int: the copies
           without the dispatch. */
        lu_v list = lk == LU_KIND_LIST ? a : b, n = lk == LU_KIND_LIST ? b : a;
        guard_type(c, list, &PyList_Type);
        lu_v args[2] = { list, unbox_int(c, n) };
        r = helper_raising(c, unseen_display(c, list) ? H_list_repeat_fresh : H_list_repeat, 2, args, 2);
        set_made(c, r, &PyList_Type);
    }
    else if (lk == LU_KIND_STR && rk == LU_KIND_STR && base == NB_ADD) {
        /* Two exact strs concatenate without running Python. */
        guard_type(c, a, &PyUnicode_Type);
        guard_type(c, b, &PyUnicode_Type);
        lu_v args[2] = { a, b };
        r = helper_raising(c, H_str_concat, 2, args, 2);
    }
    if (c->failed) {
        return -1;
    }
    if (r == LU_NOV) {
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[3] = { a, b, const_i(c, op) };
        r = helper_raising(c, H_binary_op, 3, args, 2);
    }
    pop(c, NULL);
    pop(c, NULL);
    release(c, a, owna);
    release(c, b, ownb);
    push(c, r, rown);
    return 0;
}

static int
compare_op(struct ctx *c)
{
    lu_v b = peek(c, 1), a = peek(c, 2);
    int owna = peek_owned(c, 2), ownb = peek_owned(c, 1);
    int kinds = feedback(c, c->cache);
    int lk = kinds & 0xff, rk = kinds >> 8;
    int op = c->oparg >> 5;
    lu_v bit = LU_NOV, r = LU_NOV;
    int rown = 1;
    if (lk == 0) lk = guess_kind(c, a);
    if (rk == 0) rk = guess_kind(c, b);
    if (lk == 0 && (rk == LU_KIND_INT || rk == LU_KIND_FLOAT)) lk = rk;
    if (rk == 0 && (lk == LU_KIND_INT || lk == LU_KIND_FLOAT)) rk = lk;
    if (lu_jit_nospec(c->code, c->pc)) {
        lk = rk = LU_KIND_OTHER;
    }
    if (known_type(c, a) == &PyLong_Type) lk = LU_KIND_INT;
    if (known_type(c, a) == &PyFloat_Type) lk = LU_KIND_FLOAT;
    if (known_type(c, b) == &PyLong_Type) rk = LU_KIND_INT;
    if (known_type(c, b) == &PyFloat_Type) rk = LU_KIND_FLOAT;
    if (lk == LU_KIND_INT && rk == LU_KIND_INT) {
        bit = emit(c, OP_CMP_I, REP_BIT, unbox_int(c, a), unbox_int(c, b), LU_NOV, op);
    }
    else if ((lk == LU_KIND_FLOAT || lk == LU_KIND_INT) && (rk == LU_KIND_FLOAT || rk == LU_KIND_INT)) {
        /* compare's dispatch for two exact floats is float's own method. */
        lu_v fa = lk == LU_KIND_INT ? emit(c, OP_I2F, REP_F64, unbox_int(c, a), LU_NOV, LU_NOV, 0) : unbox_float(c, a);
        lu_v fb = rk == LU_KIND_INT ? emit(c, OP_I2F, REP_F64, unbox_int(c, b), LU_NOV, LU_NOV, 0) : unbox_float(c, b);
        if (lk == LU_KIND_INT || rk == LU_KIND_INT) {
            /* float_richcompare compares an int exactly, not through a double. */
            I(c, lk == LU_KIND_INT ? fa : fb)->fs = before(c);
            I(c, lk == LU_KIND_INT ? fa : fb)->imm = 1;
        }
        lu_v args[2] = { box_float(c, fa), box_float(c, fb) };
        if (!kernel_method(c, &PyFloat_Type, compare_names[op], args, 2, &r, &rown)) {
            r = LU_NOV;
        }
    }
    else if ((op == Py_EQ || op == Py_NE) && lk && rk && !((lk | rk) & ~(LU_KIND_NONE | LU_KIND_OTHER))
             && ((lk | rk) & LU_KIND_NONE)) {
        /* None against objects whose type keeps object's __eq__: identity. */
        lu_v args[3] = { a, b, const_i(c, op) };
        lu_v ok = helper(c, H_compare_ideq, 3, args);
        guard(c, OP_GUARD_BIT, ok, 1);
        bit = is_of(c, a, b);
        if (op == Py_NE) {
            bit = not_of(c, bit);
        }
    }
    if (c->failed) {
        return -1;
    }
    if (bit != LU_NOV) {
        r = bool_of(c, bit);
        rown = 0;
    }
    if (r == LU_NOV) {
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[3] = { a, b, const_i(c, c->oparg) };
        r = helper_raising(c, H_compare_op, 3, args, 2);
    }
    pop(c, NULL);
    pop(c, NULL);
    release(c, a, owna);
    release(c, b, ownb);
    push(c, r, rown);
    return 0;
}

/* --- truth --- */

static lu_v
truth(struct ctx *c, lu_v v)
{
    if (I(c, v)->op == OP_BOOL) {
        return I(c, v)->a;
    }
    PyObject *k = known(c, v);
    if (k == Py_True || k == Py_False || k == Py_None) {
        return const_i(c, k == Py_True);
    }
    PyTypeObject *t = known_type(c, v);
    if (t == &PyLong_Type) {
        return emit(c, OP_CMP_I, REP_BIT, unbox_int(c, v), const_i(c, 0), LU_NOV, Py_NE);
    }
    lu_v bit = emit(c, OP_TRUTH, REP_BIT, v, LU_NOV, LU_NOV, 0);
    I(c, bit)->fs = before(c);
    I(c, bit)->drop = 1;
    if (c->parent != NULL) {
        /* An inlined frame's truth must not run Python: only a layout's
           own (lu_truth_exact) is answered without it. */
        I(c, bit)->imm = 1;
    }
    c->observed |= c->parent == NULL;
    return bit;
}

/* --- calls --- */

/* Why fn's code cannot inline here, or NULL: a call with nargs positional
   arguments, or with nkwonly as well its keyword-only parameters' values. */
static const char *
not_inlinable(struct ctx *c, PyFunctionObject *fn, int nargs, int nkwonly)
{
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    int defaults = fn->func_defaults != NULL ? (int)PyTuple_GET_SIZE(fn->func_defaults) : 0;
    if (!PyCode_Check(co) || !(co->co_flags & CO_OPTIMIZED) || co->_co_monitoring != NULL) {
        return "not plain code";
    }
    if (co->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_ITERABLE_COROUTINE)) {
        return "a generator";
    }
    if (co->co_kwonlyargcount != nkwonly || (co->co_argcount < nargs && !(co->co_flags & CO_VARARGS)) || nargs > 30
        || nargs + defaults < co->co_argcount) {
        return "arguments it does not bind";
    }
    if (c->depth >= INLINE_MAX_DEPTH || c->f->ninlined > INLINE_MAX_TOTAL) {
        return "the inline budget";
    }
    if (lu_jit_noinline(co)) {
        return "its inlined copies kept leaving";
    }
    for (struct ctx *p = c; p != NULL; p = p->parent) {
        if (p->code == co) {
            return "recursion";
        }
    }
    return NULL;
}

static int
build_ctx(struct ctx *c, int entry_block);
static int resume_points(struct ctx *c);

/* What an attempt that may be undone has emitted from: an undo drops the
   blocks, instructions and constants made since, and the types proved
   since (the ids are reused). */
struct mark {
    int cur, nins, nblocks, nconsts, cur_nins, nsucc, inlined, sites, cold, ngens;
    PyTypeObject **known;
    int *known_block, nknown;
};

static void
mark_take(struct ctx *c, struct mark *m)
{
    struct lu_func *f = c->f;
    *m = (struct mark){ c->cur, f->nins, f->nblocks, f->nconsts, f->blocks[c->cur].nins,
                        f->blocks[c->cur].nsucc, f->ninlined, f->sites, f->cold, f->ngens, NULL, NULL, 0 };
    m->nknown = f->nins < f->capknown ? f->nins : f->capknown;
    if (m->nknown > 0) {
        m->known = malloc(sizeof(PyTypeObject *) * (size_t)m->nknown);
        m->known_block = malloc(sizeof(int) * (size_t)m->nknown);
        memcpy(m->known, f->known, sizeof(PyTypeObject *) * (size_t)m->nknown);
        memcpy(m->known_block, f->known_block, sizeof(int) * (size_t)m->nknown);
    }
}

static void
mark_free(struct mark *m)
{
    free(m->known);
    free(m->known_block);
}

static void
mark_undo(struct ctx *c, struct mark *m)
{
    struct lu_func *f = c->f;
    for (int i = m->nblocks; i < f->nblocks; i++) {
        free(f->blocks[i].ins);
        free(f->blocks[i].preds);
    }
    for (int i = m->nins; i < f->nins; i++) {
        if (f->ins[i].op == OP_PHI) {
            free(f->ins[i].args);
        }
    }
    for (int i = m->nconsts; i < f->nconsts; i++) {
        Py_XDECREF(f->consts[i]);
    }
    for (int i = 0; i < f->capknown; i++) {
        f->known[i] = i < m->nknown ? m->known[i] : NULL;
        f->known_block[i] = i < m->nknown ? m->known_block[i] : 0;
    }
    f->nconsts = m->nconsts;
    f->nins = m->nins;
    f->nblocks = m->nblocks;
    f->ninlined = m->inlined;
    f->sites = m->sites;
    f->cold = m->cold;
    f->ngens = m->ngens;
    c->cur = m->cur;
    f->blocks[m->cur].nins = m->cur_nins;
    f->blocks[m->cur].nsucc = m->nsucc;
    f->fail = NULL;
}

/* BINARY_OP's subscript of an exact str by an int: str's own __getitem__,
   inlined as a call of it with the operands popped, so that an exit
   inside it returns its result to the operator's place. 1 with the result
   pushed, or 0 with nothing emitted. */
static int
str_item(struct ctx *c, lu_v a, lu_v b, int owna, int ownb)
{
    PyObject *self, *descr = _PyType_Lookup(&PyUnicode_Type, LU_ID(__getitem__));
    PyFunctionObject *fn = kernel_function(descr, &self);
    if (fn == NULL || self != NULL) {
        return 0;
    }
    struct mark m;
    struct bstate saved;
    mark_take(c, &m);
    state_copy(c, &saved, &c->st);
    guard_type(c, a, &PyUnicode_Type);
    guard_type(c, b, &PyLong_Type);
    pop(c, NULL);
    pop(c, NULL);
    lu_v args[2] = { a, b }, r;
    uint8_t own[2] = { (uint8_t)owna, (uint8_t)ownb };
    int rown;
    if (!inline_call(c, fn, LU_NOV, 0, args, own, 2, 0, &r, &rown)) {
        c->st = saved;
        mark_undo(c, &m);
        mark_free(&m);
        return 0;
    }
    mark_free(&m);
    if (c->cur >= 0) {
        push(c, r, rown);
    }
    return 1;
}

/* Inlines fn(args...): the caller's state has the call's operands popped.
   Takes the args' references (owned[i]); on success *result is the value
   the call returns. With nkwonly, args[nargs..nargs + nkwonly) are the
   keyword-only parameters' values (nargs then at most its positional
   parameters). With fnv, the call is of that function value, of fn's
   code and globals (a closure, lu_function_template): the frame's cells
   are fnv's, and fnv's reference (fnv_owned) is taken. Returns 1, or 0
   with nothing emitted. */
static int
inline_call(struct ctx *c, PyFunctionObject *fn, lu_v fnv, int fnv_owned, lu_v *args, uint8_t *owned, int nargs,
            int nkwonly, lu_v *result, int *result_owned)
{
    const char *no = c->cur < 0 ? "unreachable" : not_inlinable(c, fn, nargs, nkwonly);
    if (no != NULL) {
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: not inlined %s: %s\n", PyUnicode_AsUTF8(fn->func_qualname), no);
        }
        return 0;
    }
    struct lu_func *f = c->f;
    /* The types proved so far: guards the callee emits may prove more for
       the caller's values, in blocks an undo drops. */
    struct mark m;
    mark_take(c, &m);
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    struct ctx *k = calloc(1, sizeof(struct ctx));
    k->f = f;
    k->parent = c;
    k->code = co;
    k->func = fn;
    k->globals = fn->func_globals;
    k->builtins = fn->func_builtins;
    k->kernel = (co->co_flags & LU_CO_KERNEL) != 0;
    k->ncode = (int)Py_SIZE(co);
    k->nlocals = co->co_nlocalsplus;
    k->nvars = k->nlocals + co->co_stacksize + 1;
    k->depth = c->depth + 1;
    k->island = c->island;
    k->fixed = fixed_params(k);
    if (!k->kernel && fnv == LU_NOV) {
        /* The function's code is part of what was compiled: f.__code__ = ... changes its version. */
        if (fn->func_version < 2) {     /* 1: changed code or defaults, never specialized on again */
            free(k);
            mark_free(&m);
            return 0;
        }
        if (!c->made) {         /* a made frame's call guarded it where it can leave (class_call) */
            guard_func(c, fn);
        }
    }
    /* The caller resumes after the call with its operands gone. */
    k->caller = snapshot(c, c->next);
    int ok = scan(k, 0) == 0;
    if (ok) {
        count_fallthrough(k);
        k->bb[k->blkat[0]].npreds++;            /* the call */
        state_init(k, &k->st);
        for (int i = 0; i < co->co_argcount; i++) {
            if (i < nargs) {
                k->st.v[i] = args[i];
                k->st.own[i] = owned[i];
            }
            else {
                PyObject *d = PyTuple_GET_ITEM(fn->func_defaults,
                                               i - (co->co_argcount - (int)PyTuple_GET_SIZE(fn->func_defaults)));
                k->st.v[i] = konst(c, d);
                k->st.own[i] = 0;
            }
        }
        for (int i = 0; i < nkwonly; i++) {
            k->st.v[co->co_argcount + i] = args[nargs + i];
            k->st.own[co->co_argcount + i] = owned[nargs + i];
        }
        /* *args and **kwargs as the call binds them: a tuple of the extra
           positional arguments (the empty one), and a new dict. */
        int at = co->co_argcount + nkwonly;
        if (co->co_flags & CO_VARARGS) {
            int extra = nargs > at ? nargs - at : 0;
            /* Leaving, not caught: an inline undone must leave the caller's handlers as they were. */
            k->st.v[at] = extra ? helper_leaving(c, H_build_tuple, extra, args + at, 0)
                                : konst(c, (PyObject *)&_PyRuntime.singletons.empty_tuple);
            k->st.own[at] = extra != 0;
            set_made(c, k->st.v[at], &PyTuple_Type);
            for (int i = at; i < nargs; i++) {
                release(c, args[i], owned[i]);
            }
            at++;
        }
        if (co->co_flags & CO_VARKEYWORDS) {
            k->st.v[at] = helper_leaving(c, H_new_dict, 0, NULL, 0);
            k->st.own[at] = 1;
            set_made(c, k->st.v[at], &PyDict_Type);
        }
        if (fnv != LU_NOV) {
            /* COPY_FREE_VARS from the function called: its cells, each a reference of the frame's. */
            int nfree = co->co_nfreevars;
            lu_v closure = nfree > 0 ? emit(c, OP_LOAD, REP_OBJ, fnv, LU_NOV, LU_NOV,
                                            offsetof(PyFunctionObject, func_closure)) : LU_NOV;
            for (int i = 0; i < nfree; i++) {
                lu_v cell = emit(c, OP_LOAD, REP_OBJ, closure, LU_NOV, LU_NOV,
                                 offsetof(PyTupleObject, ob_item) + (int64_t)sizeof(PyObject *) * i);
                incref(c, cell);
                k->st.v[k->nlocals - nfree + i] = cell;
                k->st.own[k->nlocals - nfree + i] = 1;
            }
            k->cells = 1;
            release(c, fnv, fnv_owned);
        }
        k->cont = lu_ir_block(f);
        /* Into the callee's first block. */
        k->cur = c->cur;
        for (int i = 0; i < k->nlocals; i++) {
            if (k->fixed[i]) {
                continue;               /* a borrowed argument stays borrowed: the call holds it */
            }
            if (!k->st.own[i] && k->st.v[i] != LU_NOV && !immortal(c, k->st.v[i])) {
                emit(k, OP_INCREF, REP_NONE, k->st.v[i], LU_NOV, LU_NOV, 0);
            }
            k->st.own[i] = k->st.v[i] != LU_NOV;
        }
        edge_to(k, 0);
        emit(k, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        c->cur = -1;
        f->ninlined++;
        ok = build_ctx(k, k->blkat[0]) == 0 && !k->failed;
    }
    if (ok && lu_frame_floor(co) + k->depth > f->room) {
        f->room = lu_frame_floor(co) + k->depth;
    }
    if (ok && k->nrets > 0) {
        c->cur = k->cont;
        if (k->nrets == 1) {
            *result = k->rets[0];
        }
        else {
            lu_v phi = lu_ir_emit(f, k->cont, OP_PHI, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, -1);
            I(c, phi)->nargs = k->nrets;
            I(c, phi)->args = malloc(sizeof(lu_v) * (size_t)k->nrets);
            memcpy(I(c, phi)->args, k->rets, sizeof(lu_v) * (size_t)k->nrets);
            *result = phi;
        }
        *result_owned = 1;
    }
    else if (ok) {
        c->cur = -1;            /* the callee never returns here */
        *result = LU_NOV;
        *result_owned = 0;
    }
    if (ok) {
        wire_pending(c, k);
    }
    free(k->pend);
    free(k->rets);
    if (!ok && _PyRuntime.jit.debug) {
        fprintf(stderr, "jit: not inlined %s: %s\n", PyUnicode_AsUTF8(fn->func_qualname),
                k->why != NULL ? k->why : "not compiled");
    }
    if (!ok) {
        mark_undo(c, &m);       /* the callee's blocks and instructions go, the call stays */
    }
    mark_free(&m);
    free(k);
    return ok;
}

/* --- generators resumed in line --- */

/* The code a value that is a generator runs, when the builder knows it
   (made by a call of a known generator function), and its function or NULL. */
static PyCodeObject *
gen_made(struct ctx *c, lu_v v, PyFunctionObject **fn)
{
    for (int i = 0; v != LU_NOV && i < c->f->ngens; i++) {
        if (c->f->gens[i].v == v) {
            *fn = c->f->gens[i].func;
            return c->f->gens[i].code;
        }
    }
    return NULL;
}

static void
note_gen_made(struct ctx *c, lu_v v, PyCodeObject *code, PyFunctionObject *fn)
{
    struct lu_func *f = c->f;
    if (f->ngens == f->capgens) {
        f->capgens = f->capgens ? 2 * f->capgens : 8;
        struct lu_gen_made *g = lu_ir_alloc(f, sizeof(struct lu_gen_made) * (size_t)f->capgens);
        memcpy(g, f->gens, sizeof(struct lu_gen_made) * (size_t)f->ngens);
        f->gens = g;
    }
    f->gens[f->ngens++] = (struct lu_gen_made){ v, code, fn };
    set_known(c, v, &PyGen_Type);
}

/* Whether a local of c holds v. */
static int
local_holds(struct ctx *c, lu_v v)
{
    for (int i = 0; i < c->nlocals; i++) {
        if (c->st.v[i] == v) {
            return 1;
        }
    }
    return 0;
}

/* The first instruction from `first` on that may run Python or let a
   walker see frames, or LU_NOV: a generator resumed in line is not a real
   frame until an exit makes it one, so its body makes no such call. A
   release that may run a finalizer is allowed, as it is in any inlined
   frame (no frame is pushed for it). */
static lu_v
calls_out(struct lu_func *f, lu_v first)
{
    for (lu_v v = first; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        int flags = in->op == OP_HELPER ? lu_helpers[in->imm].flags : 0;
        if ((in->flags & INS_DEAD) || (in->op == OP_HELPER && in->imm == H_gen_finish)) {
            continue;
        }
        if ((flags & (IF_PY | IF_SEEN)) || (in->op == OP_TRUTH && in->imm != 1)) {
            return v;
        }
    }
    return LU_NOV;
}

/* The next value of the generator `it`, which runs `co`, as FOR_ITER and
   next_item take it: the generator's body built in line from each of its
   resume points, its frame's values loaded from the generator's frame at
   entry and stored back at a yield, so that between yields they are in
   registers (DESIGN 6.2 step 7). A yield's value, or MISSING once it
   returns or had finished; the generator's own checks (it may be another
   object, running, or observed by a frame object) leave before the
   instruction. Generators whose bodies run Python are not resumed in line
   (calls_out). LU_NOV with nothing emitted when it does not compile. */
static lu_v
inline_resume(struct ctx *c, lu_v it, PyCodeObject *co, PyFunctionObject *fn, int ret)
{
    struct lu_func *f = c->f;
    if (c->cur < 0 || c->depth + 1 >= INLINE_MAX_DEPTH || f->ninlined > INLINE_MAX_TOTAL
        || (co->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_ITERABLE_COROUTINE))
        || !(co->co_flags & CO_GENERATOR) || co->_co_monitoring != NULL || lu_jit_noinline(co)
        || lu_jit_nospec(c->code, c->pc)) {
        return LU_NOV;
    }
    for (struct ctx *p = c; p != NULL; p = p->parent) {
        if (p->code == co) {
            return LU_NOV;
        }
    }
    struct mark m;
    mark_take(c, &m);
    lu_v first = f->nins;
    struct ctx *k = calloc(1, sizeof(struct ctx));
    k->f = f;
    k->parent = c;
    k->code = co;
    k->func = fn;
    k->globals = fn != NULL ? fn->func_globals : c->globals;
    k->builtins = fn != NULL ? fn->func_builtins : c->builtins;
    k->ncode = (int)Py_SIZE(co);
    k->nlocals = co->co_nlocalsplus;
    k->nvars = k->nlocals + co->co_stacksize + 1;
    k->depth = c->depth + 1;
    k->island = c->island;
    k->gen = ret;
    k->genv = it;
    k->fixed = lu_ir_alloc(f, (size_t)k->nlocals + 1);
    memset(k->fixed, 0, (size_t)k->nlocals + 1);
    /* The caller is at FOR_ITER's end, the generator still on its stack;
       or past next_item's call, its operands gone, the generator (which a
       local holds) on the stack for an exit to find (deopt.c). */
    if (ret == LU_RETURN_NEXT) {
        struct bstate saved;
        state_copy(c, &saved, &c->st);
        c->st.sp -= 3;
        push(c, it, 0);
        k->caller = snapshot(c, c->next);
        c->st = saved;
    }
    else {
        k->caller = snapshot(c, c->next);
    }
    lu_v r = LU_NOV;
    int ok = resume_points(k) == 0 && scan(k, k->entries[0]) == 0;
    if (ok) {
        count_fallthrough(k);
        k->cont = lu_ir_block(f);
        for (int j = 0; j < k->nentries; j++) {
            k->bb[k->blkat[k->entries[j]]].npreds++;
        }
        lu_v args[2] = { it, konst(c, (PyObject *)co) };
        lu_v at = helper(c, H_gen_enter, 2, args);
        /* gen_enter made it running when it did not decline: stress must not leave here. */
        guard_after(c, OP_GUARD_BIT, emit(c, OP_CMP_I, REP_BIT, at, const_i(c, -2), LU_NOV, Py_NE), 1);
        /* Finished: the iteration ends. */
        lu_v fin = emit(c, OP_CMP_I, REP_BIT, at, const_i(c, 0), LU_NOV, Py_LT);
        lu_v missing = konst(c, LU_MISSING);
        int run = lu_ir_block(f);
        lu_ir_edge(f, c->cur, k->cont);
        lu_ir_edge(f, c->cur, run);
        emit(c, OP_BRANCH, REP_NONE, fin, LU_NOV, LU_NOV, 0);
        k->rets = malloc(sizeof(lu_v) * 4);
        k->caprets = 4;
        k->rets[k->nrets++] = missing;
        c->cur = run;
        int slots = (int)(offsetof(struct lu_gen, frame) + offsetof(struct lu_frame, localsplus));
        for (int j = 0; j < k->nentries; j++) {
            int other = -1, e = k->entries[j];
            if (j < k->nentries - 1) {
                lu_v bit = emit(c, OP_CMP_I, REP_BIT, at, const_i(c, e), LU_NOV, Py_EQ);
                int here = lu_ir_block(f);
                other = lu_ir_block(f);
                lu_ir_edge(f, c->cur, here);
                lu_ir_edge(f, c->cur, other);
                emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
                c->cur = here;
            }
            /* The frame's values move from its slots to compiled code; a
               resume after a yield pushes the None it is sent. */
            k->cur = c->cur;
            k->pc = e;
            state_init(k, &k->st);
            for (int i = 0; i < k->nlocals; i++) {
                k->st.v[i] = emit(k, OP_LOAD, REP_OBJ, it, LU_NOV, LU_NOV, slots + 8 * i);
                k->st.own[i] = 1;
            }
            for (int d = 0; d < k->entry_depth[j] - (e != k->entries[0]); d++) {
                push(k, emit(k, OP_LOAD, REP_OBJ, it, LU_NOV, LU_NOV, slots + 8 * (k->nlocals + d)), 1);
            }
            if (e != k->entries[0]) {
                push(k, konst(c, Py_None), 0);
            }
            edge_to(k, e);
            emit(k, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
            c->cur = other;
        }
        f->ninlined++;
        ok = build_ctx(k, k->blkat[k->entries[0]]) == 0 && !k->failed;
        lu_v out = ok ? calls_out(f, first) : LU_NOV;
        if (out != LU_NOV) {
            struct lu_ins *in = I(c, out);
            k->why = in->op == OP_HELPER ? lu_helpers[in->imm].name : lu_op_names[in->op];
            ok = 0;
        }
    }
    if (ok && lu_frame_floor(co) + k->depth > f->room) {
        f->room = lu_frame_floor(co) + k->depth;
    }
    if (ok) {
        c->cur = k->cont;
        r = lu_ir_emit(f, k->cont, OP_PHI, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, -1);
        I(c, r)->nargs = k->nrets;
        I(c, r)->args = malloc(sizeof(lu_v) * (size_t)k->nrets);
        memcpy(I(c, r)->args, k->rets, sizeof(lu_v) * (size_t)k->nrets);
        wire_pending(c, k);
    }
    else {
        if (_PyRuntime.jit.debug) {
            fprintf(stderr, "jit: not resumed in line %s: %s\n", PyUnicode_AsUTF8(co->co_qualname),
                    k->why != NULL ? k->why : f->fail != NULL ? f->fail : "it calls out");
        }
        mark_undo(c, &m);
    }
    mark_free(&m);
    free(k->pend);
    free(k->rets);
    free(k);
    return r;
}

/* A yield of a generator resumed in line: its frame's values go back to
   its slots (each slot takes a reference), it saves where it goes on, and
   the value is the resumption's. */
static int
gen_yield(struct ctx *c)
{
    int own;
    lu_v v = pop(c, &own);
    v = take(c, v, own);
    int slots = (int)(offsetof(struct lu_gen, frame) + offsetof(struct lu_frame, localsplus));
    for (int i = 0; i < c->nlocals + c->st.sp; i++) {
        lu_v w = c->st.v[i];
        if (w != LU_NOV && !c->st.own[i]) {
            incref(c, w);
        }
        emit(c, OP_STORE, REP_NONE, c->genv, w != LU_NOV ? w : konst(c, NULL), LU_NOV, slots + 8 * i);
    }
    lu_v args[3] = { c->genv, const_i(c, c->nlocals + c->st.sp), const_i(c, c->next) };
    helper(c, H_gen_yielded, 3, args);
    if (c->nrets == c->caprets) {
        c->caprets *= 2;
        c->rets = realloc(c->rets, sizeof(lu_v) * (size_t)c->caprets);
    }
    c->rets[c->nrets++] = v;
    lu_ir_edge(c->f, c->cur, c->cont);
    emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    c->cur = -1;
    return 0;
}

/* --- attributes and globals --- */

/* The class of owner when it is an instance this code made (its exact
   type known by construction) that keeps its values inline: its own
   shapes are found for it (lu_specialize_*_fresh), whatever type the
   site's cache holds, as for a site more than one class runs through (an
   inherited __init__) or one the interpreter never ran. */
static PyTypeObject *
made_type(struct ctx *c, lu_v owner)
{
    PyTypeObject *t = owner != LU_NOV && owner < c->f->capknown && c->f->known_block[owner] < 0
        ? c->f->known[owner] : NULL;
    return t != NULL && (t->tp_flags & Py_TPFLAGS_INLINE_VALUES) && !lu_jit_nospec(c->code, c->pc) ? t : NULL;
}

static PyObject *
name_at(struct ctx *c, int i)
{
    return PyTuple_GET_ITEM(c->code->co_names, i);
}

/* `name` in dict d, else in dict b (when not NULL), as a constant guarded
   on the dicts' keys versions and the entry's value: the value the code was
   compiled with, a new reference taken as LOAD_GLOBAL_MODULE takes it,
   since another thread may replace and free it meanwhile. A dict whose keys
   changed since they were last versioned is versioned again, as the
   interpreter's specializer does. NULL when the name cannot be found that
   way. */
static PyObject *
dict_const(struct ctx *c, PyObject *d, PyObject *b, PyObject *name)
{
    if (!PyDict_CheckExact(d) || (b != NULL && !PyDict_CheckExact(b))) {
        return NULL;
    }
    /* Versioned first: a table still of that version after the lookup has
       not changed meanwhile, so the entry found is its own. */
    uint32_t dv = lu_dict_keys_version((PyDictObject *)d), bv = 0;
    PyObject *at = d;
    Py_ssize_t ix = lu_ns_index(d, name);
    if (ix < 0 && b != NULL) {
        at = b;
        bv = lu_dict_keys_version((PyDictObject *)b);
        ix = lu_ns_index(b, name);
    }
    PyDictKeysObject *dk = __atomic_load_n(&((PyDictObject *)at)->ma_keys, __ATOMIC_ACQUIRE);
    if (ix < 0 || dv == 0 || (at == b && bv == 0) || !lu_dk_is_unicode(dk) || ix >= dk->dk_nentries
        || __atomic_load_n(&dk->dk_version, __ATOMIC_ACQUIRE) != (at == b ? bv : dv)) {
        return NULL;
    }
    PyObject *v = lu_try_xget_ref(lu_dk_value_slot(dk, ix));
    if (v != NULL && dk != __atomic_load_n(&((PyDictObject *)at)->ma_keys, __ATOMIC_ACQUIRE)) {
        Py_CLEAR(v);
    }
    if (v == NULL) {
        return NULL;
    }
    lu_v g1 = guard(c, OP_GUARD_VERSION, konst(c, d), dv);
    I(c, g1)->imm2 = GV_DICT_KEYS;
    if (at == b) {
        lu_v g2 = guard(c, OP_GUARD_VERSION, konst(c, b), bv);
        I(c, g2)->imm2 = GV_DICT_KEYS;
    }
    lu_v g3 = guard(c, OP_GUARD_IS, konst(c, at), (int64_t)(intptr_t)v);
    I(c, g3)->imm2 = GI_KEYS_FIELD | ((char *)lu_dk_value_slot(dk, ix) - (char *)dk);
    return v;
}

/* The global `name` as a constant (dict_const), or a kernel global. */
static PyObject *
global_const(struct ctx *c, PyObject *name)
{
    if (c->kernel && lu_ns_lookup(PyThreadState_Get()->interp->kernel_rebound, name) == NULL) {
        return kernel_global(c, name);
    }
    lu_codeunit *site = c->cache - 1;
    if (lu_opcode_deopt(__atomic_load_n(&site->op.code, __ATOMIC_RELAXED)) == LOAD_GLOBAL
        && site->op.code == LOAD_GLOBAL && PyThreadState_Get()->interp->specializing
        && PyDict_CheckExact(c->globals) && PyDict_CheckExact(c->builtins)) {
        lu_specialize_load_global(c->globals, c->builtins, site, name);
    }
    return dict_const(c, c->globals, c->builtins, name);
}

/* Whether the global `name` now names a function, class, module or C
   function: a binding programs make once (a module still defining its
   names fails the guards of one, and it stays a constant, which calls
   inline), where a value rebound as data goes through the lookup. */
static int
binds_code(struct ctx *c, PyObject *name)
{
    PyObject *v = lu_ns_get(c->globals, name);
    if (v == NULL && !PyErr_Occurred()) {
        v = lu_ns_get(c->builtins, name);
    }
    PyErr_Clear();
    int r = v != NULL && (PyFunction_Check(v) || PyType_Check(v) || PyModule_Check(v) || PyCFunction_Check(v));
    Py_XDECREF(v);
    return r;
}

static int
load_global(struct ctx *c)
{
    PyObject *name = name_at(c, c->oparg >> 1);
    if (lu_jit_nospec(c->code, c->pc) && !c->kernel && PyDict_CheckExact(c->globals)
        && PyDict_CheckExact(c->builtins) && !binds_code(c, name)) {
        /* Its guards kept failing (a value rebound on every call): loaded
           through the site's own cache each time, and a miss leaves for
           the NameError. */
        lu_v args[4] = { konst(c, c->globals), konst(c, c->builtins), konst(c, name),
                         const_i(c, (int64_t)(intptr_t)(c->cache - 1)) };
        lu_v w = helper_raising(c, H_load_global, 4, args, 0);
        guard_after(c, OP_GUARD_NOT_NULL, w, 1);
        push(c, w, 1);
        if (c->oparg & 1) {
            push(c, LU_NOV, 0);
        }
        return 0;
    }
    PyObject *v = global_const(c, name);
    if (v == NULL) {
        unsupported(c);
        return -1;
    }
    push(c, konst(c, v), 0);
    Py_DECREF(v);               /* the unit holds its constants */
    if (c->oparg & 1) {
        push(c, LU_NOV, 0);
    }
    return 0;
}

/* LOAD_COMMON_CONSTANT: an exception type, or a builtin the interpreter
   looks up by name (tuple, all, any). */
static int
load_common_constant(struct ctx *c)
{
    if (c->oparg < 2) {
        push(c, konst(c, c->oparg == 0 ? PyExc_AssertionError : PyExc_NotImplementedError), 0);
        return 0;
    }
    const char *s = c->oparg == 2 ? "tuple" : c->oparg == 3 ? "all" : c->oparg == 4 ? "any" : NULL;
    PyObject *name = s != NULL ? PyUnicode_InternFromString(s) : NULL;
    PyObject *v = name != NULL ? dict_const(c, PyThreadState_Get()->interp->builtins, NULL, name) : NULL;
    Py_XDECREF(name);
    if (v == NULL) {
        PyErr_Clear();
        unsupported(c);
        return -1;
    }
    push(c, konst(c, v), 0);
    Py_DECREF(v);               /* the unit holds its constants */
    return 0;
}

/* The current attribute instruction's specialization, read as the
   interpreter reads it (specialize.h): its opcode, version, index word and,
   for LOAD_ATTR, descriptor, all of one specialization; or `generic` when
   there is none, the tier may not speculate here, or the site changed
   meanwhile. *descr is a new reference: a cache names only objects no
   collection has freed (specialize.c), and nothing here polls before the
   incref. */
static int
cached_site(struct ctx *c, int generic, uint32_t *version, uint16_t *index, PyObject **descr)
{
    lu_codeunit *site = c->cache - 1;
    int op = __atomic_load_n(&site->op.code, __ATOMIC_RELAXED);
    *version = lu_cache_load32(c->cache + LA_VERSION);
    *index = lu_cache_load(c->cache + LA_INDEX);
    *descr = generic == LOAD_ATTR ? lu_cache_load_ptr(c->cache + LA_DESCR) : NULL;
    /* A generic site whose counter was never spent: the interpreter has not run it. */
    c->f->sites++;
    c->f->cold += op == generic && lu_cache_load(c->cache + LA_COUNTER) == 0;
    if (*version == 0 || lu_jit_nospec(c->code, c->pc) || !lu_site_unchanged(site, op)
        || lu_cache_load32(c->cache + LA_VERSION) != *version) {
        *descr = NULL;
        return generic;
    }
    Py_XINCREF(*descr);
    return op;
}

static int load_attr_cached(struct ctx *c, int op, uint32_t version, uint16_t index, PyObject *descr);
static lu_v type_of(struct ctx *c, lu_v v);

/* --- paths that join after a call --- */

/* One way through an instruction range: where it ends and with what. */
struct path {
    int block;                  /* -1: it never gets to the end */
    struct bstate st;
};

/* The current block and state as a path, for join_paths. */
static struct path
path_here(struct ctx *c)
{
    struct path p = { c->cur, { 0 } };
    if (c->cur >= 0) {
        state_copy(c, &p.st, &c->st);
    }
    return p;
}

/* Joins paths that end at the same instruction boundary: each owns its
   entries (normalize), and an entry the paths disagree on becomes a phi. */
static void
join_paths(struct ctx *c, struct path *p, int n)
{
    struct lu_func *f = c->f;
    int live = 0, first = -1;
    for (int i = 0; i < n; i++) {
        if (p[i].block >= 0) {
            live++;
            first = first < 0 ? i : first;
        }
    }
    if (live <= 1) {
        c->cur = live ? p[first].block : -1;
        if (live) {
            c->st = p[first].st;
        }
        return;
    }
    int join = lu_ir_block(f);
    for (int i = 0; i < n; i++) {
        if (p[i].block >= 0) {
            c->cur = p[i].block;
            c->st = p[i].st;
            normalize(c);
            emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
            lu_ir_edge(f, p[i].block, join);
        }
    }
    c->cur = join;
    state_copy(c, &c->st, &p[first].st);
    for (int k = 0; k < c->nlocals + c->st.sp; k++) {
        int same = 1;
        for (int i = first + 1; i < n; i++) {
            same &= p[i].block < 0 || p[i].st.v[k] == p[first].st.v[k];
        }
        if (same) {
            continue;
        }
        lu_v phi = lu_ir_emit(f, join, OP_PHI, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, k);
        I(c, phi)->nargs = live;
        I(c, phi)->args = malloc(sizeof(lu_v) * (size_t)live);
        I(c, phi)->imm2 = k < c->nlocals ? (c->parent == NULL ? 1 : 2) : 0;   /* as edge_to sets it */
        for (int i = 0, j = 0; i < n; i++) {
            if (p[i].block >= 0) {
                I(c, phi)->args[j++] = phi_input(c, p[i].st.v[k]);
            }
        }
        c->st.v[k] = phi;
        c->st.own[k] = 1;
    }
}

/* The stack depth change of an instruction an argument list may hold, or
   INT32_MIN for any other. */
static int
arg_effect(int op, int oparg)
{
    switch (op) {
    case NOP: case NOT_TAKEN: case TO_BOOL: case UNARY_NOT: case UNARY_NEGATIVE: case UNARY_INVERT:
        return 0;
    case LOAD_FAST: case LOAD_FAST_BORROW: case LOAD_FAST_CHECK: case LOAD_CONST: case LOAD_SMALL_INT:
    case LOAD_DEREF: case PUSH_NULL: case COPY: case LOAD_COMMON_CONSTANT:
        return 1;
    case LOAD_FAST_LOAD_FAST: case LOAD_FAST_BORROW_LOAD_FAST_BORROW:
        return 2;
    case LOAD_GLOBAL:
        return 1 + (oparg & 1);
    case LOAD_ATTR:
        return oparg & 1;
    case BINARY_OP: case COMPARE_OP: case CONTAINS_OP: case IS_OP:
        return -1;
    case BUILD_TUPLE: case BUILD_LIST:
        return 1 - oparg;
    case CALL:
        return -1 - oparg;
    case CALL_KW:
        return -2 - oparg;
    default:
        return INT32_MIN;
    }
}

/* Builds the instructions from the load at `at` to end on the current
   state, as one path, then puts the builder back at the load with the
   state before it (base). */
static struct path
through(struct ctx *c, const struct ctx *at, int end, const struct bstate *base)
{
    int ncalls = c->ncalls;
    c->ncalls = at->ncalls;     /* the copies are alternatives: one call's worth */
    for (int pc = at->next; pc != end && c->cur >= 0 && !c->failed;) {
        pc = step(c, pc, NULL);
    }
    struct path p = path_here(c);
    if (c->failed) {
        p.block = -1;
    }
    c->ncalls = c->ncalls > ncalls ? c->ncalls : ncalls;
    c->pc = at->pc;
    c->next = at->next;
    c->base = at->base;
    c->oparg = at->oparg;
    c->opcode = at->opcode;
    c->cache = at->cache;
    c->snap = at->snap;
    state_copy(c, &c->st, base);
    return p;
}

/* The end of the call that takes the method and self a method-form load
   at c->pc pushes, when only argument code that does not branch comes
   between: the boundary after the call, or -1. */
static int
call_end(struct ctx *c)
{
    int above = 0;              /* entries above the method and self */
    for (int pc = c->next; pc < c->ncode && c->blkat[pc] < 0;) {
        int base, oparg, op;
        int next = decode(c, pc, &base, &oparg, &op);
        if ((base == CALL && above == oparg) || (base == CALL_KW && above == oparg + 1)) {
            return next;
        }
        int d = arg_effect(base, oparg);
        if (d == INT32_MIN) {
            return -1;
        }
        above += d;
        pc = next;
    }
    return -1;
}

/* A site its polymorphic cache answers (runtime/jit/jit.h): the owner's
   type picks an entry, whose version is guarded and whose answer is
   taken as its one-shape site would; any other type takes the generic
   path, which goes on recording. */
static int
load_attr_poly(struct ctx *c, struct lu_pic *pic, int n)
{
    struct lu_func *f = c->f;
    lu_v owner = peek(c, 1);
    int own = peek_owned(c, 1);
    int method = c->oparg & 1;
    PyObject *name = name_at(c, c->oparg >> 1);
    before(c);
    lu_v first = f->nins;
    lu_v t = emit(c, OP_LOAD, REP_I64, type_of(c, owner), LU_NOV, LU_NOV, offsetof(PyTypeObject, tp_version_tag));
    I(c, t)->imm2 = 4;          /* 32 bits */
    pop(c, NULL);
    /* A method's call that follows is built once per entry (tail
       duplication), so that each builds its own method's call and inlines
       it. The copies count against the frame's inline budget: a split
       that would make an inlined callee too large leaves the arms joined. */
    int end = method ? call_end(c) : -1;
    int range = end - c->next;
    if (end >= 0 && c->parent != NULL && c->units + (c->next - c->pc) + (n + 1) * range > INLINE_MAX_UNITS) {
        end = -1;
    }
    struct ctx at = *c;         /* the load's position, which each copy starts from */
    struct bstate base;
    if (end >= 0) {
        state_copy(c, &base, &c->st);
    }
    struct path ends[LU_PIC_WAYS + 1];
    int join = end < 0 ? lu_ir_block(f) : -1;
    lu_v r1[LU_PIC_WAYS + 1], r2[LU_PIC_WAYS + 1];
    int paths = 0;
    int nospec = lu_jit_nospec(c->code, c->pc);
    for (int i = 0; i < n; i++) {
        struct lu_pic_entry e = pic->e[i];
        lu_v bit = emit(c, OP_CMP_I, REP_BIT, t, const_i(c, e.version), LU_NOV, Py_EQ);
        int arm = lu_ir_block(f), next = lu_ir_block(f);
        lu_ir_edge(f, c->cur, arm);
        lu_ir_edge(f, c->cur, next);
        emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
        c->cur = arm;
        lu_v a1, a2 = LU_NOV;
        int own1 = 1, own2 = 0;
        if (e.op == LOAD_ATTR_INSTANCE_VALUE) {
            /* An instance of the class that lacks the attribute (its class
               attribute answers until it sets its own) takes the generic
               path. */
            lu_v word = emit(c, OP_LOAD, REP_I64, owner, LU_NOV, LU_NOV, e.index);
            lu_v set = emit(c, OP_CMP_I, REP_BIT, word, const_i(c, 0), LU_NOV, Py_NE);
            int hit = lu_ir_block(f);
            lu_ir_edge(f, c->cur, hit);
            lu_ir_edge(f, c->cur, next);
            emit(c, OP_BRANCH, REP_NONE, set, LU_NOV, LU_NOV, 0);
            c->cur = hit;
            a1 = emit(c, OP_INLINE_LOAD, REP_OBJ, owner, LU_NOV, LU_NOV, e.index);
            I(c, a1)->fs = before(c);
            release(c, owner, own);
        }
        else {
            if (e.op == LOAD_ATTR_METHOD_WITH_VALUES || e.op == LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES) {
                lu_v args[2] = { owner, konst(c, name) };
                lu_v lacks = helper(c, H_dict_lacks, 2, args);
                if (nospec) {
                    /* Its check kept failing (instance dicts shadow the
                       name): those instances take the generic path. */
                    int hit = lu_ir_block(f);
                    lu_ir_edge(f, c->cur, hit);
                    lu_ir_edge(f, c->cur, next);
                    emit(c, OP_BRANCH, REP_NONE, lacks, LU_NOV, LU_NOV, 0);
                    c->cur = hit;
                }
                else {
                    guard(c, OP_GUARD_BIT, lacks, 1);
                }
            }
            /* A copy keeps the method a constant, which its call inlines; a join owns its inputs. */
            a1 = konst(c, e.descr);
            own1 = end < 0;
            if (own1) {
                incref(c, a1);
            }
            if (method && (e.op == LOAD_ATTR_METHOD_NO_DICT || e.op == LOAD_ATTR_METHOD_WITH_VALUES)) {
                a2 = end < 0 ? take(c, owner, own) : owner;
                own2 = end < 0 || own;
            }
            else {
                release(c, owner, own);
            }
        }
        if (end >= 0) {
            push(c, a1, own1);
            if (method) {
                push(c, a2, own2);
            }
            ends[paths++] = through(c, &at, end, &base);
        }
        else {
            r1[paths] = a1;
            r2[paths++] = a2;
            emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
            lu_ir_edge(f, c->cur, join);
        }
        c->cur = next;
    }
    if (!root_only(c)) {
        unsupported(c);         /* leaves at the state before the load */
    }
    else {
        lu_v args[3] = { owner, konst(c, name), const_i(c, (int64_t)(intptr_t)pic) };
        lu_v v = helper_raising(c, method ? H_load_method : H_load_attr, 3, args, 1);
        release(c, owner, own);
        if (end >= 0) {
            /* Shapes the cache does not hold: the call as it was, on what the load answered. */
            if (c->observed) {
                frames_guard(c, first);
                c->observed = 0;
            }
            push(c, v, 1);
            push(c, LU_NOV, 0);
            c->generic_call = end;
            ends[paths++] = through(c, &at, end, &base);
            c->generic_call = 0;
        }
        else {
            r1[paths] = v;
            r2[paths++] = LU_NOV;
            emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
            lu_ir_edge(f, c->cur, join);
        }
    }
    if (end >= 0) {
        /* The rest of this instruction is the call's: the frame goes on after it. */
        join_paths(c, ends, paths);
        c->units -= range;      /* the step that called this counts the range too */
        c->next = end;
        return 0;
    }
    c->cur = join;
    lu_v out[2];
    for (int k = 0; k < 1 + method; k++) {
        lu_v *r = k == 0 ? r1 : r2;
        if (paths == 1) {
            out[k] = r[0];
            continue;
        }
        lu_v phi = lu_ir_emit(f, join, OP_PHI, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, c->nlocals + c->st.sp + k);
        I(c, phi)->nargs = paths;
        I(c, phi)->args = malloc(sizeof(lu_v) * (size_t)paths);
        for (int i = 0; i < paths; i++) {
            I(c, phi)->args[i] = phi_input(c, r[i]);
        }
        out[k] = phi;
    }
    push(c, out[0], 1);
    if (method) {
        push(c, out[1], out[1] != LU_NOV);
    }
    return 0;
}

static int
load_attr(struct ctx *c)
{
    uint32_t version;
    uint16_t index;
    PyObject *descr;
    int op = cached_site(c, LOAD_ATTR, &version, &index, &descr);
    PyTypeObject *t = made_type(c, peek(c, 1));
    uint32_t fv;
    uint16_t fi;
    PyObject *fd;
    int fop = t != NULL ? lu_specialize_load_fresh(t, name_at(c, c->oparg >> 1), c->oparg & 1, &fv, &fi, &fd)
        : LOAD_ATTR;
    if (fop != LOAD_ATTR) {
        Py_XSETREF(descr, fd);
        op = fop;
        version = fv;
        index = fi;
    }
    struct lu_pic *pic = op == LOAD_ATTR ? lu_jit_pic(c->code, c->pc, 0) : NULL;
    int n = pic != NULL ? __atomic_load_n(&pic->n, __ATOMIC_ACQUIRE) : 0;
    int r = n > 0 ? load_attr_poly(c, pic, n) : load_attr_cached(c, op, version, index, descr);
    Py_XDECREF(descr);
    return r;
}

static int
load_attr_cached(struct ctx *c, int op, uint32_t version, uint16_t index, PyObject *descr)
{
    lu_v owner = peek(c, 1);
    int own = peek_owned(c, 1);
    int method = c->oparg & 1;
    PyObject *name = name_at(c, c->oparg >> 1);
    switch (op) {
    case LOAD_ATTR_METHOD_NO_DICT:
    case LOAD_ATTR_METHOD_WITH_VALUES:
    case LOAD_ATTR_NONDESCRIPTOR_NO_DICT:
    case LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES: {
        guard_version(c, owner, version);
        if (op == LOAD_ATTR_METHOD_WITH_VALUES || op == LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES) {
            lu_v args[2] = { owner, konst(c, name) };
            guard(c, OP_GUARD_BIT, helper(c, H_dict_lacks, 2, args), 1);
        }
        pop(c, NULL);
        push(c, konst(c, descr), 0);
        if (op == LOAD_ATTR_METHOD_NO_DICT || op == LOAD_ATTR_METHOD_WITH_VALUES) {
            push(c, owner, own);
        }
        else {
            release(c, owner, own);
            if (method) {
                push(c, LU_NOV, 0);
            }
        }
        return 0;
    }
    case LOAD_ATTR_WITH_HINT: {
        lu_v args[4] = { owner, const_i(c, version), const_i(c, index), konst(c, name) };
        lu_v v = emit(c, OP_ATTR_LOAD, REP_OBJ, owner, LU_NOV, args[3], version);
        I(c, v)->imm2 = index;
        I(c, v)->fs = before(c);
        pop(c, NULL);
        release(c, owner, own);
        push(c, v, 1);
        if (method) {
            push(c, LU_NOV, 0);
        }
        return 0;
    }
    case LOAD_ATTR_SLOT:
    case LOAD_ATTR_INSTANCE_VALUE: {
        guard_version(c, owner, version);
        lu_v v;
        if (op == LOAD_ATTR_INSTANCE_VALUE) {
            v = emit(c, OP_INLINE_LOAD, REP_OBJ, owner, LU_NOV, LU_NOV, index);
            I(c, v)->fs = before(c);
        }
        else {
            lu_v args[2] = { owner, const_i(c, index) };
            v = helper(c, H_slot_load, 2, args);
            guard_after(c, OP_GUARD_NOT_NULL, v, 0);
        }
        pop(c, NULL);
        release(c, owner, own);
        push(c, v, 1);
        if (method) {
            push(c, LU_NOV, 0);
        }
        return 0;
    }
    case LOAD_ATTR_CLASS:
    case LOAD_ATTR_CLASS_WITH_METACLASS_CHECK: {
        /* A class's own attribute, by the class's version, and by its
           metaclass's when that is not type (an Enum's members). */
        lu_v g = guard(c, OP_GUARD_VERSION, owner, version);
        I(c, g)->imm2 = GV_TYPE_ITSELF;
        if (op == LOAD_ATTR_CLASS_WITH_METACLASS_CHECK) {
            uint32_t meta = lu_cache_load32(c->cache + LA_KEYS_VERSION);
            if (meta == 0) {
                unsupported(c);
                return -1;
            }
            guard(c, OP_GUARD_VERSION, owner, meta);
        }
        pop(c, NULL);
        release(c, owner, own);
        push(c, konst(c, descr), 0);
        if (method) {
            push(c, LU_NOV, 0);
        }
        return 0;
    }
    default:
        break;
    }
    PyObject *cls = known(c, owner);
    PyTypeObject *ot = known_type(c, owner);
    uint32_t cv[3];
    int managed = 0, nospec = lu_jit_nospec(c->code, c->pc);
    PyObject *cm = nospec ? NULL : cls != NULL && PyType_Check(cls)
        ? lu_specialize_class_method((PyTypeObject *)cls, name, cv)
        : ot != NULL ? lu_specialize_instance_method(ot, name, cv, &managed) : NULL;
    PyObject *fn = cm != NULL ? *(PyObject **)((char *)cm + lu_classmethod_callable_offset) : NULL;
    int classmethod = cm != NULL && Py_IS_TYPE(cm, &PyClassMethod_Type);
    if (fn != NULL && (!classmethod || method)) {
        /* A classmethod or staticmethod of a constant class, or found on an
           instance of a known type, bound at compile time: its callable,
           guarded, and for a classmethod the class as the call's self (only
           a method load: anything else makes the bound method). */
        if (cls != NULL && PyType_Check(cls)) {
            lu_v g = guard(c, OP_GUARD_VERSION, owner, cv[0]);
            I(c, g)->imm2 = GV_TYPE_ITSELF;
            guard(c, OP_GUARD_VERSION, owner, cv[1]);
        }
        else {
            guard_version(c, owner, cv[0]);
            if (managed) {
                lu_v args[2] = { owner, konst(c, name) };
                guard(c, OP_GUARD_BIT, helper(c, H_dict_lacks, 2, args), 1);
            }
        }
        lu_v k = konst(c, cm);
        lu_v g = guard(c, OP_GUARD_IS, k, (int64_t)(intptr_t)fn);
        I(c, g)->imm2 = GI_FIELD | (int)lu_classmethod_callable_offset;
        lu_v f = konst(c, fn);
        Py_DECREF(cm);          /* the unit holds its constants */
        pop(c, NULL);
        push(c, f, 0);
        if (classmethod && cls != NULL && PyType_Check(cls)) {
            push(c, owner, own);
        }
        else if (classmethod) {
            release(c, owner, own);
            push(c, konst(c, (PyObject *)ot), 0);
        }
        else {
            release(c, owner, own);
            if (method) {
                push(c, LU_NOV, 0);
            }
        }
        return 0;
    }
    Py_XDECREF(cm);
    PyObject *value = cls != NULL && PyType_Check(cls) && !lu_jit_nospec(c->code, c->pc)
        ? lu_specialize_class_value((PyTypeObject *)cls, name, cv) : NULL;
    if (value != NULL) {
        /* A constant class's attribute that is a plain value, by the versions that keep it one. */
        lu_v g = guard(c, OP_GUARD_VERSION, owner, cv[0]);
        I(c, g)->imm2 = GV_TYPE_ITSELF;
        guard(c, OP_GUARD_VERSION, owner, cv[1]);
        lu_v k = konst(c, value);
        Py_DECREF(value);       /* the unit holds its constants */
        guard(c, OP_GUARD_VERSION, k, cv[2]);
        pop(c, NULL);
        release(c, owner, own);
        push(c, k, 0);
        if (method) {
            push(c, LU_NOV, 0);
        }
        return 0;
    }
    if (!root_only(c)) {
        unsupported(c);
        return -1;
    }
    /* The generic load records the shapes it meets for a polymorphic cache. */
    lu_v args[3] = { owner, konst(c, name), const_i(c, (int64_t)(intptr_t)lu_jit_pic(c->code, c->pc, 1)) };
    lu_v v = helper_raising(c, method ? H_load_method : H_load_attr, 3, args, 1);
    pop(c, NULL);
    release(c, owner, own);
    if (method) {
        /* load_method answers the bound method or attribute; CALL unbinds it. */
        push(c, v, 1);
        push(c, LU_NOV, 0);
    }
    else {
        push(c, v, 1);
    }
    return 0;
}

/* A generic STORE_ATTR site its polymorphic cache answers: the owner's
   type version picks an entry, whose inline value is stored as its
   one-shape site would store it; any other type takes the generic path
   (in a root frame, which goes on recording) or leaves. 0 when the site
   has no cache. */
static int
store_attr_poly(struct ctx *c, lu_v owner, lu_v v)
{
    struct lu_pic *pic = lu_jit_pic(c->code, c->pc, 0);
    int n = pic != NULL && pic->store ? __atomic_load_n(&pic->n, __ATOMIC_ACQUIRE) : 0;
    if (n == 0) {
        return 0;
    }
    struct lu_func *f = c->f;
    lu_v t = emit(c, OP_LOAD, REP_I64, type_of(c, owner), LU_NOV, LU_NOV, offsetof(PyTypeObject, tp_version_tag));
    I(c, t)->imm2 = 4;          /* 32 bits */
    int join = lu_ir_block(f);
    for (int i = 0; i < n; i++) {
        struct lu_pic_entry e = pic->e[i];
        lu_v bit = emit(c, OP_CMP_I, REP_BIT, t, const_i(c, e.version), LU_NOV, Py_EQ);
        int arm = lu_ir_block(f), next = lu_ir_block(f);
        lu_ir_edge(f, c->cur, arm);
        lu_ir_edge(f, c->cur, next);
        emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
        c->cur = arm;
        lu_v st = emit(c, OP_INLINE_STORE, REP_NONE, owner, v, LU_NOV, e.index);
        I(c, st)->fs = before(c);
        emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        lu_ir_edge(f, c->cur, join);
        c->cur = next;
    }
    if (root_only(c)) {
        lu_v args[4] = { owner, konst(c, name_at(c, c->oparg)), v, const_i(c, (int64_t)(intptr_t)pic) };
        helper_raising(c, H_store_attr, 4, args, 2);
    }
    else {
        unsupported(c);         /* leaves at the state before the store */
    }
    if (c->cur >= 0) {
        emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        lu_ir_edge(f, c->cur, join);
    }
    c->cur = join;
    return 1;
}

static int
store_attr(struct ctx *c)
{
    lu_v owner = peek(c, 1), v = peek(c, 2);
    int ownowner = peek_owned(c, 1), ownv = peek_owned(c, 2);
    PyObject *name = name_at(c, c->oparg);
    uint32_t version;
    uint16_t index;
    PyObject *unused;
    int op = cached_site(c, STORE_ATTR, &version, &index, &unused);
    PyTypeObject *t = made_type(c, owner);
    uint32_t fv;
    uint16_t fi;
    if (t != NULL && lu_specialize_store_fresh(t, name, &fv, &fi) == STORE_ATTR_INSTANCE_VALUE) {
        op = STORE_ATTR_INSTANCE_VALUE;
        version = fv;
        index = fi;
    }
    if (op == STORE_ATTR_WITH_HINT) {
        lu_v args[4] = { owner, v, konst(c, name), const_i(c, version) };
        guard_after(c, OP_GUARD_BIT, helper(c, H_attr_store, 4, args), 1);
    }
    else if (op == STORE_ATTR_INSTANCE_VALUE) {
        guard_version(c, owner, version);
        lu_v st = emit(c, OP_INLINE_STORE, REP_NONE, owner, v, LU_NOV, index);
        I(c, st)->fs = before(c);
        /* An instance of a known class (one this code made) may take a new
           name in line: every instance has at least the room settled now. */
        t = known_type(c, owner);
        if (t != NULL && (t->tp_flags & Py_TPFLAGS_INLINE_VALUES)) {
            size_t word = LU_INLINE_STRIDE * sizeof(PyObject *);
            I(c, st)->imm2 = (int64_t)lu_inline_settle_room(t) - (int64_t)((index - (size_t)t->tp_basicsize) / word);
        }
    }
    else if (op == STORE_ATTR_SLOT) {
        guard_version(c, owner, version);
        lu_v args[3] = { owner, const_i(c, index), v };
        helper(c, H_slot_store, 3, args);
    }
    else if (store_attr_poly(c, owner, v)) {
        /* done */
    }
    else if (root_only(c)) {
        lu_v args[4] = { owner, konst(c, name), v, const_i(c, (int64_t)(intptr_t)lu_jit_pic(c->code, c->pc, 1)) };
        helper_raising(c, H_store_attr, 4, args, 2);
    }
    else {
        unsupported(c);
        return -1;
    }
    pop(c, NULL);
    pop(c, NULL);
    release(c, owner, ownowner);
    release(c, v, ownv);
    return 0;
}

/* --- calls --- */

/* What the callable value calls: a Python function to inline, and the self
   a kernel callable binds. */
static PyFunctionObject *
call_target(struct ctx *c, lu_v callable, PyObject **bound)
{
    PyObject *o = known(c, callable);
    *bound = NULL;
    if (o == NULL) {
        return NULL;
    }
    if (PyFunction_Check(o)) {
        return (PyFunctionObject *)o;
    }
    if (PyMethod_Check(o) && PyFunction_Check(((PyMethodObject *)o)->im_func)) {
        *bound = ((PyMethodObject *)o)->im_self;
        return (PyFunctionObject *)((PyMethodObject *)o)->im_func;
    }
    return kernel_function(o, bound);
}

/* A primitive as IR, when its operands' types allow: the result, or LU_NOV. */
static lu_v
intrinsic(struct ctx *c, const char *name, lu_v *args, int nargs, int *owned)
{
    *owned = 1;
    if (nargs == 2 && known_type(c, args[0]) == &PyFloat_Type && known_type(c, args[1]) == &PyFloat_Type) {
        static const struct { const char *name; int op; } fops[] = {
            { "float_add", OP_ADD_F }, { "float_sub", OP_SUB_F }, { "float_mul", OP_MUL_F },
            { "float_truediv", OP_DIV_F },
        };
        for (size_t i = 0; i < sizeof fops / sizeof fops[0]; i++) {
            if (strcmp(name, fops[i].name) == 0) {
                lu_v r = emit(c, fops[i].op, REP_F64, unbox_float(c, args[0]), unbox_float(c, args[1]), LU_NOV, 0);
                if (lu_op_flags[fops[i].op] & IF_GUARD) {
                    I(c, r)->fs = c->island != NULL ? c->island : before(c);
                }
                return box_float(c, r);
            }
        }
    }
    if (nargs == 3 && strcmp(name, "float_cmp") == 0 && known_type(c, args[0]) == &PyFloat_Type
        && known_type(c, args[1]) == &PyFloat_Type && known(c, args[2]) != NULL
        && PyLong_CheckExact(known(c, args[2]))) {
        long op = PyLong_AsLong(known(c, args[2]));
        *owned = 0;
        return emit(c, OP_BOOL, REP_OBJ,
                    emit(c, OP_CMP_F, REP_BIT, unbox_float(c, args[0]), unbox_float(c, args[1]), LU_NOV, op),
                    LU_NOV, LU_NOV, 0);
    }
    if (nargs == 1 && known_type(c, args[0]) == &PyFloat_Type) {
        if (strcmp(name, "float_neg") == 0) {
            return box_float(c, emit(c, OP_NEG_F, REP_F64, unbox_float(c, args[0]), LU_NOV, LU_NOV, 0));
        }
        if (strcmp(name, "float_abs") == 0) {
            return box_float(c, emit(c, OP_ABS_F, REP_F64, unbox_float(c, args[0]), LU_NOV, LU_NOV, 0));
        }
        if (strcmp(name, "m_sqrt") == 0) {
            return box_float(c, emit(c, OP_SQRT_F, REP_F64, unbox_float(c, args[0]), LU_NOV, LU_NOV, 0));
        }
        if (strcmp(name, "m_to_double") == 0) {
            *owned = 0;
            return args[0];
        }
    }
    if (nargs == 1 && known_type(c, args[0]) == &PyLong_Type) {
        if (strcmp(name, "int_abs") == 0) {
            lu_v r = emit(c, OP_ABS_I, REP_I64, unbox_int(c, args[0]), LU_NOV, LU_NOV, 0);
            I(c, r)->fs = before(c);
            return box_int(c, r);
        }
        if (strcmp(name, "int_neg") == 0) {
            lu_v r = emit(c, OP_NEG_I, REP_I64, unbox_int(c, args[0]), LU_NOV, LU_NOV, 0);
            I(c, r)->fs = before(c);
            return box_int(c, r);
        }
        if (strcmp(name, "m_to_double") == 0) {
            return box_float(c, emit(c, OP_I2F, REP_F64, unbox_int(c, args[0]), LU_NOV, LU_NOV, 0));
        }
    }
    if (nargs == 1 && (known_type(c, args[0]) == &PyList_Type || known_type(c, args[0]) == &PyTuple_Type
                       || known_type(c, args[0]) == &PyBytes_Type)
        && strcmp(name, "size") == 0) {
        lu_v n = emit(c, OP_LOAD, REP_I64, args[0], LU_NOV, LU_NOV, offsetof(PyVarObject, ob_size));
        return box_int(c, n);
    }
    if (nargs == 1 && known_type(c, args[0]) == &PyUnicode_Type && strcmp(name, "size") == 0) {
        lu_v n = emit(c, OP_LOAD, REP_I64, args[0], LU_NOV, LU_NOV, offsetof(PyASCIIObject, length));
        return box_int(c, n);
    }
    if (nargs == 2 && strcmp(name, "str_ord") == 0 && !lu_jit_nospec(c->code, c->pc)) {
        /* The code point read in line from an exact str's data, by its kind. */
        guard_type(c, args[0], &PyUnicode_Type);
        lu_v r = emit(c, OP_STR_ORD, REP_I64, args[0], unbox_int(c, args[1]), LU_NOV, 0);
        I(c, r)->fs = before(c);
        return box_int(c, r);
    }
    if (nargs == 2 && strcmp(name, "type_lookup") == 0 && known(c, args[0]) != NULL
        && PyType_Check(known(c, args[0])) && known(c, args[1]) != NULL && PyUnicode_CheckExact(known(c, args[1]))
        && (((PyTypeObject *)known(c, args[0]))->tp_flags & Py_TPFLAGS_IMMUTABLETYPE)) {
        /* A static type's methods never change. */
        PyObject *r = _PyType_Lookup((PyTypeObject *)known(c, args[0]), known(c, args[1]));
        *owned = 0;
        return konst(c, r != NULL ? r : LU_MISSING);
    }
    if (nargs == 2 && strcmp(name, "list_append") == 0 && known_type(c, args[0]) == &PyList_Type) {
        helper_raising(c, H_list_append, 2, args, c->oparg + 2);
        *owned = 0;
        return konst(c, Py_None);
    }
    return LU_NOV;
}

/* type(x) for a value whose exact type is known. */
static lu_v
type_of(struct ctx *c, lu_v v)
{
    PyTypeObject *t = known_type(c, v);
    if (t != NULL) {
        return konst(c, (PyObject *)t);
    }
    return emit(c, OP_LOAD, REP_OBJ, v, LU_NOV, LU_NOV, offsetof(PyObject, ob_type));
}

/* Pops a call's n + 2 operands, releasing each. */
static void
pop_call(struct ctx *c, int n)
{
    for (int i = 0; i < n + 2; i++) {
        int own;
        lu_v v = pop(c, &own);
        release(c, v, own);
    }
}

/* Argument kinds the interpreter saw at this call become guards on the
   stack entries, so that the callee's operations on them fold. */
static void
speculate_args(struct ctx *c, int n)
{
    if (lu_jit_nospec(c->code, c->pc)) {
        return;
    }
    int kinds = n >= 1 ? feedback(c, c->cache) : 0;     /* a call without arguments records none */
    for (int i = 0; i < 2 && i < n; i++) {
        int k = (kinds >> (8 * i)) & 0xff;
        int at = c->nlocals + c->st.sp - n + i;
        lu_v a = c->st.v[at];
        if (k == LU_KIND_INT && known_type(c, a) != &PyLong_Type) {
            c->st.v[at] = rebox(c, a, &PyLong_Type);
        }
        else if (k == LU_KIND_FLOAT && known_type(c, a) != &PyFloat_Type) {
            c->st.v[at] = rebox(c, a, &PyFloat_Type);
        }
        else if (k == LU_KIND_LIST) {
            guard_type(c, a, &PyList_Type);
        }
        else if (k == LU_KIND_TUPLE) {
            guard_type(c, a, &PyTuple_Type);
        }
        else if (k == LU_KIND_STR) {
            guard_type(c, a, &PyUnicode_Type);
        }
        else if (k == LU_KIND_BYTES) {
            guard_type(c, a, &PyBytes_Type);
        }
    }
}

/* Whether v joins answers of a polymorphic attribute site: a phi with a
   constant input. */
static int
poly_join(struct ctx *c, lu_v v)
{
    for (int i = 0; I(c, v)->op == OP_PHI && i < I(c, v)->nargs; i++) {
        if (I(c, I(c, v)->args[i])->op == OP_CONST && I(c, I(c, v)->args[i])->imm != 0) {
            return 1;
        }
    }
    return 0;
}

/* --- class calls --- */

static int opaque_call(struct ctx *c);

/* __init__ called, when it does not inline, from type.__call__'s made
   frame t at its call: the arguments as the frame would pass them (a
   tuple and an empty dict, which only exits see), so that an exit of the
   call, and a walker during it, see the frames the interpreter would.
   Takes the arguments' references (own); the value the call returns is
   pushed in t. 0 when the frame cannot make the call (opaque_call). With
   descr (a kernel __init__'s descriptor), the frame's call is
   call_special(descr, obj, *args, **kwargs) instead, and init its
   kernel function. */
static int
init_call(struct ctx *c, struct ctx *t, PyFunctionObject *init, PyObject *descr, lu_v *args, uint8_t *own, int nargs)
{
    PyObject *special = NULL;
    if (descr != NULL) {
        special = PyDict_GetItemString(t->globals, "call_special");
        if (special == NULL || !PyFunction_Check(special)) {
            return 0;
        }
    }
    if (!opaque_call(c)) {
        return 0;
    }
    lu_v first = c->f->nins;
    struct lu_fs *bare = snapshot(t, t->pc);   /* an error unwinds from after the call */
    lu_v dict = helper(t, H_new_dict, 0, NULL);
    I(c, dict)->fs = bare;
    lu_v tuple;
    if (descr != NULL) {
        lu_v items[34];
        items[0] = konst(t, descr);
        memcpy(items + 1, args, sizeof(lu_v) * (size_t)nargs);
        tuple = helper(t, H_build_tuple, nargs + 1, items);
        I(c, tuple)->fs = bare;
    }
    else {
        tuple = nargs > 1 ? helper(t, H_build_tuple, nargs - 1, args + 1)
            : konst(t, (PyObject *)&_PyRuntime.singletons.empty_tuple);
        if (nargs > 1) {
            I(c, tuple)->fs = bare;
        }
    }
    for (int i = 1; i < nargs; i++) {
        release(t, args[i], own[i]);    /* the tuple holds them */
    }
    push(t, konst(t, descr != NULL ? special : (PyObject *)init), 0);
    push(t, LU_NOV, 0);
    push(t, tuple, descr != NULL || nargs > 1);
    push(t, dict, 1);
    struct lu_fs *at = snapshot(t, t->pc);
    lu_v hargs[34];
    hargs[0] = konst(t, (PyObject *)init);
    memcpy(hargs + 1, args, sizeof(lu_v) * (size_t)nargs);
    lu_v r = helper(t, H_call, nargs + 1, hargs);
    I(c, r)->fs = at;
    I(c, r)->drop = 4;
    /* Declined (the C stack is too deep): the interpreter makes the call. */
    lu_v g = emit(t, OP_GUARD_NOT_NULL, REP_NONE, r, LU_NOV, LU_NOV, 1);
    I(c, g)->fs = at;
    I(c, g)->flags |= INS_NOSTRESS;
    for (int i = 0; i < 4; i++) {
        int own;
        lu_v e = pop(t, &own);
        release(t, e, own);
    }
    push(t, r, 1);
    frames_guard(t, first);
    return 1;
}

/* type.__call__ as the caller of an initializer: the kernel function, its
   call `res = init(obj, *args, **kwargs)` of a Python initializer (which
   = 0) or `res = call_special(init, obj, *args, **kwargs)` of another
   (which = 1) and the instruction after it, and the local obj; 0 when the
   kernel's code is not shaped so, or a handler covers the call. */
struct type_call {
    PyFunctionObject *fn;
    int call_pc, next, obj;
};

static int
local_named(PyCodeObject *co, int i, const char *name)
{
    return PyUnicode_CompareWithASCIIString(PyTuple_GET_ITEM(co->co_localsplusnames, i), name) == 0;
}

static int
type_call_site(struct type_call *tc, int which)
{
    PyObject *self;
    tc->fn = kernel_function(_PyType_Lookup(&PyType_Type, LU_ID(__call__)), &self);
    if (tc->fn == NULL) {
        return 0;
    }
    struct ctx probe = { .code = (PyCodeObject *)tc->fn->func_code };
    PyCodeObject *co = probe.code;
    tc->obj = -1;
    for (int i = 0; i < co->co_nlocalsplus; i++) {
        tc->obj = local_named(co, i, "obj") ? i : tc->obj;
    }
    for (int pc = 0, n = (int)Py_SIZE(co); pc < n && tc->obj >= 0;) {
        int base, oparg, op, base2, oparg2;
        int next = decode(&probe, pc, &base, &oparg, &op);
        if (base == CALL_FUNCTION_EX && next < n && (decode(&probe, next, &base2, &oparg2, &op), base2 == STORE_FAST)
            && local_named(co, oparg2, "res") && which-- == 0) {
            int level, handler, lasti;
            tc->call_pc = pc;
            tc->next = next;
            return !lu_exception_handler(co, pc, &level, &handler, &lasti);
        }
        pc = next;
    }
    return 0;
}

/* Whether a call of class tp with nargs arguments is one type.__call__
   makes by object.__new__ and then a Python __init__ (*init), or object's
   own given none (*init NULL), for an instance of an ordinary layout: as
   of tp's version *version, which the call guards, and its metaclass's
   (*meta: 0 for type itself), a class of type's whose __call__ is type's
   (ABCMeta's). */
static int
class_init(struct ctx *c, PyTypeObject *tp, int nargs, PyFunctionObject **init, uint32_t *version, uint32_t *meta)
{
    PyTypeObject *mt = Py_TYPE(tp);
    *meta = 0;
    if (mt != &PyType_Type) {
        *meta = lu_type_assign_version(mt) ? __atomic_load_n(&mt->tp_version_tag, __ATOMIC_ACQUIRE) : 0;
        if (*meta == 0 || mt->tp_call != PyType_Type.tp_call
            || _PyType_Lookup(mt, LU_ID(__call__)) != _PyType_Lookup(&PyType_Type, LU_ID(__call__))
            || __atomic_load_n(&mt->tp_version_tag, __ATOMIC_ACQUIRE) != *meta) {
            return 0;
        }
    }
    if (!PyType_IsSubtype(mt, &PyType_Type) || !(tp->tp_flags & Py_TPFLAGS_HEAPTYPE) || !(tp->tp_flags & Py_TPFLAGS_HAVE_GC)
        || (tp->tp_flags & Py_TPFLAGS_IS_ABSTRACT) || tp->tp_alloc != PyType_GenericAlloc || tp->tp_itemsize != 0
        || tp->tp_new == NULL || lu_jit_nospec(c->code, c->pc) || !lu_type_assign_version(tp)) {
        return 0;
    }
    PyTypeObject *solid = tp;
    while (solid->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        solid = solid->tp_base;
    }
    if (solid != &PyBaseObject_Type) {
        return 0;               /* object.__new__ refuses it (check_new) */
    }
    *version = __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE);
    PyObject *new = _PyType_Lookup(tp, LU_ID(__new__)), *in = _PyType_Lookup(tp, LU_ID(__init__));
    if (new != _PyType_Lookup(&PyBaseObject_Type, LU_ID(__new__)) || in == NULL
        || __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) != *version) {
        return 0;
    }
    *init = Py_IS_TYPE(in, &PyFunction_Type) ? (PyFunctionObject *)in : NULL;
    return *init != NULL || (in == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__init__)) && nargs == 0);
}

/* A call of class tp that type.__call__ makes by the kernel __new__ of
   its builtin base (*new: list, set, bytearray, an exception class and
   their subclasses), which makes an instance of tp, and then __init__: a
   Python function, or (*kinit) the kernel function of the base's own; as
   of tp's version *version and its metaclass's *meta (class_init). */
static int
kernel_class(struct ctx *c, PyTypeObject *tp, PyFunctionObject **new, PyFunctionObject **init, int *kinit,
             uint32_t *version, uint32_t *meta)
{
    PyTypeObject *mt = Py_TYPE(tp);
    *meta = 0;
    if (mt != &PyType_Type) {
        *meta = lu_type_assign_version(mt) ? __atomic_load_n(&mt->tp_version_tag, __ATOMIC_ACQUIRE) : 0;
        if (*meta == 0 || mt->tp_call != PyType_Type.tp_call
            || _PyType_Lookup(mt, LU_ID(__call__)) != _PyType_Lookup(&PyType_Type, LU_ID(__call__))
            || __atomic_load_n(&mt->tp_version_tag, __ATOMIC_ACQUIRE) != *meta) {
            return 0;
        }
    }
    if (!PyType_IsSubtype(mt, &PyType_Type) || (tp->tp_flags & Py_TPFLAGS_IS_ABSTRACT) || tp->tp_new == NULL
        || lu_jit_nospec(c->code, c->pc) || !lu_type_assign_version(tp)) {
        return 0;
    }
    PyTypeObject *solid = tp;
    while (solid->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        solid = solid->tp_base;
    }
    if (solid == &PyBaseObject_Type || solid == &PyType_Type) {
        return 0;
    }
    *version = __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE);
    PyObject *new_ = _PyType_Lookup(tp, LU_ID(__new__)), *in = _PyType_Lookup(tp, LU_ID(__init__)), *f, *self;
    if (new_ == NULL || new_ != _PyType_Lookup(solid, LU_ID(__new__)) || !lu_new_function(new_, &f)
        || !(((PyCodeObject *)((PyFunctionObject *)f)->func_code)->co_flags & LU_CO_KERNEL) || in == NULL
        || in == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__init__))) {
        return 0;
    }
    *new = (PyFunctionObject *)f;
    *kinit = !Py_IS_TYPE(in, &PyFunction_Type);
    *init = *kinit ? kernel_function(in, &self) : (PyFunctionObject *)in;
    if (*init == NULL || (*kinit && (self != NULL || in != _PyType_Lookup(solid, LU_ID(__init__))
                                     || !(((PyCodeObject *)(*init)->func_code)->co_flags & LU_CO_KERNEL)))) {
        return 0;
    }
    return __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) == *version;
}

/* A class whose __new__ is a kernel function of its own and whose
   __init__ is object's (str, bytes, int, range, zip and their
   subclasses): type.__call__ calls no __init__ after such a __new__ and
   returns what it returns, so a call of the class is a call of its
   __new__ with the class first. The function, as of the class's version
   *version, which the call guards; NULL for anything else. */
static PyFunctionObject *
new_call(struct ctx *c, PyTypeObject *tp, uint32_t *version)
{
    PyObject *f;
    if (!Py_IS_TYPE(tp, &PyType_Type) || tp == &PyType_Type || tp->tp_new == NULL || lu_jit_nospec(c->code, c->pc)
        || !lu_type_assign_version(tp)) {
        return NULL;
    }
    *version = __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE);
    PyObject *new_ = _PyType_Lookup(tp, LU_ID(__new__)), *init = _PyType_Lookup(tp, LU_ID(__init__));
    if (new_ == NULL || new_ == _PyType_Lookup(&PyBaseObject_Type, LU_ID(__new__))
        || init != _PyType_Lookup(&PyBaseObject_Type, LU_ID(__init__)) || !lu_new_function(new_, &f)
        || !(((PyCodeObject *)((PyFunctionObject *)f)->func_code)->co_flags & LU_CO_KERNEL)
        || __atomic_load_n(&tp->tp_version_tag, __ATOMIC_ACQUIRE) != *version) {
        return NULL;
    }
    return (PyFunctionObject *)f;
}

/* A call of class tp as type.__call__ makes it (class_init): the class's
   version guarded, the instance made in line (or by the kernel __new__ of
   its base, new, called: kernel_class), then __init__ inlined with a
   made frame of type.__call__ as its caller, at its boundary after the
   call (after call_special's for a kernel __init__, kinit). An exit inside the initializer resumes there, and type.__call__
   checks the result and returns the instance, as it does when one returns
   a value. With nkwonly, args holds __init__'s parameters but self bound
   by keyword (bind_keywords): nargs positional ones, then its keyword-only
   ones; such a call inlines or is not made here. nops: the stack entries
   the call consumes. 1 with the call done, or 0 with nothing emitted. */
static int
class_call(struct ctx *c, PyTypeObject *tp, PyFunctionObject *new, PyFunctionObject *init, int kinit, uint32_t version,
           uint32_t meta, lu_v *args, uint8_t *own, int nargs, int nkwonly, int nops)
{
    PyObject *init_descr = kinit ? _PyType_Lookup(tp, LU_ID(__init__)) : NULL;
    struct type_call tc = { 0 };
    if (c->cur < 0 || nargs > 31
        || (init != NULL && ((!kinit && init->func_version < 2) || !type_call_site(&tc, kinit)))) {
        return 0;
    }
    int n = nops - 2, by_keyword = c->base == CALL_KW;
    struct mark m;
    mark_take(c, &m);
    lu_v cls = konst(c, (PyObject *)tp);
    lu_v g = guard(c, OP_GUARD_VERSION, cls, version);
    I(c, g)->imm2 = GV_TYPE_ITSELF;
    if (meta != 0) {
        guard(c, OP_GUARD_VERSION, cls, meta);  /* its metaclass's __call__ is still type's */
    }
    if (init != NULL && !kinit) {
        guard_func(c, init);
    }
    lu_v obj;
    if (new != NULL) {
        /* __new__ raises at the call, where nothing is made yet, and when
           the C stack is too deep the interpreter makes the call. */
        lu_v hargs[33];
        hargs[0] = konst(c, (PyObject *)new);
        hargs[1] = cls;
        memcpy(hargs + 2, args, sizeof(lu_v) * (size_t)nargs);
        /* An error exit, never a handler of this unit's: an undone
           inline would leave a handler's pending edge behind. */
        obj = helper(c, H_call, nargs + 2, hargs);
        I(c, obj)->fs = before(c);
        I(c, obj)->drop = (uint8_t)(n + 2);
        I(c, obj)->imm2 = 1 + CNT_call_c;
        guard_after(c, OP_GUARD_NOT_NULL, obj, 1);
        set_known(c, obj, tp);
    }
    else {
        /* Nothing leaves at the call's state after this: the instance is in no state there. */
        obj = helper_leaving(c, H_new_object, 1, &cls, n + 2);
        set_made(c, obj, tp);
    }
    lu_v callable = peek(c, n + 2);
    int owncallable = peek_owned(c, n + 2);
    struct bstate saved;
    state_copy(c, &saved, &c->st);
    c->st.sp -= n + 2;
    if (owncallable) {
        emit(c, OP_DECREF, REP_NONE, callable, LU_NOV, LU_NOV, 0);     /* the unit keeps the class */
    }
    if (init != NULL) {
        PyCodeObject *co = (PyCodeObject *)tc.fn->func_code;
        struct ctx *t = calloc(1, sizeof(struct ctx));
        t->f = c->f;
        t->parent = c;
        t->code = co;
        t->func = tc.fn;
        t->globals = tc.fn->func_globals;
        t->builtins = tc.fn->func_builtins;
        t->kernel = 1;
        t->made = 1;
        t->ncode = (int)Py_SIZE(co);
        t->nlocals = co->co_nlocalsplus;
        t->nvars = t->nlocals + co->co_stacksize + 1;
        t->depth = c->depth + 1;
        t->island = c->island;
        t->fixed = fixed_params(t);
        t->caller = snapshot(c, c->next);
        state_init(t, &t->st);
        t->st.v[tc.obj] = obj;
        t->st.own[tc.obj] = 1;
        t->pc = tc.call_pc;
        t->next = tc.next;
        t->cur = c->cur;
        lu_v iargs[33], res;
        uint8_t iown[33];
        iargs[0] = obj;
        iown[0] = 0;
        memcpy(iargs + 1, args, sizeof(lu_v) * (size_t)(nargs + nkwonly));
        memcpy(iown + 1, own, (size_t)(nargs + nkwonly));
        int resown, ok = inline_call(t, init, LU_NOV, 0, iargs, iown, nargs + 1, nkwonly, &res, &resown);
        if (!ok && !c->failed && !by_keyword && (ok = init_call(c, t, init, kinit ? init_descr : NULL, iargs, iown, nargs + 1))) {
            res = pop(t, &resown);
        }
        if (ok && t->cur >= 0 && known(t, res) != Py_None) {
            /* Anything else is type.__call__'s TypeError, raised after the call. */
            push(t, res, resown);
            struct lu_fs *fs = snapshot(t, t->next);
            pop(t, NULL);
            lu_v gi = emit(t, OP_GUARD_IS, REP_NONE, res, LU_NOV, LU_NOV, (int64_t)(intptr_t)Py_None);
            I(c, gi)->fs = fs;
            release(t, res, resown);
        }
        if (ok) {
            wire_pending(c, t);
            c->cur = t->cur;
        }
        free(t->pend);
        free(t);
        if (!ok) {
            c->st = saved;
            mark_undo(c, &m);
            mark_free(&m);
            return 0;
        }
    }
    if (c->cur >= 0) {
        push(c, obj, 1);
    }
    mark_free(&m);
    return 1;
}

/* Whether the call at c->pc, which this frame cannot inline, compiles
   here. In an inlined frame it pushes the frame as a real one, which costs
   about what calling the frame did: so a callee one deep with one such
   call outside its loops inlines, and any other is called instead (the
   inline is undone). A call the interpreter never made there leaves: a
   cold path's call would make the root's locals real boxes on hot ones. */
static int
opaque_call(struct ctx *c)
{
    if (c->parent == NULL) {
        return 1;
    }
    if (__atomic_load_n(&c->cache->cache, __ATOMIC_RELAXED) == 0) {
        unsupported(c);
        return 0;
    }
    int loop = 0;
    for (int pc = c->next; pc < c->ncode && !loop;) {
        int base, oparg, op;
        int next = decode(c, pc, &base, &oparg, &op);
        loop = base == JUMP_BACKWARD && jump_target(base, oparg, next) <= c->pc;
        pc = next;
    }
    if (c->depth > 1 || c->ncalls++ > 0 || loop) {
        c->failed = 1;
        c->why = "a call it cannot inline";
        return 0;
    }
    return 1;
}

/* Whether code has a loop. An inlined loop keeps its ints boxed where the
   callee's own unit keeps them raw (Sieve 2.8x the instructions), so a
   callee found only by feedback that loops is called. */
static int
loops(PyCodeObject *co)
{
    for (int pc = 0; pc < (int)Py_SIZE(co); pc = lu_jit_next_pc(co, pc)) {
        if (lu_base_opcode(co, pc) == JUMP_BACKWARD) {
            return 1;
        }
    }
    return 0;
}

/* A function this unit made (MAKE_FUNCTION), reached however it was
   passed (a closure handed to a callee that inlined): its code and globals
   are known, as a template the unit keeps (lu_function_template), and only
   its code can change since (f.__code__ = ...), which a guard checks. */
static PyFunctionObject *
made_function(struct ctx *c, lu_v v)
{
    struct lu_ins *in = I(c, v);
    if (in->op != OP_HELPER || in->imm != H_make_function || lu_jit_nospec(c->code, c->pc)) {
        return NULL;
    }
    PyObject *code = known(c, in->args[0]), *globals = known(c, in->args[1]);
    PyObject *tmpl = code != NULL && globals != NULL ? lu_function_template(code, globals) : NULL;
    if (tmpl == NULL) {
        PyErr_Clear();
        return NULL;
    }
    konst(c, tmpl);
    Py_DECREF(tmpl);
    lu_v g = guard(c, OP_GUARD_IS, v, (int64_t)(intptr_t)code);
    I(c, g)->imm2 = GI_FIELD | offsetof(PyFunctionObject, func_code);
    return (PyFunctionObject *)tmpl;
}

/* Leaves for block fail, or with fail < 0 deopts, unless bit is set. */
static void
test_bit(struct ctx *c, lu_v bit, int fail)
{
    if (fail < 0) {
        guard(c, OP_GUARD_BIT, bit, 1);
        return;
    }
    int ok = lu_ir_block(c->f);
    lu_ir_edge(c->f, c->cur, ok);
    lu_ir_edge(c->f, c->cur, fail);
    emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
    c->cur = ok;
}

/* The same unless the word at offset off of v (off < 0: v itself) is o. */
static void
test_word(struct ctx *c, lu_v v, int off, PyObject *o, int fail)
{
    lu_v k = konst(c, o);       /* the unit keeps what it compares with */
    if (fail < 0) {
        lu_v g = guard(c, OP_GUARD_IS, v, (int64_t)(intptr_t)o);
        I(c, g)->imm2 = off < 0 ? 0 : GI_FIELD | off;
        return;
    }
    lu_v w = off < 0 ? v : emit(c, OP_LOAD, REP_OBJ, v, LU_NOV, LU_NOV, off);
    test_bit(c, emit(c, OP_IS, REP_BIT, w, k, LU_NOV, 0), fail);
}

/* Goes on in a new block when callable is e's callee (obj its function or
   code), else leaves for fail (fail < 0: deopts, as a guard): the function
   it calls. */
static lu_v
callee_test(struct ctx *c, lu_v callable, const struct lu_callee *e, int fail)
{
    lu_v fv = callable;
    if (e->kind & LU_CALLEE_BOUND) {
        test_word(c, callable, offsetof(PyObject, ob_type), (PyObject *)&PyMethod_Type, fail);
        fv = emit(c, OP_LOAD, REP_OBJ, callable, LU_NOV, LU_NOV, offsetof(PyMethodObject, im_func));
    }
    if (e->kind & LU_CALLEE_CODE) {
        test_word(c, fv, offsetof(PyObject, ob_type), (PyObject *)&PyFunction_Type, fail);
        test_word(c, fv, offsetof(PyFunctionObject, func_code), e->obj, fail);
        test_word(c, fv, offsetof(PyFunctionObject, func_globals), e->globals, fail);
    }
    else {
        test_word(c, fv, -1, e->obj, fail);
    }
    return fv;
}

/* A call of a callable the code does not name, by the callees the
   interpreter met at the site (jit.h): each is tested in turn, the last as
   a guard, and the call is built once per callee with that callee known,
   so that each inlines; the paths join after the call. A callee added to
   the site's table later recompiles the unit. 1 when built, 0 when the site
   has no table to use, -1 when the frame cannot be compiled. */
static int call_kw(struct ctx *c);

static int
call_dispatch(struct ctx *c, int n)
{
    int site = (int)(c->cache - 1 - lu_code_start(c->code));
    struct lu_callees *t = lu_jit_callees(c->code, site);
    if (t == NULL || __atomic_load_n(&t->mega, __ATOMIC_ACQUIRE) || lu_jit_nospec(c->code, c->pc)
        || c->generic_call == c->next) {
        return 0;
    }
    /* Entries name only what a collection frees, and this thread does not
       poll before the unit holds them; a closure's globals are compared
       only, and used only when a frame being compiled holds them. */
    struct lu_callee e[LU_CALLEE_WAYS];
    int m = 0;
    for (int i = 0, k = __atomic_load_n(&t->n, __ATOMIC_ACQUIRE); i < k; i++) {
        struct lu_callee x = t->e[i];
        int live = !(x.kind & LU_CALLEE_CODE);
        for (struct ctx *p = c; p != NULL && !live; p = p->parent) {
            live = p->globals == x.globals;
        }
        PyObject *fo = x.kind & LU_CALLEE_CODE ? x.obj : x.obj != NULL && PyFunction_Check(x.obj)
            ? ((PyFunctionObject *)x.obj)->func_code : NULL;
        if (fo != NULL && (lu_jit_noinline((PyCodeObject *)fo) || loops((PyCodeObject *)fo))) {
            return 0;           /* a callee called anyway, or one that loops: the call as it is costs least */
        }
        if (live && x.obj != NULL) {
            e[m++] = x;
        }
    }
    if (m == 0) {
        return 0;
    }
    struct lu_fdep dep = { .kind = LU_DEP_CALLEES, .obj = (uintptr_t)t, .code = c->code, .pc = c->pc };
    lu_ir_dep(c->f, &dep);
    int callable_at = c->nlocals + c->st.sp - (n + 2 + (c->base == CALL_KW));
    lu_v callable = c->st.v[callable_at];
    int own = c->st.own[callable_at];
    struct ctx at = *c;
    struct bstate base;
    state_copy(c, &base, &c->st);
    struct path ends[LU_CALLEE_WAYS];
    int ncalls = c->ncalls, most = ncalls;     /* the arms are alternatives: one call's worth */
    for (int i = 0; i < m; i++) {
        c->ncalls = ncalls;
        int fail = i < m - 1 ? lu_ir_block(c->f) : -1;
        lu_v fv = callee_test(c, callable, &e[i], fail);
        if (e[i].kind & LU_CALLEE_BOUND) {
            /* The bound method's self and function become the call's, and it goes. */
            lu_v self = emit(c, OP_LOAD, REP_OBJ, callable, LU_NOV, LU_NOV, offsetof(PyMethodObject, im_self));
            incref(c, self);
            c->st.v[callable_at + 1] = self;
            c->st.own[callable_at + 1] = 1;
            if (e[i].kind & LU_CALLEE_CODE) {
                incref(c, fv);
            }
            release(c, callable, own);
        }
        if (e[i].kind & LU_CALLEE_CODE) {
            PyObject *tmpl = lu_function_template(e[i].obj, e[i].globals);
            if (tmpl == NULL) {
                Py_FatalError("lucifer jit: out of memory");
            }
            konst(c, tmpl);     /* the unit names the inlined frames by it */
            Py_DECREF(tmpl);
            c->st.v[callable_at] = fv;
            c->st.own[callable_at] = (e[i].kind & LU_CALLEE_BOUND) || own;
            c->code_fn = (PyFunctionObject *)tmpl;
            c->code_value = fv;
        }
        else {
            if (!(e[i].kind & LU_CALLEE_BOUND)) {
                release(c, callable, own);      /* the constant is the same object */
            }
            c->st.v[callable_at] = konst(c, e[i].obj);
            c->st.own[callable_at] = 0;
        }
        /* Guards of the call leave with the stack as it is now: the
           interpreter calls the function with the bound method's self. */
        c->snap = NULL;
        before(c);
        int built = c->base == CALL_KW ? call_kw(c) : call(c);
        c->code_fn = NULL;
        if (built < 0 && c->failed) {
            return -1;
        }
        ends[i] = path_here(c);
        most = c->ncalls > most ? c->ncalls : most;
        c->next = at.next;
        c->snap = at.snap;
        state_copy(c, &c->st, &base);
        c->cur = fail;
    }
    c->ncalls = most;
    join_paths(c, ends, m);
    return 1;
}

static int
call(struct ctx *c)
{
    int n = c->oparg;
    if (n + 1 > 30) {
        unsupported(c);
        return -1;
    }
    lu_v callable = peek(c, n + 2), self = peek(c, n + 1);
    PyObject *kc = known(c, callable);
    PyObject *bound;
    PyFunctionObject *fn = call_target(c, callable, &bound);
    lu_v fnv = LU_NOV;
    if (fn == NULL && c->code_fn != NULL && callable == c->code_value) {
        fn = c->code_fn;        /* a callee test proved its code */
        fnv = callable;
    }
    else if (kc == NULL && (fn = made_function(c, callable)) != NULL) {
        fnv = callable;
    }
    else if (kc == NULL && self == LU_NOV) {
        int built = call_dispatch(c, n);
        if (built != 0) {
            return built < 0 ? -1 : 0;
        }
    }
    const char *prim = primitive_name(kc);
    if (kc != NULL && PyCFunction_Check(kc) && (((PyCFunctionObject *)kc)->m_ml->ml_flags & LU_METH_FRAME)) {
        /* It hands the interpreter a frame to run: a generator's resumption
           runs here; anything else leaves. */
        if (self != LU_NOV || n != 2 || strcmp(((PyCFunctionObject *)kc)->m_ml->ml_name, "gen_send") != 0) {
            unsupported(c);
            return -1;
        }
        if (!opaque_call(c)) {
            return -1;
        }
        lu_v hargs[3] = { callable, peek(c, 2), peek(c, 1) };
        lu_v r = helper_raising(c, H_gen_send, 3, hargs, n + 2);
        guard_after(c, OP_GUARD_NOT_NULL, r, 1);    /* declined: the interpreter resumes it */
        pop_call(c, n);
        push(c, r, 1);
        return 0;
    }
    lu_v r = LU_NOV;
    int rown = 1;
    PyFunctionObject *init = NULL;
    uint32_t version = 0, meta = 0;
    int klass = kc != NULL && PyType_Check(kc) && self == LU_NOV
        && class_init(c, (PyTypeObject *)kc, n, &init, &version, &meta);
    if (_PyRuntime.jit.debug && kc != NULL && PyType_Check(kc) && !klass
        && (((PyTypeObject *)kc)->tp_flags & Py_TPFLAGS_HEAPTYPE)) {
        fprintf(stderr, "jit: class call not lowered: %s\n", ((PyTypeObject *)kc)->tp_name);
    }
    if ((fn != NULL && bound == NULL) || klass) {
        speculate_args(c, n);
    }
    lu_v args[32];
    uint8_t own[32];
    int nargs = 0;
    if (self != LU_NOV) {
        args[nargs] = self;
        own[nargs++] = (uint8_t)peek_owned(c, n + 1);
    }
    for (int i = n; i >= 1; i--) {
        args[nargs] = peek(c, i);
        own[nargs++] = (uint8_t)peek_owned(c, i);
    }
    /* range(...) iterated at once: the range never exists (scalar replaced). */
    int nbase, noparg, nopcode;
    if (kc == (PyObject *)&PyRange_Type && self == LU_NOV && n >= 1 && n <= 3
        && c->next < c->ncode && (decode(c, c->next, &nbase, &noparg, &nopcode), nbase == GET_ITER)) {
        lu_v start = n >= 2 ? unbox_int(c, args[0]) : const_i(c, 0);
        lu_v stop = unbox_int(c, args[n >= 2 ? 1 : 0]);
        lu_v step = n == 3 ? unbox_int(c, args[2]) : const_i(c, 1);
        if (n == 3) {
            guard(c, OP_GUARD_BIT, emit(c, OP_CMP_I, REP_BIT, step, const_i(c, 0), LU_NOV, Py_NE), 1);
        }
        int ibase, ioparg, iop;
        if (n <= 2 && steps_fresh(c, decode(c, c->next, &ibase, &ioparg, &iop)) && !lu_jit_nospec(c->code, c->pc)) {
            /* Its length fits, and so does every value with step 1. */
            lu_v len = emit(c, OP_SUB_I, REP_I64, stop, start, LU_NOV, 0);
            I(c, len)->fs = before(c);
            lu_v fields[3] = { start, step, len };
            r = helper(c, H_rangeiter_new, 3, fields);  /* a loop iterator (for_step) */
        }
        else {
            lu_v hargs[3] = { start, stop, step };
            r = helper(c, H_range_iter, 3, hargs);
            guard_after(c, OP_GUARD_NOT_NULL, r, 0);
        }
        pop_call(c, n);
        push(c, r, 1);
        /* Skip GET_ITER: the next instruction gets the iterator. */
        int gbase, goparg, gop;
        c->next = decode(c, c->next, &gbase, &goparg, &gop);
        return 0;
    }
    if (kc == (PyObject *)&PyType_Type && self == LU_NOV && n == 1) {
        r = type_of(c, args[0]);
        rown = 0;
        if (I(c, r)->op == OP_LOAD) {
            incref(c, r);
            rown = 1;
        }
    }
    else if (prim != NULL) {
        r = intrinsic(c, prim, args, nargs, &rown);
    }
    if (r != LU_NOV) {
        pop_call(c, n);
        push(c, r, rown);
        return 0;
    }
    if (klass && class_call(c, (PyTypeObject *)kc, NULL, init, 0, version, meta, args, own, nargs, 0, n + 2)) {
        return 0;
    }
    PyFunctionObject *knew;
    int kinit;
    /* Only where the call has run: an error path's exception class would
       add exits whose states keep the hot path's values boxed. */
    if (!klass && kc != NULL && PyType_Check(kc) && self == LU_NOV
        && __atomic_load_n(&c->cache->cache, __ATOMIC_RELAXED) != 0
        && kernel_class(c, (PyTypeObject *)kc, &knew, &init, &kinit, &version, &meta)
        && class_call(c, (PyTypeObject *)kc, knew, init, kinit, version, meta, args, own, nargs, 0, n + 2)) {
        return 0;
    }
    PyFunctionObject *newfn = !klass && kc != NULL && PyType_Check(kc) && self == LU_NOV && nargs < 31
        ? new_call(c, (PyTypeObject *)kc, &version) : NULL;
    if (newfn != NULL) {
        lu_v g = guard(c, OP_GUARD_VERSION, callable, version);
        I(c, g)->imm2 = GV_TYPE_ITSELF;
        memmove(args + 1, args, sizeof(lu_v) * (size_t)nargs);
        memmove(own + 1, own, (size_t)nargs);
        args[0] = callable;     /* the class, a constant */
        own[0] = 0;
        nargs++;
        fn = newfn;
    }
    PyFunctionObject *gfn;
    PyCodeObject *gco;
    if (fn != NULL && bound == NULL && self == LU_NOV && n == 1 && (((PyCodeObject *)fn->func_code)->co_flags & LU_CO_KERNEL)
        && PyUnicode_CompareWithASCIIString(fn->func_qualname, "next_item") == 0
        && (gco = gen_made(c, args[0], &gfn)) != NULL && local_holds(c, args[0])) {
        /* The iteration step of a generator whose code is known: resumed in line. */
        lu_v next = inline_resume(c, args[0], gco, gfn, LU_RETURN_NEXT);
        if (next != LU_NOV) {
            pop_call(c, n);
            push(c, next, 1);
            return 0;
        }
    }
    if (fn != NULL && bound == NULL && self == LU_NOV && n == 1 && (((PyCodeObject *)fn->func_code)->co_flags & LU_CO_KERNEL)
        && PyUnicode_CompareWithASCIIString(fn->func_qualname, "next_item") == 0) {
        /* next_item is FOR_ITER's step, which C takes for the iterators it
           knows (a generator resumed in its own unit), else the kernel's. */
        if (!opaque_call(c)) {
            return -1;
        }
        r = helper_raising(c, H_for_iter, 1, args, n + 2);
        pop_call(c, n);
        push(c, r, 1);
        return 0;
    }
    if (fn != NULL && bound == NULL && fn->func_code != NULL
        && (((PyCodeObject *)fn->func_code)->co_flags & LU_CO_KERNEL)
        && PyUnicode_CompareWithASCIIString(fn->func_qualname, "call_special") == 0
        && nargs >= 2 && known(c, args[0]) != NULL) {
        /* call_special(descr, obj, ...) with a constant function or kernel
           descriptor is descr(obj, ...): its first test decides. */
        PyObject *descr = known(c, args[0]), *dself;
        PyFunctionObject *target = PyFunction_Check(descr) ? (PyFunctionObject *)descr
            : (Py_IS_TYPE(descr, &PyMethodDescr_Type) || Py_IS_TYPE(descr, &PyWrapperDescr_Type))
              ? kernel_function(descr, &dself) : NULL;
        if (target != NULL) {
            fn = target;
            memmove(args, args + 1, sizeof(lu_v) * (size_t)(nargs - 1));
            memmove(own, own + 1, (size_t)(nargs - 1));
            nargs--;
        }
    }
    else if (fn != NULL && bound == NULL && fn->func_code != NULL
             && (((PyCodeObject *)fn->func_code)->co_flags & LU_CO_KERNEL)
             && PyUnicode_CompareWithASCIIString(fn->func_qualname, "call_special") == 0
             && self == LU_NOV && n >= 2 && n <= 32 && !lu_jit_nospec(c->code, c->pc)) {
        /* A descriptor known only at run time: called unbound when it is a
           function or a kernel descriptor, which is what call_special's
           first test does, without its *args call the tier leaves at. */
        if (!opaque_call(c)) {
            return -1;
        }
        lu_v hargs[32];
        for (int i = 0; i < n; i++) {
            hargs[i] = peek(c, n - i);
        }
        r = helper_raising(c, H_call_special, n, hargs, n + 2);
        I(c, r)->imm2 = CNT_call_unknown + 1;
        guard_after(c, OP_GUARD_NOT_NULL, r, 1);
        pop_call(c, n);
        push(c, r, 1);
        return 0;
    }
    if (fn != NULL && bound == NULL) {
        /* The callee takes the arguments' references; the rest go here. */
        int owncallable = peek_owned(c, n + 2);
        lu_v descr = nargs < n + (self != LU_NOV) ? peek(c, n) : LU_NOV;
        int owndescr = descr != LU_NOV ? peek_owned(c, n) : 0;
        struct bstate saved;
        state_copy(c, &saved, &c->st);
        c->st.sp -= n + 2;
        if (inline_call(c, fn, fnv, owncallable, args, own, nargs, 0, &r, &rown)) {
            if (c->cur >= 0) {          /* else every path of the callee left */
                if (fnv == LU_NOV) {
                    release(c, callable, owncallable);
                }
                release(c, descr, owndescr);
                push(c, r, rown);
            }
            return 0;
        }
        c->st = saved;
        r = LU_NOV;
    }
    if (c->failed) {
        return -1;
    }
    int flags = prim != NULL ? ((PyCFunctionObject *)kc)->m_ml->ml_flags : 0;
    if (prim != NULL && self == LU_NOV
        && (flags & (METH_VARARGS | METH_FASTCALL | METH_NOARGS | METH_O | METH_KEYWORDS | METH_METHOD))
           == METH_FASTCALL) {
        /* A primitive that runs no Python needs no frame to be real, inlined
           or not; one that may is called as h_call would, without its
           dispatch. */
        int py = !(flags & LU_METH_NOPY);
        if (py && !opaque_call(c)) {
            return -1;
        }
        lu_v hargs[33];
        hargs[0] = callable;
        for (int i = 0; i < n; i++) {
            hargs[1 + i] = peek(c, n - i);
        }
        r = helper_raising(c, py ? H_prim_call : H_prim, n + 1, hargs, n + 2);
        I(c, r)->imm2 = CNT_call_c + 1;
        if (py) {
            guard_after(c, OP_GUARD_NOT_NULL, r, 1);    /* declined: the C stack is too deep */
        }
        pop_call(c, n);
        push(c, r, 1);
        return 0;
    }
    if (!opaque_call(c)) {
        return -1;
    }
    /* A call of a generator function whose code is known (a function the
       code was compiled for, or one this code made) makes a generator of
       that code. */
    struct lu_ins *made = I(c, callable);
    gfn = NULL;
    gco = self != LU_NOV ? NULL
        : fn != NULL && bound == NULL && PyCode_Check(fn->func_code) && fn->func_version >= 2
          ? (gfn = fn, (PyCodeObject *)fn->func_code)
        : made->op == OP_HELPER && made->imm == H_make_function && I(c, made->args[0])->op == OP_CONST
          ? (PyCodeObject *)(intptr_t)I(c, made->args[0])->imm : NULL;
    if (gco != NULL && (gco->co_flags & (LU_CO_GENERATORS | CO_ITERABLE_COROUTINE | LU_CO_KERNEL)) != CO_GENERATOR) {
        gco = NULL;
    }
    if (gco != NULL && gfn != NULL) {
        guard_func(c, gfn);     /* its code is what the call runs */
    }
    lu_v hargs[34];
    hargs[0] = newfn != NULL ? konst(c, (PyObject *)newfn) : callable;
    hargs[1] = newfn != NULL ? callable : self != LU_NOV ? self : konst(c, NULL);    /* __new__'s class first */
    for (int i = 0; i < n; i++) {
        hargs[2 + i] = peek(c, n - i);
    }
    r = helper_raising(c, H_call, n + 2, hargs, n + 2);
    I(c, r)->imm2 = 1 + (fn != NULL ? CNT_call_refused : kc != NULL ? CNT_call_c
                         : poly_join(c, callable) ? CNT_call_poly : CNT_call_unknown);
    if (_PyRuntime.jit.debug) {
        fprintf(stderr, "jit: call %s at %s@%d\n", lu_count_names[I(c, r)->imm2 - 1],
                PyUnicode_AsUTF8(c->code->co_qualname), c->pc);
    }
    /* The helper declines when the C stack is too deep to recurse: the interpreter makes the call. */
    guard_after(c, OP_GUARD_NOT_NULL, r, 1);
    pop_call(c, n);
    push(c, r, 1);
    if (gco != NULL) {
        note_gen_made(c, r, gco, gfn);
    }
    return 0;
}

/* CALL_KW's operands bound to fn's parameters by name, at compile time:
   pv[0..argcount) the positional parameters' values and then the
   keyword-only ones', each an operand (owned as the stack entry is) or a
   default as a constant. 0 where the binding would need more: keywords
   for **kwargs or none of its names, more positional arguments than
   parameters, a parameter given twice or not at all, or a default of a
   function whose defaults the unit does not guard (fnv). */
static int
bind_keywords(struct ctx *c, PyFunctionObject *fn, lu_v fnv, PyObject *names, int skip, lu_v *pv, uint8_t *pown)
{
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    int n = c->oparg, nkw = (int)PyTuple_GET_SIZE(names), total = co->co_argcount + co->co_kwonlyargcount;
    int self = peek(c, n + 2) != LU_NOV, npos = self + n - nkw;
    if (!PyCode_Check(co) || total > 30 || skip + npos > co->co_argcount || skip > total) {
        return 0;
    }
    for (int i = 0; i < total; i++) {
        pv[i] = LU_NOV;
        pown[i] = 0;
    }
    for (int i = 0; i < npos; i++) {
        int depth = n + 1 - i + self;       /* from self (or the first argument) on */
        pv[skip + i] = peek(c, depth);
        pown[skip + i] = (uint8_t)peek_owned(c, depth);
    }
    for (int k = 0; k < nkw; k++) {
        PyObject *name = PyTuple_GET_ITEM(names, k);
        int j = co->co_posonlyargcount;
        while (j < total && (!PyUnicode_CheckExact(name)
                             || PyUnicode_Compare(PyTuple_GET_ITEM(co->co_localsplusnames, j), name) != 0)) {
            j++;
        }
        if (j == total || pv[j] != LU_NOV) {
            return 0;
        }
        pv[j] = peek(c, nkw - k + 1);
        pown[j] = (uint8_t)peek_owned(c, nkw - k + 1);
    }
    PyObject *defaults = fn->func_defaults, *kwdefaults = fn->func_kwdefaults;
    int first = co->co_argcount - (defaults != NULL ? (int)PyTuple_GET_SIZE(defaults) : 0);
    for (int j = skip; j < total; j++) {
        if (pv[j] != LU_NOV) {
            continue;
        }
        PyObject *d = j < co->co_argcount ? (j >= first ? PyTuple_GET_ITEM(defaults, j - first) : NULL)
            : kwdefaults != NULL && PyDict_CheckExact(kwdefaults)
              ? PyDict_GetItemWithError(kwdefaults, PyTuple_GET_ITEM(co->co_localsplusnames, j)) : NULL;
        if (d == NULL || fnv != LU_NOV) {
            PyErr_Clear();
            return 0;
        }
        pv[j] = konst(c, d);
        pown[j] = 0;
    }
    return 1;
}

/* CALL_KW: callable, self or NULL, the arguments, the keywords' names. A
   Python function the code knows (as CALL knows one, or by the site's
   callees) whose parameters take the keywords by name is CALL of it with
   them in place, inlined, and so is a class whose __init__'s do, as CALL
   of a class makes it (class_call); anything else is made as C makes it
   (h_call_kw, which enters a Python callee's direct entry). 0, or -1 when
   the frame cannot be compiled. */
static int
call_kw(struct ctx *c)
{
    int n = c->oparg, nops = n + 3;
    if (nops > 32) {
        unsupported(c);
        return -1;
    }
    lu_v callable = peek(c, n + 3), self = peek(c, n + 2), w;
    PyObject *names = known(c, peek(c, 1)), *kc = known(c, callable), *bound = NULL;
    PyFunctionObject *fn = kc != NULL ? call_target(c, callable, &bound) : NULL;
    lu_v fnv = LU_NOV;
    if (fn == NULL && c->code_fn != NULL && callable == c->code_value) {
        fn = c->code_fn;
        fnv = callable;
    }
    else if (kc == NULL && (fn = made_function(c, callable)) != NULL) {
        fnv = callable;
    }
    else if (kc == NULL && self == LU_NOV) {
        int built = call_dispatch(c, n);
        if (built != 0) {
            return built < 0 ? -1 : 0;
        }
    }
    lu_v pv[32];
    uint8_t pown[32];
    int named = names != NULL && PyTuple_CheckExact(names) && !lu_jit_nospec(c->code, c->pc);
    PyFunctionObject *init = NULL;
    uint32_t version = 0, meta = 0;
    if (named && kc != NULL && PyType_Check(kc) && self == LU_NOV
        && class_init(c, (PyTypeObject *)kc, n, &init, &version, &meta)
        && init != NULL && bind_keywords(c, init, LU_NOV, names, 1, pv, pown)) {
        PyCodeObject *ico = (PyCodeObject *)init->func_code;
        if (class_call(c, (PyTypeObject *)kc, NULL, init, 0, version, meta, pv + 1, pown + 1, ico->co_argcount - 1,
                       ico->co_kwonlyargcount, nops)) {
            return 0;
        }
    }
    PyFunctionObject *newfn = named && kc != NULL && PyType_Check(kc) && self == LU_NOV && init == NULL
        ? new_call(c, (PyTypeObject *)kc, &version) : NULL;
    int bound_new = newfn != NULL && bind_keywords(c, newfn, LU_NOV, names, 1, pv, pown);
    if (bound_new) {
        /* A class whose call is its __new__'s (new_call), the class first. */
        lu_v g = guard(c, OP_GUARD_VERSION, callable, version);
        I(c, g)->imm2 = GV_TYPE_ITSELF;
        pv[0] = callable;
        pown[0] = 0;
        fn = newfn;
        fnv = LU_NOV;
    }
    if (named && fn != NULL && bound == NULL && (bound_new || bind_keywords(c, fn, fnv, names, 0, pv, pown))) {
        PyCodeObject *co = (PyCodeObject *)fn->func_code;
        int owncallable = peek_owned(c, n + 3), rown;
        lu_v r;
        struct bstate saved;
        state_copy(c, &saved, &c->st);
        c->st.sp -= nops;
        if (inline_call(c, fn, fnv, owncallable, pv, pown, co->co_argcount, co->co_kwonlyargcount, &r, &rown)) {
            if (c->cur >= 0) {
                if (fnv == LU_NOV) {
                    release(c, callable, owncallable);
                }
                push(c, r, rown);
            }
            return 0;
        }
        c->st = saved;
    }
    if (c->failed || !opaque_call(c)) {
        return -1;
    }
    lu_v hargs[32];
    for (int i = 0; i < nops; i++) {
        hargs[i] = peek(c, nops - i) != LU_NOV ? peek(c, nops - i) : konst(c, NULL);
    }
    if (bound_new) {
        hargs[0] = konst(c, (PyObject *)newfn);     /* __new__, the class first */
        hargs[1] = callable;
    }
    w = helper_raising(c, H_call_kw, nops, hargs, nops);
    I(c, w)->imm2 = CNT_call_kw + 1;
    guard_after(c, OP_GUARD_NOT_NULL, w, 1);
    for (int i = 0; i < nops; i++) {
        int own;
        lu_v v = pop(c, &own);
        release(c, v, own);
    }
    push(c, w, 1);
    return 0;
}

/* --- iteration --- */

/* FOR_ITER of an iterator steps_fresh allowed: the next item from its
   fields, and a new iterator with the fields after the step in its stack
   entry, the old one released. A list's length is read each time, so
   that its changes during the loop show. */
static int
for_step(struct ctx *c, lu_v it, PyTypeObject *t, int exit_pc)
{
    int range = t == &PyRangeIter_Type;
    lu_v seq = LU_NOV, idx, step = LU_NOV, len = LU_NOV, done;
    if (range) {
        idx = emit(c, OP_LOAD, REP_I64, it, LU_NOV, LU_NOV, offsetof(rangeiterobject, start));
        step = emit(c, OP_LOAD, REP_I64, it, LU_NOV, LU_NOV, offsetof(rangeiterobject, step));
        len = emit(c, OP_LOAD, REP_I64, it, LU_NOV, LU_NOV, offsetof(rangeiterobject, len));
        done = emit(c, OP_CMP_I, REP_BIT, len, const_i(c, 0), LU_NOV, Py_LE);
    }
    else {
        seq = emit(c, OP_LOAD, REP_OBJ, it, LU_NOV, LU_NOV, offsetof(seqiterobject, it_seq));
        idx = emit(c, OP_LOAD, REP_I64, it, LU_NOV, LU_NOV, offsetof(seqiterobject, it_index));
        lu_v n = emit(c, OP_LOAD, REP_I64, seq, LU_NOV, LU_NOV, offsetof(PyVarObject, ob_size));
        done = emit(c, OP_CMP_I, REP_BIT, idx, n, LU_NOV, Py_GE);
    }
    normalize(c);
    edge_to(c, exit_pc);
    int body = lu_ir_block(c->f);
    lu_ir_edge(c->f, c->cur, body);
    emit(c, OP_BRANCH, REP_NONE, done, LU_NOV, LU_NOV, 0);
    c->cur = body;
    lu_v v, next;
    if (range) {
        v = box_int(c, idx);
        lu_v fields[3] = { emit(c, OP_ADD_I, REP_I64, idx, step, LU_NOV, 0), step,
                           emit(c, OP_SUB_I, REP_I64, len, const_i(c, 1), LU_NOV, 0) };
        I(c, fields[0])->fs = I(c, fields[2])->fs = before(c);     /* neither overflows */
        next = helper(c, H_rangeiter_new, 3, fields);
    }
    else {
        lu_v fields[2] = { seq, emit(c, OP_ADD_I, REP_I64, idx, const_i(c, 1), LU_NOV, 0) };
        I(c, fields[1])->fs = before(c);
        v = emit(c, OP_LIST_ITEM, REP_OBJ, seq, idx, LU_NOV, t == &PyTupleIter_Type);
        I(c, v)->fs = before(c);
        next = helper(c, t == &PyTupleIter_Type ? H_tupleiter_new : H_listiter_new, 2, fields);
    }
    c->st.v[c->nlocals + c->st.sp - 1] = next;
    c->st.own[c->nlocals + c->st.sp - 1] = 1;
    release(c, it, 1);
    push(c, v, 1);
    return 0;
}

static int
for_iter(struct ctx *c)
{
    lu_v it = peek(c, 1);
    int exit_pc = jump_target(FOR_ITER, c->oparg, c->next);
    PyFunctionObject *gfn;
    PyCodeObject *gco = gen_made(c, I(c, it)->op == OP_PHI && I(c, it)->nargs > 0 ? I(c, it)->args[0] : it, &gfn);
    lu_v v = gco != NULL ? inline_resume(c, it, gco, gfn, LU_RETURN_FOR_ITER) : LU_NOV;
    if (v != LU_NOV) {
        lu_v done = emit(c, OP_IS, REP_BIT, v, konst(c, LU_MISSING), LU_NOV, 0);
        normalize(c);
        edge_to(c, exit_pc);
        int body = lu_ir_block(c->f);
        lu_ir_edge(c->f, c->cur, body);
        emit(c, OP_BRANCH, REP_NONE, done, LU_NOV, LU_NOV, 0);
        c->cur = body;
        push(c, v, 1);
        return 0;
    }
    lu_v made = I(c, it)->op == OP_PHI && I(c, it)->nargs > 0 ? I(c, it)->args[0] : it;
    if (lu_iter_type(I(c, made)) != NULL) {
        return for_step(c, it, lu_iter_type(I(c, made)), exit_pc);
    }
    int kind = feedback(c, c->cache);
    if (lu_jit_nospec(c->code, c->pc)) {
        kind = LU_ITER_OTHER;
    }
    /* The iterator's type, or the one its loop entered with. */
    PyTypeObject *t = known_type(c, it);
    if (t == NULL && I(c, it)->op == OP_PHI && I(c, it)->nargs > 0) {
        t = known_type(c, I(c, it)->args[0]);
    }
    int guessed = t == &PyRangeIter_Type ? LU_ITER_RANGE : t == &PyListIter_Type ? LU_ITER_LIST
        : t == &PyTupleIter_Type ? LU_ITER_TUPLE : 0;
    if (kind == 0) {
        kind = guessed;
    }
    int range = known_type(c, it) == &PyRangeIter_Type || kind == LU_ITER_RANGE;
    lu_v done;
    if (range) {
        guard_type(c, it, &PyRangeIter_Type);
        done = emit(c, OP_RITER_DONE, REP_BIT, it, LU_NOV, LU_NOV, 0);
    }
    else if (kind == LU_ITER_LIST || kind == LU_ITER_TUPLE) {
        guard_type(c, it, kind == LU_ITER_LIST ? &PyListIter_Type : &PyTupleIter_Type);
        v = helper(c, H_seqiter_next, 1, &it);
        done = emit(c, OP_IS, REP_BIT, v, konst(c, NULL), LU_NOV, 0);
    }
    else if (root_only(c)) {
        v = helper_raising(c, H_for_iter, 1, &it, 0);
        done = emit(c, OP_IS, REP_BIT, v, konst(c, LU_MISSING), LU_NOV, 0);
    }
    else {
        unsupported(c);
        return -1;
    }
    normalize(c);
    edge_to(c, exit_pc);
    int body = lu_ir_block(c->f);
    lu_ir_edge(c->f, c->cur, body);
    emit(c, OP_BRANCH, REP_NONE, done, LU_NOV, LU_NOV, 0);
    c->cur = body;
    if (range) {
        v = box_int(c, emit(c, OP_RITER_NEXT, REP_I64, it, LU_NOV, LU_NOV, 0));
    }
    push(c, v, 1);
    return 0;
}

/* The local a LOAD_FAST at pc reads, or -1; the next instruction in *next. */
static int
loads_local(struct ctx *c, int pc, int *next)
{
    int base, oparg, op;
    *next = decode(c, pc, &base, &oparg, &op);
    return base == LOAD_FAST || base == LOAD_FAST_BORROW ? oparg : -1;
}

/* f(a, ..., *args, **kwargs), from BUILD_LIST or BUILD_MAP to
   CALL_FUNCTION_EX: the compiler's list of the leading arguments extended
   by a local, made a tuple, and the keywords merged from a local into an
   empty dict (or none), compiled as one call that passes the star tuple's
   items and the dict as they are (h_call_forward). An exception the call
   raises is CALL_FUNCTION_EX's, with the operands that remain on the
   stack consumed; a declined call leaves from the first instruction,
   where nothing has been made. 1 when it did. */
static int
forward_call(struct ctx *c)
{
    int base, oparg, op, pc = c->pc, next, star = -1, kw = -1, lead = 0;
    if (c->parent != NULL || lu_jit_nospec(c->code, c->pc)) {
        return 0;
    }
    if (c->base == BUILD_LIST) {
        lead = c->oparg;
        if (lead > 8 || lead > c->st.sp - 2 || (star = loads_local(c, c->next, &next)) < 0
            || (decode(c, next, &base, &oparg, &op), base != LIST_EXTEND || oparg != 1)
            || (next = decode(c, decode(c, next, &base, &oparg, &op), &base, &oparg, &op),
                base != CALL_INTRINSIC_1 || oparg != INTRINSIC_LIST_TO_TUPLE)) {
            return 0;
        }
        pc = next;
    }
    else {
        pc = c->pc;                     /* the star tuple is on the stack already */
    }
    int after = decode(c, pc, &base, &oparg, &op);
    if (base == BUILD_MAP && oparg == 0) {
        if ((kw = loads_local(c, after, &next)) < 0 || (after = decode(c, next, &base, &oparg, &op),
                                                         base != DICT_MERGE || oparg != 1)) {
            return 0;
        }
    }
    else if (base != PUSH_NULL) {
        return 0;
    }
    int call_pc = after, end = decode(c, after, &base, &oparg, &op);
    if (base != CALL_FUNCTION_EX || (star >= 0 ? c->st.v[star] : peek(c, 1)) == LU_NOV
        || (kw >= 0 && c->st.v[kw] == LU_NOV) || !opaque_call(c)) {
        return 0;
    }
    /* The callable, its NULL (CALL_FUNCTION_EX ignores what is there), the
       leading arguments (or the star tuple), from the stack. */
    int nst = star >= 0 ? lead + 2 : 3;
    lu_v args[12];
    for (int i = 0; i < nst; i++) {
        args[i] = peek(c, nst - i) != LU_NOV && i != 1 ? peek(c, nst - i) : konst(c, NULL);
    }
    int n = nst;
    if (star >= 0) {
        args[n++] = c->st.v[star];
    }
    args[n++] = kw >= 0 ? c->st.v[kw] : konst(c, NULL);
    before(c);
    lu_v w = helper_raising_at(c, H_call_forward, n, args, nst, snapshot(c, call_pc));
    I(c, w)->imm2 = CNT_call_kw + 1;
    guard_after(c, OP_GUARD_NOT_NULL, w, 1);
    for (int i = 0; i < nst; i++) {
        int own;
        lu_v v = pop(c, &own);
        release(c, v, own);
    }
    push(c, w, 1);
    c->next = end;
    return 1;
}

/* --- the instruction --- */

static int
translate(struct ctx *c)
{
    int oparg = c->oparg;
    int own;
    lu_v v, w;
    switch (c->base) {
    case NOP: case NOT_TAKEN:
        return 0;
    case RESUME:
        if (c->parent != NULL && !c->kernel) {
            /* An inlined call polls where the interpreter's would: a tool
               turned on since the compile (sys.settrace) sees the call. */
            lu_v p = emit(c, OP_POLL, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
            I(c, p)->fs = before(c);
        }
        return 0;
    case LOAD_FAST: case LOAD_FAST_BORROW:
        push(c, c->st.v[oparg], 0);
        return 0;
    case LOAD_FAST_CHECK:
        v = c->st.v[oparg];
        if (v == LU_NOV) {
            unsupported(c);
            return -1;
        }
        if (I(c, v)->op == OP_ENTRY_LOCAL || I(c, v)->op == OP_PHI) {
            guard(c, OP_GUARD_NOT_NULL, v, 0);
        }
        push(c, v, 0);
        return 0;
    case LOAD_FAST_LOAD_FAST: case LOAD_FAST_BORROW_LOAD_FAST_BORROW:
        push(c, c->st.v[oparg >> 4], 0);
        push(c, c->st.v[oparg & 15], 0);
        return 0;
    case STORE_FAST:
        v = pop(c, &own);
        store_local(c, oparg, v, own);
        return 0;
    case STORE_FAST_LOAD_FAST:
        v = pop(c, &own);
        store_local(c, oparg >> 4, v, own);
        push(c, c->st.v[oparg & 15], 0);
        return 0;
    case STORE_FAST_STORE_FAST:
        v = pop(c, &own);
        store_local(c, oparg >> 4, v, own);
        v = pop(c, &own);
        store_local(c, oparg & 15, v, own);
        return 0;
    case LOAD_CONST:
        push(c, konst(c, PyTuple_GET_ITEM(c->code->co_consts, oparg)), 0);
        return 0;
    case LOAD_SMALL_INT:
        push(c, konst(c, lu_small_int(oparg)), 0);
        return 0;
    case POP_TOP: case END_FOR: case POP_ITER:
        v = pop(c, &own);
        release(c, v, own);
        return 0;
    case PUSH_NULL:
        push(c, LU_NOV, 0);
        return 0;
    case COPY:
        v = peek(c, oparg);
        incref(c, v);
        push(c, v, 1);
        return 0;
    case SWAP: {
        int a = c->nlocals + c->st.sp - 1, b = c->nlocals + c->st.sp - oparg;
        lu_v t = c->st.v[a];
        uint8_t o = c->st.own[a];
        c->st.v[a] = c->st.v[b];
        c->st.own[a] = c->st.own[b];
        c->st.v[b] = t;
        c->st.own[b] = o;
        return 0;
    }
    case LOAD_GLOBAL:
        return load_global(c);
    case LOAD_ATTR:
        return load_attr(c);
    case STORE_ATTR:
        return store_attr(c);
    case BINARY_OP:
        return binary_op(c);
    case COMPARE_OP:
        return compare_op(c);
    case TO_BOOL:
        v = peek(c, 1);
        own = peek_owned(c, 1);
        w = truth(c, v);
        pop(c, NULL);
        release(c, v, own);
        push(c, bool_of(c, w), 0);
        return 0;
    case UNARY_NOT:
        v = pop(c, &own);
        w = I(c, v)->op == OP_BOOL ? I(c, v)->a : is_of(c, v, konst(c, Py_True));
        release(c, v, own);
        push(c, bool_of(c, not_of(c, w)), 0);
        return 0;
    case UNARY_NEGATIVE: {
        v = peek(c, 1);
        own = peek_owned(c, 1);
        PyTypeObject *t = known_type(c, v);
        if (t == &PyFloat_Type) {
            w = box_float(c, emit(c, OP_NEG_F, REP_F64, unbox_float(c, v), LU_NOV, LU_NOV, 0));
        }
        else if (t == &PyLong_Type || known(c, v) == NULL) {
            lu_v x = emit(c, OP_NEG_I, REP_I64, unbox_int(c, v), LU_NOV, LU_NOV, 0);
            I(c, x)->fs = before(c);
            w = box_int(c, x);
        }
        else {
            unsupported(c);
            return -1;
        }
        pop(c, NULL);
        release(c, v, own);
        push(c, w, 1);
        return 0;
    }
    case IS_OP: {
        int owna, ownb;
        lu_v b = pop(c, &ownb), a = pop(c, &owna);
        w = is_of(c, a, b);
        if (oparg) {
            w = not_of(c, w);
        }
        release(c, a, owna);
        release(c, b, ownb);
        push(c, bool_of(c, w), 0);
        return 0;
    }
    case POP_JUMP_IF_FALSE: case POP_JUMP_IF_TRUE: case POP_JUMP_IF_NONE: case POP_JUMP_IF_NOT_NONE: {
        v = pop(c, &own);
        if (c->base == POP_JUMP_IF_FALSE || c->base == POP_JUMP_IF_TRUE) {
            w = I(c, v)->op == OP_BOOL ? I(c, v)->a : is_of(c, v, konst(c, Py_True));
            if (c->base == POP_JUMP_IF_FALSE) {
                w = not_of(c, w);
            }
        }
        else {
            w = is_of(c, v, konst(c, Py_None));
            if (c->base == POP_JUMP_IF_NOT_NONE) {
                w = not_of(c, w);
            }
        }
        int target = jump_target(c->base, oparg, c->next);
        int seen = Py_SIZE(c->code) <= INLINE_MAX_UNITS || lu_jit_nospec(c->code, c->pc) ? 0
            : __atomic_load_n(&c->cache->cache, __ATOMIC_RELAXED);
        if (I(c, w)->op != OP_CONST_I && (seen == LU_BRANCH_FELL || seen == LU_BRANCH_JUMPED)) {
            /* In code too large to inline whole (a callee or the root: its
               compile costs its size), a direction the interpreter never
               took leaves instead. */
            guard(c, OP_GUARD_BIT, w, seen == LU_BRANCH_JUMPED);
            w = const_i(c, seen == LU_BRANCH_JUMPED);
        }
        release(c, v, own);
        if (I(c, w)->op == OP_CONST_I) {
            /* A branch decided at compile time: the other side is not compiled. */
            jump_to(c, I(c, w)->imm ? target : c->next);
            return 0;
        }
        branch_to(c, w, target, c->next);
        return 0;
    }
    case JUMP_FORWARD:
        jump_to(c, jump_target(c->base, oparg, c->next));
        return 0;
    case JUMP_BACKWARD: {
        lu_v p = emit(c, OP_POLL, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        I(c, p)->fs = before(c);
        c->f->loops++;
        jump_to(c, jump_target(c->base, oparg, c->next));
        return 0;
    }
    case JUMP_BACKWARD_NO_INTERRUPT:
        c->f->loops++;
        jump_to(c, jump_target(c->base, oparg, c->next));
        return 0;
    case GET_ITER: {
        v = peek(c, 1);
        own = peek_owned(c, 1);
        PyTypeObject *t = known_type(c, v);
        if (t == &PyGen_Type) {
            return 0;           /* a generator is its own iterator */
        }
        int nbase, noparg, nop;
        if (t == NULL && c->next < c->ncode && !lu_jit_nospec(c->code, c->pc)
            && (decode(c, c->next, &nbase, &noparg, &nop), nbase == FOR_ITER)) {
            /* The sequence the loop's iterators came from, when it met one kind. */
            int seen = __atomic_load_n(&cache_of(c, c->next)->cache, __ATOMIC_RELAXED);
            t = seen == LU_ITER_TUPLE ? &PyTuple_Type : seen == LU_ITER_LIST ? &PyList_Type : NULL;
            if (t != NULL) {
                guard_type(c, v, t);
            }
        }
        if ((t == &PyTuple_Type || t == &PyList_Type) && steps_fresh(c, c->next)) {
            lu_v fields[2] = { v, const_i(c, 0) };
            w = helper(c, t == &PyTuple_Type ? H_tupleiter_new : H_listiter_new, 2, fields);   /* (for_step) */
        }
        else if (t == &PyTuple_Type || t == &PyList_Type) {
            /* A sequence iterator runs no Python, so an inlined frame makes one too. */
            w = helper_raising(c, H_get_iter_seq, 1, &v, 1);
            set_made(c, w, t == &PyTuple_Type ? &PyTupleIter_Type : &PyListIter_Type);
        }
        else if (root_only(c)) {
            w = helper_raising(c, H_get_iter, 1, &v, 1);
        }
        else {
            unsupported(c);
            return -1;
        }
        pop(c, NULL);
        release(c, v, own);
        push(c, w, 1);
        return 0;
    }
    case FOR_ITER:
        return for_iter(c);
    case CALL:
        return call(c);
    case LOAD_SUPER_ATTR: {
        /* global_super, class, self -- attr, NULL in the method form (a bound method) */
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[5] = { peek(c, 3), peek(c, 2), peek(c, 1),
                         konst(c, PyTuple_GET_ITEM(c->code->co_names, oparg >> 2)), const_i(c, oparg & 2) };
        v = helper_raising(c, H_super_attr, 5, args, 3);
        for (int i = 0; i < 3; i++) {
            w = pop(c, &own);
            release(c, w, own);
        }
        push(c, v, 1);
        if (oparg & 1) {
            push(c, LU_NOV, 0);
        }
        return 0;
    }
    case SEND: {
        /* A generator or coroutine receiver is resumed (any other declines,
           and the interpreter sends to it): what it yields stays on the
           stack for the YIELD_VALUE after; what it returns leaves the
           yield-from loop. Generator code is never inlined. */
        if (!root_only(c) || lu_jit_nospec(c->code, c->pc)) {
            unsupported(c);
            return -1;
        }
        lu_v first = c->f->nins;
        lu_v recv = peek(c, 2);
        v = peek(c, 1);
        own = peek_owned(c, 1);
        lu_v args[2] = { recv, v };
        w = helper_raising(c, H_send, 2, args, 1);
        guard_after(c, OP_GUARD_NOT_NULL, w, 1);
        lu_v done = helper(c, H_gen_done, 1, &recv);
        pop(c, NULL);
        release(c, v, own);
        push(c, w, 1);
        /* Each way on has the frames guard after the call, at its own place. */
        int returned = lu_ir_block(c->f), yielded = lu_ir_block(c->f), next = c->next;
        lu_ir_edge(c->f, c->cur, returned);
        lu_ir_edge(c->f, c->cur, yielded);
        emit(c, OP_BRANCH, REP_NONE, done, LU_NOV, LU_NOV, 0);
        struct bstate saved;
        state_copy(c, &saved, &c->st);
        c->cur = returned;
        c->next = jump_target(SEND, oparg, next);
        frames_guard(c, first);
        jump_to(c, c->next);
        c->next = next;
        c->st = saved;
        c->cur = yielded;
        frames_guard(c, first);
        c->observed = 0;
        return 0;
    }
    case END_SEND: {
        int ownr;
        v = pop(c, &own);
        w = pop(c, &ownr);
        release(c, w, ownr);
        push(c, v, own);
        return 0;
    }
    case GET_AWAITABLE: case GET_YIELD_FROM_ITER: {
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        v = peek(c, 1);
        own = peek_owned(c, 1);
        lu_v args[2] = { v, const_i(c, c->base == GET_AWAITABLE ? oparg
                                       : (c->code->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE)) != 0) };
        w = helper_raising(c, c->base == GET_AWAITABLE ? H_get_awaitable : H_get_yield_from_iter, 2, args, 1);
        pop(c, NULL);
        release(c, v, own);
        push(c, w, 1);
        return 0;
    }
    case GET_AITER: case GET_ANEXT: {
        /* The kernel's: GET_AITER replaces the iterable, GET_ANEXT pushes
           the awaitable above the iterator. */
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        v = peek(c, 1);
        own = peek_owned(c, 1);
        int anext = c->base == GET_ANEXT;
        w = helper_raising(c, anext ? H_get_anext : H_get_aiter, 1, &v, anext ? 0 : 1);
        if (!anext) {
            pop(c, NULL);
            release(c, v, own);
        }
        push(c, w, 1);
        return 0;
    }
    case YIELD_VALUE: {
        /* The generator yields: compiled code leaves its frame at the
           YIELD_VALUE, which the caller runs (the interpreter's loop, or
           lu_jit_resume). A resume unit goes on from the next resume
           point, whose entry reads the stack as it is here, the sent value
           for the yielded one. */
        if (c->gen) {
            return gen_yield(c);
        }
        if (c->parent != NULL) {
            unsupported(c);
            return -1;
        }
        int base, arg, op;
        int at = decode(c, c->next, &base, &arg, &op);
        for (int k = 0; k < c->nentries; k++) {
            if (c->entries[k] == at && c->entry_depth[k] != c->st.sp) {
                c->f->fail = "stack depths differ at a resume point";
                c->failed = 1;
                return -1;
            }
        }
        side_exit(c, EXIT_YIELD);
        return 0;
    }
    case RETURN_VALUE: {
        if (c->parent == NULL) {
            /* A frame object another holds takes the locals when the
               frame stops: the interpreter returns from such a frame. */
            guard(c, OP_GUARD_FRAMES, LU_NOV, 1);
        }
        v = pop(c, &own);
        for (int i = 0; !own && i < c->nlocals; i++) {
            if (c->st.v[i] == v && c->st.own[i]) {
                c->st.own[i] = 0;       /* a local's value: its reference moves */
                own = 1;
            }
        }
        v = take(c, v, own);
        if (c->parent == NULL && !(c->code->co_flags & LU_CO_GENERATORS)) {
            /* GH-99729: a finalizer the releases run sees the caller on top. */
            emit(c, OP_UNLINK, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        }
        while (c->st.sp > 0) {
            w = pop(c, &own);
            release(c, w, own);
        }
        for (int i = 0; i < c->nlocals; i++) {
            release(c, c->st.v[i], c->st.own[i]);
        }
        if (c->parent == NULL) {
            emit(c, OP_RETURN, REP_NONE, v, LU_NOV, LU_NOV, 0);
        }
        else {
            if (c->gen) {
                /* A generator resumed in line returned: the iteration ends. */
                release(c, v, 1);
                int observed = c->observed;
                helper(c, H_gen_finish, 1, &c->genv);
                c->observed = observed;     /* control leaves the frame: no frames guard after */
                v = konst(c, LU_MISSING);
            }
            if (c->nrets == c->caprets) {
                c->caprets = c->caprets ? c->caprets * 2 : 4;
                c->rets = realloc(c->rets, sizeof(lu_v) * (size_t)c->caprets);
            }
            c->rets[c->nrets++] = v;
            lu_ir_edge(c->f, c->cur, c->cont);
            emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        }
        c->cur = -1;
        return 0;
    }
    case STORE_SUBSCR: {
        lu_v sub = peek(c, 1), cont = peek(c, 2), val = peek(c, 3);
        int ownsub = peek_owned(c, 1), owncont = peek_owned(c, 2), ownval = peek_owned(c, 3);
        int kinds = feedback(c, c->cache);
        if (!lu_jit_nospec(c->code, c->pc) && (kinds & 0xff) == LU_KIND_LIST && (kinds >> 8) == LU_KIND_INT) {
            guard_type(c, cont, &PyList_Type);
            lu_v i = unbox_int(c, sub);
            lu_v ok = emit(c, OP_LIST_STORE, REP_NONE, cont, i, val, 0);
            I(c, ok)->fs = before(c);
        }
        else if (root_only(c)) {
            lu_v args[3] = { cont, sub, val };
            helper_raising(c, H_setitem, 3, args, 3);
        }
        else {
            unsupported(c);
            return -1;
        }
        pop(c, NULL); pop(c, NULL); pop(c, NULL);
        release(c, sub, ownsub);
        release(c, cont, owncont);
        release(c, val, ownval);
        return 0;
    }
    case DELETE_SUBSCR: {
        lu_v sub = peek(c, 1), cont = peek(c, 2);
        int ownsub = peek_owned(c, 1), owncont = peek_owned(c, 2);
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[2] = { cont, sub };
        helper_raising(c, H_delitem, 2, args, 2);
        pop(c, NULL); pop(c, NULL);
        release(c, sub, ownsub);
        release(c, cont, owncont);
        return 0;
    }
    case LIST_EXTEND: {
        /* An exact tuple or list source runs no Python, so an inlined frame
           extends from one too. */
        lu_v it = peek(c, 1), list = peek(c, oparg + 1);
        int ownit = peek_owned(c, 1);
        PyTypeObject *t = known_type(c, it);
        int kind = t == NULL && !lu_jit_nospec(c->code, c->pc) ? guess_kind(c, it) : 0;
        if (kind == LU_KIND_TUPLE || kind == LU_KIND_LIST) {
            /* What the loop entered with, as every input so far is. */
            t = kind == LU_KIND_TUPLE ? &PyTuple_Type : &PyList_Type;
            guard_type(c, it, t);
        }
        int seq = t == &PyTuple_Type || t == &PyList_Type;
        if (!seq && !root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[2] = { list, it };
        helper_raising(c, seq ? H_list_extend_seq : H_list_extend, 2, args, 1);
        pop(c, NULL);
        release(c, it, ownit);
        return 0;
    }
    case COPY_FREE_VARS: {
        PyObject *closure = c->func->func_closure;
        if (c->cells) {
            return 0;           /* the caller copied the function's */
        }
        if (c->parent == NULL || closure == NULL) {
            unsupported(c);
            return -1;
        }
        int off = c->nlocals - oparg;
        for (int i = 0; i < oparg; i++) {
            c->st.v[off + i] = konst(c, PyTuple_GET_ITEM(closure, i));
            c->st.own[off + i] = 0;
        }
        return 0;
    }
    case LOAD_DEREF: {
        lu_v cell = c->st.v[oparg];
        PyObject *kcell = known(c, cell);
        PyObject *val = kcell != NULL && PyCell_Check(kcell) ? lu_cell_get(kcell) : NULL;
        if (val != NULL) {
            /* A closure's cell of an inlined function: its value, guarded. */
            lu_v g = guard(c, OP_GUARD_IS, cell, (int64_t)(intptr_t)val);
            I(c, g)->imm2 = GI_FIELD | offsetof(PyCellObject, ob_ref);
            push(c, konst(c, val), 0);
            Py_DECREF(val);         /* the unit holds its constants */
            return 0;
        }
        if (cell == LU_NOV) {
            unsupported(c);
            return -1;
        }
        v = helper(c, H_cell_load, 1, &cell);
        guard_after(c, OP_GUARD_NOT_NULL, v, 0);
        push(c, v, 1);
        return 0;
    }
    case BUILD_LIST: case BUILD_TUPLE: case BUILD_SET: case BUILD_MAP: {
        if ((c->base == BUILD_LIST || (c->base == BUILD_MAP && oparg == 0)) && forward_call(c)) {
            return c->failed ? -1 : 0;
        }
        /* A set's or a dict's items may run Python (hashing): the root only. */
        int set = c->base == BUILD_SET || c->base == BUILD_MAP;
        oparg = c->base == BUILD_MAP ? 2 * oparg : oparg;
        if (oparg > 30 || (set && !root_only(c))) {
            unsupported(c);
            return -1;
        }
        lu_v args[32] = { 0 };     /* none for an empty list */
        uint8_t owns[32];
        for (int i = oparg - 1; i >= 0; i--) {
            args[i] = pop(c, &own);
            owns[i] = (uint8_t)own;
        }
        c->st.sp += oparg;
        static const int helpers[] = { [BUILD_LIST] = H_build_list, [BUILD_TUPLE] = H_build_tuple,
                                       [BUILD_SET] = H_build_set, [BUILD_MAP] = H_build_map };
        v = helper_raising(c, helpers[c->base], oparg, args, oparg);
        if (!set) {
            set_made(c, v, c->base == BUILD_LIST ? &PyList_Type : &PyTuple_Type);
        }
        c->st.sp -= oparg;
        for (int i = 0; i < oparg; i++) {
            release(c, args[i], owns[i]);
        }
        push(c, v, 1);
        return 0;
    }
    case UNPACK_SEQUENCE: {
        /* An exact tuple or list (the kind the interpreter met, else a
           tuple) of oparg items, its items pushed last first; any other
           sequence leaves, and after enough of that the kernel unpacks
           it into a tuple (at the root: it runs Python). A list's items
           are read under its lock, as a tuple. */
        v = peek(c, 1);
        own = peek_owned(c, 1);
        int generic = oparg > 30 || lu_jit_nospec(c->code, c->pc);
        if (generic && !root_only(c)) {
            unsupported(c);
            return -1;
        }
        PyTypeObject *t = known_type(c, v);
        int list = !generic
            && (t == &PyList_Type || (t != &PyTuple_Type && (feedback(c, c->cache) & 0xff) == LU_KIND_LIST));
        lu_v items = v;
        if (generic) {
            lu_v args[2] = { v, const_i(c, oparg) };
            items = helper_raising(c, H_unpack, 2, args, 1);
        }
        else if (list) {
            guard_type(c, v, &PyList_Type);
            lu_v args[2] = { v, const_i(c, oparg) };
            items = helper_raising(c, H_list_to_tuple, 2, args, 0);
            guard_after(c, OP_GUARD_NOT_NULL, items, 1);
        }
        else {
            guard_type(c, v, &PyTuple_Type);
            lu_v size = emit(c, OP_LOAD, REP_I64, v, LU_NOV, LU_NOV, offsetof(PyVarObject, ob_size));
            guard(c, OP_GUARD_BIT, emit(c, OP_CMP_I, REP_BIT, size, const_i(c, oparg), LU_NOV, Py_EQ), 1);
        }
        pop(c, NULL);
        for (int i = oparg - 1; i >= 0; i--) {
            lu_v item = emit(c, OP_LOAD, REP_OBJ, items, LU_NOV, LU_NOV,
                             (int64_t)(offsetof(PyTupleObject, ob_item) + sizeof(PyObject *) * (size_t)i));
            incref(c, item);
            push(c, item, 1);
        }
        release(c, items, list || generic);
        release(c, v, own);
        return 0;
    }
    case MAKE_FUNCTION: {
        lu_v code = peek(c, 1);
        own = peek_owned(c, 1);
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[2] = { code, konst(c, c->globals) };
        v = helper_raising(c, H_make_function, 2, args, 1);
        pop(c, NULL);
        release(c, code, own);
        push(c, v, 1);
        return 0;
    }
    case SET_FUNCTION_ATTRIBUTE: {
        lu_v func = peek(c, 1), attr = peek(c, 2);
        int ownfunc = peek_owned(c, 1);
        own = peek_owned(c, 2);
        lu_v args[3] = { func, attr, const_i(c, oparg) };
        helper(c, H_set_function_attribute, 3, args);
        pop(c, NULL);
        pop(c, NULL);
        release(c, attr, own);
        push(c, func, ownfunc);
        return 0;
    }
    case STORE_DEREF: {
        lu_v cell = c->st.v[oparg];
        if (cell == LU_NOV) {
            unsupported(c);
            return -1;
        }
        v = peek(c, 1);
        own = peek_owned(c, 1);
        lu_v args[2] = { cell, v };
        helper(c, H_cell_store, 2, args);
        pop(c, NULL);
        release(c, v, own);
        return 0;
    }
    case LOAD_FAST_AND_CLEAR:
        /* A comprehension's variable, kept on the stack while it runs. */
        push(c, c->st.v[oparg], 0);
        store_local(c, oparg, LU_NOV, 0);
        return 0;
    case LIST_APPEND: case SET_ADD: {
        /* The comprehension's own list, which no Python sees until it is
           done; a set's items are hashed, which may run Python. */
        v = peek(c, 1);
        own = peek_owned(c, 1);
        if (c->base == SET_ADD && !root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[2] = { peek(c, oparg + 1), v };
        helper_raising(c, c->base == LIST_APPEND ? H_list_push : H_set_add, 2, args, 1);
        pop(c, NULL);
        release(c, v, own);
        return 0;
    }
    case MAP_ADD: {
        lu_v val = peek(c, 1), key = peek(c, 2);
        int ownval = peek_owned(c, 1), ownkey = peek_owned(c, 2);
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[3] = { peek(c, oparg + 2), key, val };
        helper_raising(c, H_setitem, 3, args, 2);
        pop(c, NULL);
        pop(c, NULL);
        release(c, key, ownkey);
        release(c, val, ownval);
        return 0;
    }
    case CALL_INTRINSIC_1: {
        v = peek(c, 1);
        own = peek_owned(c, 1);
        if (oparg == INTRINSIC_ASYNC_GEN_WRAP) {
            w = helper_raising(c, H_async_gen_wrap, 1, &v, 0);
            pop(c, NULL);
            release(c, v, own);
            push(c, w, 1);
            return 0;
        }
        if (oparg != INTRINSIC_LIST_TO_TUPLE) {
            unsupported(c);
            return -1;
        }
        lu_v args[2] = { v, const_i(c, -1) };
        w = helper_raising(c, H_list_to_tuple, 2, args, 0);
        pop(c, NULL);
        release(c, v, own);
        push(c, w, 1);
        set_made(c, w, &PyTuple_Type);
        return 0;
    }
    case STORE_GLOBAL: {
        if (!PyDict_CheckExact(c->globals)) {
            unsupported(c);
            return -1;
        }
        v = peek(c, 1);
        own = peek_owned(c, 1);
        lu_v args[3] = { konst(c, c->globals), konst(c, name_at(c, oparg)), v };
        helper_raising(c, H_store_global, 3, args, 1);
        pop(c, NULL);
        release(c, v, own);
        return 0;
    }
    case BUILD_SLICE: {
        int n = oparg == 3 ? 3 : 2;
        lu_v args[3] = { peek(c, n), peek(c, n - 1), n == 3 ? peek(c, 1) : const_i(c, 0) };
        w = helper_raising(c, H_build_slice, 3, args, n);
        for (int i = 0; i < n; i++) {
            v = pop(c, &own);
            release(c, v, own);
        }
        push(c, w, 1);
        set_made(c, w, &PySlice_Type);
        return 0;
    }
    case CONVERT_VALUE: case FORMAT_SIMPLE: case FORMAT_WITH_SPEC: {
        /* They run the kernel's str, repr, ascii or format: the root only. */
        int n = c->base == FORMAT_WITH_SPEC ? 2 : 1;
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[2] = { peek(c, n), c->base == CONVERT_VALUE ? const_i(c, oparg)
                         : n == 2 ? peek(c, 1) : konst(c, (PyObject *)&_PyRuntime.singletons.empty_str) };
        w = helper_raising(c, c->base == CONVERT_VALUE ? H_convert_value : H_format, 2, args, n);
        for (int i = 0; i < n; i++) {
            v = pop(c, &own);
            release(c, v, own);
        }
        push(c, w, 1);
        return 0;
    }
    case BUILD_STRING: {
        if (oparg > 30) {
            unsupported(c);
            return -1;
        }
        lu_v args[32] = { 0 };
        for (int i = 0; i < oparg; i++) {
            args[i] = peek(c, oparg - i);
        }
        w = helper_raising(c, H_build_string, oparg, args, oparg);
        for (int i = 0; i < oparg; i++) {
            v = pop(c, &own);
            release(c, v, own);
        }
        push(c, w, 1);
        set_made(c, w, &PyUnicode_Type);
        return 0;
    }
    case BINARY_SLICE: {
        /* A str, bytes, list or tuple sliced by ints runs no Python, so an
           inlined frame slices one; anything else leaves it. Bounds the
           code computed go as machine ints, unboxed (luc-14n0). */
        lu_v stop = peek(c, 1), start = peek(c, 2), cont = peek(c, 3);
        int ownstop = peek_owned(c, 1), ownstart = peek_owned(c, 2), owncont = peek_owned(c, 3);
        lu_v rstart, rstop;
        int none = 0;
        if ((I(c, start)->op == OP_BOX_INT || I(c, stop)->op == OP_BOX_INT)
            && raw_bound(c, start, &rstart, &none, 1) && raw_bound(c, stop, &rstop, &none, 2)) {
            lu_v args[4] = { cont, rstart, rstop, const_i(c, none | (root_only(c) ? 4 : 0)) };
            w = helper_raising(c, root_only(c) ? H_binary_slice_raw : H_slice_native_raw, 4, args, 3);
        }
        else {
            lu_v args[4] = { cont, start, stop, const_i(c, root_only(c)) };
            w = helper_raising(c, root_only(c) ? H_binary_slice : H_slice_native, 4, args, 3);
        }
        if (!root_only(c)) {
            guard_after(c, OP_GUARD_NOT_NULL, w, 1);
        }
        pop(c, NULL); pop(c, NULL); pop(c, NULL);
        release(c, stop, ownstop);
        release(c, start, ownstart);
        release(c, cont, owncont);
        push(c, w, 1);
        return 0;
    }
    case STORE_SLICE: {
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[4] = { peek(c, 4), peek(c, 3), peek(c, 2), peek(c, 1) };
        helper_raising(c, H_store_slice, 4, args, 4);
        for (int i = 0; i < 4; i++) {
            v = pop(c, &own);
            release(c, v, own);
        }
        return 0;
    }
    case LOAD_COMMON_CONSTANT:
        return load_common_constant(c);
    case CALL_KW:
        return call_kw(c);
    case CALL_FUNCTION_EX: {
        /* A call with star arguments, made as C makes one (it records no
           feedback: the root's only). */
        int nops = 4;
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        if (!opaque_call(c)) {
            return -1;
        }
        lu_v hargs[4];
        for (int i = 0; i < nops; i++) {
            hargs[i] = peek(c, nops - i) != LU_NOV ? peek(c, nops - i) : konst(c, NULL);
        }
        lu_v args[3] = { hargs[0], hargs[2], hargs[3] };     /* the callable, *args, **kwargs */
        w = helper_raising(c, H_call_ex, 3, args, nops);
        I(c, w)->imm2 = CNT_call_kw + 1;
        guard_after(c, OP_GUARD_NOT_NULL, w, 1);
        for (int i = 0; i < nops; i++) {
            v = pop(c, &own);
            release(c, v, own);
        }
        push(c, w, 1);
        return 0;
    }
    case DICT_MERGE: {
        v = peek(c, 1);
        own = peek_owned(c, 1);
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[3] = { peek(c, oparg + 4), peek(c, oparg + 1), v };
        helper_raising(c, H_dict_merge, 3, args, 1);
        pop(c, NULL);
        release(c, v, own);
        return 0;
    }
    case DICT_UPDATE: {
        v = peek(c, 1);
        own = peek_owned(c, 1);
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[2] = { peek(c, oparg + 1), v };
        helper_raising(c, H_dict_update, 2, args, 1);
        pop(c, NULL);
        release(c, v, own);
        return 0;
    }
    case CONTAINS_OP: {
        int owna, ownb;
        lu_v b = peek(c, 1), a = peek(c, 2);
        owna = peek_owned(c, 2);
        ownb = peek_owned(c, 1);
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        lu_v args[3] = { a, b, const_i(c, oparg) };
        v = helper_raising(c, H_contains, 3, args, 2);
        pop(c, NULL);
        pop(c, NULL);
        release(c, a, owna);
        release(c, b, ownb);
        push(c, v, 1);
        return 0;
    }
    case PUSH_EXC_INFO:
        /* exc -- prev_exc, exc */
        v = pop(c, &own);
        push(c, helper(c, H_push_exc_info, 1, &v), 1);
        push(c, v, own);
        return 0;
    case POP_EXCEPT:
        v = pop(c, &own);
        helper(c, H_pop_except, 1, &v);
        release(c, v, own);
        return 0;
    case CHECK_EXC_MATCH: {
        /* exc, type -- exc, bool */
        lu_v args[2] = { peek(c, 2), peek(c, 1) };
        own = peek_owned(c, 1);
        v = helper_raising(c, H_exc_match, 2, args, 1);
        pop(c, NULL);
        release(c, args[1], own);
        push(c, v, 0);
        return 0;
    }
    case RAISE_VARARGS:
    case RERAISE: {
        /* raise exc [from cause]. A bare raise and RERAISE raise again
           without a traceback entry here, as kernel code makes none: only
           there (the lasti RERAISE restores is seen only by a traceback or
           a frame object). An inlined frame raises only to a handler of the
           unit: else the side exit costs the same, and its boxes stay
           virtual. */
        struct ctx *k;
        int handler, level, lasti, at;
        int again = c->base == RERAISE || oparg == 0;
        if ((again && !hidden(c->code))
            || (c->parent != NULL && !catcher(c, before(c), &k, &handler, &level, &lasti, &at))) {
            unsupported(c);
            return -1;
        }
        if (again) {
            lu_v exc = c->base == RERAISE ? peek(c, 1) : const_i(c, 0);
            helper_raising(c, H_reraise, 1, &exc, c->base == RERAISE);
        }
        else {
            lu_v args[2] = { peek(c, oparg), oparg == 2 ? peek(c, 1) : konst(c, LU_MISSING) };
            helper_raising(c, H_raise, 2, args, oparg);
        }
        side_exit(c, EXIT_SIDE);        /* not reached: the helper always raises */
        return 0;
    }
    case LOAD_SPECIAL: {
        /* owner -- attr, NULL: the special method looked up on owner's
           type, bound by the kernel (which raises the interpreter's error
           for a type without it). */
        v = peek(c, 1);
        own = peek_owned(c, 1);
        lu_v args[2] = { v, const_i(c, oparg) };
        lu_v r = helper_raising(c, H_load_special, 2, args, 1);
        pop(c, NULL);
        release(c, v, own);
        push(c, r, 1);
        push(c, LU_NOV, 0);
        return 0;
    }
    case DELETE_FAST:
        /* A local known bound: an unbound one raises, in the interpreter. */
        v = c->st.v[oparg];
        if (v == LU_NOV) {
            unsupported(c);
            return -1;
        }
        if (I(c, v)->op == OP_ENTRY_LOCAL || I(c, v)->op == OP_PHI) {
            guard(c, OP_GUARD_NOT_NULL, v, 0);
        }
        store_local(c, oparg, LU_NOV, 0);
        return 0;
    default:
        unsupported(c);
        return -1;
    }
}

/* Builds the instruction at pc from the current state; the next one's pc.
   A join's first instruction records its state for guards moved before
   it (entry). */
static int
step(struct ctx *c, int pc, struct lu_block *entry)
{
    c->pc = pc;
    c->next = decode(c, pc, &c->base, &c->oparg, &c->opcode);
    c->cache = cache_of(c, pc);
    c->snap = NULL;
    before(c);
    if (entry != NULL) {
        entry->entry_fs = c->snap;
    }
    if (!supported(c->base)) {
        unsupported(c);
        return c->next;
    }
    c->observed = 0;
    lu_v first = c->f->nins;
    translate(c);
    if (c->observed && c->cur >= 0 && !c->failed) {
        frames_guard(c, first);
    }
    c->observed = 0;
    c->units += c->next - pc;
    if (c->parent != NULL && c->units > INLINE_MAX_UNITS) {
        c->failed = 1;      /* too large to inline: it is called instead */
        c->why = "too large";
    }
    return c->next;
}

/* Whether block k sends no more edges: it is built, or nothing reached
   it and nothing that could still reach it is left. memo: 0 unknown, 1
   settled, 2 not (or being decided, which a cycle leaves unsettled). */
static int
settled(struct ctx *c, int k, char *memo)
{
    if (memo[k] == 0) {
        memo[k] = 2;
        int ok = c->bb[k].done || (!c->bb[k].has && !c->bb[k].exc);      /* a handler may still be reached */
        for (int j = 0; ok && !c->bb[k].done && j < c->nbb; j++) {
            for (int i = 0; ok && i < c->bb[j].nsucc; i++) {
                ok = c->bb[j].succ[i] != k || settled(c, j, memo);
            }
        }
        memo[k] = ok ? 1 : 2;
    }
    return memo[k] == 1;
}

/* Whether block bi, about to be built, has every edge it will get. */
static int
preds_built(struct ctx *c, int bi)
{
    char *memo = calloc((size_t)c->nbb, 1);
    memo[bi] = 2;               /* its own edges are still to come */
    int ok = 1;
    for (int k = 0; ok && k < c->nbb; k++) {
        for (int i = 0; ok && i < c->bb[k].nsucc; i++) {
            ok = c->bb[k].succ[i] != bi || settled(c, k, memo);
        }
    }
    free(memo);
    return ok;
}

static int
build_block(struct ctx *c, int bi)
{
    struct bblk *b = &c->bb[bi];
    b->done = 1;
    c->cur = b->id;
    state_copy(c, &c->st, &b->in);
    if (b->phis && !b->exc && preds_built(c, bi)) {
        b->folded = 1;
        /* A join with every edge it will get: if they agree, it has that
           value, and a constant stays one for what folds on it. Each edge
           makes its own constant, so constants agree by object; a NULL
           entry (a call's beside its callable, an unbound local) stays
           one, which a call reads as "no self". */
        for (int k = 0; k < c->nlocals + c->st.sp; k++) {
            struct lu_ins *phi = I(c, c->st.v[k]);
            int same = phi->op == OP_PHI && phi->nargs > 0;
            lu_v first = same ? phi->args[0] : LU_NOV;
            int is_const = same && I(c, first)->op == OP_CONST;
            for (int i = 1; same && i < phi->nargs; i++) {
                lu_v in = phi->args[i];
                same = in == first || (is_const && I(c, in)->op == OP_CONST && I(c, in)->imm == I(c, first)->imm);
            }
            if (same) {
                c->st.v[k] = is_const && I(c, first)->imm == 0 ? LU_NOV : first;
            }
        }
    }
    int pc = b->pc;
    while (c->cur >= 0 && !c->failed) {
        if (pc >= c->ncode) {
            c->f->fail = "fell off the code";
            return -1;
        }
        if (pc != b->pc && c->blkat[pc] >= 0) {
            jump_to(c, pc);
            break;
        }
        pc = step(c, pc, pc == b->pc && b->phis && !b->exc ? &c->f->blocks[b->id] : NULL);
    }
    return c->failed ? -1 : 0;
}

static int
build_ctx(struct ctx *c, int entry_block)
{
    (void)entry_block;
    for (;;) {
        int pick = -1;
        for (int k = 0; k < c->nbb; k++) {
            if (c->bb[k].has && !c->bb[k].done) {
                pick = k;
                break;
            }
        }
        if (pick < 0) {
            return 0;
        }
        if (build_block(c, pick) < 0) {
            return -1;
        }
    }
}

/* A generator's resume points, the code units after each RESUME, with the
   stack depth there (the depth at the yield before it, whose value the
   sent one replaces), as CPython's compiler computes depths: a walk from
   the code's start over fall-throughs, jumps and handlers. Into
   c->entries and c->entry_depth; -1 when the walk cannot follow the code
   or misses one. */
static int
resume_points(struct ctx *c)
{
    int n = c->ncode, ok = 1, nw = 0;
    int *depth = malloc(sizeof(int) * ((size_t)n + 1)), *work = malloc(sizeof(int) * ((size_t)n + 1));
    for (int pc = 0; pc <= n; pc++) {
        depth[pc] = -1;
    }
#define REACH(t, d) do { \
        int t_ = (t), d_ = (d); \
        if (t_ < 0 || t_ >= n || d_ < 0 || d_ > c->code->co_stacksize) { ok = 0; } \
        else if (depth[t_] < 0) { depth[t_] = d_; work[nw++] = t_; } \
        else if (depth[t_] != d_) { ok = 0; } \
    } while (0)
    REACH(0, 0);
    while (nw > 0 && ok) {
        int pc = work[--nw], d = depth[pc];
        int base, oparg, op;
        int next = decode(c, pc, &base, &oparg, &op);
        int level, handler, lasti;
        if (lu_exception_handler(c->code, pc, &level, &handler, &lasti) && handler >= 0) {
            REACH(handler, level + lasti + 1);
        }
        if (base == FOR_ITER) {
            REACH(jump_target(base, oparg, next), d);   /* past END_FOR, nothing pushed */
            REACH(next, d + 1);
            continue;
        }
        int fall = PyCompile_OpcodeStackEffectWithJump(base, oparg, 0);
        int jump = PyCompile_OpcodeStackEffectWithJump(base, oparg, 1);
        if (fall == PY_INVALID_STACK_EFFECT || jump == PY_INVALID_STACK_EFFECT) {
            ok = 0;
            break;
        }
        if (is_jump(base) || is_branch(base)) {
            REACH(jump_target(base, oparg, next), d + jump);
        }
        if (!is_jump(base) && base != RETURN_VALUE && base != RAISE_VARARGS && base != RERAISE) {
            REACH(next, d + fall);
        }
    }
#undef REACH
    int count = 0;
    for (int pc = 0; ok && pc < n; pc = lu_jit_next_pc(c->code, pc)) {
        if (lu_base_opcode(c->code, pc) == RESUME) {
            int at = lu_jit_next_pc(c->code, pc);
            ok = at < n && depth[at] >= 0;
            count++;
        }
    }
    if (ok) {
        c->entries = lu_ir_alloc(c->f, sizeof(int) * (size_t)(count ? count : 1));
        c->entry_depth = lu_ir_alloc(c->f, sizeof(int) * (size_t)(count ? count : 1));
        for (int pc = 0; pc < n; pc = lu_jit_next_pc(c->code, pc)) {
            if (lu_base_opcode(c->code, pc) == RESUME) {
                int at = lu_jit_next_pc(c->code, pc);
                c->entries[c->nentries] = at;
                c->entry_depth[c->nentries++] = depth[at];
            }
        }
    }
    free(depth);
    free(work);
    return ok && count > 0 ? 0 : -1;
}

/* A resume unit's entry: the frame's instruction picks the resume point,
   whose state is every local and the stack its depth there, each read
   from the frame as an entry reads it. */
static int
build_resume(struct ctx *c)
{
    struct lu_func *f = c->f;
    for (int k = 0; k < c->nentries; k++) {
        c->bb[c->blkat[c->entries[k]]].npreds++;
    }
    c->cur = lu_ir_block(f);
    c->pc = f->entry_pc;
    lu_v at = emit(c, OP_ENTRY_PC, REP_I64, LU_NOV, LU_NOV, LU_NOV, 0);
    lu_v *room = lu_ir_alloc(f, sizeof(lu_v) * (size_t)c->nentries);
    f->room = INT32_MIN;
    for (int k = 0; k < c->nentries; k++) {
        int other = -1;
        if (k < c->nentries - 1) {
            lu_v bit = emit(c, OP_CMP_I, REP_BIT, at, const_i(c, c->entries[k]), LU_NOV, Py_EQ);
            int here = lu_ir_block(f);
            other = lu_ir_block(f);
            lu_ir_edge(f, c->cur, here);
            lu_ir_edge(f, c->cur, other);
            emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
            c->cur = here;
        }
        c->pc = c->entries[k];
        state_init(c, &c->st);
        for (int i = 0; i < c->nlocals; i++) {
            c->st.v[i] = emit(c, OP_ENTRY_LOCAL, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, i);
            c->st.own[i] = i >= f->nborrowed;
        }
        for (int j = 0; j < c->entry_depth[k]; j++) {
            push(c, emit(c, OP_ENTRY_STACK, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, j), 1);
        }
        c->snap = snapshot(c, c->pc);
        room[k] = guard(c, OP_GUARD_DEPTH, LU_NOV, 0);
        edge_to(c, c->pc);
        emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        c->cur = other;
    }
    int r = build_ctx(c, c->blkat[c->entries[0]]);
    for (int k = 0; k < c->nentries; k++) {
        f->ins[room[k]].flags |= f->room == INT32_MIN ? INS_DEAD : 0;
        f->ins[room[k]].imm = f->room;
    }
    free(c->rets);
    free(c->pend);
    if (r < 0 && f->fail == NULL) {
        f->fail = "build failed";
    }
    return r;
}

int
lu_ssa_build(struct lu_func *f, struct lu_frame *frame)
{
    PyCodeObject *co = frame->code;
    struct ctx root = { 0 };
    struct ctx *c = &root;
    c->f = f;
    c->live = frame;
    c->code = co;
    c->func = (PyFunctionObject *)frame->func;
    c->globals = frame->globals;
    c->builtins = frame->builtins;
    c->kernel = (co->co_flags & LU_CO_KERNEL) != 0;
    c->ncode = (int)Py_SIZE(co);
    c->nlocals = co->co_nlocalsplus;
    c->nvars = c->nlocals + co->co_stacksize + 1;
    /* The leading parameters the code never rebinds are borrowed: from
       the slots, which keep them for the frame (its stackpointer covers
       them), or from a direct caller (runtime/jit/x86_64/emit.c), which
       gives them all: a default is not borrowed. */
    c->fixed = fixed_params(c);
    PyObject *defaults = c->func->func_defaults;
    int required = co->co_argcount - (defaults != NULL ? (int)PyTuple_GET_SIZE(defaults) : 0);
    while (f->nborrowed < required && c->fixed[f->nborrowed]) {
        f->nborrowed++;
    }
    for (int i = f->nborrowed; i < c->nlocals; i++) {
        c->fixed[i] = 0;
    }
    int entry = f->entry_pc;
    if (f->resume && resume_points(c) < 0) {
        f->fail = "unfollowable generator";
        return -1;
    }
    if (scan(c, entry) < 0) {
        f->fail = "unfollowable jumps";
        return -1;
    }
    count_fallthrough(c);
    if (f->resume) {
        return build_resume(c);
    }
    /* At a call's entry every local but the parameters (and cells, which
       the prologue made) is unbound, unless a jump comes there too: then
       the unit is a loop's entry as well, and reads them all. */
    int params = co->co_argcount + co->co_kwonlyargcount + ((co->co_flags & CO_VARARGS) != 0)
        + ((co->co_flags & CO_VARKEYWORDS) != 0);
    int call = entry == lu_jit_call_entry(co) && c->bb[c->blkat[entry]].npreds == 0;
    c->bb[c->blkat[entry]].npreds++;
    c->cur = lu_ir_block(f);
    state_init(c, &c->st);
    c->pc = entry;
    for (int i = 0; i < c->nlocals; i++) {
        int plain = co->co_localspluskinds == NULL
            || !(((unsigned char *)PyBytes_AS_STRING(co->co_localspluskinds))[i] & (CO_FAST_CELL | CO_FAST_FREE));
        c->st.v[i] = call && i >= params && plain ? LU_NOV
            : emit(c, OP_ENTRY_LOCAL, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, i);
        c->st.own[i] = i >= f->nborrowed;       /* the slot's reference is compiled code's from entry */
    }
    int depth = (int)(frame->stackpointer - (frame->localsplus + c->nlocals));
    for (int j = 0; j < depth; j++) {
        push(c, emit(c, OP_ENTRY_STACK, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, j), 1);
    }
    /* Locals the live frame holds as ints or floats enter unboxed, so that
       loop-carried values stay raw (a guard at entry). */
    c->snap = snapshot(c, entry);
    /* The interpreter would push each inlined frame at its call, and raise
       there past the recursion limit. The budget is the same wherever the
       unit runs (its calls return before it goes on), so one check at entry
       that there is room for the deepest leaves the unit to the interpreter
       when there is not, and a deopt or a call never lacks it. */
    f->room = INT32_MIN;
    lu_v room = guard(c, OP_GUARD_DEPTH, LU_NOV, 0);
    if (!lu_jit_nospec(co, entry)) {
        /* Locals that hold one object enter as one value, so `x is y` stays true. */
        for (int i = 0; i < c->nlocals; i++) {
            PyObject *o = frame->localsplus[i];
            if (o == NULL || !(PyLong_CheckExact(o) || PyFloat_CheckExact(o))) {
                continue;
            }
            for (int j = 0; j < i; j++) {
                if (frame->localsplus[j] == o) {
                    guard(c, OP_GUARD_BIT, emit(c, OP_IS, REP_BIT, c->st.v[i], c->st.v[j], LU_NOV, 0), 1);
                    c->st.v[i] = c->st.v[j];
                    break;
                }
            }
        }
        for (int i = 0; i < c->nlocals; i++) {
            PyObject *o = frame->localsplus[i];
            int dup = 0;
            for (int j = 0; j < i; j++) {
                dup |= frame->localsplus[j] == o && c->st.v[j] != LU_NOV
                       && I(c, c->st.v[j])->op != OP_ENTRY_LOCAL && I(c, c->st.v[j])->b == c->st.v[i];
                if (dup) {
                    c->st.v[i] = c->st.v[j];    /* reboxed already, as an earlier local */
                    break;
                }
            }
            if (dup) {
                continue;
            }
            if (o == NULL || (co->co_localspluskinds != NULL
                              && (((unsigned char *)PyBytes_AS_STRING(co->co_localspluskinds))[i]
                                  & (CO_FAST_CELL | CO_FAST_FREE)))) {
                continue;
            }
            if (PyLong_CheckExact(o) && _PyLong_IsCompact((PyLongObject *)o)) {
                c->st.v[i] = rebox(c, c->st.v[i], &PyLong_Type);
            }
            else if (PyFloat_CheckExact(o)) {
                c->st.v[i] = rebox(c, c->st.v[i], &PyFloat_Type);
            }
        }
    }
    edge_to(c, entry);
    emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    int r = build_ctx(c, c->blkat[entry]);
    if (f->room == INT32_MIN) {
        f->ins[room].flags |= INS_DEAD;
    }
    f->ins[room].imm = f->room;
    free(c->rets);
    free(c->pend);
    if (r < 0 && f->fail == NULL) {
        f->fail = "build failed";
    }
    return r;
}
