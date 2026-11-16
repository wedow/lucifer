/* Control flow for the evaluator (eval.h): blocks of bytecode, the edges
   between them, joins, loops, and the walk over a frame's code.

   A block starts where a jump lands or a branch falls through. Control
   reaches it over edges, each an open graph block and a state. A block is
   built once, lowest instruction first, so every forward edge to it is
   there when it is: one edge goes on in its own graph block, several are
   joined (merge). What a join or a loop header may replace with a phi is
   a place: a slot of the frame, or a field of a virtual object. A slot
   that differs between edges becomes a phi. A virtual object the edges
   know differently stays virtual when no edge made it and its records
   have one layout: each field that differs is a place of the join, and
   gets a phi as a slot does. Otherwise it is made real on every edge.

   A loop header is built from its forward edges before its back-edges
   exist. It makes a phi for each slot the loop may change: a raw one
   where the slot holds an int or a float (a box that never exists), an
   object's otherwise. A virtual object the loop may change is carried
   round it virtual, its fields places of the header: a field that is a
   raw word (an iterator's index) is a phi, the others stay what they are
   until a back-edge shows one changing. A back-edge must fit what the
   header made. One that does not records a hint for the header (the
   place gets a phi, a raw one for a box the loop carries, or an object;
   an object that came back made, or in another layout, exists before
   the loop) and the whole build starts over with it: nothing is undone
   piecemeal, and each rebuild knows strictly more. */
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
   reads until its entry needs a block of its own (block_at). A frame
   entered where a loop may already be running (a generator's, where it
   resumes) gets a second set: the way from the entry to the first loop
   header is built on blocks of its own, so that the loop, when it comes
   to the same code, builds it as its own and joins nothing with the way
   in. c->loops holds the blocks the loops are built on meanwhile. */
static int
scan(struct ctx *c, int entered_inside)
{
    const struct lu_ev_scan *s = scan_of(c->g, c->code);
    if (!s->ok) {
        return -1;
    }
    const int *bbs = s->data + s->ncode + 1;
    c->blkat = s->data;
    c->nbb = s->nbb;
    c->lo = 0;
    for (int way = 0; way < 2; way++) {
        struct bblk *bb = lu_ir_alloc(c->g->f, sizeof(struct bblk) * (size_t)(s->nbb + 2));
        for (int i = 0; i < s->nbb; i++) {
            bb[i].pc = bbs[2 * i];
            bb[i].loop = bbs[2 * i + 1] != 0;
            bb[i].loop_end = bbs[2 * i + 1];
        }
        c->loops = way == 0 && entered_inside ? bb : c->loops;
        c->bb = bb;
        if (!entered_inside) {
            break;
        }
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
lu_ev_hint(struct gctx *g, PyCodeObject *code, int pc, uint64_t place, int kind)
{
    for (int i = 0; i < g->nhints; i++) {
        struct hint *h = &g->hints[i];
        if (h->code == code && h->pc == pc && h->place == place) {
            /* Stronger than what was known, or two shapes: an object. */
            int now = h->kind == kind || kind == HINT_VARIES ? h->kind : h->kind == HINT_VARIES ? kind : HINT_OBJ;
            if (now == h->kind) {
                g->f->fail = "a loop whose shape does not settle";      /* the header had the hint and did not fit */
            }
            h->kind = now;
            g->restart = 1;
            return;
        }
    }
    if (g->nhints == g->caphints) {
        g->caphints = g->caphints ? 2 * g->caphints : 16;
        g->hints = realloc(g->hints, sizeof(struct hint) * (size_t)g->caphints);
    }
    g->hints[g->nhints++] = (struct hint){ code, pc, kind, place };
    g->restart = 1;
}

int
lu_ev_hinted(struct gctx *g, PyCodeObject *code, int pc, uint64_t place)
{
    for (int i = 0; i < g->nhints; i++) {
        struct hint *h = &g->hints[i];
        if (h->code == code && h->pc == pc && h->place == place) {
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

static lu_v new_phi(struct ctx *c, int block, int rep);

/* The phi beside a raw phi that carries the object its value was, where
   an edge has one (NULL elsewhere), and the box of the two. */
static lu_v
origin_phi(struct ctx *c, int block, lu_v raw)
{
    lu_v phi = new_phi(c, block, REP_OBJ);
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
new_phi(struct ctx *c, int block, int rep)
{
    struct lu_func *f = c->g->f;
    lu_v phi = lu_ir_emit(f, block, OP_PHI, rep, LU_NOV, LU_NOV, LU_NOV, 0);
    f->ins[phi].src = c->code;
    f->ins[phi].src_pc = c->pc;
    return phi;
}

static void
phi_add(struct ctx *c, lu_v phi, lu_v arg)
{
    struct lu_ins *in = I(c, phi);
    if (in->nargs == 0 || (in->nargs >= 2 && (in->nargs & (in->nargs - 1)) == 0)) {
        /* Full: room for twice as many (2, 4, 8 ...), in the arena. */
        lu_v *args = lu_ir_alloc(c->g->f, sizeof(lu_v) * (size_t)(in->nargs ? 2 * in->nargs : 2));
        memcpy(args, in->args, sizeof(lu_v) * (size_t)in->nargs);
        in->args = args;
    }
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

/* --- places --- */

static void *
regrow(struct gctx *g, const void *old, int n, int cap, size_t size)
{
    void *p = lu_ir_alloc(g->f, size * (size_t)(cap ? cap : 1));
    if (n > 0) {
        memcpy(p, old, size * (size_t)n);
    }
    return p;
}

static int
place_add(struct gctx *g, struct places *P, int id, int at)
{
    if (P->n == P->cap) {
        int cap = P->cap ? 2 * P->cap : 16;
        P->at = regrow(g, P->at, P->n, cap, sizeof(struct place));
        P->kind = regrow(g, P->kind, P->n, cap, 1);
        P->phi = regrow(g, P->phi, P->n, cap, sizeof(lu_v));
        P->origin = regrow(g, P->origin, P->n, cap, sizeof(lu_v));
        P->lead = regrow(g, P->lead, P->n, cap, sizeof(int));
        P->cap = cap;
    }
    P->at[P->n] = (struct place){ id, at };
    P->kind[P->n] = PLACE_SAME;
    P->phi[P->n] = P->origin[P->n] = LU_NOV;
    P->lead[P->n] = -1;
    return P->n++;
}

/* The slots of c's frame, each a place nothing is made of yet. */
static void
places_of_slots(struct ctx *c, struct places *P, int nslots)
{
    *P = (struct places){ 0 };
    for (int i = 0; i < nslots; i++) {
        place_add(c->g, P, -1, i);
    }
}

/* Virtual object id's record in the state while nothing has made it, else NULL. */
static struct lu_vobj *
unmade_in(struct gctx *g, const struct state *st, int id)
{
    lu_v real;
    struct lu_vobj *o = lu_ev_virt(g, st, LU_VIRT(id), &real);
    return real == LU_NOV ? o : NULL;
}

/* What the place holds in the state; nothing for a field of an object
   that was made. */
static lu_v
place_get(struct gctx *g, const struct state *st, struct place pl)
{
    if (pl.id < 0) {
        return st->v[st->depth][pl.at];
    }
    struct lu_vobj *o = unmade_in(g, st, pl.id);
    return o != NULL && pl.at < o->n ? o->f[pl.at] : LU_NOV;
}

/* How a field's value can meet another across a join: not at all (the
   field is unset), as a raw word of its representation, or as an object. */
enum { FIELD_UNSET = 0, FIELD_OBJ = REP_OBJ };

static int
field_class(struct ctx *c, lu_v v)
{
    if (v == LU_NOV || (v >= 0 && I(c, v)->op == OP_CONST && I(c, v)->imm == 0)) {
        return FIELD_UNSET;
    }
    return v >= 0 ? I(c, v)->rep : FIELD_OBJ;
}

/* A generator's fields are its frame's slots: one that holds nothing is
   an object's place all the same. */
static int
place_class(struct ctx *c, const struct lu_vobj *o, lu_v v)
{
    int cls = field_class(c, v);
    return cls == FIELD_UNSET && o->kind == VK_GEN ? FIELD_OBJ : cls;
}

/* What each phi takes on the current edge, for the places from `from`
   on: the object an object phi takes (made real here if it was not), the
   object a raw phi's value stands for if there is one, or LU_NOV. */
static void
edge_inputs(struct ctx *c, const struct places *P, int from, lu_v *in)
{
    struct gctx *g = c->g;
    for (int p = from; p < P->n; p++) {
        in[p] = LU_NOV;
        if (P->lead[p] != p || P->kind[p] == PLACE_DIRECT || P->kind[p] == PLACE_VIRT) {
            continue;
        }
        lu_v r = place_get(g, &g->st, P->at[p]);
        if (P->kind[p] == PLACE_OBJ) {
            in[p] = r == LU_NOV ? lu_ev_konst(c, NULL) : lu_ev_val(c, r);
        }
        else if (LU_IS_VIRT(r)) {
            /* The object the box became here, or the one it came from. */
            struct lu_vobj *o = lu_ev_virt(g, &g->st, r, &in[p]);
            if (in[p] == LU_NOV && o != NULL && o->n == 2) {
                in[p] = o->f[1];
            }
        }
        else if (r == LU_NOV) {
            in[p] = lu_ev_konst(c, &lu_jit_unbound);
        }
        else if (lu_ev_const(c, r) == NULL) {
            in[p] = r;          /* the object whose raw value the phi takes */
        }
    }
}

/* Whether place p comes round a loop unchanged: what its phi would take
   is the phi itself. The place goes on naming it, with the reference it has. */
static int
carried(const struct places *P, const lu_v *in, int p)
{
    return P->phi[p] != LU_NOV && in[p] != LU_NOV && in[p] == (P->kind[p] == PLACE_OBJ ? P->phi[p] : P->origin[p]);
}

/* The state at pc as an exit would read it, without what the phis take
   over on this edge: the slots among them hold nothing, and the virtual
   objects among whose fields they are have records without them. */
static struct lu_fs *
rest_of(struct ctx *c, const struct places *P, const lu_v *in, int pc)
{
    struct gctx *g = c->g;
    struct lu_fs *rest = lu_ev_snapshot(c, pc);
    struct lu_vobj *copy = NULL;
    int copied = -1, own = 0;
    for (int p = 0; p < P->n; p++) {
        struct place pl = P->at[p];
        if (P->phi[p] == LU_NOV || P->kind[p] == PLACE_DIRECT || carried(P, in, p)) {
            continue;
        }
        if (pl.id < 0) {
            *lu_fs_slot(rest, pl.at) = LU_NOV;
            continue;
        }
        if (copied != pl.id) {
            /* An object's fields are places in a row: its record is copied once. */
            lu_v real;
            struct lu_vobj *o = lu_fs_vobj(g->f, rest, pl.id, &real);
            if (o == NULL || real != LU_NOV) {
                continue;
            }
            copy = lu_ir_vobj(g->f, o->kind, o->type, o->n);
            copy->neg = o->neg;
            memcpy(copy->f, o->f, sizeof(lu_v) * (size_t)o->n);
            if (!own) {
                rest->binds = regrow(g, rest->binds, rest->nbinds, rest->nbinds + P->n, sizeof(struct lu_bind));
                own = 1;
            }
            int b = rest->nbinds - 1;
            while (b >= 0 && rest->binds[b].id != pl.id) {
                b--;
            }
            b = b >= 0 ? b : rest->nbinds++;
            rest->binds[b] = (struct lu_bind){ pl.id, LU_NOV, copy };
            copied = pl.id;
        }
        copy->f[pl.at] = LU_NOV;
    }
    return rest;
}

/* The phis take their inputs. An object phi takes a reference: the
   value's own when no place left names it and no earlier phi took it, a
   new one otherwise. A raw phi's inputs were added by its maker; the
   object its value stood for on this edge goes to the phi beside it
   (origin, which keeps `is` true of a value that was an object before
   the join), or is released when there is none and nothing names it. A
   place a loop carries unchanged gives nothing up: it is one of the
   places left. */
static void
edge_take(struct ctx *c, const struct places *P, const lu_v *in, int pc)
{
    struct gctx *g = c->g;
    lu_ev_names_walk(g, rest_of(c, P, in, pc));
    for (int p = 0; p < P->n; p++) {
        lu_v x = in[p], phi = P->phi[p], origin = P->origin[p];
        if (x == LU_NOV) {
            if (P->lead[p] == p && origin != LU_NOV) {
                phi_add(c, origin, lu_ev_konst(c, NULL));       /* no object yet on this edge */
            }
            continue;
        }
        if (carried(P, in, p)) {
            phi_add(c, P->kind[p] == PLACE_OBJ ? phi : origin, x);
            continue;
        }
        int owned = (I(c, x)->flags & INS_OWNED) != 0;
        int free_now = owned && !lu_ev_names(g, x);
        for (int k = 0; k < p && free_now; k++) {
            free_now = in[k] != x;      /* an earlier phi took it, or it was released */
        }
        if (P->kind[p] != PLACE_OBJ) {
            if (origin != LU_NOV) {
                phi_add(c, origin, x);
                if (owned && !free_now) {
                    lu_ev_retain(c, x);
                }
            }
            else if (free_now) {
                lu_ev_emit(c, OP_DECREF, REP_NONE, x, LU_NOV, LU_NOV, 0);       /* an int or a float */
            }
            continue;
        }
        phi_add(c, phi, x);
        if ((I(c, phi)->flags & INS_OWNED) && !free_now) {
            lu_ev_retain(c, x);
        }
    }
}

/* What each place holds after the join: its phi, the box of a raw one
   (places that share a phi share the box), or LU_NOV where it holds what
   it held. In the joined state. */
static lu_v *
joined_values(struct ctx *c, const struct places *P)
{
    lu_v *out = lu_ir_alloc(c->g->f, sizeof(lu_v) * (size_t)(P->n ? P->n : 1));
    for (int p = 0; p < P->n; p++) {
        out[p] = P->phi[p] == LU_NOV ? LU_NOV : P->lead[p] != p ? out[P->lead[p]]
            : P->kind[p] == PLACE_RAW ? box_of(c, P->phi[p], P->origin[p]) : P->phi[p];
    }
    return out;
}

/* A record like o, its own. */
static struct lu_vobj *
record_copy(struct gctx *g, const struct lu_vobj *o)
{
    struct lu_vobj *n = lu_ir_vobj(g->f, o->kind, o->type, o->n);
    n->neg = o->neg;
    memcpy(n->f, o->f, sizeof(lu_v) * (size_t)o->n);
    return n;
}

/* --- joins --- */

/* What the joined state knows of a virtual object the edges knew
   differently: the phi of the objects it was made on each edge, or the
   record whose differing fields are places of the join. */
struct override {
    int id;
    lu_v real;
    struct lu_vobj *obj;
};

struct join {
    struct edge *edges;
    int n, pc, J, nslots;
    struct places P;
    struct override *one;       /* the tuples the join made one of (PLACE_VIRT): their ids and records */
    int none, capone;
};

/* The edge's state and block are the current ones, for what a value is
   there; no instruction begins. */
static void
on_edge(struct ctx *c, struct edge *e)
{
    c->g->st = e->st;
    lu_ev_changed(c->g);
    c->g->cur = e->block;
}

static lu_v *
edge_values(struct ctx *c, struct join *j, struct place pl)
{
    lu_v *vals = lu_ir_alloc(c->g->f, sizeof(lu_v) * (size_t)j->n);
    for (int e = 0; e < j->n; e++) {
        vals[e] = place_get(c->g, &j->edges[e].st, pl);
    }
    return vals;
}

/* The representation of the phi for objects that are vals[e] on edge e:
   REP_BIT for a bool that is no object on every edge (a flag set on some
   paths), a raw one for a box of one kind on every edge, else REP_OBJ. */
static int
join_rep(struct ctx *c, struct join *j, const lu_v *vals)
{
    int bits = 1, rep = REP_NONE;
    for (int e = 0; e < j->n && (bits || e == 0 || rep != REP_NONE); e++) {
        on_edge(c, &j->edges[e]);
        bits = bits && is_bit(c, vals[e]);
        int re = raw_rep(c, vals[e]);
        rep = e == 0 || re == rep ? re : REP_NONE;
    }
    return bits ? REP_BIT : rep != REP_NONE ? rep : REP_OBJ;
}

static int
reaches_id(struct gctx *g, const struct state *st, lu_v r, int id)
{
    if (!LU_IS_VIRT(r)) {
        return 0;
    }
    if (LU_VIRT_ID(r) == id) {
        return 1;
    }
    struct lu_vobj *o = unmade_in(g, st, LU_VIRT_ID(r));
    for (int k = 0; o != NULL && k < o->n; k++) {
        if (reaches_id(g, st, o->f[k], id)) {
            return 1;
        }
    }
    return 0;
}

static void join_place(struct ctx *c, struct join *j, int p);

/* Whether the place holds, on every edge, a tuple nothing has made that
   only this place and the slots that hold the very same values reach:
   tuples of one size, a different one on some edges (two arms each built
   their result). They are one tuple after the join, virtual still, whose
   items that differ are places of the join; nothing can tell it from the
   edge's own, which nothing else names. Then the place is given it. */
static int
join_tuples(struct ctx *c, struct join *j, int p, const lu_v *vals)
{
    struct gctx *g = c->g;
    struct places *P = &j->P;
    struct lu_vobj *first = NULL;
    for (int e = 0; e < j->n; e++) {
        const struct state *st = &j->edges[e].st;
        struct lu_vobj *o = LU_IS_VIRT(vals[e]) ? unmade_in(g, st, LU_VIRT_ID(vals[e])) : NULL;
        if (o == NULL || o->kind != VK_TUPLE || (first != NULL && o->n != first->n)) {
            return 0;
        }
        first = first != NULL ? first : o;
        for (int d = 0; d <= st->depth; d++) {
            for (int i = 0; i < st->nlocals[d] + st->sp[d]; i++) {
                int same = d == st->depth;
                for (int k = 0; k < j->n && same; k++) {
                    same = j->edges[k].st.v[d][i] == vals[k];
                }
                if (!same && reaches_id(g, st, st->v[d][i], LU_VIRT_ID(vals[e]))) {
                    return 0;
                }
            }
        }
    }
    struct lu_vobj *one = lu_ir_vobj(g->f, VK_TUPLE, first->type, first->n);
    int id = lu_ir_vobj_id(g->f, one);
    P->kind[p] = PLACE_VIRT;
    P->phi[p] = LU_VIRT(id);
    if (j->none == j->capone) {
        j->capone = j->capone ? 2 * j->capone : 4;
        j->one = regrow(g, j->one, j->none, j->capone, sizeof(struct override));
    }
    j->one[j->none++] = (struct override){ id, LU_NOV, one };
    /* On each edge the new tuple is the edge's own, for what its items are there. */
    for (int e = 0; e < j->n; e++) {
        lu_ev_bind(g, &j->edges[e].st, id, LU_NOV, unmade_in(g, &j->edges[e].st, LU_VIRT_ID(vals[e])));
    }
    for (int k = 0; k < one->n; k++) {
        lu_v *items = edge_values(c, j, (struct place){ id, k });
        int same = 1;
        for (int e = 1; e < j->n; e++) {
            same &= items[e] == items[0];
        }
        one->f[k] = items[0];
        if (!same) {
            join_place(c, j, place_add(g, P, id, k));
        }
    }
    return 1;
}

/* Place p, which differs between the edges, gets its phi. An object phi
   takes its inputs later (edge_take), once each edge has made them. */
static void
join_place(struct ctx *c, struct join *j, int p)
{
    struct gctx *g = c->g;
    struct places *P = &j->P;
    lu_v *vals = edge_values(c, j, P->at[p]);
    /* Places that hold the same values on every edge are one value after. */
    for (int k = 0; k < p; k++) {
        int eq = P->lead[k] == k;
        for (int e = 0; e < j->n && eq; e++) {
            eq = place_get(g, &j->edges[e].st, P->at[k]) == vals[e];
        }
        if (eq) {
            P->kind[p] = P->kind[k];
            P->phi[p] = P->phi[k];
            P->lead[p] = k;
            return;
        }
    }
    P->lead[p] = p;
    if (vals[0] >= 0 && I(c, vals[0])->rep != REP_OBJ) {
        P->kind[p] = PLACE_DIRECT;
        P->phi[p] = new_phi(c, j->J, I(c, vals[0])->rep);
        for (int e = 0; e < j->n; e++) {
            phi_add(c, P->phi[p], vals[e]);
        }
        return;
    }
    int rep = join_rep(c, j, vals);
    if (rep == REP_OBJ && join_tuples(c, j, p, vals)) {
        return;
    }
    P->phi[p] = new_phi(c, j->J, rep);
    if (rep == REP_OBJ) {
        P->kind[p] = PLACE_OBJ;
        I(c, P->phi[p])->flags |= INS_OWNED;
        return;
    }
    P->kind[p] = PLACE_RAW;
    int came = 0;
    for (int e = 0; e < j->n; e++) {
        on_edge(c, &j->edges[e]);
        lu_v r = vals[e], real;
        phi_add(c, P->phi[p], rep == REP_BIT ? lu_ev_truth(c, r) : raw_of(c, r, rep));
        came |= rep != REP_BIT && LU_IS_VIRT(r) && lu_ev_virt(g, &g->st, r, &real)->n == 2;
    }
    if (came) {
        P->origin[p] = origin_phi(c, j->J, P->phi[p]);  /* a box that was an object on some edge */
    }
}

/* Whether every edge knows virtual object id the same: one record, one
   object made. */
static int
same_on_edges(struct ctx *c, struct join *j, int id)
{
    lu_v r0, r;
    struct lu_vobj *o0 = lu_ev_virt(c->g, &j->edges[0].st, LU_VIRT(id), &r0);
    for (int e = 1; e < j->n; e++) {
        if (lu_ev_virt(c->g, &j->edges[e].st, LU_VIRT(id), &r) != o0 || r != r0) {
            return 0;
        }
    }
    return 1;
}

/* Virtual object id, which the edges know differently, as it can stay
   virtual after the join (its record on the first edge), or NULL: no edge
   made it, and its records have one layout, so that each field that
   differs can be a phi. */
static struct lu_vobj *
stays_virtual(struct ctx *c, struct join *j, int id)
{
    struct gctx *g = c->g;
    struct lu_vobj *first = unmade_in(g, &j->edges[0].st, id);
    if (first == NULL || !LU_VKIND_MUTABLE(first->kind)) {
        return NULL;
    }
    for (int e = 1; e < j->n; e++) {
        struct lu_vobj *o = unmade_in(g, &j->edges[e].st, id);
        if (o == NULL || o->kind != first->kind || o->n != first->n || o->type != first->type || o->neg != first->neg) {
            return NULL;
        }
        for (int k = 0; k < o->n; k++) {
            if (o->f[k] != first->f[k]
                && ((o->kind == VK_DICT && !(k & 1)) || place_class(c, o, o->f[k]) == FIELD_UNSET
                    || place_class(c, o, o->f[k]) != place_class(c, o, first->f[k]))) {
                return NULL;
            }
        }
    }
    return first;
}

/* Whether field k of virtual object id differs between the edges while
   the object can stay virtual: the join makes a phi of it. */
static int
field_differs(struct ctx *c, struct join *j, int id, int k)
{
    struct lu_vobj *first = stays_virtual(c, j, id);
    for (int e = 1; first != NULL && e < j->n; e++) {
        if (unmade_in(c->g, &j->edges[e].st, id)->f[k] != first->f[k]) {
            return 1;
        }
    }
    return 0;
}

static int
reaches_past(struct ctx *c, struct join *j, const struct state *st, lu_v r, int id)
{
    if (!LU_IS_VIRT(r)) {
        return 0;
    }
    if (LU_VIRT_ID(r) == id) {
        return 1;
    }
    struct lu_vobj *o = unmade_in(c->g, st, LU_VIRT_ID(r));
    for (int k = 0; o != NULL && k < o->n; k++) {
        if (LU_IS_VIRT(o->f[k]) && !field_differs(c, j, LU_VIRT_ID(r), k) && reaches_past(c, j, st, o->f[k], id)) {
            return 1;
        }
    }
    return 0;
}

/* Whether some edge's state reaches virtual object id through places
   that stay: not through a slot that has a phi, nor through a field that
   will get one. */
static int
stays_reached(struct ctx *c, struct join *j, int id)
{
    for (int e = 0; e < j->n; e++) {
        const struct state *st = &j->edges[e].st;
        for (int d = 0; d <= st->depth; d++) {
            for (int i = 0; i < st->nlocals[d] + st->sp[d]; i++) {
                if (!(d == st->depth && j->P.phi[i] != LU_NOV) && reaches_past(c, j, st, st->v[d][i], id)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Something is made on edge e, before the instruction the join is at. */
static void
edge_begin(struct ctx *c, struct join *j, int e)
{
    enter_edge(c, &j->edges[e], j->pc);
}

static void
edge_end(struct ctx *c, struct join *j, int e)
{
    lu_ev_settle(c, j->pc);
    j->edges[e].block = c->g->cur;
    j->edges[e].st = c->g->st;
}

/* Virtual object id, which stays virtual: what its fields' object phis
   will take is made real on each edge now, so that nothing is made once
   the phis exist. Whether anything was. */
static int
fields_made(struct ctx *c, struct join *j, int id)
{
    struct gctx *g = c->g;
    struct lu_vobj *first = unmade_in(g, &j->edges[0].st, id);
    int n = first->n, made = 0;
    for (int k = 0; k < n && !made; k++) {
        lu_v *vals = edge_values(c, j, (struct place){ id, k });
        if (!field_differs(c, j, id, k) || place_class(c, first, vals[0]) != FIELD_OBJ || join_rep(c, j, vals) != REP_OBJ) {
            continue;
        }
        for (int e = 0; e < j->n; e++) {
            if (LU_IS_VIRT(vals[e]) && unmade_in(g, &j->edges[e].st, LU_VIRT_ID(vals[e])) != NULL) {
                edge_begin(c, j, e);
                lu_ev_val(c, vals[e]);
                edge_end(c, j, e);
                made = 1;
            }
        }
    }
    return made;
}

/* Virtual object id, which cannot stay virtual, is an object on every
   edge: a phi of them, which takes each edge's. */
static struct override
made_real(struct ctx *c, struct join *j, int id)
{
    struct gctx *g = c->g;
    lu_v r = LU_VIRT(id), real;
    struct lu_vobj *first = NULL;
    for (int e = 0; e < j->n && first == NULL; e++) {
        first = lu_ev_virt(g, &j->edges[e].st, r, &real);
    }
    lu_v phi = new_phi(c, j->J, REP_OBJ);
    int owned = 0;
    PyTypeObject *type = NULL;
    for (int e = 0; e < j->n; e++) {
        edge_begin(c, j, e);
        lu_v x = lu_ev_val(c, r);
        type = e == 0 || type == lu_ev_type(c, x) ? lu_ev_type(c, x) : NULL;
        owned |= (I(c, x)->flags & INS_OWNED) != 0;
        phi_add(c, phi, x);
        g->keep = x;            /* the phi's now, even where this edge's state does not reach it */
        edge_end(c, j, e);
        g->keep = LU_NOV;
    }
    if (owned) {
        I(c, phi)->flags |= INS_OWNED;
    }
    if (first != NULL || type != NULL) {
        lu_ev_def_type(c, phi, first != NULL ? first->type : type);     /* the one object, whichever edge made it */
    }
    return (struct override){ id, phi, first == NULL || LU_VKIND_MUTABLE(first->kind) ? NULL : first };
}

/* The next virtual object, from position *e, *b of the edges' binds on,
   that the edges know differently, that the joined state still reaches
   and that `over` does not speak for; -1 when there is none. Whether an
   object differs does not depend on the edge that names it: `seen` has
   each looked at once per round. */
static int
next_differing(struct ctx *c, struct join *j, const struct override *over, int nover, char *seen, int *e, int *b)
{
    for (; *e < j->n; ++*e, *b = 0) {
        for (; *b < j->edges[*e].st.nbinds; ++*b) {
            int id = j->edges[*e].st.binds[*b].id, done = seen[id];
            seen[id] = 1;
            for (int k = 0; k < nover; k++) {
                done |= over[k].id == id;
            }
            if (!done && !same_on_edges(c, j, id) && stays_reached(c, j, id)) {
                return id;
            }
        }
    }
    return -1;
}

/* Virtual objects the joined state still reaches mean one thing after
   the join: an object the edges know differently stays virtual when it
   can, its differing fields places of the join, and is made real on
   every edge otherwise. First everything that must exist is made, round
   by round, since making one object real may make others real; then the
   objects that stay virtual get their records and places, which makes
   nothing. */
static struct override *
reconcile(struct ctx *c, struct join *j, int *count)
{
    struct gctx *g = c->g;
    struct override *over = NULL;
    int nover = 0, capover = 0, id;
    for (int changed = 1; changed;) {
        changed = 0;
        char *seen = lu_ir_alloc(g->f, (size_t)g->f->nvobjs + 1);
        for (int e = 0, b = 0; !changed && (id = next_differing(c, j, over, nover, seen, &e, &b)) >= 0;) {
            if (stays_virtual(c, j, id) != NULL) {
                changed = fields_made(c, j, id);
                continue;
            }
            if (nover == capover) {
                capover = capover ? 2 * capover : 4;
                over = regrow(g, over, nover, capover, sizeof(struct override));
            }
            over[nover++] = made_real(c, j, id);
            changed = 1;
        }
    }
    char *seen = lu_ir_alloc(g->f, (size_t)g->f->nvobjs + 1);
    for (int e = 0, b = 0; (id = next_differing(c, j, over, nover, seen, &e, &b)) >= 0;) {
        struct lu_vobj *first = stays_virtual(c, j, id);
        if (first == NULL) {
            g->f->fail = "a join whose objects do not settle";
            break;
        }
        for (int k = 0; k < first->n; k++) {
            if (field_differs(c, j, id, k)) {
                join_place(c, j, place_add(g, &j->P, id, k));
            }
        }
        if (nover == capover) {
            capover = capover ? 2 * capover : 4;
            over = regrow(g, over, nover, capover, sizeof(struct override));
        }
        over[nover++] = (struct override){ id, LU_NOV, record_copy(g, first) };
    }
    *count = nover;
    return over;
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
    int d = edges[0].st.depth;
    struct join j = { .edges = edges, .n = n, .pc = pc, .J = lu_ir_block(g->f),
                      .nslots = edges[0].st.nlocals[d] + edges[0].st.sp[d] };
    struct places *P = &j.P;
    places_of_slots(c, P, j.nslots);
    /* The slots: one that differs between edges becomes a phi. */
    for (int i = 0; i < j.nslots; i++) {
        lu_v r0 = edges[0].st.v[d][i];
        int same = 1;
        for (int e = 1; e < n; e++) {
            same &= edges[e].st.v[d][i] == r0;
        }
        if (!same) {
            join_place(c, &j, i);
        }
    }
    /* What the object phis take is made real on each edge. */
    lu_v **in = lu_ir_alloc(g->f, sizeof(lu_v *) * (size_t)n);
    int had = P->n;
    for (int e = 0; e < n; e++) {
        in[e] = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(had ? had : 1));
        edge_begin(c, &j, e);
        edge_inputs(c, P, 0, in[e]);
        edge_end(c, &j, e);
    }
    int nover;
    struct override *over = reconcile(c, &j, &nover);
    for (int e = 0; e < n; e++) {
        enter_edge(c, &edges[e], pc);
        if (P->n > had) {
            in[e] = regrow(g, in[e], had, P->n, sizeof(lu_v));
            edge_inputs(c, P, had, in[e]);
        }
        edge_take(c, P, in[e], pc);
        lu_ev_settle(c, pc);
        lu_ir_edge(g->f, g->cur, j.J);
        lu_ev_emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
        edges[e].st = g->st;
    }
    /* The joined state: the first edge's, with what differed replaced.
       A fact holds after the join when every edge has it. */
    g->st = edges[0].st;
    lu_ev_changed(g);
    g->cur = j.J;
    lu_v *out = joined_values(c, P);
    for (int p = j.nslots; p < P->n; p++) {
        struct lu_vobj *o = NULL;
        for (int k = 0; k < j.none && o == NULL; k++) {
            o = j.one[k].id == P->at[p].id ? j.one[k].obj : NULL;
        }
        for (int k = 0; k < nover && o == NULL; k++) {
            o = over[k].id == P->at[p].id && over[k].real == LU_NOV ? over[k].obj : NULL;
        }
        o->f[P->at[p].at] = out[p];
    }
    for (int k = 0; k < nover; k++) {
        lu_ev_bind(g, &g->st, over[k].id, over[k].real, over[k].obj);
    }
    for (int k = 0; k < j.none; k++) {
        lu_ev_bind(g, &g->st, j.one[k].id, LU_NOV, j.one[k].obj);
    }
    for (int i = 0; i < j.nslots; i++) {
        if (out[i] != LU_NOV) {
            g->st.v[d][i] = out[i];
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

/* A guard the header made for a loop-carried place blames the header:
   when it keeps failing, the header stops speculating (nospec). */
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

/* The raw representation the header gives a place that holds r, or
   REP_NONE for an object: a box, a constant, an object known to be an int
   or a float, or what an earlier build saw the loop carry there (hint). */
static int
header_rep(struct ctx *c, lu_v r, int pc, int hint)
{
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

/* A hint names a slot by its index, and a virtual object a header carries
   by the way to it from the frame's slots (the slot, then the field of
   each object on the way), which a rebuild finds again: ids are a build's
   own. The object's key names the object itself, and with a field's
   index that field. */
#define KEYED ((uint64_t)1 << 63)

static uint64_t
key_step(uint64_t key, int i)
{
    return (key ^ ((uint64_t)i + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2))) | KEYED;
}

/* The virtual objects a loop may change that nothing has made, as the
   frame's slots reach them, each once. */
struct carry {
    int *id;
    uint64_t *key;
    int n, cap;
};

static void
find_carried(struct ctx *c, lu_v r, uint64_t key, struct carry *k)
{
    struct gctx *g = c->g;
    struct lu_vobj *o = LU_IS_VIRT(r) ? unmade_in(g, &g->st, LU_VIRT_ID(r)) : NULL;
    if (o == NULL) {
        return;
    }
    if (LU_VKIND_MUTABLE(o->kind)) {
        for (int i = 0; i < k->n; i++) {
            if (k->id[i] == LU_VIRT_ID(r)) {
                return;
            }
        }
        if (k->n == k->cap) {
            int cap = k->cap ? 2 * k->cap : 8;
            k->id = regrow(g, k->id, k->n, cap, sizeof(int));
            k->key = regrow(g, k->key, k->n, cap, sizeof(uint64_t));
            k->cap = cap;
        }
        k->id[k->n] = LU_VIRT_ID(r);
        k->key[k->n++] = key;
    }
    for (int i = 0; i < o->n; i++) {
        find_carried(c, o->f[i], key_step(key, i + 1), k);
    }
}

static void
carried_objects(struct ctx *c, int nslots, struct carry *k)
{
    k->n = 0;
    for (int i = 0; i < nslots; i++) {
        find_carried(c, slots(c)[i], key_step(0, i), k);
    }
}

/* Whether a header can carry field i of a virtual object as a phi of
   objects or numbers: a dict's keys are its layout, and a field that is
   unset cannot be told from one that holds an object once it is a phi. */
static int
field_carriable(struct ctx *c, const struct lu_vobj *o, int i)
{
    return !(o->kind == VK_DICT && !(i & 1)) && place_class(c, o, o->f[i]) != FIELD_UNSET;
}

/* What the hints say must exist before the loop is made, until the
   virtual objects left are the ones the header carries: an object a
   back-edge did not find as the header left it, one with a field that
   changes and cannot be a phi, and whatever an object phi of a field
   takes. Making one real may make others real. */
static void
carried_settle(struct ctx *c, int pc, int nslots, struct carry *k)
{
    struct gctx *g = c->g;
    for (int made = 1; made;) {
        made = 0;
        carried_objects(c, nslots, k);
        for (int j = 0; j < k->n && !made; j++) {
            struct lu_vobj *o = unmade_in(g, &g->st, k->id[j]);
            lu_v make = lu_ev_hinted(g, c->code, pc, k->key[j]) == HINT_OBJ ? LU_VIRT(k->id[j]) : LU_NOV;
            for (int i = 0; i < o->n && make == LU_NOV; i++) {
                int hint = lu_ev_hinted(g, c->code, pc, key_step(k->key[j], i + 1)), cls = field_class(c, o->f[i]);
                if (hint == 0 || (cls != FIELD_UNSET && cls != FIELD_OBJ)) {
                    continue;
                }
                if (!field_carriable(c, o, i)) {
                    make = LU_VIRT(k->id[j]);
                }
                else if (header_rep(c, o->f[i], pc, hint) == REP_NONE && LU_IS_VIRT(o->f[i])
                         && unmade_in(g, &g->st, LU_VIRT_ID(o->f[i])) != NULL) {
                    make = o->f[i];
                }
            }
            if (make != LU_NOV) {
                lu_ev_val(c, make);
                made = 1;
            }
        }
    }
}

/* Place p of a header, which holds r, gets a phi for what the loop makes
   of it: a raw one where it holds an int or a float (a box that never
   exists), an object's otherwise. */
static void
header_place(struct ctx *c, struct places *P, int p, lu_v r, int H, int pc, int hint)
{
    int rep = header_rep(c, r, pc, hint);
    P->lead[p] = p;
    if (rep != REP_NONE) {
        P->kind[p] = PLACE_RAW;
        P->phi[p] = new_phi(c, H, rep);
        phi_add(c, P->phi[p], raw_of(c, r, rep));
        P->origin[p] = origin_phi(c, H, P->phi[p]);
        return;
    }
    P->kind[p] = PLACE_OBJ;
    P->phi[p] = new_phi(c, H, REP_OBJ);
    I(c, P->phi[p])->flags |= INS_OWNED;
}

/* The loop header at pc, from the state its forward edges joined into.
   A local the loop does not assign stays as it is, and so does an object
   on the stack that nothing has made, unless it is a number or a bool:
   the loop works above it. The other slots get phis. A virtual object
   the loop may change (an iterator it steps, an instance whose fields it
   sets) stays virtual, under its id: a field that is a raw word is a phi,
   and so is a field an earlier build saw a back-edge change. */
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
    struct places *P = &bb->P;
    places_of_slots(c, P, nslots);
    lu_v first = g->f->nins;
    for (int i = 0; i < nslots; i++) {
        lu_v r = slots(c)[i];
        int hint = lu_ev_hinted(g, c->code, pc, (uint64_t)i);
        struct lu_vobj *o = LU_IS_VIRT(r) ? unmade_in(g, &g->st, LU_VIRT_ID(r)) : NULL;
        int stays = i < c->nlocals ? !set[i] || (o != NULL && o->kind == VK_SEQITER)
            : o != NULL && o->kind != VK_BOX_INT && o->kind != VK_BOX_FLOAT && o->kind != VK_BOOL;
        if (!stays || hint == HINT_OBJ) {
            header_place(c, P, i, r, H, pc, hint);
        }
    }
    lu_v *in = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots ? nslots : 1));
    edge_inputs(c, P, 0, in);
    struct carry k = { 0 };
    carried_settle(c, pc, nslots, &k);
    for (int j = 0; j < k.n; j++) {
        struct lu_vobj *o = unmade_in(g, &g->st, k.id[j]);
        for (int i = 0; i < o->n; i++) {
            int p = place_add(g, P, k.id[j], i), cls = field_class(c, o->f[i]);
            int hint = lu_ev_hinted(g, c->code, pc, key_step(k.key[j], i + 1));
            if (cls != FIELD_UNSET && cls != FIELD_OBJ) {
                P->kind[p] = PLACE_DIRECT;
                P->lead[p] = p;
                P->phi[p] = new_phi(c, H, cls);
                phi_add(c, P->phi[p], o->f[i]);
                if (o->kind == VK_GEN && i == GEN_PC) {
                    lu_ev_gen_header(c, P->phi[p], pc, key_step(k.key[j], i + 1));
                }
            }
            else if (hint != 0) {
                header_place(c, P, p, o->f[i], H, pc, hint);
            }
        }
    }
    blame_header(c, first, pc);
    in = regrow(g, in, nslots, P->n, sizeof(lu_v));
    edge_inputs(c, P, nslots, in);
    edge_take(c, P, in, pc);
    lu_ev_settle(c, pc);
    lu_ir_edge(g->f, g->cur, H);
    lu_ev_emit(c, OP_JUMP, REP_NONE, LU_NOV, LU_NOV, LU_NOV, 0);
    g->cur = H;
    lu_v *out = joined_values(c, P);
    bb->ref = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(P->n ? P->n : 1));
    bb->key = lu_ir_alloc(g->f, sizeof(uint64_t) * (size_t)(P->n ? P->n : 1));
    for (int i = 0; i < nslots; i++) {
        if (out[i] != LU_NOV) {
            slots(c)[i] = out[i];
        }
        bb->ref[i] = slots(c)[i];
        bb->key[i] = (uint64_t)i;
    }
    for (int j = 0, p = nslots; j < k.n; j++) {
        struct lu_vobj *o = record_copy(g, unmade_in(g, &g->st, k.id[j]));
        for (int i = 0; i < o->n; i++, p++) {
            if (out[p] != LU_NOV) {
                o->f[i] = out[p];
            }
            bb->ref[p] = o->f[i];
            bb->key[p] = key_step(k.key[j], i + 1);
        }
        lu_ev_bind(g, &g->st, k.id[j], LU_NOV, o);
    }
    bb->carried = k.id;
    bb->carried_key = k.key;
    bb->ncarried = k.n;
    g->npending = 0;
    lu_ev_changed(g);
    bb->ir = H;
    lu_ev_state_copy(g, &bb->hdr, &g->st);
}

/* Whether what the current state knows of the virtual objects r reaches
   is what the header knew: the same records, the same objects made. An
   object the header carries differs by design: its fields are places. */
static int
same_binding(struct gctx *g, const struct bblk *bb, lu_v r)
{
    if (!LU_IS_VIRT(r)) {
        return 1;
    }
    for (int j = 0; j < bb->ncarried; j++) {
        if (bb->carried[j] == LU_VIRT_ID(r)) {
            return 1;
        }
    }
    lu_v ra, rb;
    struct lu_vobj *oa = lu_ev_virt(g, &g->st, r, &ra), *ob = lu_ev_virt(g, &bb->hdr, r, &rb);
    if (oa != ob || ra != rb) {
        return 0;
    }
    for (int i = 0; oa != NULL && ra == LU_NOV && i < oa->n; i++) {
        if (!same_binding(g, bb, oa->f[i])) {
            return 0;
        }
    }
    return 1;
}

/* The header's carried objects must come round as it left them: made by
   nothing, with the header's layout, each field a value its place can
   take. One that does not exists before the loop in the next build. */
static void
check_carried(struct ctx *c, struct bblk *bb)
{
    struct gctx *g = c->g;
    for (int j = 0; j < bb->ncarried; j++) {
        struct lu_vobj *o = unmade_in(g, &g->st, bb->carried[j]), *h = unmade_in(g, &bb->hdr, bb->carried[j]);
        int fits = o != NULL && o->kind == h->kind && o->n == h->n && o->type == h->type && o->neg == h->neg;
        for (int i = 0; fits && i < o->n; i++) {
            fits = o->f[i] == h->f[i]
                || (field_carriable(c, o, i) && place_class(c, o, o->f[i]) == place_class(c, o, h->f[i]));
        }
        if (!fits) {
            lu_ev_hint(g, c->code, bb->pc, bb->carried_key[j], HINT_OBJ);
        }
    }
}

/* A place the header left alone must hold what it held: a slot that does
   not becomes an object's phi in the next build, a field a phi of what
   the loop carries in it. */
static void
check_same(struct ctx *c, struct bblk *bb)
{
    struct gctx *g = c->g;
    const struct places *P = &bb->P;
    for (int p = 0; p < P->n; p++) {
        lu_v r = place_get(g, &g->st, P->at[p]);
        if (P->kind[p] != PLACE_SAME || (r == bb->ref[p] && same_binding(g, bb, r))) {
            continue;
        }
        int rep = P->at[p].id < 0 || lu_ev_const(c, r) != NULL || nospec(c, bb->pc) ? REP_OBJ : raw_rep(c, r);
        lu_ev_hint(g, c->code, bb->pc, bb->key[p],
                   P->at[p].id < 0 ? HINT_OBJ : rep == REP_I64 ? HINT_INT : rep == REP_F64 ? HINT_FLOAT : HINT_VARIES);
    }
}

/* A back-edge into the built header: the state it carries is given the
   header's shape, or the header a hint and the build a restart. */
static void
back_edge(struct ctx *c, struct bblk *bb)
{
    struct gctx *g = c->g;
    const struct places *P = &bb->P;
    int pc = bb->pc;
    g->npending = 0;
    g->observed = 0;
    c->pc = pc;
    lu_ev_begin(c, pc);
    lu_v first = g->f->nins;
    check_carried(c, bb);
    if (g->restart) {
        g->cur = -1;
        return;
    }
    check_same(c, bb);
    for (int p = 0; p < P->n; p++) {
        lu_v r = place_get(g, &g->st, P->at[p]);
        if (P->kind[p] == PLACE_DIRECT) {
            lu_ev_gen_back_edge(c, P->phi[p], r);       /* a generator's resume point the header did not expect */
        }
        else if (P->kind[p] == PLACE_RAW) {
            int rep = I(c, P->phi[p])->rep, have = raw_rep(c, r);
            PyTypeObject *t = lu_ev_type(c, r);
            int typed = t == &PyLong_Type ? REP_I64 : t == &PyFloat_Type ? REP_F64 : REP_NONE;
            if (have == rep || typed == rep || r == LU_NOV || (!LU_IS_VIRT(r) && t == NULL)) {
                phi_add(c, P->phi[p], raw_of(c, r, rep));       /* an unknown object: a guard, the header's */
            }
            else {
                lu_ev_hint(g, c->code, pc, bb->key[p], HINT_OBJ);
            }
        }
        else if (P->kind[p] == PLACE_OBJ && raw_rep(c, r) != REP_NONE && !LU_IS_VIRT(bb->ref[p])
                 && lu_ev_const(c, r) == NULL && lu_ev_hinted(g, c->code, pc, bb->key[p]) <= HINT_VARIES
                 && !nospec(c, pc)) {
            /* The loop carries a box where the header took an object. */
            lu_ev_hint(g, c->code, pc, bb->key[p], raw_rep(c, r) == REP_I64 ? HINT_INT : HINT_FLOAT);
        }
    }
    if (g->restart) {
        g->cur = -1;
        return;
    }
    blame_header(c, first, pc);
    lu_v *in = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(P->n ? P->n : 1));
    edge_inputs(c, P, 0, in);
    edge_take(c, P, in, pc);
    /* What the phis took was made real here: what the header left alone
       must not have changed with it. */
    check_carried(c, bb);
    if (!g->restart) {
        check_same(c, bb);
    }
    if (g->restart) {
        g->cur = -1;
        return;
    }
    for (int p = 0; p < P->n; p++) {
        if (P->kind[p] == PLACE_DIRECT) {
            phi_add(c, P->phi[p], place_get(g, &g->st, P->at[p]));
        }
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
    int way_in = c->loops != NULL && !bb->loop;
    if (c->loops != NULL && bb->loop) {
        bb = &c->loops[bb - c->bb];     /* the way in reached a loop: the loop's own blocks from here */
    }
    if (bb->built && bb->loop && bb->ref != NULL) {
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
    if ((c->loops == NULL || way_in) && bb - c->bb < c->lo) {
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
    int scanned = 0;
    if (scan(c, pc != (c->parent != NULL ? 0 : lu_jit_call_entry(c->code))) < 0) {
        g->f->fail = "unfollowable jumps";
        return -1;
    }
    scanned = c->nbb;
    add_edge(c, pc);
    for (;;) {
        /* The lowest block control reaches that is not built: blocks are
           in order of their pc but for the entry's, which is built first,
           and an edge to one below the cursor moves it back. */
        while (c->lo < c->nbb && (c->bb[c->lo].built || c->bb[c->lo].nin == 0)) {
            c->lo++;
        }
        if (g->restart) {
            return 0;
        }
        if (c->lo == c->nbb) {
            if (c->loops == NULL) {
                return 0;
            }
            /* The way in is built: now the loops it reached. */
            for (int i = scanned; i < c->nbb; i++) {
                c->loops[i].pc = c->bb[i].pc;
            }
            c->bb = c->loops;
            c->loops = NULL;
            c->lo = 0;
            continue;
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
        struct resume *resumes = g.resumes;
        int nresumes = g.nresumes, capresumes = g.capresumes;
        int nhints = g.nhints, caphints = g.caphints, budget = f->budget;
        reset(f);
        g = (struct gctx){ .f = f, .live = frame, .hints = hints, .nhints = nhints, .caphints = caphints,
                           .resumes = resumes, .nresumes = nresumes, .capresumes = capresumes,
                           .budget = budget, .keep = LU_NOV, .learned = lu_jit_prof_learned(f->code) };
        r = build_root(&g);
        if (r < 0 || !g.restart) {
            break;
        }
        if (tries > 2 * (g.nhints + g.nresumes) + 2) {
            f->fail = "loops whose shapes do not settle";
            r = -1;
            break;
        }
    }
    free(g.hints);
    free(g.resumes);
    if (r < 0 && f->fail == NULL) {
        f->fail = "build failed";
    }
    return r;
}
