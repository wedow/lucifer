/* The evaluator's state (eval.h): frames, virtual objects, facts, and the
   frame states exits are given. Everything is allocated from the
   function's arena and never freed singly. */
#include "jit/eval.h"

#include <string.h>

static void *
copy_of(struct gctx *g, const void *p, size_t size)
{
    void *q = lu_ir_alloc(g->f, size ? size : 1);
    if (size) {
        memcpy(q, p, size);
    }
    return q;
}

/* Frames below the innermost do not change while it runs, so copies share
   them; the innermost's slots, the binds and the facts are the copy's own. */
void
lu_ev_state_copy(struct gctx *g, struct state *to, const struct state *from)
{
    int n = from->depth + 1;
    to->depth = from->depth;
    to->v = copy_of(g, from->v, sizeof(lu_v *) * (size_t)n);
    to->sp = copy_of(g, from->sp, sizeof(int) * (size_t)n);
    to->cap = copy_of(g, from->cap, sizeof(int) * (size_t)n);
    to->nlocals = copy_of(g, from->nlocals, sizeof(int) * (size_t)n);
    to->v[n - 1] = copy_of(g, from->v[n - 1], sizeof(lu_v) * (size_t)from->cap[n - 1]);
    to->nbinds = to->capbinds = from->nbinds;
    to->binds = copy_of(g, from->binds, sizeof(struct lu_bind) * (size_t)from->nbinds);
    to->nfacts = to->capfacts = from->nfacts;
    to->facts = copy_of(g, from->facts, sizeof(struct fact) * (size_t)from->nfacts);
    to->version = from->version;
}

void
lu_ev_state_push(struct gctx *g, int nlocals, int nslots)
{
    struct state *st = &g->st;
    int n = st->depth + 2;      /* the root comes with depth -1 */
    lu_v **v = lu_ir_alloc(g->f, sizeof(lu_v *) * (size_t)n);
    int *sp = lu_ir_alloc(g->f, sizeof(int) * (size_t)n);
    int *cap = lu_ir_alloc(g->f, sizeof(int) * (size_t)n);
    int *nl = lu_ir_alloc(g->f, sizeof(int) * (size_t)n);
    for (int i = 0; i < n - 1; i++) {
        v[i] = st->v[i];
        sp[i] = st->sp[i];
        cap[i] = st->cap[i];
        nl[i] = st->nlocals[i];
    }
    v[n - 1] = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)(nslots ? nslots : 1));
    for (int i = 0; i < nslots; i++) {
        v[n - 1][i] = LU_NOV;
    }
    sp[n - 1] = 0;
    cap[n - 1] = nslots;
    nl[n - 1] = nlocals;
    st->v = v;
    st->sp = sp;
    st->cap = cap;
    st->nlocals = nl;
    st->depth = n - 1;
    lu_ev_changed(g);
}

/* The caller is the innermost again, and may change: its slots become
   this state's own. */
void
lu_ev_state_pop(struct gctx *g)
{
    struct state *st = &g->st;
    st->depth--;
    int d = st->depth;
    st->v = copy_of(g, st->v, sizeof(lu_v *) * (size_t)(d + 1));
    st->sp = copy_of(g, st->sp, sizeof(int) * (size_t)(d + 1));
    st->v[d] = copy_of(g, st->v[d], sizeof(lu_v) * (size_t)st->cap[d]);
    lu_ev_changed(g);
}

static void
pend(struct gctx *g, lu_v r)
{
    if (r == LU_NOV || (!LU_IS_VIRT(r) && !(g->f->ins[r].flags & INS_OWNED))) {
        return;
    }
    if (g->npending == g->cappending) {
        int cap = g->cappending ? 2 * g->cappending : 16;
        lu_v *p = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)cap);
        memcpy(p, g->pending, sizeof(lu_v) * (size_t)g->npending);
        g->pending = p;
        g->cappending = cap;
    }
    g->pending[g->npending++] = r;
}

void
lu_ev_begin(struct ctx *c, int pc)
{
    c->snap = NULL;
    c->snap_pc = pc;
    c->snap_sp = *depth_of(c);
    c->nundo = 0;
}

/* Slot i of c's frame is about to change: what it held goes to the log
   the instruction's snapshot is made from, unless that is made already. */
static void
changes(struct ctx *c, int i)
{
    if (c->snap != NULL) {
        return;
    }
    if (c->nundo == c->capundo) {
        int cap = c->capundo ? 2 * c->capundo : 16;
        struct undo *p = lu_ir_alloc(c->g->f, sizeof(struct undo) * (size_t)cap);
        memcpy(p, c->undo, sizeof(struct undo) * (size_t)c->nundo);
        c->undo = p;
        c->capundo = cap;
    }
    c->undo[c->nundo++] = (struct undo){ i, slots(c)[i] };
}

void
lu_ev_push(struct ctx *c, lu_v r)
{
    int *sp = depth_of(c);
    if (c->nlocals + *sp >= c->g->st.cap[c->depth]) {
        c->g->f->fail = "a stack deeper than its code says";
        return;
    }
    changes(c, c->nlocals + *sp);
    slots(c)[c->nlocals + (*sp)++] = r;
}

lu_v
lu_ev_pop(struct ctx *c)
{
    int *sp = depth_of(c);
    if (*sp == 0) {
        c->g->f->fail = "a pop from an empty stack";
        return LU_NOV;
    }
    lu_v r = slots(c)[c->nlocals + --*sp];
    changes(c, c->nlocals + *sp);
    slots(c)[c->nlocals + *sp] = LU_NOV;
    pend(c->g, r);
    return r;
}

lu_v
lu_ev_peek(struct ctx *c, int depth)
{
    return slots(c)[c->nlocals + *depth_of(c) - depth];
}

void
lu_ev_set_local(struct ctx *c, int i, lu_v r)
{
    pend(c->g, slots(c)[i]);
    changes(c, i);
    slots(c)[i] = r;
    lu_ev_changed(c->g);
}

lu_v
lu_ev_own(struct ctx *c, lu_v v)
{
    I(c, v)->flags |= INS_OWNED;
    pend(c->g, v);
    return v;
}

void
lu_ev_drop(struct ctx *c, lu_v r)
{
    pend(c->g, r);
}

lu_v
lu_ev_virt_new(struct ctx *c, int kind, PyTypeObject *type, int n, struct lu_vobj **out)
{
    struct lu_vobj *o = lu_ir_vobj(c->g->f, kind, type, n);
    *out = o;
    lu_v r = LU_VIRT(lu_ir_vobj_id(c->g->f, o));
    pend(c->g, r);              /* a new object nothing names yet */
    return r;
}

struct lu_vobj *
lu_ev_virt(struct gctx *g, const struct state *st, lu_v r, lu_v *real)
{
    int id = LU_VIRT_ID(r);
    *real = LU_NOV;
    for (int i = st->nbinds - 1; i >= 0; i--) {
        if (st->binds[i].id == id) {
            *real = st->binds[i].real;
            return st->binds[i].obj;
        }
    }
    return g->f->vobjs[id];
}

void
lu_ev_bind(struct gctx *g, struct state *st, int id, lu_v real, struct lu_vobj *obj)
{
    lu_ev_changed(g);
    for (int i = 0; i < st->nbinds; i++) {
        if (st->binds[i].id == id) {
            /* Another state may share the array, under the version it had
               (lu_ev_replaced): the change is the copy's. */
            st->binds = copy_of(g, st->binds, sizeof(struct lu_bind) * (size_t)st->nbinds);
            st->capbinds = st->nbinds;
            st->binds[i].real = real;
            st->binds[i].obj = obj;
            return;
        }
    }
    if (st->nbinds == st->capbinds) {
        int cap = st->capbinds ? 2 * st->capbinds : 8;
        struct lu_bind *p = lu_ir_alloc(g->f, sizeof(struct lu_bind) * (size_t)cap);
        memcpy(p, st->binds, sizeof(struct lu_bind) * (size_t)st->nbinds);
        st->binds = p;
        st->capbinds = cap;
    }
    st->binds[st->nbinds++] = (struct lu_bind){ id, real, obj };
}

struct fact *
lu_ev_fact(struct ctx *c, lu_v v, int create)
{
    /* Sorted by value: found by halving, and a new value's goes last. */
    struct state *st = &c->g->st;
    int lo = 0, hi = st->nfacts;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (st->facts[mid].v < v) {
            lo = mid + 1;
        }
        else {
            hi = mid;
        }
    }
    if (lo < st->nfacts && st->facts[lo].v == v) {
        return &st->facts[lo];
    }
    if (!create) {
        return NULL;
    }
    if (st->nfacts == st->capfacts) {
        int cap = st->capfacts ? 2 * st->capfacts : 16;
        struct fact *p = lu_ir_alloc(c->g->f, sizeof(struct fact) * (size_t)cap);
        memcpy(p, st->facts, sizeof(struct fact) * (size_t)st->nfacts);
        st->facts = p;
        st->capfacts = cap;
    }
    memmove(&st->facts[lo + 1], &st->facts[lo], sizeof(struct fact) * (size_t)(st->nfacts - lo));
    st->nfacts++;
    st->facts[lo] = (struct fact){ v, NULL, LU_NOV };
    return &st->facts[lo];
}

void
lu_ev_def_type(struct ctx *c, lu_v v, PyTypeObject *t)
{
    struct lu_func *f = c->g->f;
    if (v >= f->capknown) {
        int cap = f->capknown ? f->capknown : 256;
        while (cap <= v) {
            cap *= 2;
        }
        f->known = realloc(f->known, sizeof(PyTypeObject *) * (size_t)cap);
        memset(f->known + f->capknown, 0, sizeof(PyTypeObject *) * (size_t)(cap - f->capknown));
        f->capknown = cap;
    }
    f->known[v] = t;
}

void
lu_ev_set_type(struct ctx *c, lu_v v, PyTypeObject *t)
{
    lu_ev_fact(c, v, 1)->t = t;
}

PyObject *
lu_ev_const(struct ctx *c, lu_v r)
{
    if (r < 0 || I(c, r)->op != OP_CONST) {
        return NULL;
    }
    return (PyObject *)(intptr_t)I(c, r)->imm;
}

/* An object's class can be reassigned (object.__class__, prim/type.c
   set_class), by any code the unit calls. So what is known of the type of
   an object whose class allows it is relied on as of that class's
   version: a dependency of the unit, which the reassignment takes back,
   and which compiled code learns of where it checks its frames after a
   call (GUARD_FRAMES). A class with no version to rely on is not known. */
static PyTypeObject *
relied(struct ctx *c, PyTypeObject *t)
{
    struct gctx *g = c->g;
    if (t == NULL || t == g->relied || ((t->tp_flags & Py_TPFLAGS_IMMUTABLETYPE) && !PyType_IsSubtype(t, &PyModule_Type))) {
        return t;
    }
    uint32_t version = lu_type_assign_version(t) ? __atomic_load_n(&t->tp_version_tag, __ATOMIC_ACQUIRE) : 0;
    if (version == 0) {
        return NULL;
    }
    struct lu_fdep dep = { .kind = LU_DEP_TYPE, .obj = version, .code = c->code, .pc = c->pc };
    lu_ir_dep(g->f, &dep);
    g->relied = t;
    return t;
}

PyTypeObject *
lu_ev_type(struct ctx *c, lu_v r)
{
    if (r == LU_NOV) {
        return NULL;
    }
    if (LU_IS_VIRT(r)) {
        lu_v real;
        struct lu_vobj *o = lu_ev_virt(c->g, &c->g->st, r, &real);
        return o != NULL ? o->type : lu_ev_type(c, real);
    }
    PyObject *k = lu_ev_const(c, r);
    if (k != NULL) {
        return relied(c, Py_TYPE(k));
    }
    struct lu_func *f = c->g->f;
    if (r < f->capknown && f->known[r] != NULL) {
        return relied(c, f->known[r]);
    }
    struct fact *fa = lu_ev_fact(c, r, 0);
    return fa != NULL ? relied(c, fa->t) : NULL;
}

struct lu_fs *
lu_ev_snapshot(struct ctx *c, int pc)
{
    struct gctx *g = c->g;
    struct lu_fs *fs = lu_ir_alloc(g->f, sizeof(struct lu_fs));
    fs->caller = c->caller_fs;
    fs->code = c->code;
    fs->func = c->func != NULL ? (PyObject *)c->func : g->live->func;
    fs->funcv = c->funcv;
    fs->pc = pc;
    fs->nlocals = c->nlocals;
    fs->nstack = *depth_of(c);
    fs->locals = copy_of(g, slots(c), sizeof(lu_v) * (size_t)c->nlocals);
    fs->stack = copy_of(g, slots(c) + c->nlocals, sizeof(lu_v) * (size_t)fs->nstack);
    fs->ret = (uint8_t)c->ret;
    fs->gen = c->adopted ? -1 - c->gen : LU_IS_VIRT(c->gen) ? 1 + LU_VIRT_ID(c->gen) : 0;
    fs->nbinds = g->st.nbinds;
    fs->binds = copy_of(g, g->st.binds, sizeof(struct lu_bind) * (size_t)g->st.nbinds);
    return fs;
}

static int
reaches(struct gctx *g, const struct state *st, lu_v r, int id)
{
    if (!LU_IS_VIRT(r)) {
        return 0;
    }
    if (LU_VIRT_ID(r) == id) {
        return 1;
    }
    lu_v real;
    struct lu_vobj *o = lu_ev_virt(g, st, r, &real);
    for (int i = 0; real == LU_NOV && i < o->n; i++) {
        if (reaches(g, st, o->f[i], id)) {
            return 1;
        }
    }
    return 0;
}

int
lu_ev_reaches(struct gctx *g, const struct state *st, int id)
{
    for (int d = 0; d <= st->depth; d++) {
        for (int i = 0; i < st->nlocals[d] + st->sp[d]; i++) {
            if (reaches(g, st, st->v[d][i], id)) {
                return 1;
            }
        }
    }
    return 0;
}

static uint32_t *
stamps(struct gctx *g, uint32_t *old, int *cap, int n)
{
    if (n <= *cap) {
        return old;
    }
    int room = *cap ? *cap : 256;
    while (room < n) {
        room *= 2;
    }
    *cap = room;
    return lu_ir_alloc(g->f, sizeof(uint32_t) * (size_t)room);  /* zeroed; earlier stamps are not read again */
}

struct walk {
    struct gctx *g;
    uint32_t *named;
    uint32_t stamp;
};

static void
stamp_value(struct lu_func *f, lu_v v, void *arg)
{
    struct walk *w = arg;
    if (v >= 0) {
        w->named[v] = w->stamp;
    }
}

static void
stamp_reached(struct gctx *g, lu_v r, uint32_t *reached, uint32_t stamp)
{
    if (!LU_IS_VIRT(r) || reached[LU_VIRT_ID(r)] == stamp) {
        return;
    }
    reached[LU_VIRT_ID(r)] = stamp;
    lu_v real;
    struct lu_vobj *o = lu_ev_virt(g, &g->st, r, &real);
    for (int i = 0; real == LU_NOV && i < o->n; i++) {
        stamp_reached(g, o->f[i], reached, stamp);
    }
}

/* The base is walked again when the frame or the state's version is not
   the one it was walked for. */
static void
walk_base(struct ctx *c)
{
    struct gctx *g = c->g;
    if (g->base_ctx == c && g->base_version == g->st.version) {
        return;
    }
    g->base_ctx = c;
    g->base_version = g->st.version;
    g->base_stamp++;
    g->base_named = stamps(g, g->base_named, &g->capbase_named, g->f->nins);
    g->base_reached = stamps(g, g->base_reached, &g->capbase_reached, g->f->nvobjs);
    struct lu_fs base = { .caller = c->caller_fs, .funcv = c->funcv, .nlocals = c->nlocals, .locals = slots(c),
                          .binds = g->st.binds, .nbinds = g->st.nbinds };
    struct walk w = { g, g->base_named, g->base_stamp };
    lu_fs_each(g->f, &base, stamp_value, &w);
    for (int d = 0; d <= g->st.depth; d++) {
        int n = g->st.nlocals[d] + (d < g->st.depth ? g->st.sp[d] : 0);
        for (int i = 0; i < n; i++) {
            stamp_reached(g, g->st.v[d][i], g->base_reached, g->base_stamp);
        }
    }
}

/* The stamps of a walk of `stack` (n entries) beside the base. */
static void
walk_stack(struct ctx *c, lu_v *stack, int n)
{
    struct gctx *g = c->g;
    g->stamp++;
    g->named = stamps(g, g->named, &g->capnamed, g->f->nins);
    g->reached = stamps(g, g->reached, &g->capreached, g->f->nvobjs);
    struct lu_fs top = { .funcv = LU_NOV, .nstack = n, .stack = stack, .binds = g->st.binds, .nbinds = g->st.nbinds };
    struct walk w = { g, g->named, g->stamp };
    lu_fs_each(g->f, &top, stamp_value, &w);
    for (int i = 0; i < n; i++) {
        stamp_reached(g, stack[i], g->reached, g->stamp);
    }
}

void
lu_ev_walk(struct ctx *c)
{
    walk_base(c);
    walk_stack(c, slots(c) + c->nlocals, *depth_of(c));
}

void
lu_ev_names_walk(struct gctx *g, struct lu_fs *fs)
{
    struct lu_fs slots_only = *fs;
    slots_only.nheld = 0;
    g->stamp++;
    g->named = stamps(g, g->named, &g->capnamed, g->f->nins);
    struct walk w = { g, g->named, g->stamp };
    lu_fs_each(g->f, &slots_only, stamp_value, &w);
}

int
lu_ev_names(struct gctx *g, lu_v v)
{
    return g->named[v] == g->stamp;
}

int
lu_ev_named(struct gctx *g, lu_v v)
{
    if (g->named[v] == g->stamp) {
        return 1;
    }
    if (g->base_ctx != NULL && v < g->capbase_named && g->base_named[v] == g->base_stamp) {
        return 1;
    }
    g->named[v] = g->stamp;     /* asked once: the asker deals with it */
    return 0;
}

int
lu_ev_reached(struct gctx *g, int id)
{
    if (g->reached[id] == g->stamp) {
        return 1;
    }
    if (g->base_ctx != NULL && id < g->capbase_reached && g->base_reached[id] == g->base_stamp) {
        return 1;
    }
    g->reached[id] = g->stamp;
    return 0;
}

/* What compiled code holds for r, a pending value, that state fs does not
   name (the values it names are stamped): itself, or for a virtual object
   the object it became, else what its fields hold. Each once. */
static void
held_of(struct gctx *g, struct lu_fs *fs, lu_v r, lu_v **out, int *n, int *cap)
{
    if (r == LU_NOV) {
        return;
    }
    if (LU_IS_VIRT(r)) {
        lu_v real;
        struct lu_vobj *o = lu_fs_vobj(g->f, fs, LU_VIRT_ID(r), &real);
        if (real != LU_NOV) {
            held_of(g, fs, real, out, n, cap);
            return;
        }
        for (int i = 0; i < o->n; i++) {
            held_of(g, fs, o->f[i], out, n, cap);
        }
        return;
    }
    if (!(g->f->ins[r].flags & INS_OWNED) || lu_ev_named(g, r)) {
        return;
    }
    if (*n == *cap) {
        int room = *cap ? 2 * *cap : 4;
        lu_v *p = lu_ir_alloc(g->f, sizeof(lu_v) * (size_t)room);
        memcpy(p, *out, sizeof(lu_v) * (size_t)*n);
        *out = p;
        *cap = room;
    }
    (*out)[(*n)++] = r;
}

/* The state before the current instruction: the slots as they are, with
   what the instruction has changed since put back. Its locals are the
   last such state's when no local has changed since that was made. */
static void
snap(struct ctx *c)
{
    struct gctx *g = c->g;
    struct lu_fs *fs = lu_ir_alloc(g->f, sizeof(struct lu_fs));
    int now = *depth_of(c), deep = c->snap_sp > now ? c->snap_sp : now, wrote = 0;
    for (int k = 0; k < c->nundo; k++) {
        wrote |= c->undo[k].i < c->nlocals;
    }
    fs->caller = c->caller_fs;
    fs->code = c->code;
    fs->func = c->func != NULL ? (PyObject *)c->func : g->live->func;
    fs->funcv = c->funcv;
    fs->pc = c->snap_pc;
    fs->nlocals = c->nlocals;
    fs->nstack = c->snap_sp;
    fs->ret = (uint8_t)c->ret;
    fs->gen = c->adopted ? -1 - c->gen : LU_IS_VIRT(c->gen) ? 1 + LU_VIRT_ID(c->gen) : 0;
    if (!wrote && c->kept_of == slots(c) && c->kept_epoch == g->epoch) {
        fs->locals = c->kept_locals;
    }
    else {
        fs->locals = copy_of(g, slots(c), sizeof(lu_v) * (size_t)c->nlocals);
        if (!wrote) {
            c->kept_locals = fs->locals;
            c->kept_of = slots(c);
            c->kept_epoch = g->epoch;
        }
    }
    fs->stack = copy_of(g, slots(c) + c->nlocals, sizeof(lu_v) * (size_t)deep);
    for (int k = c->nundo - 1; k >= 0; k--) {
        if (c->undo[k].i < c->nlocals + c->snap_sp) {
            *lu_fs_slot(fs, c->undo[k].i) = c->undo[k].old;
        }
    }
    c->snap = fs;
    c->snap_live = !wrote;
}

struct lu_fs *
lu_ev_exit_fs(struct ctx *c)
{
    struct gctx *g = c->g;
    if (c->snap == NULL) {
        snap(c);
    }
    struct lu_fs *fs = copy_of(g, c->snap, sizeof(struct lu_fs));
    fs->nbinds = g->st.nbinds;
    fs->binds = copy_of(g, g->st.binds, sizeof(struct lu_bind) * (size_t)g->st.nbinds);
    fs->nheld = 0;
    fs->held = NULL;
    if (g->npending > 0) {
        /* What is pending and the state does not name is held. The
           state's locals are the frame's own unless the instruction wrote
           one: then the whole state is walked. */
        if (c->snap_live && c->kept_of == slots(c) && c->kept_epoch == g->epoch) {
            walk_base(c);
            walk_stack(c, fs->stack, fs->nstack);
        }
        else {
            g->base_ctx = NULL;
            g->stamp++;
            g->named = stamps(g, g->named, &g->capnamed, g->f->nins);
            struct walk w = { g, g->named, g->stamp };
            lu_fs_each(g->f, fs, stamp_value, &w);
        }
        int cap = 0;
        for (int i = 0; i < g->npending; i++) {
            held_of(g, fs, g->pending[i], &fs->held, &fs->nheld, &cap);
        }
    }
    return fs;
}
