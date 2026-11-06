/* Bytecode to block SSA (DESIGN 6.2 steps 1-3).

   The builder interprets bytecode abstractly over SSA values: a state is
   the value of every local and stack entry, plus, per entry, whether
   compiled code owns a reference for it. Blocks start at jump targets;
   a block with more than one predecessor gets a phi per local and stack
   entry, and phis that turn out trivial are removed by the optimizer.

   References follow the interpreter's: the root frame's slots own their
   values (a STORE_LOCAL writes the slot), a stack entry or an inlined
   frame's local is owned when compiled code holds a reference for it. An
   instruction that consumes an operand it borrows releases it afterwards;
   one that keeps it takes a reference first. The optimizer deletes the
   pairs that a virtual box makes moot.

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
   inlined. */
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>

/* Inlining policy: code units an inlined callee compiles (one larger than
   that compiles only the branch directions it took), and inlined frames
   deep. */
#define INLINE_MAX_UNITS 400
#define INLINE_MAX_DEPTH 4
#define INLINE_MAX_TOTAL 3000

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
    int has;                    /* the entry state is set */
    int done;
    struct bstate in;
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
    int units;                  /* code units compiled */
};

/* Per-code JIT state the builder consults: pcs not to speculate at. */
int lu_jit_nospec(PyCodeObject *code, int pc);
int lu_jit_noinline(PyCodeObject *code);

static int build_block(struct ctx *c, int bi);

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
    if (c->parent == NULL) {
        memset(fs->owned, 0, (size_t)c->nlocals);       /* the root frame's slots own its locals */
    }
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
    if (lu_op_flags[op] & IF_GUARD) {
        c->f->ins[v].fs = c->snap;      /* it leaves before the instruction it belongs to */
    }
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
    if (v < c->f->capknown && c->f->known[v] != NULL && dominates(c, c->f->known_block[v])) {
        return c->f->known[v];
    }
    switch (I(c, v)->op) {
    case OP_BOX_INT: return &PyLong_Type;
    case OP_BOX_FLOAT: return &PyFloat_Type;
    case OP_BOOL: return &PyBool_Type;
    case OP_CONST: return known(c, v) != NULL ? Py_TYPE(known(c, v)) : NULL;
    case OP_HELPER: return I(c, v)->imm == H_range_iter ? &PyRangeIter_Type : NULL;
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

static void
release(struct ctx *c, lu_v v, int owned)
{
    if (owned && !immortal(c, v)) {
        emit(c, OP_DECREF, REP_NONE, v, LU_NOV, LU_NOV, 0);
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
    if (c->parent == NULL) {
        emit(c, OP_STORE_LOCAL, REP_NONE, v, LU_NOV, LU_NOV, i);
    }
    else {
        release(c, c->st.v[i], c->st.own[i]);
    }
    c->st.v[i] = v;
    c->st.own[i] = 1;
}

static lu_v
helper(struct ctx *c, int id, int nargs, const lu_v *args)
{
    const struct lu_helper *h = &lu_helpers[id];
    lu_v v = emit(c, OP_HELPER, h->rep, LU_NOV, LU_NOV, LU_NOV, id);
    struct lu_ins *in = I(c, v);
    in->nargs = nargs;
    in->args = lu_ir_alloc(c->f, sizeof(lu_v) * (size_t)(nargs ? nargs : 1));
    memcpy(in->args, args, sizeof(lu_v) * (size_t)nargs);
    return v;
}

/* A helper that may raise: it leaves at the state before the instruction,
   minus the `drop` stack entries the instruction consumed. */
static lu_v
helper_raising(struct ctx *c, int id, int nargs, const lu_v *args, int drop)
{
    lu_v v = helper(c, id, nargs, args);
    I(c, v)->fs = before(c);
    I(c, v)->drop = (uint8_t)drop;
    return v;
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

/* A call from an inlined frame runs with the inlined frames pushed as
   real ones (the backend's), which go again when it returns unless the
   callee kept a frame object of one: then the frames stay, and compiled
   code leaves here, after the call, for the interpreter to run them. */
static void
frames_guard(struct ctx *c)
{
    if (c->parent != NULL) {
        lu_v g = emit(c, OP_GUARD_FRAMES, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        I(c, g)->fs = snapshot(c, c->next);
    }
}

/* --- edges and blocks --- */

/* Every stack entry and inlined local is owned at a block's end, so that a
   join's inputs agree. */
static void
normalize(struct ctx *c)
{
    int from = c->parent == NULL ? c->nlocals : 0;
    for (int k = from; k < c->nlocals + c->st.sp; k++) {
        if (!c->st.own[k] && c->st.v[k] != LU_NOV) {
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
                b->in.own[k] = 1;       /* every input was owned (normalize) */
                I(c, phi)->imm2 = c->parent == NULL && k < c->nlocals;     /* a root slot's value */
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
    case DELETE_SUBSCR:
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
        || op == POP_JUMP_IF_NOT_NONE || op == FOR_ITER;
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
    int *preds = calloc((size_t)n + 1, sizeof(int));
    int *work = malloc(sizeof(int) * ((size_t)n + 1) * 2), nw = 0;
    c->blkat = lu_ir_alloc(c->f, sizeof(int) * ((size_t)n + 1));
    start[entry] = 1;
    preds[entry] = 1;
    work[nw++] = entry;
    while (nw > 0) {
        int pc = work[--nw];
        while (pc < n && !reach[pc]) {
            int base, oparg, opcode;
            reach[pc] = 1;
            int next = decode(c, pc, &base, &oparg, &opcode);
            int ends = base == RETURN_VALUE || !supported(base) || is_jump(base);
            if (is_jump(base) || is_branch(base)) {
                int t = jump_target(base, oparg, next);
                if (t < 0 || t >= n) {
                    free(reach); free(start); free(preds); free(work);
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
            c->bb[k].id = -1;
            c->blkat[pc] = k++;
        }
    }
    free(reach);
    free(start);
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
            if (base == RETURN_VALUE || !supported(base) || is_jump(base)) {
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

static int inline_call(struct ctx *c, PyFunctionObject *fn, lu_v *args, uint8_t *owned, int nargs,
                       lu_v *result, int *result_owned);

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
    return inline_call(c, fn, args, own, nargs, result, owned);
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
    if (lu_jit_nospec(c->code, c->pc)) {
        lk = rk = LU_KIND_OTHER;
    }
    if (known_type(c, a) == &PyLong_Type) lk = LU_KIND_INT;
    if (known_type(c, a) == &PyFloat_Type) lk = LU_KIND_FLOAT;
    if (known_type(c, b) == &PyLong_Type) rk = LU_KIND_INT;
    if (known_type(c, b) == &PyFloat_Type) rk = LU_KIND_FLOAT;
    if (op == NB_SUBSCR) {
        if (lk == LU_KIND_LIST && rk == LU_KIND_INT) {
            guard_type(c, a, &PyList_Type);
            lu_v i = unbox_int(c, b);
            r = emit(c, OP_LIST_ITEM, REP_OBJ, a, i, LU_NOV, 0);
            I(c, r)->fs = before(c);
        }
        else if (lk == LU_KIND_BYTES && rk == LU_KIND_INT) {
            guard_type(c, a, &PyBytes_Type);
            lu_v byte = emit(c, OP_BYTES_ITEM, REP_I64, a, unbox_int(c, b), LU_NOV, 0);
            I(c, byte)->fs = before(c);
            r = box_int(c, byte);
        }
    }
    else if (lk == LU_KIND_INT && rk == LU_KIND_INT && base == NB_TRUE_DIVIDE) {
        lu_v x = emit(c, OP_I2F, REP_F64, unbox_int(c, a), LU_NOV, LU_NOV, 0);
        lu_v y = emit(c, OP_I2F, REP_F64, unbox_int(c, b), LU_NOV, LU_NOV, 0);
        I(c, x)->fs = I(c, y)->fs = before(c);
        I(c, x)->imm = I(c, y)->imm = 1;      /* exact only below 2**53: guarded */
        lu_v args[2] = { box_float(c, x), box_float(c, y) };
        if (!kernel_method(c, &PyFloat_Type, "__truediv__", args, 2, &r, &rown)) {
            r = LU_NOV;
        }
    }
    else if (lk == LU_KIND_INT && rk == LU_KIND_INT) {
        lu_v x = unbox_int(c, a), y = unbox_int(c, b);
        lu_v z = int_op(c, base, x, y);
        r = z != LU_NOV ? box_int(c, z) : LU_NOV;
    }
    else if ((lk == LU_KIND_FLOAT || lk == LU_KIND_INT) && (rk == LU_KIND_FLOAT || rk == LU_KIND_INT)) {
        if (!float_binary(c, base, a, b, lk, rk, &r, &rown)) {
            r = LU_NOV;
        }
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
    return bit;
}

/* --- calls --- */

/* Whether fn's code can inline here. */
static int
inlinable(struct ctx *c, PyFunctionObject *fn, int nargs)
{
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    if (!PyCode_Check(co) || c->depth >= INLINE_MAX_DEPTH || c->f->ninlined > INLINE_MAX_TOTAL
        || (co->co_flags & (CO_GENERATOR | CO_COROUTINE | CO_ASYNC_GENERATOR | CO_ITERABLE_COROUTINE))
        || !(co->co_flags & CO_OPTIMIZED) || co->co_kwonlyargcount != 0 || co->_co_monitoring != NULL
        || (co->co_argcount < nargs && !(co->co_flags & CO_VARARGS)) || nargs > 30 || lu_jit_noinline(co)) {
        return 0;
    }
    int defaults = fn->func_defaults != NULL ? (int)PyTuple_GET_SIZE(fn->func_defaults) : 0;
    if (nargs + defaults < co->co_argcount) {
        return 0;
    }
    for (struct ctx *p = c; p != NULL; p = p->parent) {
        if (p->code == co) {
            return 0;           /* recursion */
        }
    }
    return 1;
}

static int
build_ctx(struct ctx *c, int entry_block);

/* Inlines fn(args...): the caller's state has the call's operands popped.
   Takes the args' references (owned[i]); on success *result is the value
   the call returns. Returns 1, or 0 with nothing emitted. */
static int
inline_call(struct ctx *c, PyFunctionObject *fn, lu_v *args, uint8_t *owned, int nargs,
            lu_v *result, int *result_owned)
{
    if (!inlinable(c, fn, nargs) || c->cur < 0) {
        return 0;
    }
    struct lu_func *f = c->f;
    int save_cur = c->cur;
    int save_nins = f->nins, save_nblocks = f->nblocks, save_consts = f->nconsts;
    int save_cur_nins = f->blocks[c->cur].nins, save_nsucc = f->blocks[c->cur].nsucc;
    int save_inlined = f->ninlined;
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
    if (!k->kernel) {
        /* The function's code is part of what was compiled: f.__code__ = ... changes its version. */
        if (fn->func_version < 2) {     /* 1: changed code or defaults, never specialized on again */
            free(k);
            return 0;
        }
        lu_v g = guard(c, OP_GUARD_VERSION, konst(c, (PyObject *)fn), fn->func_version);
        I(c, g)->imm2 = GV_FUNC;
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
        /* *args and **kwargs as the call binds them: a tuple of the extra
           positional arguments (the empty one), and a new dict. */
        int at = co->co_argcount;
        if (co->co_flags & CO_VARARGS) {
            int extra = nargs > at ? nargs - at : 0;
            k->st.v[at] = extra ? helper_raising(c, H_build_tuple, extra, args + at, 0)
                                : konst(c, (PyObject *)&_PyRuntime.singletons.empty_tuple);
            k->st.own[at] = extra != 0;
            set_known(c, k->st.v[at], &PyTuple_Type);
            for (int i = at; i < nargs; i++) {
                release(c, args[i], owned[i]);
            }
            at++;
        }
        if (co->co_flags & CO_VARKEYWORDS) {
            k->st.v[at] = helper_raising(c, H_new_dict, 0, NULL, 0);
            k->st.own[at] = 1;
            set_known(c, k->st.v[at], &PyDict_Type);
        }
        k->cont = lu_ir_block(f);
        /* Into the callee's first block. */
        k->cur = c->cur;
        for (int i = 0; i < k->nlocals; i++) {
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
    free(k->rets);
    if (!ok && _PyRuntime.jit.debug) {
        fprintf(stderr, "jit: not inlined %s: %s\n", PyUnicode_AsUTF8(fn->func_qualname),
                k->why != NULL ? k->why : "not compiled");
    }
    if (!ok) {
        /* Undo: the callee's blocks and instructions go, the call stays. */
        for (int i = save_nblocks; i < f->nblocks; i++) {
            free(f->blocks[i].ins);
            free(f->blocks[i].preds);
        }
        for (int i = save_nins; i < f->nins; i++) {
            if (f->ins[i].op == OP_PHI) {
                free(f->ins[i].args);
            }
        }
        for (int i = save_consts; i < f->nconsts; i++) {
            Py_XDECREF(f->consts[i]);
        }
        for (int i = save_nins; i < f->nins && i < f->capknown; i++) {
            f->known[i] = NULL;         /* the ids are reused */
        }
        f->nconsts = save_consts;
        f->nins = save_nins;
        f->nblocks = save_nblocks;
        f->ninlined = save_inlined;
        c->cur = save_cur;
        f->blocks[save_cur].nins = save_cur_nins;
        f->blocks[save_cur].nsucc = save_nsucc;
        f->fail = NULL;
    }
    free(k);
    return ok;
}

/* --- attributes and globals --- */

static PyObject *
name_at(struct ctx *c, int i)
{
    return PyTuple_GET_ITEM(c->code->co_names, i);
}

/* The global `name` as a constant, guarded on the dicts' keys versions
   and the entry's value: the value the code was compiled with, a new
   reference taken as LOAD_GLOBAL_MODULE takes it, since another thread may
   replace and free it meanwhile. NULL when the name cannot be found that
   way. */
static PyObject *
global_const(struct ctx *c, PyObject *name)
{
    if (c->kernel && lu_ns_lookup(PyThreadState_Get()->interp->kernel_rebound, name) == NULL) {
        return kernel_global(c, name);
    }
    if (!PyDict_CheckExact(c->globals) || !PyDict_CheckExact(c->builtins)) {
        return NULL;
    }
    lu_codeunit *site = lu_code_start(c->code) + (c->cache - lu_code_start(c->code)) - 1;
    if (lu_opcode_deopt(__atomic_load_n(&site->op.code, __ATOMIC_RELAXED)) == LOAD_GLOBAL
        && site->op.code == LOAD_GLOBAL && PyThreadState_Get()->interp->specializing) {
        lu_specialize_load_global(c->globals, c->builtins, site, name);
    }
    PyDictObject *g = (PyDictObject *)c->globals, *b = (PyDictObject *)c->builtins;
    PyDictKeysObject *gk = g->ma_keys, *bk = b->ma_keys;
    if (gk->dk_version == 0 || !lu_dk_is_unicode(gk)) {
        return NULL;
    }
    Py_ssize_t ix = lu_ns_index(c->globals, name);
    PyDictObject *d = g;
    PyDictKeysObject *dk = gk;
    if (ix < 0) {
        if (bk->dk_version == 0 || !lu_dk_is_unicode(bk) || (ix = lu_ns_index(c->builtins, name)) < 0) {
            return NULL;
        }
        d = b;
        dk = bk;
    }
    PyObject *v = ix < dk->dk_nentries ? lu_try_xget_ref(lu_dk_value_slot(dk, ix)) : NULL;
    if (v != NULL && dk != __atomic_load_n(&d->ma_keys, __ATOMIC_ACQUIRE)) {
        Py_CLEAR(v);
    }
    if (v == NULL) {
        return NULL;
    }
    lu_v gd = konst(c, c->globals);
    lu_v g1 = guard(c, OP_GUARD_VERSION, gd, gk->dk_version);
    I(c, g1)->imm2 = GV_DICT_KEYS;
    if (d == b) {
        lu_v g2 = guard(c, OP_GUARD_VERSION, konst(c, c->builtins), bk->dk_version);
        I(c, g2)->imm2 = GV_DICT_KEYS;
    }
    lu_v g3 = guard(c, OP_GUARD_IS, konst(c, (PyObject *)d), (int64_t)(intptr_t)v);
    I(c, g3)->imm2 = GI_KEYS_FIELD | ((char *)lu_dk_value_slot(dk, ix) - (char *)dk);
    return v;
}

static int
load_global(struct ctx *c)
{
    PyObject *name = name_at(c, c->oparg >> 1);
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
    lu_v t = emit(c, OP_LOAD, REP_I64, type_of(c, owner), LU_NOV, LU_NOV, offsetof(PyTypeObject, tp_version_tag));
    I(c, t)->imm2 = 4;          /* 32 bits */
    int join = lu_ir_block(f);
    lu_v r1[LU_PIC_WAYS + 1], r2[LU_PIC_WAYS + 1];
    int paths = 0;
    pop(c, NULL);
    for (int i = 0; i < n; i++) {
        struct lu_pic_entry e = pic->e[i];
        lu_v bit = emit(c, OP_CMP_I, REP_BIT, t, const_i(c, e.version), LU_NOV, Py_EQ);
        int arm = lu_ir_block(f), next = lu_ir_block(f);
        lu_ir_edge(f, c->cur, arm);
        lu_ir_edge(f, c->cur, next);
        emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
        c->cur = arm;
        lu_v a1, a2 = LU_NOV;
        if (e.op == LOAD_ATTR_INSTANCE_VALUE) {
            a1 = emit(c, OP_INLINE_LOAD, REP_OBJ, owner, LU_NOV, LU_NOV, e.index);
            I(c, a1)->fs = before(c);
            release(c, owner, own);
        }
        else {
            if (e.op == LOAD_ATTR_METHOD_WITH_VALUES || e.op == LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES) {
                lu_v args[2] = { owner, konst(c, name) };
                guard(c, OP_GUARD_BIT, helper(c, H_dict_lacks, 2, args), 1);
            }
            a1 = take(c, konst(c, e.descr), 0);
            if (e.op == LOAD_ATTR_METHOD_NO_DICT || e.op == LOAD_ATTR_METHOD_WITH_VALUES) {
                a2 = method ? take(c, owner, own) : LU_NOV;
                if (!method) {
                    release(c, owner, own);
                }
            }
            else {
                release(c, owner, own);
            }
        }
        r1[paths] = a1;
        r2[paths++] = a2;
        emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        lu_ir_edge(f, arm, join);
        c->cur = next;
    }
    if (root_only(c)) {
        lu_v args[3] = { owner, konst(c, name), const_i(c, (int64_t)(intptr_t)pic) };
        lu_v v = helper_raising(c, method ? H_load_method : H_load_attr, 3, args, 1);
        release(c, owner, own);
        r1[paths] = v;
        r2[paths++] = LU_NOV;
        emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        lu_ir_edge(f, c->cur, join);
    }
    else {
        unsupported(c);         /* leaves at the state before the load */
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
        guard(c, OP_GUARD_VERSION, owner, version);
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
        guard(c, OP_GUARD_VERSION, owner, version);
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
    case LOAD_ATTR_CLASS: {
        /* A class's own attribute, by the class's version. */
        lu_v g = guard(c, OP_GUARD_VERSION, owner, version);
        I(c, g)->imm2 = GV_TYPE_ITSELF;
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
    if (op == STORE_ATTR_WITH_HINT) {
        lu_v args[4] = { owner, v, konst(c, name), const_i(c, version) };
        guard_after(c, OP_GUARD_BIT, helper(c, H_attr_store, 4, args), 1);
    }
    else if (op == STORE_ATTR_INSTANCE_VALUE) {
        guard(c, OP_GUARD_VERSION, owner, version);
        lu_v st = emit(c, OP_INLINE_STORE, REP_NONE, owner, v, LU_NOV, index);
        I(c, st)->fs = before(c);
    }
    else if (op == STORE_ATTR_SLOT) {
        guard(c, OP_GUARD_VERSION, owner, version);
        lu_v args[3] = { owner, const_i(c, index), v };
        helper(c, H_slot_store, 3, args);
    }
    else if (root_only(c)) {
        lu_v args[3] = { owner, konst(c, name), v };
        helper_raising(c, H_store_attr, 3, args, 2);
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
                    I(c, r)->fs = before(c);
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
    const char *prim = primitive_name(kc);
    if (kc != NULL && PyCFunction_Check(kc) && (((PyCFunctionObject *)kc)->m_ml->ml_flags & LU_METH_FRAME)) {
        unsupported(c);         /* it hands the interpreter a frame to run */
        return -1;
    }
    lu_v r = LU_NOV;
    int rown = 1;
    if (fn != NULL && bound == NULL) {
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
        lu_v hargs[3] = { start, stop, step };
        r = helper(c, H_range_iter, 3, hargs);
        guard_after(c, OP_GUARD_NOT_NULL, r, 0);
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
        guard_after(c, OP_GUARD_NOT_NULL, r, 1);
        pop_call(c, n);
        push(c, r, 1);
        frames_guard(c);
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
        if (inline_call(c, fn, args, own, nargs, &r, &rown)) {
            if (c->cur >= 0) {          /* else every path of the callee left */
                release(c, callable, owncallable);
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
    if (prim != NULL && self == LU_NOV && (((PyCFunctionObject *)kc)->m_ml->ml_flags & LU_METH_NOPY)) {
        /* It runs no Python, so no frame needs to be real, inlined or not. */
        lu_v hargs[33];
        hargs[0] = callable;
        for (int i = 0; i < n; i++) {
            hargs[1 + i] = peek(c, n - i);
        }
        r = helper_raising(c, H_prim, n + 1, hargs, n + 2);
        pop_call(c, n);
        push(c, r, 1);
        return 0;
    }
    if (!opaque_call(c)) {
        return -1;
    }
    lu_v hargs[34];
    hargs[0] = callable;
    hargs[1] = self != LU_NOV ? self : konst(c, NULL);
    for (int i = 0; i < n; i++) {
        hargs[2 + i] = peek(c, n - i);
    }
    r = helper_raising(c, H_call, n + 2, hargs, n + 2);
    /* The helper declines when the C stack is too deep to recurse: the interpreter makes the call. */
    guard_after(c, OP_GUARD_NOT_NULL, r, 1);
    pop_call(c, n);
    push(c, r, 1);
    frames_guard(c);
    return 0;
}

/* --- iteration --- */

static int
for_iter(struct ctx *c)
{
    lu_v it = peek(c, 1);
    int exit_pc = jump_target(FOR_ITER, c->oparg, c->next);
    int kind = feedback(c, c->cache);
    if (lu_jit_nospec(c->code, c->pc)) {
        kind = LU_ITER_OTHER;
    }
    int guessed = 0;
    if (I(c, it)->op == OP_PHI && I(c, it)->nargs > 0) {
        PyTypeObject *t = known_type(c, I(c, it)->args[0]);
        guessed = t == &PyRangeIter_Type ? LU_ITER_RANGE : t == &PyListIter_Type ? LU_ITER_LIST : 0;
    }
    if (kind == 0) {
        kind = known_type(c, it) == &PyRangeIter_Type ? LU_ITER_RANGE : guessed;
    }
    int range = known_type(c, it) == &PyRangeIter_Type || kind == LU_ITER_RANGE;
    lu_v done, v;
    if (range) {
        guard_type(c, it, &PyRangeIter_Type);
        done = emit(c, OP_RITER_DONE, REP_BIT, it, LU_NOV, LU_NOV, 0);
    }
    else if (kind == LU_ITER_LIST) {
        guard_type(c, it, &PyListIter_Type);
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
        int seen = c->parent == NULL || Py_SIZE(c->code) <= INLINE_MAX_UNITS || lu_jit_nospec(c->code, c->pc) ? 0
            : __atomic_load_n(&c->cache->cache, __ATOMIC_RELAXED);
        if (I(c, w)->op != OP_CONST_I && (seen == LU_BRANCH_FELL || seen == LU_BRANCH_JUMPED)) {
            /* In a callee too large to inline whole, a direction the
               interpreter never took leaves instead. */
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
        if (!root_only(c)) {
            unsupported(c);
            return -1;
        }
        w = helper_raising(c, H_get_iter, 1, &v, 1);
        pop(c, NULL);
        release(c, v, own);
        push(c, w, 1);
        return 0;
    }
    case FOR_ITER:
        return for_iter(c);
    case CALL:
        return call(c);
    case RETURN_VALUE: {
        v = pop(c, &own);
        v = take(c, v, own);
        while (c->st.sp > 0) {
            w = pop(c, &own);
            release(c, w, own);
        }
        if (c->parent == NULL) {
            emit(c, OP_RETURN, REP_NONE, v, LU_NOV, LU_NOV, 0);
        }
        else {
            for (int i = 0; i < c->nlocals; i++) {
                release(c, c->st.v[i], c->st.own[i]);
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
    case BUILD_LIST: case BUILD_TUPLE: {
        if (oparg > 30) {
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
        v = helper_raising(c, c->base == BUILD_LIST ? H_build_list : H_build_tuple, oparg, args, oparg);
        c->st.sp -= oparg;
        for (int i = 0; i < oparg; i++) {
            release(c, args[i], owns[i]);
        }
        push(c, v, 1);
        return 0;
    }
    case UNPACK_SEQUENCE: {
        /* An exact tuple of oparg items, its items pushed last first; any
           other sequence leaves, and after enough of that it always does. */
        v = peek(c, 1);
        own = peek_owned(c, 1);
        if (oparg > 30 || lu_jit_nospec(c->code, c->pc)) {
            unsupported(c);
            return -1;
        }
        guard_type(c, v, &PyTuple_Type);
        lu_v size = emit(c, OP_LOAD, REP_I64, v, LU_NOV, LU_NOV, offsetof(PyVarObject, ob_size));
        guard(c, OP_GUARD_BIT, emit(c, OP_CMP_I, REP_BIT, size, const_i(c, oparg), LU_NOV, Py_EQ), 1);
        pop(c, NULL);
        for (int i = oparg - 1; i >= 0; i--) {
            lu_v item = emit(c, OP_LOAD, REP_OBJ, v, LU_NOV, LU_NOV,
                             (int64_t)(offsetof(PyTupleObject, ob_item) + sizeof(PyObject *) * (size_t)i));
            incref(c, item);
            push(c, item, 1);
        }
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
    default:
        unsupported(c);
        return -1;
    }
}

/* Whether block k sends no more edges: it is built, or nothing reached
   it and nothing that could still reach it is left. memo: 0 unknown, 1
   settled, 2 not (or being decided, which a cycle leaves unsettled). */
static int
settled(struct ctx *c, int k, char *memo)
{
    if (memo[k] == 0) {
        memo[k] = 2;
        int ok = c->bb[k].done || !c->bb[k].has;
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
    lu_codeunit *start = lu_code_start(c->code);
    b->done = 1;
    c->cur = b->id;
    state_copy(c, &c->st, &b->in);
    if (b->phis && preds_built(c, bi)) {
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
        int mpc = pc;
        while (lu_base_opcode(c->code, mpc) == EXTENDED_ARG) {
            mpc++;
        }
        c->pc = pc;
        c->next = decode(c, pc, &c->base, &c->oparg, &c->opcode);
        c->cache = start + mpc + 1;
        c->snap = NULL;
        before(c);
        if (!supported(c->base)) {
            unsupported(c);
            break;
        }
        translate(c);
        c->units += c->next - pc;
        if (c->parent != NULL && c->units > INLINE_MAX_UNITS) {
            c->failed = 1;      /* too large to inline: it is called instead */
            c->why = "too large";
        }
        pc = c->next;
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
    int entry = f->entry_pc;
    if (scan(c, entry) < 0) {
        f->fail = "unfollowable jumps";
        return -1;
    }
    count_fallthrough(c);
    c->bb[c->blkat[entry]].npreds++;
    c->cur = lu_ir_block(f);
    state_init(c, &c->st);
    c->pc = entry;
    /* At a call's entry every local but the parameters (and cells, which
       the prologue made) is unbound. */
    int params = co->co_argcount + co->co_kwonlyargcount + ((co->co_flags & CO_VARARGS) != 0)
        + ((co->co_flags & CO_VARKEYWORDS) != 0);
    int call = entry == lu_jit_call_entry(co);
    for (int i = 0; i < c->nlocals; i++) {
        int plain = co->co_localspluskinds == NULL
            || !(((unsigned char *)PyBytes_AS_STRING(co->co_localspluskinds))[i] & (CO_FAST_CELL | CO_FAST_FREE));
        c->st.v[i] = call && i >= params && plain && frame->localsplus[i] == NULL ? LU_NOV
            : emit(c, OP_ENTRY_LOCAL, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, i);
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
    if (r < 0 && f->fail == NULL) {
        f->fail = "build failed";
    }
    return r;
}
