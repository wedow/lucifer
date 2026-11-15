/* Control flow for the evaluator (eval.h): blocks of bytecode, the edges
   between them, joins, loops, and the walk over a frame's code.

   A block starts where a jump lands or a branch falls through. Control
   reaches it over edges, each an open graph block and a state. A block is
   built once, lowest instruction first, so every forward edge to it is
   there when it is: one edge goes on in its own graph block, several are
   joined (merge). A value that differs between edges becomes a phi; a
   virtual object stays virtual when every edge has it so.

   A loop header is built from its forward edges before its back-edges
   exist. It makes a phi for each slot the loop may change: a raw one
   where the slot holds an int or a float (a box that never exists), an
   object's otherwise. A back-edge must then fit what the header made. One
   that does not records a hint for the header (the slot gets an object; a
   box the loop carries wants a raw phi) and the whole build starts over
   with it: nothing is undone piecemeal, and each rebuild knows strictly
   more. */
#include "jit/eval.h"

#include <stdlib.h>
#include <string.h>

int
lu_ev_decode(struct ctx *c, int pc, int *base, int *oparg, int *opcode)
{
    lu_codeunit *start = lu_code_start(c->code);
    int arg = 0;
    while (lu_base_opcode(c->code, pc) == EXTENDED_ARG) {
        arg = arg << 8 | start[pc].op.arg;
        pc++;
    }
    *base = lu_base_opcode(c->code, pc);
    *opcode = __atomic_load_n(&start[pc].op.code, __ATOMIC_RELAXED);
    *oparg = arg << 8 | start[pc].op.arg;
    return pc + 1 + lu_opcode_caches[*base];
}

int
lu_ev_jump_target(int op, int oparg, int next)
{
    return op == FOR_ITER ? next + oparg + 1
        : (op == JUMP_BACKWARD || op == JUMP_BACKWARD_NO_INTERRUPT) ? next - oparg
        : next + oparg;
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

static int
terminal(int op)
{
    return op == RETURN_VALUE || op == RAISE_VARARGS || op == RERAISE || op == YIELD_VALUE;
}

/* What every evaluation of a code starts from, made once per code and
   kept with its JIT state (jit.c), or in the build's arena while it has
   none: its blocks, and whether the evaluator compiles what a call of it
   can reach. One allocation without pointers, so that it can be copied. */
struct lu_ev_scan {
    int ncode;
    int ok;                     /* every jump lands in the code */
    int supported;              /* every instruction a call reaches without an exception is compiled */
    int nbb;
    int data[];                 /* blkat[ncode + 1] (a block's index, or -1), then per block its pc and loop end */
};

/* Whether every instruction a call of the code reaches from its first
   without an exception is compiled. */
static int
reach_supported(struct ctx *c, int n)
{
    char *seen = calloc((size_t)n + 1, 1);
    int *stack = malloc(sizeof(int) * (size_t)(n + 1)), sp = 0, ok = 1;
    stack[sp++] = 0;
    while (sp > 0 && ok) {
        int pc = stack[--sp];
        while (pc < n && !seen[pc]) {
            seen[pc] = 1;
            int base, oparg, op;
            int next = lu_ev_decode(c, pc, &base, &oparg, &op);
            if (!lu_ev_op_supported(base)) {
                ok = 0;
                break;
            }
            if (is_jump(base) || is_branch(base)) {
                int target = lu_ev_jump_target(base, oparg, next);
                if (target < 0 || target >= n) {
                    ok = 0;
                    break;
                }
                if (!seen[target]) {
                    stack[sp++] = target;
                }
            }
            if (is_jump(base) || terminal(base)) {
                break;
            }
            pc = next;
        }
    }
    free(seen);
    free(stack);
    return ok;
}

/* The code's blocks: every jump target and every instruction after a
   jump, a branch or an end. Malloc'd. */
static struct lu_ev_scan *
make_scan(PyCodeObject *code)
{
    struct ctx c = { .code = code };
    int n = (int)Py_SIZE(code), ok = 1, nbb = 0;
    char *start = calloc((size_t)n + 1, 1);
    int *loop_end = calloc((size_t)n + 1, sizeof(int));
    for (int pc = 0; pc < n;) {
        int base, oparg, op;
        int next = lu_ev_decode(&c, pc, &base, &oparg, &op);
        if (is_jump(base) || is_branch(base)) {
            int target = lu_ev_jump_target(base, oparg, next);
            if (target < 0 || target >= n) {
                ok = 0;
                break;
            }
            start[target] = 1;
            if (target <= pc && loop_end[target] < pc + 1) {
                loop_end[target] = pc + 1;      /* a back-edge: nonzero marks the header */
            }
        }
        if ((is_jump(base) || is_branch(base) || terminal(base)) && next < n) {
            start[next] = 1;
        }
        pc = next;
    }
    for (int pc = 0; pc < n; pc++) {
        nbb += start[pc];
    }
    struct lu_ev_scan *s = malloc(sizeof(struct lu_ev_scan) + sizeof(int) * (size_t)(n + 1 + 2 * nbb));
    int *blkat = s->data, *bbs = s->data + n + 1;
    s->ncode = n;
    s->ok = ok;
    s->nbb = 0;
    for (int pc = 0; pc <= n; pc++) {
        blkat[pc] = -1;
        if (pc < n && start[pc]) {
            blkat[pc] = s->nbb;
            bbs[2 * s->nbb] = pc;
            bbs[2 * s->nbb + 1] = loop_end[pc];
            s->nbb++;
        }
    }
    free(start);
    free(loop_end);
    s->supported = ok && reach_supported(&c, n);
    return s;
}

static const struct lu_ev_scan *
scan_of(struct gctx *g, PyCodeObject *code)
{
    struct lu_ev_scan *s = lu_jit_scan(code);
    if (s != NULL) {
        return s;
    }
    s = make_scan(code);
    struct lu_ev_scan *kept = lu_jit_keep_scan(code, s);
    if (kept == NULL) {
        size_t size = sizeof(struct lu_ev_scan) + sizeof(int) * (size_t)(s->ncode + 1 + 2 * s->nbb);
        kept = lu_ir_alloc(g->f, size);
        memcpy(kept, s, size);
        free(s);
    }
    return kept;
}

int
lu_ev_supported(struct gctx *g, PyCodeObject *code)
{
    return scan_of(g, code)->supported;
}

/* c's blocks, from its code's scan, whose table of where they start c
   reads until its entry needs a block of its own (block_at). */
static int
scan(struct ctx *c)
{
    const struct lu_ev_scan *s = scan_of(c->g, c->code);
    if (!s->ok) {
        return -1;
    }
    const int *bbs = s->data + s->ncode + 1;
    c->blkat = s->data;
    c->bb = lu_ir_alloc(c->g->f, sizeof(struct bblk) * (size_t)(s->nbb + 2));
    c->nbb = s->nbb;
    c->lo = 0;
    for (int i = 0; i < s->nbb; i++) {
        c->bb[i].pc = bbs[2 * i];
        c->bb[i].loop = bbs[2 * i + 1] != 0;
        c->bb[i].loop_end = bbs[2 * i + 1];
    }
    return 0;
}

/* The block at pc, made one now if the walk enters code there that no
   jump names (the unit's entry). */
static struct bblk *
block_at(struct ctx *c, int pc)
{
    if (c->blkat[pc] < 0) {
        /* Blocks stay sorted only by lookup: the walk picks the lowest pc. */
        int *blkat = lu_ir_alloc(c->g->f, sizeof(int) * (size_t)(c->ncode + 1));
        memcpy(blkat, c->blkat, sizeof(int) * (size_t)(c->ncode + 1));
        blkat[pc] = c->nbb;
        c->blkat = blkat;
        c->bb[c->nbb].pc = pc;
        c->nbb++;
    }
    return &c->bb[c->blkat[pc]];
}

void
lu_ev_hint(struct gctx *g, PyCodeObject *code, int pc, int slot, int kind)
{
    for (int i = 0; i < g->nhints; i++) {
        struct hint *h = &g->hints[i];
        if (h->code == code && h->pc == pc && h->slot == slot) {
            if (h->kind != kind) {
                h->kind = HINT_OBJ;     /* two shapes: the slot holds an object */
                g->restart = 1;
            }
            return;
        }
    }
    if (g->nhints == g->caphints) {
        g->caphints = g->caphints ? 2 * g->caphints : 16;
        g->hints = realloc(g->hints, sizeof(struct hint) * (size_t)g->caphints);
    }
    g->hints[g->nhints++] = (struct hint){ code, pc, slot, kind };
    g->restart = 1;
}

int
lu_ev_hinted(struct gctx *g, PyCodeObject *code, int pc, int slot)
{
    for (int i = 0; i < g->nhints; i++) {
        struct hint *h = &g->hints[i];
        if (h->code == code && h->pc == pc && h->slot == slot) {
            return h->kind;
        }
    }
    return 0;
}

/* --- values at joins --- */

/* The raw representation a slot's value has without a guard: a box that
   does not exist, or a constant; REP_NONE otherwise. */
static int
raw_rep(struct ctx *c, lu_v r)
{
    if (r == LU_NOV) {
        return REP_NONE;
    }
    if (LU_IS_VIRT(r)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
        if (o == NULL || real != LU_NOV) {
            return REP_NONE;
        }
        return o->kind == VK_BOX_INT ? REP_I64 : o->kind == VK_BOX_FLOAT ? REP_F64 : REP_NONE;
    }
    PyObject *k = lu_ev_const(c, r);
    if (k == NULL) {
        return REP_NONE;
    }
    if (PyFloat_CheckExact(k)) {
        return REP_F64;
    }
    int overflow = 0;
    if (PyLong_CheckExact(k)) {
        long long x = PyLong_AsLongLongAndOverflow(k, &overflow);
        return !overflow && x != INT64_MIN ? REP_I64 : REP_NONE;
    }
    return REP_NONE;
}

/* r's raw value; an unbound local's is anything (the object beside it,
   lu_jit_unbound, says so, and nothing reads the value before a store). */
static lu_v
raw_of(struct ctx *c, lu_v r, int rep)
{
    if (r == LU_NOV) {
        return rep == REP_I64 ? lu_ev_const_i(c, 0) : lu_ev_const_f(c, 0.0);
    }
    return rep == REP_I64 ? lu_ev_unbox_int(c, r) : lu_ev_unbox_float(c, r);
}

/* Whether r is a bool that is no object: True or False themselves, or one
   compiled code has not made. Its bit is lu_ev_truth's. */
static int
is_bit(struct ctx *c, lu_v r)
{
    PyObject *k = r >= 0 ? lu_ev_const(c, r) : NULL;
    lu_v real = LU_NOV;
    struct lu_vobj *o = LU_IS_VIRT(r) ? lu_ev_virt(c->g, &c->g->st, r, &real) : NULL;
    return k == Py_True || k == Py_False || (o != NULL && real == LU_NOV && o->kind == VK_BOOL);
}

static lu_v new_phi(struct ctx *c, int block, int rep, int cap);

/* The phi beside a raw phi that carries the object its value was, where
   an edge has one (NULL elsewhere), and the box of the two. */
static lu_v
origin_phi(struct ctx *c, int block, lu_v raw)
{
    lu_v phi = new_phi(c, block, REP_OBJ, 2);
    I(c, phi)->flags |= INS_OWNED;
    lu_ev_def_type(c, phi, I(c, raw)->rep == REP_I64 ? &PyLong_Type : &PyFloat_Type);
    return phi;
}

static lu_v
box_of(struct ctx *c, lu_v raw, lu_v origin)
{
    if (I(c, raw)->rep == REP_BIT) {
        return lu_ev_bool(c, raw, 0);
    }
    if (origin == LU_NOV) {
        return I(c, raw)->rep == REP_I64 ? lu_ev_box_int(c, raw) : lu_ev_box_float(c, raw);
    }
    struct lu_vobj *o;
    int is_int = I(c, raw)->rep == REP_I64;
    lu_v r = lu_ev_virt_new(c, is_int ? VK_BOX_INT : VK_BOX_FLOAT, is_int ? &PyLong_Type : &PyFloat_Type, 2, &o);
    o->f[0] = raw;
    o->f[1] = origin;
    return r;
}

static lu_v
new_phi(struct ctx *c, int block, int rep, int cap)
{
    struct lu_func *f = c->g->f;
    lu_v phi = lu_ir_emit(f, block, OP_PHI, rep, LU_NOV, LU_NOV, LU_NOV, 0);
    f->ins[phi].args = malloc(sizeof(lu_v) * (size_t)(cap ? cap : 1));
    f->ins[phi].nargs = 0;
    f->ins[phi].src = c->code;
    f->ins[phi].src_pc = c->pc;
    return phi;
}

static void
phi_add(struct ctx *c, lu_v phi, lu_v arg)
{
    struct lu_ins *in = I(c, phi);
    in->args = realloc(in->args, sizeof(lu_v) * (size_t)(in->nargs + 1));
    in->args[in->nargs++] = arg;
}

/* The edge's state becomes the current one, in its block, before the
   instruction at pc: what a join converts there is emitted in it. */
static void
enter_edge(struct ctx *c, struct edge *e, int pc)
{
    struct gctx *g = c->g;
    g->cur = e->block;
    g->st = e->st;
    lu_ev_changed(g);
    g->npending = 0;
    g->observed = 0;
    c->pc = pc;
    lu_ev_begin(c, pc);
}

/* Per slot of a loop header, whose phis are distinct, the slot whose phi
   it takes first (lead): itself, or -1 without one. */
static int *
distinct_leads(struct gctx *g, const lu_v *phis, int nslots)
{
    int *lead = lu_ir_alloc(g->f, sizeof(int) * (size_t)(nslots ? nslots : 1));
    for (int i = 0; i < nslots; i++) {
        lead[i] = phis[i] != LU_NOV ? i : -1;
    }
    return lead;
}

/* What each phi of a join takes on the current edge: per slot, the
   object an object phi takes (made real here if it was not), the object a
   raw phi's value stands for if there is one, or LU_NOV. `phis` holds,
   per slot, the phi that replaces it (slots that held the same values
   share one) or LU_NOV, and lead the first slot with that phi (-1: none). */
static lu_v *
edge_inputs(struct ctx *c, const lu_v *phis, const int *lead, int nslots)
{
    struct gctx *g = c->g;
    lu_v *in = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots ? nslots : 1));
    for (int i = 0; i < nslots; i++) {
        in[i] = LU_NOV;
        if (lead[i] != i) {
            continue;
        }
        lu_v r = slots(c)[i];
        if (I(c, phis[i])->rep == REP_OBJ) {
            in[i] = r == LU_NOV ? lu_ev_konst(c, NULL) : lu_ev_val(c, r);
        }
        else if (LU_IS_VIRT(r)) {
            /* The object the box became here, or the one it came from. */
            struct lu_vobj *o = lu_ev_virt(g, &g->st, r, &in[i]);
            if (in[i] == LU_NOV && o != NULL && o->n == 2) {
                in[i] = o->f[1];
            }
        }
        else if (r == LU_NOV) {
            in[i] = lu_ev_konst(c, &lu_jit_unbound);
        }
        else if (lu_ev_const(c, r) == NULL) {
            in[i] = r;          /* the object whose raw value the phi takes */
        }
    }
    return in;
}

/* Whether slot i comes round a loop unchanged: what its phi would take is
   the phi itself. The slot goes on naming it, with the reference it has. */
static int
carried(struct ctx *c, const lu_v *phis, const lu_v *origin, const lu_v *in, int i)
{
    return phis[i] != LU_NOV && in[i] != LU_NOV && in[i] == (I(c, phis[i])->rep == REP_OBJ ? phis[i] : origin[i]);
}

/* The phis take their inputs. An object phi takes a reference: the
   value's own when no slot left names it and no earlier phi took it, a
   new one otherwise. A raw phi's inputs were added by its maker; the
   object its value stood for on this edge goes to the phi beside it
   (origin[i], which keeps `is` true of a value that was an object before
   the join), or is released when there is none and nothing names it. A
   slot a loop carries unchanged gives nothing up: it is one of the slots
   left. */
static void
edge_take(struct ctx *c, const lu_v *phis, const int *lead, const lu_v *origin, const lu_v *in, int nslots, int pc)
{
    struct gctx *g = c->g;
    struct lu_fs *rest = lu_ev_snapshot(c, pc);
    for (int i = 0; i < nslots; i++) {
        if (phis[i] != LU_NOV && !carried(c, phis, origin, in, i)) {
            *lu_fs_slot(rest, i) = LU_NOV;
        }
    }
    lu_ev_names_walk(g, rest);
    for (int i = 0; i < nslots; i++) {
        lu_v x = in[i];
        if (x == LU_NOV) {
            if (lead[i] == i && origin[i] != LU_NOV) {
                phi_add(c, origin[i], lu_ev_konst(c, NULL));    /* no object yet on this edge */
            }
            continue;
        }
        if (carried(c, phis, origin, in, i)) {
            phi_add(c, I(c, phis[i])->rep == REP_OBJ ? phis[i] : origin[i], x);
            continue;
        }
        int owned = (I(c, x)->flags & INS_OWNED) != 0;
        int free_now = owned && !lu_ev_names(g, x);
        for (int k = 0; k < i && free_now; k++) {
            free_now = in[k] != x;      /* an earlier phi took it, or it was released */
        }
        if (I(c, phis[i])->rep != REP_OBJ) {
            if (origin[i] != LU_NOV) {
                phi_add(c, origin[i], x);
                if (owned && !free_now) {
                    lu_ev_retain(c, x);
                }
            }
            else if (free_now) {
                lu_ev_emit(c, OP_DECREF, REP_NONE, x, LU_NOV, LU_NOV, 0);       /* an int or a float */
            }
            continue;
        }
        phi_add(c, phis[i], x);
        if ((I(c, phis[i])->flags & INS_OWNED) && !free_now) {
            lu_ev_retain(c, x);
        }
    }
}

/* Whether what state `a` knows of the virtual objects r reaches is what
   state `b` knows: the same records, the same objects made. */
static int
same_binding(struct gctx *g, const struct state *a, const struct state *b, lu_v r)
{
    if (!LU_IS_VIRT(r)) {
        return 1;
    }
    lu_v ra, rb;
    struct lu_vobj *oa = lu_ev_virt(g, a, r, &ra), *ob = lu_ev_virt(g, b, r, &rb);
    if (oa != ob || ra != rb) {
        return 0;
    }
    for (int i = 0; oa != NULL && ra == LU_NOV && i < oa->n; i++) {
        if (!same_binding(g, a, b, oa->f[i])) {
            return 0;
        }
    }
    return 1;
}

/* Whether the slots of the state that stay (phis[i] == LU_NOV) reach
   virtual object id. */
static int
stays_reached(struct gctx *g, const struct state *st, const lu_v *phis, int id)
{
    struct state rest = *st;
    int d = st->depth, n = st->nlocals[d] + st->sp[d];
    lu_v *v = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(st->cap[d] ? st->cap[d] : 1));
    lu_v **frames = lu_ir_alloc(g->f, sizeof(lu_v *) * (size_t)(d + 1));
    memcpy(frames, st->v, sizeof(lu_v *) * (size_t)(d + 1));
    memcpy(v, st->v[d], sizeof(lu_v) * (size_t)st->cap[d]);
    for (int i = 0; i < n; i++) {
        if (phis[i] != LU_NOV) {
            v[i] = LU_NOV;
        }
    }
    frames[d] = v;
    rest.v = frames;
    return lu_ev_reaches(g, &rest, id);
}

/* What the joined state knows of a virtual object the edges knew
   differently. */
struct override {
    int id;
    lu_v real;
    struct lu_vobj *obj;
};

/* Virtual object `id`, which the edges know differently and the joined
   state still reaches, made one thing for the join J: an object on every
   edge (a phi of them, which takes each edge's) when any edge made it,
   else a record whose differing fields are phis. The edges' own states
   stay as they are: their exits read them. */
static struct override
reconcile(struct ctx *c, struct edge *edges, int n, int pc, int id, int J)
{
    struct gctx *g = c->g;
    lu_v r = LU_VIRT(id);
    int any_real = 0, kind = -1;
    struct lu_vobj *first = NULL;
    for (int e = 0; e < n; e++) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(g, &edges[e].st, r, &real);
        any_real |= real != LU_NOV;
        if (o != NULL) {
            first = first != NULL ? first : o;
            kind = o->kind;
        }
    }
    if (any_real || first == NULL || kind != VK_SEQITER) {
        lu_v phi = new_phi(c, J, REP_OBJ, n);
        int owned = 0;
        PyTypeObject *type = NULL;
        for (int e = 0; e < n; e++) {
            enter_edge(c, &edges[e], pc);
            lu_v x = lu_ev_val(c, r);
            type = e == 0 || type == lu_ev_type(c, x) ? lu_ev_type(c, x) : NULL;
            owned |= (I(c, x)->flags & INS_OWNED) != 0;
            phi_add(c, phi, x);
            g->keep = x;        /* the phi's now, even where this edge's state does not reach it */
            lu_ev_settle(c, pc);
            g->keep = LU_NOV;
            edges[e].block = g->cur;
            edges[e].st = g->st;
        }
        if (owned) {
            I(c, phi)->flags |= INS_OWNED;
        }
        if (first != NULL || type != NULL) {
            lu_ev_def_type(c, phi, first != NULL ? first->type : type);     /* the one object, whichever edge made it */
        }
        return (struct override){ id, phi, kind < 0 || LU_VKIND_MUTABLE(kind) ? NULL : first };
    }
    /* An iterator stepped differently on each edge: its index is a phi. */
    struct lu_vobj *o = lu_ir_vobj(g->f, VK_SEQITER, first->type, 2);
    lu_v phi = new_phi(c, J, REP_I64, n);
    o->f[0] = first->f[0];
    o->f[1] = phi;
    for (int e = 0; e < n; e++) {
        lu_v real;
        phi_add(c, phi, lu_ev_virt(g, &edges[e].st, r, &real)->f[1]);
    }
    return (struct override){ id, LU_NOV, o };
}

void
lu_ev_merge(struct ctx *c, struct edge *edges, int n, int pc)
{
    struct gctx *g = c->g;
    if (n == 1) {
        g->cur = edges[0].block;
        g->st = edges[0].st;
        lu_ev_changed(g);
        return;
    }
    int J = lu_ir_block(g->f);
    int d = edges[0].st.depth;
    int nslots = edges[0].st.nlocals[d] + edges[0].st.sp[d];
    lu_v *phis = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots ? nslots : 1));
    lu_v *origin = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots ? nslots : 1));
    int *lead = lu_ir_alloc(g->f, sizeof(int) * (size_t)(nslots ? nslots : 1));
    /* The slots: one that differs between edges becomes a phi. */
    for (int i = 0; i < nslots; i++) {
        lu_v r0 = edges[0].st.v[d][i];
        origin[i] = LU_NOV;
        int same = 1;
        for (int e = 1; e < n; e++) {
            same &= edges[e].st.v[d][i] == r0;
        }
        phis[i] = LU_NOV;
        lead[i] = -1;
        if (same) {
            continue;
        }
        /* Slots that hold the same values on every edge are one value after. */
        int twin = -1;
        for (int k = 0; k < i && twin < 0; k++) {
            int eq = phis[k] != LU_NOV;
            for (int e = 0; e < n && eq; e++) {
                eq = edges[e].st.v[d][k] == edges[e].st.v[d][i];
            }
            twin = eq ? k : -1;
        }
        if (twin >= 0) {
            phis[i] = phis[twin];
            lead[i] = twin;
            continue;
        }
        lead[i] = i;
        int bits = 1;
        for (int e = 0; e < n && bits; e++) {
            g->st = edges[e].st;
            lu_ev_changed(g);
            bits = is_bit(c, edges[e].st.v[d][i]);
        }
        if (bits) {
            /* A bool on every edge (a flag set on some paths): its bit is the phi. */
            phis[i] = new_phi(c, J, REP_BIT, n);
            for (int e = 0; e < n; e++) {
                g->st = edges[e].st;
                lu_ev_changed(g);
                g->cur = edges[e].block;
                phi_add(c, phis[i], lu_ev_truth(c, edges[e].st.v[d][i]));
            }
            continue;
        }
        int rep = REP_NONE;
        for (int e = 0; e < n; e++) {
            g->st = edges[e].st;
            lu_ev_changed(g);
            int re = raw_rep(c, edges[e].st.v[d][i]);
            rep = e == 0 || re == rep ? re : REP_NONE;
            if (rep == REP_NONE) {
                break;
            }
        }
        if (rep != REP_NONE) {
            /* A box on every edge: its value is the phi, and it stays no object. */
            phis[i] = new_phi(c, J, rep, n);
            int came = 0;
            for (int e = 0; e < n; e++) {
                g->st = edges[e].st;
                lu_ev_changed(g);
                g->cur = edges[e].block;
                lu_v r = edges[e].st.v[d][i], real;
                phi_add(c, phis[i], raw_of(c, r, rep));
                came |= LU_IS_VIRT(r) && lu_ev_virt(g, &g->st, r, &real)->n == 2;
            }
            if (came) {
                origin[i] = origin_phi(c, J, phis[i]);  /* a box that was an object on some edge */
            }
            continue;
        }
        phis[i] = new_phi(c, J, REP_OBJ, n);
        I(c, phis[i])->flags |= INS_OWNED;
    }
    /* What the object phis take is made real on each edge. */
    lu_v **in = lu_ir_alloc(g->f, sizeof(lu_v *) * (size_t)n);
    for (int e = 0; e < n; e++) {
        enter_edge(c, &edges[e], pc);
        in[e] = edge_inputs(c, phis, lead, nslots);
        lu_ev_settle(c, pc);
        edges[e].block = g->cur;
        edges[e].st = g->st;
    }
    /* Virtual objects the joined state still reaches mean one thing after
       the join. Making one real may make others real, so until none differs.
       Whether an object differs does not depend on the edge that names it:
       each is looked at once per round. */
    struct override *over = NULL;
    int nover = 0, capover = 0;
    for (int changed = 1; changed;) {
        changed = 0;
        char *seen = lu_ir_alloc(g->f, (size_t)g->f->nvobjs + 1);
        for (int e = 0; e < n && !changed; e++) {
            for (int b = 0; b < edges[e].st.nbinds && !changed; b++) {
                int id = edges[e].st.binds[b].id, done = seen[id], same = 1, reached = 0;
                seen[id] = 1;
                for (int k = 0; k < nover; k++) {
                    done |= over[k].id == id;
                }
                for (int k = 0; k < n && same && !done; k++) {
                    same = same_binding(g, &edges[e].st, &edges[k].st, LU_VIRT(id));
                }
                for (int k = 0; k < n && !same && !done && !reached; k++) {
                    reached = stays_reached(g, &edges[k].st, phis, id);
                }
                if (done || same || !reached) {
                    continue;
                }
                if (nover == capover) {
                    capover = capover ? 2 * capover : 4;
                    struct override *p = lu_ir_alloc(g->f, sizeof(struct override) * (size_t)capover);
                    memcpy(p, over, sizeof(struct override) * (size_t)nover);
                    over = p;
                }
                over[nover++] = reconcile(c, edges, n, pc, id, J);
                changed = 1;
            }
        }
    }
    for (int e = 0; e < n; e++) {
        enter_edge(c, &edges[e], pc);
        edge_take(c, phis, lead, origin, in[e], nslots, pc);
        lu_ev_settle(c, pc);
        lu_ir_edge(g->f, g->cur, J);
        lu_ev_emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        edges[e].st = g->st;
    }
    /* The joined state: the first edge's, with what differed replaced.
       A fact holds after the join when every edge has it. */
    g->st = edges[0].st;
    lu_ev_changed(g);
    g->cur = J;
    for (int k = 0; k < nover; k++) {
        lu_ev_bind(g, &g->st, over[k].id, over[k].real, over[k].obj);
    }
    for (int i = 0; i < nslots; i++) {
        if (phis[i] == LU_NOV) {
            continue;
        }
        if (lead[i] != i) {
            g->st.v[d][i] = g->st.v[d][lead[i]];
        }
        else if (I(c, phis[i])->rep == REP_OBJ) {
            g->st.v[d][i] = phis[i];
        }
        else {
            g->st.v[d][i] = box_of(c, phis[i], origin[i]);
        }
    }
    g->npending = 0;
    lu_ev_changed(g);
    int kept = 0;
    int *at = lu_ir_alloc(g->f, sizeof(int) * (size_t)n);
    memset(at, 0, sizeof(int) * (size_t)n);
    for (int i = 0; i < g->st.nfacts; i++) {
        struct fact fa = g->st.facts[i];
        for (int e = 1; e < n && fa.t != NULL; e++) {
            /* Each edge's facts are sorted by value, as these are. */
            struct fact *other = NULL;
            while (at[e] < edges[e].st.nfacts && edges[e].st.facts[at[e]].v < fa.v) {
                at[e]++;
            }
            if (at[e] < edges[e].st.nfacts && edges[e].st.facts[at[e]].v == fa.v) {
                other = &edges[e].st.facts[at[e]];
            }
            if (other == NULL || other->t != fa.t) {
                fa.t = NULL;
            }
            else if (other->raw != fa.raw) {
                fa.raw = LU_NOV;
            }
        }
        if (fa.t != NULL) {
            g->st.facts[kept++] = fa;
        }
    }
    g->st.nfacts = kept;
}

/* --- loops --- */

/* The locals instructions in [from, to) may rebind. */
static char *
assigned(struct ctx *c, int from, int to)
{
    char *set = lu_ir_alloc(c->g->f, (size_t)c->nlocals + 1);
    for (int pc = from; pc < to;) {
        int base, oparg, op;
        int next = lu_ev_decode(c, pc, &base, &oparg, &op);
        switch (base) {
        case STORE_FAST: case DELETE_FAST: case LOAD_FAST_AND_CLEAR: case MAKE_CELL:
            set[oparg] = 1;
            break;
        case STORE_FAST_LOAD_FAST:
            set[oparg >> 4] = 1;
            break;
        case STORE_FAST_STORE_FAST:
            set[oparg >> 4] = 1;
            set[oparg & 15] = 1;
            break;
        default:
            break;
        }
        pc = next;
    }
    return set;
}

#define MAX_ITERS 64

/* Calls fn on every virtual iterator the innermost frame's slots reach
   that is still no object. */
static void
each_iter(struct ctx *c, lu_v r, int *ids, int *n)
{
    if (!LU_IS_VIRT(r)) {
        return;
    }
    lu_v real;
    struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
    if (o == NULL || real != LU_NOV) {
        return;
    }
    if (o->kind == VK_SEQITER) {
        for (int i = 0; i < *n; i++) {
            if (ids[i] == LU_VIRT_ID(r)) {
                return;
            }
        }
        if (*n == MAX_ITERS) {
            lu_ev_val(c, r);    /* more iterators than a header tracks: this one exists */
            return;
        }
        ids[(*n)++] = LU_VIRT_ID(r);
    }
    for (int i = 0; i < o->n; i++) {
        each_iter(c, o->f[i], ids, n);
    }
}

/* A guard the header made for a loop-carried slot blames the header: when
   it keeps failing, the header stops speculating (nospec). */
static void
blame_header(struct ctx *c, lu_v from, int pc)
{
    for (lu_v v = from; v < c->g->f->nins; v++) {
        if (lu_op_flags[I(c, v)->op] & IF_GUARD) {
            I(c, v)->blame = c->code;
            I(c, v)->blame_pc = pc;
        }
    }
}

/* The raw representation the header gives a slot that holds r, or
   REP_NONE for an object: a box, a constant, an object known to be an int
   or a float, or what an earlier build saw the loop carry there. */
static int
header_rep(struct ctx *c, lu_v r, int pc, int slot)
{
    int hint = lu_ev_hinted(c->g, c->code, pc, slot);
    if (hint == HINT_OBJ || nospec(c, pc)) {
        return REP_NONE;
    }
    if (r == LU_NOV) {
        /* Unbound where the loop starts and a number round it. */
        return hint == HINT_INT ? REP_I64 : hint == HINT_FLOAT ? REP_F64 : REP_NONE;
    }
    int rep = raw_rep(c, r);
    if (rep != REP_NONE) {
        return rep;
    }
    PyTypeObject *t = lu_ev_type(c, r);
    if (LU_IS_VIRT(r)) {
        return REP_NONE;
    }
    return t == &PyLong_Type || (t == NULL && hint == HINT_INT) ? REP_I64
        : t == &PyFloat_Type || (t == NULL && hint == HINT_FLOAT) ? REP_F64 : REP_NONE;
}

/* The loop header at pc, from the state its forward edges joined into. */
static void
build_header(struct ctx *c, struct bblk *bb)
{
    struct gctx *g = c->g;
    int pc = bb->pc, nslots = c->nlocals + *depth_of(c);
    int H = lu_ir_block(g->f);
    g->npending = 0;
    g->observed = 0;
    c->pc = pc;
    lu_ev_begin(c, pc);
    char *set = assigned(c, pc, bb->loop_end);
    bb->slot_kind = lu_ir_alloc(g->f, (size_t)nslots + 1);
    bb->slot_phi = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots + 1));
    bb->slot_ref = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots + 1));
    lu_v *phis = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots + 1));
    lu_v *origin = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots + 1));
    bb->slot_origin = origin;
    lu_v first = g->f->nins;
    if (lu_ev_hinted(g, c->code, pc, -1) == HINT_OBJ) {
        /* An iterator the loop let out exists before it. */
        for (int i = 0; i < nslots; i++) {
            lu_v real;
            lu_v r = slots(c)[i];
            if (LU_IS_VIRT(r) && lu_ev_virt(g, &g->st, r, &real) != NULL
                && lu_ev_virt(g, &g->st, r, &real)->kind == VK_SEQITER) {
                lu_ev_val(c, r);
            }
        }
    }
    for (int i = 0; i < nslots; i++) {
        lu_v r = slots(c)[i];
        phis[i] = origin[i] = LU_NOV;
        bb->slot_kind[i] = SLOT_SAME;
        int fixed = i < c->nlocals && !set[i] && lu_ev_hinted(g, c->code, pc, i) != HINT_OBJ;
        lu_v real = LU_NOV;
        struct lu_vobj *o = LU_IS_VIRT(r) ? lu_ev_virt(g, &g->st, r, &real) : NULL;
        if (fixed || (o != NULL && real == LU_NOV && o->kind == VK_SEQITER)) {
            continue;
        }
        int rep = header_rep(c, r, pc, i);
        if (rep != REP_NONE) {
            phis[i] = new_phi(c, H, rep, 2);
            phi_add(c, phis[i], raw_of(c, r, rep));
            origin[i] = origin_phi(c, H, phis[i]);
            bb->slot_kind[i] = SLOT_RAW;
            continue;
        }
        phis[i] = new_phi(c, H, REP_OBJ, 2);
        I(c, phis[i])->flags |= INS_OWNED;
        bb->slot_kind[i] = SLOT_OBJ;
    }
    blame_header(c, first, pc);
    int *lead = distinct_leads(g, phis, nslots);
    edge_take(c, phis, lead, origin, edge_inputs(c, phis, lead, nslots), nslots, pc);
    /* Iterators the loop steps: the index is a phi, under the same id. */
    int *ids = lu_ir_alloc(g->f, sizeof(int) * (size_t)(MAX_ITERS + 1));
    bb->niter = 0;
    for (int i = 0; i < nslots; i++) {
        each_iter(c, slots(c)[i], ids, &bb->niter);
    }
    bb->iter_id = ids;
    bb->iter_phi = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(bb->niter + 1));
    for (int k = 0; k < bb->niter; k++) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(g, &g->st, LU_VIRT(ids[k]), &real);
        struct lu_vobj *n = lu_ir_vobj(g->f, VK_SEQITER, o->type, 2);
        bb->iter_phi[k] = new_phi(c, H, REP_I64, 2);
        phi_add(c, bb->iter_phi[k], o->f[1]);
        n->f[0] = o->f[0];
        n->f[1] = bb->iter_phi[k];
        lu_ev_bind(g, &g->st, ids[k], LU_NOV, n);
    }
    lu_ev_settle(c, pc);
    lu_ir_edge(g->f, g->cur, H);
    lu_ev_emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    g->cur = H;
    for (int i = 0; i < nslots; i++) {
        if (phis[i] != LU_NOV) {
            slots(c)[i] = I(c, phis[i])->rep == REP_OBJ ? phis[i] : box_of(c, phis[i], origin[i]);
        }
        bb->slot_phi[i] = phis[i];
        bb->slot_ref[i] = slots(c)[i];
    }
    g->npending = 0;
    lu_ev_changed(g);
    bb->ir = H;
    lu_ev_state_copy(g, &bb->hdr, &g->st);
}

/* Whether r is an iterator the loop steps: its index differs from the
   header's by design, and goes to the index's phi. */
static int
stepped(const struct bblk *bb, lu_v r)
{
    for (int k = 0; LU_IS_VIRT(r) && k < bb->niter; k++) {
        if (bb->iter_id[k] == LU_VIRT_ID(r)) {
            return 1;
        }
    }
    return 0;
}

/* A back-edge into the built header: the state it carries is given the
   header's shape, or the header a hint and the build a restart. */
static void
back_edge(struct ctx *c, struct bblk *bb)
{
    struct gctx *g = c->g;
    int pc = bb->pc, nslots = c->nlocals + *depth_of(c);
    g->npending = 0;
    g->observed = 0;
    c->pc = pc;
    lu_ev_begin(c, pc);
    lu_v *phis = bb->slot_phi;
    lu_v first = g->f->nins;
    for (int k = 0; k < bb->niter; k++) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(g, &g->st, LU_VIRT(bb->iter_id[k]), &real);
        if (o == NULL || real != LU_NOV) {
            lu_ev_hint(g, c->code, pc, -1, HINT_OBJ);
            continue;
        }
        phi_add(c, bb->iter_phi[k], o->f[1]);
    }
    for (int i = 0; i < nslots; i++) {
        lu_v r = slots(c)[i];
        if (bb->slot_kind[i] == SLOT_SAME) {
            if (r != bb->slot_ref[i] || !(stepped(bb, r) || same_binding(g, &g->st, &bb->hdr, r))) {
                lu_ev_hint(g, c->code, pc, i, HINT_OBJ);
            }
        }
        else if (bb->slot_kind[i] == SLOT_RAW) {
            int rep = I(c, phis[i])->rep, have = raw_rep(c, r);
            PyTypeObject *t = lu_ev_type(c, r);
            int typed = t == &PyLong_Type ? REP_I64 : t == &PyFloat_Type ? REP_F64 : REP_NONE;
            if (have == rep || typed == rep || r == LU_NOV || (!LU_IS_VIRT(r) && t == NULL)) {
                phi_add(c, phis[i], raw_of(c, r, rep));         /* an unknown object: a guard, the header's */
            }
            else {
                lu_ev_hint(g, c->code, pc, i, HINT_OBJ);
            }
        }
        else if (raw_rep(c, r) != REP_NONE && !LU_IS_VIRT(bb->slot_ref[i]) && lu_ev_const(c, r) == NULL
                 && lu_ev_hinted(g, c->code, pc, i) == 0 && !nospec(c, pc)) {
            /* The loop carries a box where the header took an object. */
            lu_ev_hint(g, c->code, pc, i, raw_rep(c, r) == REP_I64 ? HINT_INT : HINT_FLOAT);
        }
    }
    if (g->restart) {
        g->cur = -1;
        return;
    }
    blame_header(c, first, pc);
    int *lead = distinct_leads(g, phis, nslots);
    edge_take(c, phis, lead, bb->slot_origin, edge_inputs(c, phis, lead, nslots), nslots, pc);
    /* What the phis took was made real here: a slot the header left alone
       must not have changed with it. */
    for (int i = 0; i < nslots; i++) {
        if (bb->slot_kind[i] == SLOT_SAME && !stepped(bb, slots(c)[i])
            && !same_binding(g, &g->st, &bb->hdr, slots(c)[i])) {
            lu_ev_hint(g, c->code, pc, i, HINT_OBJ);
        }
    }
    if (g->restart) {
        g->cur = -1;
        return;
    }
    lu_ev_settle(c, pc);
    lu_ir_edge(g->f, g->cur, bb->ir);
    lu_ev_emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    g->f->loops++;
    g->cur = -1;
}

/* --- edges --- */

/* Control goes on at pc with the current state. */
static void
add_edge(struct ctx *c, int pc)
{
    struct gctx *g = c->g;
    if (g->cur < 0) {
        return;
    }
    struct bblk *bb = block_at(c, pc);
    if (bb->built && bb->loop && bb->slot_kind != NULL) {
        back_edge(c, bb);
        return;
    }
    if (bb->built) {
        /* Code built for another way in (a unit entered inside a loop
           comes round to its entry): the interpreter goes on from here. */
        c->pc = pc;
        lu_ev_begin(c, pc);
        lu_v v = lu_ev_emit(c, OP_DEOPT, REP_NONE, LU_NOV, LU_NOV, LU_NOV, EXIT_SIDE);
        I(c, v)->fs = lu_ev_exit_fs(c);
        g->cur = -1;
        return;
    }
    if (bb - c->bb < c->lo) {
        c->lo = (int)(bb - c->bb);
    }
    if (bb->nin == bb->capin) {
        int cap = bb->capin ? 2 * bb->capin : 2;
        struct edge *p = lu_ir_alloc(g->f, sizeof(struct edge) * (size_t)cap);
        memcpy(p, bb->in, sizeof(struct edge) * (size_t)bb->nin);
        bb->in = p;
        bb->capin = cap;
    }
    bb->in[bb->nin].block = g->cur;
    lu_ev_state_copy(g, &bb->in[bb->nin].st, &g->st);
    bb->nin++;
    g->cur = -1;
}

void
lu_ev_jump(struct ctx *c, int pc)
{
    lu_ev_settle(c, pc);
    add_edge(c, pc);
}

void
lu_ev_fork(struct ctx *c, lu_v bit, int pc_true)
{
    struct gctx *g = c->g;
    if (g->cur < 0) {
        return;
    }
    if (I(c, bit)->op == OP_CONST_I) {
        if (I(c, bit)->imm) {
            lu_ev_jump(c, pc_true);     /* decided: the other arm is not built */
        }
        return;
    }
    int T = lu_ir_block(g->f), F = lu_ir_block(g->f);
    lu_ir_edge(g->f, g->cur, T);
    lu_ir_edge(g->f, g->cur, F);
    lu_ev_emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
    /* What the instruction owes is released on each arm, after the branch. */
    struct state saved;
    lu_ev_state_copy(g, &saved, &g->st);
    int npending = g->npending, observed = g->observed;
    lu_v *pending = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(npending ? npending : 1));
    memcpy(pending, g->pending, sizeof(lu_v) * (size_t)npending);
    g->cur = T;
    lu_ev_jump(c, pc_true);
    g->st = saved;
    lu_ev_changed(g);
    g->npending = 0;
    for (int i = 0; i < npending; i++) {
        lu_ev_drop(c, pending[i]);
    }
    g->observed = observed;
    g->cur = F;
}

void
lu_ev_split(struct ctx *c, lu_v bit, struct split *s)
{
    struct gctx *g = c->g;
    int T = lu_ir_block(g->f);
    s->other = lu_ir_block(g->f);
    s->n = 0;
    lu_ir_edge(g->f, g->cur, T);
    lu_ir_edge(g->f, g->cur, s->other);
    lu_ev_emit(c, OP_BRANCH, REP_NONE, bit, LU_NOV, LU_NOV, 0);
    lu_ev_state_copy(g, &s->saved, &g->st);
    s->pc = c->pc;
    s->sp = c->snap_sp;
    s->npending = g->npending;
    s->observed = g->observed;
    s->pending = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(g->npending ? g->npending : 1));
    memcpy(s->pending, g->pending, sizeof(lu_v) * (size_t)g->npending);
    g->cur = T;
}

/* A way of a split instruction reached the instruction's end. */
static void
split_arm(struct ctx *c, struct split *s)
{
    struct gctx *g = c->g;
    if (g->cur >= 0 && !g->restart) {
        lu_ev_settle(c, c->next);
    }
    if (g->cur >= 0 && !g->restart) {
        s->arms[s->n].block = g->cur;
        lu_ev_state_copy(g, &s->arms[s->n].st, &g->st);
        s->n++;
    }
}

void
lu_ev_split_else(struct ctx *c, struct split *s)
{
    struct gctx *g = c->g;
    split_arm(c, s);
    g->st = s->saved;
    lu_ev_changed(g);
    /* The instruction begins again: a way that split left the frame at
       the instruction's end (its join's). */
    c->pc = s->pc;
    lu_ev_begin(c, s->pc);
    c->snap_sp = s->sp;
    g->npending = 0;
    for (int i = 0; i < s->npending; i++) {
        lu_ev_drop(c, s->pending[i]);
    }
    g->observed = s->observed;
    g->cur = s->other;
}

void
lu_ev_split_end(struct ctx *c, struct split *s)
{
    struct gctx *g = c->g;
    split_arm(c, s);
    if (s->n == 0 || g->restart) {
        g->cur = -1;
        return;
    }
    lu_ev_merge(c, s->arms, s->n, c->next);
}

void
lu_ev_branch(struct ctx *c, lu_v bit, int pc_true, int pc_false)
{
    lu_ev_fork(c, bit, pc_true);
    lu_ev_jump(c, pc_false);
}

/* --- the walk --- */

/* Builds the block: joins its edges, then evaluates instructions until
   one ends the path or the next block starts. */
static int
build_block(struct ctx *c, struct bblk *bb)
{
    struct gctx *g = c->g;
    bb->built = 1;
    lu_ev_merge(c, bb->in, bb->nin, bb->pc);
    if (bb->loop) {
        build_header(c, bb);
    }
    for (int pc = bb->pc; g->cur >= 0;) {
        if (pc != bb->pc && c->blkat[pc] >= 0) {
            add_edge(c, pc);
            break;
        }
        if (pc >= c->ncode) {
            g->f->fail = "code that runs off its end";
            return -1;
        }
        c->pc = pc;
        c->next = lu_ev_decode(c, pc, &c->base, &c->oparg, &c->opcode);
        c->cache = lu_code_start(c->code) + c->next - lu_opcode_caches[c->base];
        lu_ev_begin(c, pc);
        int next = c->next;
        if (lu_ev_instruction(c) < 0 || g->f->fail != NULL) {
            if (g->f->fail == NULL) {
                g->f->fail = "an instruction failed to build";
            }
            return -1;
        }
        if (g->restart) {
            return 0;
        }
        lu_ev_settle(c, next);
        pc = next;
    }
    return 0;
}

int
lu_ev_build(struct ctx *c, int pc)
{
    struct gctx *g = c->g;
    c->ncode = (int)Py_SIZE(c->code);
    c->nlocals = c->code->co_nlocalsplus;
    if (scan(c) < 0) {
        g->f->fail = "unfollowable jumps";
        return -1;
    }
    add_edge(c, pc);
    for (;;) {
        /* The lowest block control reaches that is not built: blocks are
           in order of their pc but for the entry's, which is built first,
           and an edge to one below the cursor moves it back. */
        while (c->lo < c->nbb && (c->bb[c->lo].built || c->bb[c->lo].nin == 0)) {
            c->lo++;
        }
        if (c->lo == c->nbb || g->restart) {
            return 0;
        }
        if (build_block(c, &c->bb[c->lo]) < 0) {
            return -1;
        }
    }
}

/* --- the unit --- */

static void
reset(struct lu_func *f)
{
    PyCodeObject *code = f->code;
    int entry = f->entry_pc, stress = f->stress, budget = f->budget;
    lu_ir_free(f);
    memset(f, 0, sizeof(*f));
    f->code = code;
    f->entry_pc = entry;
    f->stress = stress;
    f->budget = budget;
}

static int
build_root(struct gctx *g)
{
    struct lu_func *f = g->f;
    struct lu_frame *frame = g->live;
    PyCodeObject *co = frame->code;
    struct ctx root = { .funcv = LU_NOV }, *c = &root;
    c->g = g;
    c->code = co;
    c->func = PyFunction_Check(frame->func) ? (PyFunctionObject *)frame->func : NULL;
    c->globals = frame->globals;
    c->builtins = frame->builtins;
    c->kernel = (co->co_flags & LU_CO_KERNEL) != 0;
    c->ncode = (int)Py_SIZE(co);
    c->nlocals = co->co_nlocalsplus;
    int entry = f->entry_pc;
    g->cur = lu_ir_block(f);
    g->st.depth = -1;
    lu_ev_state_push(g, c->nlocals, c->nlocals + co->co_stacksize + 2);
    c->pc = entry;
    /* At a call's entry every local but the parameters (and the cells the
       prologue made) is unbound. */
    int params = co->co_argcount + co->co_kwonlyargcount + ((co->co_flags & CO_VARARGS) != 0)
        + ((co->co_flags & CO_VARKEYWORDS) != 0);
    int call = entry == lu_jit_call_entry(co);
    for (int pc = 0; call && pc < c->ncode;) {
        /* Unless a jump comes there too: then the unit is a loop's entry
           as well, and its locals are whatever the frame holds. */
        int base, oparg, op;
        int next = lu_ev_decode(c, pc, &base, &oparg, &op);
        call = !((is_jump(base) || is_branch(base)) && lu_ev_jump_target(base, oparg, next) == entry);
        pc = next;
    }
    for (int i = 0; i < c->nlocals; i++) {
        int plain = co->co_localspluskinds == NULL
            || !(((unsigned char *)PyBytes_AS_STRING(co->co_localspluskinds))[i] & (CO_FAST_CELL | CO_FAST_FREE));
        if (!(call && i >= params && plain)) {
            slots(c)[i] = lu_ev_own(c, lu_ev_emit(c, OP_ENTRY_LOCAL, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, i));
        }
    }
    int depth = (int)(frame->stackpointer - (frame->localsplus + c->nlocals));
    for (int j = 0; j < depth; j++) {
        lu_ev_push(c, lu_ev_own(c, lu_ev_emit(c, OP_ENTRY_STACK, REP_OBJ, LU_NOV, LU_NOV, LU_NOV, j)));
    }
    g->npending = 0;            /* the slots name them all */
    lu_ev_changed(g);
    lu_ev_begin(c, entry);
    /* The interpreter would push each inlined frame at its call, and raise
       there past the recursion limit: one check at entry that there is
       room for the deepest leaves the unit to the interpreter when there
       is not, and an exit never lacks it. */
    lu_v room = lu_ev_guard(c, OP_GUARD_DEPTH, LU_NOV, 0);
    if (!lu_jit_nospec(co, entry)) {
        /* Locals the live frame holds as ints or floats are taken to be
           ints or floats: their raw values are at hand from here on. */
        for (int i = 0; i < c->nlocals; i++) {
            PyObject *o = frame->localsplus[i];
            lu_v v = slots(c)[i];
            int cell = co->co_localspluskinds != NULL
                && (((unsigned char *)PyBytes_AS_STRING(co->co_localspluskinds))[i] & (CO_FAST_CELL | CO_FAST_FREE));
            if (o == NULL || v == LU_NOV || cell) {
                continue;
            }
            if (PyLong_CheckExact(o) && _PyLong_IsCompact((PyLongObject *)o)) {
                lu_ev_unbox_int(c, v);
            }
            else if (PyFloat_CheckExact(o)) {
                lu_ev_unbox_float(c, v);
            }
        }
    }
    int r = lu_ev_build(c, entry);
    f->ins[room].imm = g->maxdepth;
    if (g->maxdepth == 0) {
        f->ins[room].flags |= INS_DEAD;
    }
    return r;
}

int
lu_ssa_build(struct lu_func *f, struct lu_frame *frame)
{
    struct gctx g = { .f = f, .live = frame };
    int r = -1;
    for (int tries = 0;; tries++) {
        struct hint *hints = g.hints;
        int nhints = g.nhints, caphints = g.caphints, budget = f->budget;
        reset(f);
        g = (struct gctx){ .f = f, .live = frame, .hints = hints, .nhints = nhints, .caphints = caphints,
                           .budget = budget, .keep = LU_NOV, .learned = lu_jit_prof_learned(f->code) };
        r = build_root(&g);
        if (r < 0 || !g.restart) {
            break;
        }
        if (tries > 2 * g.nhints + 2) {
            f->fail = "loops whose shapes do not settle";
            r = -1;
            break;
        }
    }
    free(g.hints);
    if (r < 0 && f->fail == NULL) {
        f->fail = "build failed";
    }
    return r;
}
