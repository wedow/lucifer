/* Generators in line (eval.h): a generator the unit makes is a virtual
   object whose fields are its frame's slots and the code unit the frame
   resumes at. A for loop over one runs the generator's code, as an
   inlined frame of the state, from where the generator stands to its
   next yield, where the slots become its fields again and the value is
   the loop's item. A generator made and consumed in one unit is then
   nothing in the graph but its body's instructions. Where the code unit
   it resumes at differs by path (a phi: the consumer's loop comes round
   from more than one yield), the step begins with a test per code unit.
   One that escapes is made with its frame as it stands (H_gen_new), and
   an exit inside its body rebuilds its frame as the generator's own,
   running above its consumer's (exit.c). */
#include "jit/eval.h"
#include "interp/gen.h"

#include <stdlib.h>
#include <string.h>

/* Whether a generator of the code may stay virtual: a plain generator
   whose every suspension is outside any try or with block, so that
   dropping it unfinished only releases what its frame holds, as closing
   it would. */
int
lu_ev_gen_takes(PyCodeObject *co)
{
    if ((co->co_flags & (LU_CO_GENERATORS | CO_ITERABLE_COROUTINE)) != CO_GENERATOR) {
        return 0;
    }
    struct ctx c = { .code = co };
    for (int pc = 0, n = (int)Py_SIZE(co); pc < n;) {
        int base, oparg, op;
        pc = lu_ev_decode(&c, pc, &base, &oparg, &op);
        if (base == PUSH_EXC_INFO || base == WITH_EXCEPT_START || base == CLEANUP_THROW || base == SEND) {
            return 0;
        }
    }
    return 1;
}

static struct lu_vobj *
record_of(struct ctx *c, lu_v r)
{
    lu_v real;
    struct lu_vobj *o = LU_IS_VIRT(r) ? lu_ev_virt(c->g, &c->g->st, r, &real) : NULL;
    return o != NULL && real == LU_NOV && o->kind == VK_GEN ? o : NULL;
}

int
lu_ev_gen_make(struct ctx *c)
{
    if (c->parent == NULL || !lu_ev_gen_takes(c->code)) {
        lu_ev_side_exit(c, EXIT_SIDE);
        return 0;
    }
    PyCodeObject *co = c->code;
    struct lu_vobj *o;
    lu_v r = lu_ev_virt_new(c, VK_GEN, &PyGen_Type, GEN_SLOTS + co->co_nlocalsplus + co->co_stacksize, &o);
    o->f[GEN_FUNCV] = c->funcv;
    o->f[GEN_FN] = lu_ev_konst(c, (PyObject *)c->func);
    o->f[GEN_PC] = lu_ev_const_i(c, c->next);
    o->f[GEN_DEPTH] = lu_ev_const_i(c, 0);
    memcpy(o->f + GEN_SLOTS, slots(c), sizeof(lu_v) * (size_t)c->nlocals);
    /* The generator is what the call returns: the frame's slots are its
       fields from here, and the frame leaves. */
    lu_ev_push(c, r);
    return lu_ev_return(c);
}

/* --- what a rebuild knows --- */

static struct resume *
resume_find(struct gctx *g, PyCodeObject *code, int at, uint64_t place, int pc)
{
    for (int i = 0; i < g->nresumes; i++) {
        struct resume *r = &g->resumes[i];
        if (r->code == code && r->at == at && r->place == place && r->pc == pc) {
            return r;
        }
    }
    return NULL;
}

/* 1 when it was not known. */
static int
resume_note(struct gctx *g, PyCodeObject *code, int at, uint64_t place, int pc, int depth)
{
    if (resume_find(g, code, at, place, pc) != NULL) {
        return 0;
    }
    if (g->nresumes == g->capresumes) {
        g->capresumes = g->capresumes ? 2 * g->capresumes : 16;
        g->resumes = realloc(g->resumes, sizeof(struct resume) * (size_t)g->capresumes);
    }
    g->resumes[g->nresumes++] = (struct resume){ code, at, place, pc, depth };
    return 1;
}

struct pcs {
    int *pc;
    int n, cap;
    lu_v *seen;                 /* the phis looked into */
    int nseen, capseen;
};

static void
pcs_add(struct pcs *s, int pc)
{
    for (int i = 0; i < s->n; i++) {
        if (s->pc[i] == pc) {
            return;
        }
    }
    if (s->n == s->cap) {
        s->cap = s->cap ? 2 * s->cap : 8;
        s->pc = realloc(s->pc, sizeof(int) * (size_t)s->cap);
    }
    s->pc[s->n++] = pc;
}

static void
pcs_of(struct ctx *c, lu_v v, struct pcs *s)
{
    struct gctx *g = c->g;
    struct lu_ins *in = I(c, v);
    if (in->op == OP_CONST_I) {
        pcs_add(s, (int)in->imm);
        return;
    }
    if (in->op != OP_PHI) {
        g->f->fail = "a generator's resume point that is not known";
        return;
    }
    for (int i = 0; i < s->nseen; i++) {
        if (s->seen[i] == v) {
            return;
        }
    }
    if (s->nseen == s->capseen) {
        s->capseen = s->capseen ? 2 * s->capseen : 8;
        s->seen = realloc(s->seen, sizeof(lu_v) * (size_t)s->capseen);
    }
    s->seen[s->nseen++] = v;
    for (int i = 0; i < g->nresume_phis; i++) {
        struct resume_phi *h = &g->resume_phis[i];
        for (int k = 0; h->phi == v && k < g->nresumes; k++) {
            struct resume *r = &g->resumes[k];
            if (r->code == h->code && r->at == h->pc && r->place == h->place) {
                pcs_add(s, r->pc);
            }
        }
    }
    for (int i = 0; i < in->nargs; i++) {
        pcs_of(c, in->args[i], s);
    }
}

int
lu_ev_gen_resumes(struct ctx *c, lu_v v, int **pcs)
{
    struct pcs s = { 0 };
    pcs_of(c, v, &s);
    *pcs = lu_ir_alloc(c->g->f, sizeof(int) * (size_t)(s.n ? s.n : 1));
    memcpy(*pcs, s.pc, sizeof(int) * (size_t)s.n);
    free(s.pc);
    free(s.seen);
    return s.n;
}

void
lu_ev_gen_header(struct ctx *c, lu_v phi, int pc, uint64_t place)
{
    struct gctx *g = c->g;
    if (g->nresume_phis == g->capresume_phis) {
        int cap = g->capresume_phis ? 2 * g->capresume_phis : 4;
        struct resume_phi *p = lu_ir_alloc(g->f, sizeof(struct resume_phi) * (size_t)cap);
        memcpy(p, g->resume_phis, sizeof(struct resume_phi) * (size_t)g->nresume_phis);
        g->resume_phis = p;
        g->capresume_phis = cap;
    }
    g->resume_phis[g->nresume_phis++] = (struct resume_phi){ phi, c->code, pc, place };
}

int
lu_ev_gen_back_edge(struct ctx *c, lu_v phi, lu_v now)
{
    struct gctx *g = c->g;
    struct resume_phi *h = NULL;
    for (int i = 0; i < g->nresume_phis && h == NULL; i++) {
        h = g->resume_phis[i].phi == phi ? &g->resume_phis[i] : NULL;
    }
    if (h == NULL) {
        return 0;
    }
    int *have, *come, added = 0;
    int nhave = lu_ev_gen_resumes(c, phi, &have), ncome = lu_ev_gen_resumes(c, now, &come);
    for (int i = 0; i < ncome; i++) {
        int known = 0;
        for (int k = 0; k < nhave; k++) {
            known |= have[k] == come[i];
        }
        added |= !known && resume_note(g, h->code, h->pc, h->place, come[i], 0);
    }
    g->restart |= added;
    return added;
}

/* --- the step --- */

/* Whether the unit has room to run the generator's code in line here. */
static int
room(struct ctx *c, PyCodeObject *co)
{
    int again = 0;
    for (struct ctx *k = c; k != NULL; k = k->parent) {
        again += k->code == co;
    }
    return c->depth < 24 && again <= 1 && !lu_jit_noinline(co)
        && c->g->f->nins + 2 * c->g->f->ninlined <= c->g->budget;
}

/* The generator stands at code unit pc: its frame joins the state with
   its fields as its slots and the value a for loop sends, None, on its
   stack, and its code is evaluated from there. Its yields and its
   return go on in the consumer (lu_ev_gen_yield, lu_ev_return). */
static int
resume_at(struct ctx *c, lu_v it, int pc, int exit_pc)
{
    struct gctx *g = c->g;
    struct lu_vobj *o = record_of(c, it);
    if (pc == GEN_FINISHED) {
        lu_ev_fork(c, lu_ev_const_i(c, 1), exit_pc);    /* a finished generator ends the loop */
        return 0;
    }
    PyFunctionObject *fn = (PyFunctionObject *)lu_ev_const(c, o->f[GEN_FN]);
    PyCodeObject *co = (PyCodeObject *)fn->func_code;
    struct resume *known = resume_find(g, co, 0, 0, pc);
    int depth = known != NULL ? known->depth : 0;
    struct ctx *k = lu_ir_alloc(g->f, sizeof(struct ctx));
    k->ret = LU_RETURN_FOR_ITER;
    k->gen = it;
    k->g = g;
    k->parent = c;
    k->depth = c->depth + 1;
    k->code = co;
    k->func = fn;
    k->funcv = o->f[GEN_FUNCV];
    k->globals = fn->func_globals;
    k->builtins = fn->func_builtins;
    k->kernel = (co->co_flags & LU_CO_KERNEL) != 0;
    k->ncode = (int)Py_SIZE(co);
    k->nlocals = co->co_nlocalsplus;
    k->caller_fs = lu_ev_snapshot(c, c->next);
    lu_ev_state_push(g, k->nlocals, k->nlocals + co->co_stacksize + 2);
    memcpy(slots(k), o->f + GEN_SLOTS, sizeof(lu_v) * (size_t)(k->nlocals + depth));
    *depth_of(k) = depth;
    /* While it runs its record holds nothing: the frame does. */
    struct lu_vobj *running = lu_ir_vobj(g->f, VK_GEN, o->type, o->n);
    running->f[GEN_FUNCV] = o->f[GEN_FUNCV];
    running->f[GEN_FN] = o->f[GEN_FN];
    running->f[GEN_PC] = lu_ev_const_i(c, GEN_RUNNING);
    running->f[GEN_DEPTH] = lu_ev_const_i(c, 0);
    for (int i = k->nlocals + depth; i < o->n - GEN_SLOTS; i++) {
        lu_ev_drop(k, o->f[GEN_SLOTS + i]);     /* what a longer stack left behind */
    }
    lu_ev_bind(g, &g->st, LU_VIRT_ID(it), LU_NOV, running);
    lu_ev_push(k, lu_ev_konst(k, Py_None));
    if (k->depth > g->maxdepth) {
        g->maxdepth = k->depth;
    }
    g->f->ninlined++;
    k->pc = pc;
    lu_ev_begin(k, pc);
    lu_ev_settle(k, pc);
    if (lu_ev_build(k, pc) < 0) {
        return -1;
    }
    if (g->restart || k->nrets == 0) {
        g->cur = -1;
        return 0;
    }
    lu_ev_merge(c, k->rets, k->nrets, c->next);
    return 0;
}

static int
resume_among(struct ctx *c, lu_v it, const int *pcs, int n, int exit_pc)
{
    if (n == 1) {
        return resume_at(c, it, pcs[0], exit_pc);
    }
    struct split s;
    lu_v at = record_of(c, it)->f[GEN_PC];
    lu_ev_split(c, lu_ev_emit(c, OP_CMP_I, REP_BIT, at, lu_ev_const_i(c, pcs[0]), LU_NOV, Py_EQ), &s);
    if (resume_at(c, it, pcs[0], exit_pc) < 0) {
        return -1;
    }
    lu_ev_split_else(c, &s);
    if (resume_among(c, it, pcs + 1, n - 1, exit_pc) < 0) {
        return -1;
    }
    lu_ev_split_end(c, &s);
    return 0;
}

int
lu_ev_gen_step(struct ctx *c, lu_v it, int exit_pc)
{
    struct lu_vobj *o = record_of(c, it);
    PyCodeObject *co = (PyCodeObject *)((PyFunctionObject *)lu_ev_const(c, o->f[GEN_FN]))->func_code;
    int *pcs, n = lu_ev_gen_resumes(c, o->f[GEN_PC], &pcs);
    if (c->g->f->fail != NULL) {
        return -1;
    }
    if (!room(c, co)) {
        c->g->f->refused++;
        lu_ev_val(c, it);
        return 0;
    }
    return resume_among(c, it, pcs, n, exit_pc) < 0 ? -1 : 1;
}

int
lu_ev_gen_yield(struct ctx *c)
{
    struct gctx *g = c->g;
    if (c->oparg != 0) {
        lu_ev_side_exit(c, EXIT_SIDE);  /* a delegation's yield: the interpreter's loop runs it */
        return 0;
    }
    lu_v v = lu_ev_peek(c, 1);
    lu_ev_pop(c);
    int depth = *depth_of(c);
    struct lu_vobj *o = record_of(c, c->gen), *stands = lu_ir_vobj(g->f, VK_GEN, o->type, o->n);
    stands->f[GEN_FUNCV] = o->f[GEN_FUNCV];
    stands->f[GEN_FN] = o->f[GEN_FN];
    stands->f[GEN_PC] = lu_ev_const_i(c, c->next);
    stands->f[GEN_DEPTH] = lu_ev_const_i(c, depth);
    memcpy(stands->f + GEN_SLOTS, slots(c), sizeof(lu_v) * (size_t)(c->nlocals + depth));
    resume_note(g, c->code, 0, 0, c->next, depth);
    lu_ev_bind(g, &g->st, LU_VIRT_ID(c->gen), LU_NOV, stands);
    lu_ev_state_pop(g);
    return lu_ev_returned(c, v);
}

void
lu_ev_gen_finish(struct ctx *c)
{
    struct gctx *g = c->g;
    struct lu_vobj *o = record_of(c, c->gen), *done = lu_ir_vobj(g->f, VK_GEN, o->type, o->n);
    done->f[GEN_FUNCV] = o->f[GEN_FUNCV];
    done->f[GEN_FN] = o->f[GEN_FN];
    done->f[GEN_PC] = lu_ev_const_i(c, GEN_FINISHED);
    done->f[GEN_DEPTH] = lu_ev_const_i(c, 0);
    lu_ev_bind(g, &g->st, LU_VIRT_ID(c->gen), LU_NOV, done);
}

lu_v
lu_ev_gen_real(struct ctx *c, struct lu_vobj *o)
{
    struct gctx *g = c->g;
    if (I(c, o->f[GEN_PC])->op == OP_CONST_I && I(c, o->f[GEN_PC])->imm == GEN_RUNNING) {
        g->f->fail = "a running generator made real";
        return lu_ev_konst(c, NULL);
    }
    lu_v *args = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)o->n);
    args[0] = o->f[GEN_FUNCV] != LU_NOV ? o->f[GEN_FUNCV] : o->f[GEN_FN];
    args[1] = o->f[GEN_PC];
    args[2] = o->f[GEN_DEPTH];
    memcpy(args + 3, o->f + GEN_SLOTS, sizeof(lu_v) * (size_t)(o->n - GEN_SLOTS));
    lu_v v = lu_ev_helper(c, H_gen_new, o->n - 1, args, 0);
    lu_ev_def_type(c, v, &PyGen_Type);
    return v;
}
