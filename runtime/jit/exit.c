/* Making what compiled code kept virtual (docs/design/jit-architecture.md
   4.3): one routine turns a description (target.h struct lu_deopt) into
   objects and frames, for its three callers.

   An exit. Compiled code leaves at a bytecode boundary that has not
   executed: the root frame's slots are filled, each inlined frame is
   pushed above it as the interpreter would have pushed it at its call,
   and the innermost is the task's current frame. A slot takes a new
   reference; the references compiled code held are dropped last, so a
   finalizer sees whole frames.

   A walker. While compiled code is in a call during which its frames may
   be seen, the root frame names that call's description (jit_desc) and
   the spill words, where the allocator keeps every value the description
   names. A walker that needs the root's locals gives the frame real slots
   (lu_jit_sync); one that reaches inlined frames a user can see gets a
   record for each, off the datastack, linked above the root
   (lu_jit_force). Either way the root is no longer compiled code's, which
   the code tests after the call (GUARD_FRAMES) and leaves through an
   ordinary exit. That exit keeps the slots a walker may have written and
   moves the records onto the datastack.

   Identity. A virtual object is made at most once per activation: the
   first to need it enters it in the activation's forced table by state
   id, and every later request, the exit's included, takes it from there.
   Within one exit, objects are also shared by their index.

   No memory: the exit raises MemoryError at its instruction, with the
   slots it could not fill unbound; a walker gets it in its own thread. */
#include "jit/target.h"
#include "jit/jit.h"
#include "interp/instrument.h"
#include "interp/gen.h"

#include <stdlib.h>
#include <string.h>

void lu_jit_note_exit(struct lu_deopt *d);
int lu_jit_next_pc(PyCodeObject *code, int pc);
struct lu_frame *lu_jit_force(struct lu_frame *f, int all);

struct lu_forced {
    int nobjs, capobjs;
    struct {
        int32_t id;
        PyObject *obj;          /* the table's own reference */
    } *objs;
    int nframes;
    struct lu_frame **frames;   /* the inlined frames, outermost first */
};

/* Where an activation's values are, and what this pass has made. */
struct maker {
    const uint64_t *gpr, *fpr, *spills;
    struct lu_deopt *d;
    struct lu_forced *forced;   /* the activation's table, or NULL */
    PyObject **made;            /* per virtual object of d: made by this pass (borrowed from `refs`) */
    PyObject **refs;            /* this pass's own references to what it made, dropped at its end */
    int nrefs;
    int failed;                 /* an allocation failed: MemoryError is set */
};

static uint64_t
at(struct maker *m, int32_t loc)
{
    if (LU_LOC_IS_SPILL(loc)) {
        return *(const uint64_t *)((const char *)m->spills - 8 * LU_LOC_SPILL_INDEX(loc));
    }
    if (m->gpr == NULL || loc == LU_LOC_NONE) {
        return 0;               /* the allocator keeps published values in memory */
    }
    return loc >= LU_LOC_FPR ? m->fpr[loc - LU_LOC_FPR] : m->gpr[loc];
}

/* A raw word's value: a constant, or the word at its location. */
static uint64_t
raw(struct maker *m, lu_pv v)
{
    return LU_PV_KIND(v) == DV_IMM ? (uint64_t)m->d->unit->imms[LU_PV_ARG(v)] : at(m, lu_pv_loc(v));
}

static PyObject *take(struct maker *m, lu_pv v);

static PyObject *
forced_find(struct lu_forced *fo, int32_t id)
{
    for (int i = 0; fo != NULL && i < fo->nobjs; i++) {
        if (fo->objs[i].id == id) {
            return fo->objs[i].obj;
        }
    }
    return NULL;
}

static int
forced_add(struct lu_forced *fo, int32_t id, PyObject *o)
{
    if (fo->nobjs == fo->capobjs) {
        int cap = fo->capobjs ? 2 * fo->capobjs : 8;
        void *p = realloc(fo->objs, sizeof(*fo->objs) * (size_t)cap);
        if (p == NULL) {
            return -1;
        }
        fo->objs = p;
        fo->capobjs = cap;
    }
    fo->objs[fo->nobjs].id = id;
    fo->objs[fo->nobjs++].obj = Py_NewRef(o);
    return 0;
}

/* An instance of tp as object.__new__ makes it, with kv's n/2 names set
   to their values in order: a VK_INST made. */
static PyObject *
instance_new(PyTypeObject *tp, PyObject *const *kv, int n)
{
    PyObject *o = PyType_GenericAlloc(tp, 0);
    for (int i = 0; o != NULL && i + 1 < n; i += 2) {
        if (PyObject_GenericSetAttr(o, kv[i], kv[i + 1]) < 0) {
            Py_CLEAR(o);
        }
    }
    return o;
}

/* Virtual object k of the description, made now unless it was: borrowed,
   or NULL with the pass failed. */
static PyObject *
object(struct maker *m, int k)
{
    if (m->made[k] != NULL) {
        return m->made[k];
    }
    const struct lu_dobj *ob = lu_deopt_obj(m->d, k);
    const lu_pv *fields = m->d->unit->vals + ob->fields;
    PyObject *o = forced_find(m->forced, ob->id);
    if (o != NULL) {
        return m->made[k] = o;
    }
    if ((ob->kind == VK_BOX_INT || ob->kind == VK_BOX_FLOAT) && ob->nfields >= 2) {
        o = take(m, fields[1]);         /* the object the value came in as, where it did */
        if (o == NULL && ob->nfields == 4 && !PyErr_Occurred()) {
            /* Read unboxed from an attribute: the attribute's own object,
               while it is still that number. */
            o = lu_inline_number((PyObject *)(uintptr_t)raw(m, fields[2]), (Py_ssize_t)raw(m, fields[3]),
                                 raw(m, fields[0]), ob->kind == VK_BOX_FLOAT);
        }
    }
    switch (o != NULL ? VK_KINDS : ob->kind) {
    case VK_KINDS:
        break;
    case VK_BOX_INT:
        o = PyLong_FromLongLong((long long)raw(m, fields[0]));
        break;
    case VK_BOX_FLOAT: {
        uint64_t bits = raw(m, fields[0]);
        double v;
        memcpy(&v, &bits, 8);
        o = PyFloat_FromDouble(v);
        break;
    }
    case VK_BOOL:
        o = Py_NewRef(((raw(m, fields[0]) ^ ob->neg) & 1) ? Py_True : Py_False);
        break;
    case VK_TUPLE:
        o = PyTuple_New(ob->nfields);
        for (int i = 0; o != NULL && i < ob->nfields; i++) {
            PyTuple_SET_ITEM(o, i, take(m, fields[i]));
        }
        break;
    case VK_SEQITER: {
        PyObject *seq = take(m, fields[0]);
        o = seq != NULL ? lu_seqiter_at(ob->type, seq, (Py_ssize_t)raw(m, fields[1])) : NULL;
        Py_XDECREF(seq);
        break;
    }
    case VK_METHOD: {
        PyObject *fn = take(m, fields[0]), *self = take(m, fields[1]);
        o = fn != NULL && self != NULL ? PyMethod_New(fn, self) : NULL;
        Py_XDECREF(fn);
        Py_XDECREF(self);
        break;
    }
    case VK_FIELDS:
        o = lu_alloc_instance(ob->type, ob->type);
        for (int i = 0; o != NULL && i < ob->nfields; i++) {
            *(PyObject **)((char *)o + lu_field_offset(ob->type, i)) = take(m, fields[i]);
        }
        break;
    case VK_INST: {
        PyObject *kv[ob->nfields + 1];
        int n = 0;
        while (n < ob->nfields && (kv[n] = take(m, fields[n])) != NULL) {
            n++;
        }
        o = n == ob->nfields ? instance_new(ob->type, kv, n) : NULL;
        while (n > 0) {
            Py_DECREF(kv[--n]);
        }
        break;
    }
    case VK_EXC:
        o = PyObject_CallNoArgs((PyObject *)ob->type);
        break;
    case VK_LIST:
        o = PyList_New(ob->nfields);
        for (int i = 0; o != NULL && i < ob->nfields; i++) {
            PyList_SET_ITEM(o, i, take(m, fields[i]));
        }
        break;
    case VK_SPLICE: {
        PyObject *items[ob->nfields + 1];
        int n = 0;
        while (n < ob->nfields && (items[n] = take(m, fields[n])) != NULL) {
            n++;
        }
        o = n == ob->nfields ? lu_splice(ob->type, items, n) : NULL;
        while (n > 0) {
            Py_DECREF(items[--n]);
        }
        break;
    }
    case VK_DICT:
        o = PyDict_New();
        for (int i = 0; o != NULL && i + 1 < ob->nfields; i += 2) {
            PyObject *key = take(m, fields[i]), *value = take(m, fields[i + 1]);
            if (key == NULL || value == NULL || PyDict_SetItem(o, key, value) < 0) {
                m->failed = 1;
            }
            Py_XDECREF(key);
            Py_XDECREF(value);
        }
        break;
    case VK_GEN: {
        /* As it stands: suspended with what its frame holds, or finished.
           One that runs gets its frame's contents where the frame is made. */
        PyObject *fn = take(m, fields[GEN_FUNCV]);
        fn = fn != NULL ? fn : take(m, fields[GEN_FN]);
        int64_t pc = (int64_t)raw(m, fields[GEN_PC]);
        o = fn != NULL ? lu_gen_blank((PyFunctionObject *)fn) : NULL;
        if (o != NULL && pc != GEN_RUNNING) {
            struct lu_frame *f = &((struct lu_gen *)o)->frame;
            int held = pc < 0 ? 0 : f->code->co_nlocalsplus + (int)raw(m, fields[GEN_DEPTH]);
            for (int i = 0; i < held; i++) {
                f->localsplus[i] = take(m, fields[GEN_SLOTS + i]);
            }
            f->stackpointer = f->localsplus + (pc < 0 ? f->code->co_nlocalsplus : held);
            lu_gen_stand(o, (int)pc);
        }
        Py_XDECREF(fn);
        break;
    }
    default:
        PyErr_SetString(PyExc_SystemError, "an exit names a virtual object of no kind");
        break;
    }
    if (o == NULL || m->failed) {
        m->failed = 1;
        Py_XDECREF(o);
        return NULL;
    }
    m->refs[m->nrefs++] = o;
    if (m->forced != NULL && forced_add(m->forced, ob->id, o) < 0) {
        PyErr_NoMemory();
        m->failed = 1;
        return NULL;
    }
    return m->made[k] = o;
}

/* A new reference to what v names, or NULL (an unbound slot, or the pass
   failed). */
static PyObject *
take(struct maker *m, lu_pv v)
{
    switch (LU_PV_KIND(v)) {
    case DV_CONST: {
        PyObject *k = (PyObject *)(intptr_t)m->d->unit->imms[LU_PV_ARG(v)];
        if (k == &lu_jit_exc_prev) {
            k = PyThreadState_Get()->exc_info->exc_value;      /* what PUSH_EXC_INFO would have saved */
            k = k != NULL ? k : Py_None;
        }
        return Py_NewRef(k);
    }
    case DV_OBJ:
        return Py_XNewRef((PyObject *)(uintptr_t)at(m, lu_pv_loc(v)));
    case DV_VIRT: {
        PyObject *o = object(m, (int)LU_PV_ARG(v));
        return o == &lu_jit_unbound ? NULL : Py_XNewRef(o);     /* a number's local, unbound */
    }
    default:
        return NULL;
    }
}

static int
maker_init(struct maker *m, struct lu_frame *root, struct lu_deopt *d,
           const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills)
{
    *m = (struct maker){ .gpr = gpr, .fpr = fpr, .spills = spills, .d = d, .forced = root->jit_forced };
    size_t n = (size_t)(d->nobjs ? d->nobjs : 1);
    m->made = calloc(n, sizeof(PyObject *));
    m->refs = calloc(n, sizeof(PyObject *));
    if (m->made == NULL || m->refs == NULL) {
        free(m->made);
        free(m->refs);
        PyErr_NoMemory();
        return -1;
    }
    return 0;
}

static void
maker_done(struct maker *m)
{
    for (int i = 0; i < m->nrefs; i++) {
        Py_DECREF(m->refs[i]);
    }
    free(m->made);
    free(m->refs);
}

/* The published description's reader: the registers a slow path saved,
   when it says so, and the spill words. */
static int
published(struct maker *m, struct lu_frame *f, struct lu_deopt *d)
{
    const uint64_t *saved = d->regs ? f->jit_regs : NULL;
    return maker_init(m, f, d, saved, saved != NULL ? saved + 16 : NULL, f->jit_spills);
}

void
lu_jit_forced_free(struct lu_forced *fo)
{
    if (fo == NULL) {
        return;
    }
    for (int i = 0; i < fo->nobjs; i++) {
        Py_DECREF(fo->objs[i].obj);
    }
    free(fo->objs);
    free(fo->frames);
    free(fo);
}

static struct lu_forced *
forced_of(struct lu_frame *root)
{
    if (root->jit_forced == NULL) {
        root->jit_forced = calloc(1, sizeof(struct lu_forced));
        if (root->jit_forced == NULL) {
            PyErr_NoMemory();
        }
    }
    return root->jit_forced;
}

/* The instruction frame k of d shows as the one after the last it began:
   the innermost is in its call, the others are past theirs. */
static lu_codeunit *
shown(struct lu_deopt *d, int k)
{
    const struct lu_deopt_frame *fr = lu_deopt_frame(d, k);
    return lu_code_start(fr->code) + (k == d->nframes - 1 ? d->instr : fr->pc);
}

lu_codeunit *
lu_jit_instr(struct lu_frame *f)
{
    struct lu_deopt *d = __atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE);
    return d != NULL ? shown(d, 0) : f->instr;
}

PyCodeObject *
lu_jit_running_code(struct lu_frame *f)
{
    struct lu_deopt *d = __atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE);
    return d != NULL ? lu_deopt_frame(d, d->nframes - 1)->code : f->code;
}

/* The builtins of the innermost frame f stands for that is not the
   kernel's, read without making anything: 1 with *out set, 0 when f
   stands for kernel frames only, -1 when the function of that frame is
   a value compiled code holds. */
int
lu_jit_user_builtins(struct lu_frame *f, PyObject **out)
{
    struct lu_deopt *d = __atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE);
    for (int k = d != NULL ? d->nframes - 1 : 0; k >= 0; k--) {
        const struct lu_deopt_frame *fr = d != NULL ? lu_deopt_frame(d, k) : NULL;
        if ((fr != NULL ? fr->code : f->code)->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN)) {
            continue;
        }
        if (k == 0) {
            *out = f->builtins;
            return 1;
        }
        if (LU_PV_KIND(fr->funcv) != DV_NULL || fr->func == NULL || !PyFunction_Check(fr->func)) {
            return -1;
        }
        *out = ((PyFunctionObject *)fr->func)->func_builtins;
        return 1;
    }
    return 0;
}

/* The root gets real slots, each with a reference of its own, its
   instruction, and the interpreter's owner. -1 with an exception set. */
static int
sync_root(struct maker *m, struct lu_frame *f)
{
    const struct lu_deopt_frame *fr = lu_deopt_frame(m->d, 0);
    const lu_pv *vals = m->d->unit->vals + fr->vals;
    for (int i = 0; i < fr->nlocals; i++) {
        f->localsplus[i] = take(m, vals[i]);
    }
    f->stackpointer = f->localsplus + fr->nlocals;
    f->instr = shown(m->d, 0);
    f->last = lu_instr_before(f->code, f->instr);
    __atomic_store_n(&f->owner, f->home, __ATOMIC_RELEASE);
    return m->failed ? -1 : 0;
}

int
lu_jit_sync(struct lu_frame *f)
{
    struct lu_deopt *d = __atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE);
    if (d == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "cannot write the locals of a frame compiled code is running");
        return -1;
    }
    struct maker m;
    if (forced_of(f) == NULL || published(&m, f, d) < 0) {
        return -1;
    }
    int r = sync_root(&m, f);
    maker_done(&m);
    return r;
}

/* Local i of a root frame compiled code runs, as a new reference: the
   frame gets its slots (so that what is read once is the object read
   again), or NULL (unbound) outside a call that published. */
PyObject *
lu_jit_slot(struct lu_frame *f, int i)
{
    if (__atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE) == NULL) {
        return NULL;
    }
    /* A reader with an exception in flight (a traceback being made) keeps it. */
    PyObject *exc = PyErr_GetRaisedException();
    int r = lu_jit_sync(f);
    PyErr_Clear();
    PyErr_SetRaisedException(exc);
    return r < 0 ? NULL : Py_XNewRef(f->localsplus[i]);
}

/* The frame's function: the one called, where the unit knew it by its
   code only and carries it as a value. */
static PyObject *
frame_func(struct maker *m, const struct lu_deopt_frame *fr)
{
    return LU_PV_KIND(fr->funcv) == DV_OBJ ? (PyObject *)(uintptr_t)at(m, lu_pv_loc(fr->funcv))
        : LU_PV_KIND(fr->funcv) == DV_CONST ? (PyObject *)(intptr_t)m->d->unit->imms[LU_PV_ARG(fr->funcv)] : fr->func;
}

/* Inlined frame k of d is the frame of a generator the unit resumed in
   line: the generator is made (once per activation) and its own frame
   runs above `previous` with the frame's locals, as a resumption by the
   interpreter leaves it. A walker gets it so (counted 0: compiled code
   has counted its inlined frames across the call); the exit counts it. */
static struct lu_frame *
gen_frame(PyThreadState *ts, struct maker *m, const struct lu_deopt_frame *fr, struct lu_frame *previous, int k,
          int counted)
{
    /* A generator the unit made, or one that exists whose body it runs. */
    PyObject *gen = LU_PV_KIND(fr->gen) == DV_VIRT ? object(m, (int)LU_PV_ARG(fr->gen))
        : LU_PV_KIND(fr->gen) == DV_CONST ? (PyObject *)(intptr_t)m->d->unit->imms[LU_PV_ARG(fr->gen)]
        : (PyObject *)(uintptr_t)at(m, lu_pv_loc(fr->gen));
    if (gen == NULL) {
        return NULL;
    }
    struct lu_frame *f = &((struct lu_gen *)gen)->frame;
    if (f->previous == NULL) {
        lu_gen_enter(ts, gen, previous);
        __atomic_store_n(&f->owner, LU_FRAME_GENERATOR, __ATOMIC_RELEASE);     /* its words are its own again */
        const lu_pv *vals = m->d->unit->vals + fr->vals;
        for (int i = 0; i < fr->nlocals; i++) {
            f->localsplus[i] = take(m, vals[i]);
        }
        f->stackpointer = f->localsplus + fr->nlocals;
    }
    if (counted) {
        lu_depth_take(ts, f->code);
        lu_gen_enter_exc(ts, gen);
    }
    f->previous = previous;     /* a walker's record of its caller gave way to the caller's frame */
    f->ret = fr->ret;
    f->instr = shown(m->d, k);
    f->last = lu_instr_before(f->code, f->instr);
    return f;
}

/* A record for inlined frame k of d, as the interpreter would hold the
   frame during the call: its locals, no stack. */
static struct lu_frame *
record(struct maker *m, int k, struct lu_frame *previous)
{
    const struct lu_deopt_frame *fr = lu_deopt_frame(m->d, k);
    if (LU_PV_KIND(fr->gen) != DV_NULL) {
        return gen_frame(PyThreadState_Get(), m, fr, previous, k, 0);
    }
    PyFunctionObject *func = (PyFunctionObject *)frame_func(m, fr);
    struct lu_frame *f = calloc(1, sizeof(PyObject *) * (size_t)fr->code->co_framesize);
    if (f == NULL) {
        PyErr_NoMemory();
        m->failed = 1;
        return NULL;
    }
    f->code = (PyCodeObject *)Py_NewRef(fr->code);
    f->func = Py_NewRef((PyObject *)func);
    f->globals = func->func_globals;
    f->builtins = func->func_builtins;
    f->owner = lu_frame_owner_for(fr->code);
    f->ret = fr->ret;
    f->previous = previous;
    const lu_pv *vals = m->d->unit->vals + fr->vals;
    for (int i = 0; i < fr->nlocals; i++) {
        f->localsplus[i] = take(m, vals[i]);
    }
    f->stackpointer = f->localsplus + fr->nlocals;
    f->instr = shown(m->d, k);
    f->last = lu_instr_before(f->code, f->instr);
    return f;
}

static int
own_frame(struct lu_frame *f)
{
    for (struct lu_frame *p = (struct lu_frame *)PyThreadState_Get()->current_frame; p != NULL; p = p->previous) {
        if (p == f) {
            return 1;
        }
    }
    return 0;
}

int
lu_jit_surface(struct lu_frame *f)
{
    if (f->home != LU_FRAME_GENERATOR || f->jit_desc != NULL) {
        return 0;
    }
    /* A generator whose body this task's compiled code runs in line: the
       activation that runs it makes its frames real, this one among them. */
    for (struct lu_frame *p = (struct lu_frame *)PyThreadState_Get()->current_frame;
         p != NULL && f->owner == LU_FRAME_JIT; p = p->previous) {
        if (p->owner == LU_FRAME_JIT && p->jit_desc != NULL) {
            lu_jit_force(p, 1);
        }
    }
    return f->owner != LU_FRAME_JIT;
}

struct lu_frame *
lu_jit_force(struct lu_frame *f, int all)
{
    struct lu_deopt *d = __atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE);
    if (d == NULL || d->nframes == 1 || !(all || d->visible) || !own_frame(f)) {
        return f;
    }
    struct lu_forced *fo = forced_of(f);
    struct maker m;
    if (fo == NULL || published(&m, f, d) < 0) {
        return NULL;
    }
    if (fo->frames == NULL) {
        fo->frames = calloc((size_t)d->nframes, sizeof(struct lu_frame *));
        if (fo->frames == NULL) {
            PyErr_NoMemory();
            m.failed = 1;
        }
    }
    if (!m.failed && f->owner == LU_FRAME_JIT) {
        sync_root(&m, f);
    }
    struct lu_frame *top = f;
    for (int k = 1; !m.failed && k < d->nframes; k++) {
        if (k - 1 < fo->nframes) {
            top = fo->frames[k - 1];
            continue;
        }
        struct lu_frame *r = record(&m, k, top);
        if (r == NULL) {
            break;
        }
        fo->frames[fo->nframes++] = r;
        top = r;
    }
    int failed = m.failed;
    maker_done(&m);
    if (failed) {
        return NULL;            /* what was made stays: the exit finds it */
    }
    PyThreadState *ts = PyThreadState_Get();
    if (ts->current_frame == (struct _PyInterpreterFrame *)f) {
        ts->current_frame = (struct _PyInterpreterFrame *)top;
    }
    return top;
}

/* Inlined frame k, pushed above top with its locals: moved from the
   record a walker made, which keeps what the walker wrote, else made now.
   NULL with an exception set when the datastack has no room. */
static struct lu_frame *
push_inlined(PyThreadState *ts, struct maker *m, int k, struct lu_frame *top)
{
    const struct lu_deopt_frame *fr = lu_deopt_frame(m->d, k);
    if (LU_PV_KIND(fr->gen) != DV_NULL) {
        struct lu_frame *g = gen_frame(ts, m, fr, top, k, 1);
        if (g != NULL) {
            g->instr = g->last = lu_code_start(fr->code) + fr->pc;
            if (m->forced != NULL && k - 1 < m->forced->nframes) {
                m->forced->frames[k - 1] = NULL;        /* a walker's record of it was this frame */
            }
        }
        return g;
    }
    struct lu_frame *f = lu_frame_push(ts, (PyFunctionObject *)frame_func(m, fr), NULL);
    if (f == NULL) {
        return NULL;
    }
    struct lu_frame *rec = m->forced != NULL && k - 1 < m->forced->nframes ? m->forced->frames[k - 1] : NULL;
    if (rec != NULL) {
        for (int i = 0; i < fr->nlocals; i++) {
            f->localsplus[i] = rec->localsplus[i];
        }
        f->locals = rec->locals;
        lu_frame_rehome(rec, f);
        Py_DECREF(rec->code);
        Py_DECREF(rec->func);
        free(rec);
        m->forced->frames[k - 1] = NULL;
    }
    else {
        const lu_pv *vals = m->d->unit->vals + fr->vals;
        for (int i = 0; i < fr->nlocals; i++) {
            f->localsplus[i] = take(m, vals[i]);
        }
    }
    f->ret = fr->ret;
    f->instr = f->last = lu_code_start(fr->code) + fr->pc;
    f->previous = top;
    return f;
}

int
lu_jit_deopt(PyThreadState *ts, struct lu_frame *root, struct lu_deopt *d,
             const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills)
{
    lu_jit_note_exit(d);
    struct maker m;
    int nomem = maker_init(&m, root, d, gpr, fpr, spills) < 0;
    /* A walker that made the root real (lu_jit_sync) left it its slots. */
    int synced = root->owner != LU_FRAME_JIT;
    const struct lu_deopt_frame *fr = lu_deopt_frame(d, 0);
    const lu_pv *vals = d->unit->vals + fr->vals;
    for (int i = 0; !nomem && !synced && i < fr->nlocals; i++) {
        root->localsplus[i] = take(&m, vals[i]);
    }
    PyObject **sp = root->localsplus + (nomem && !synced ? 0 : fr->nlocals);
    for (int j = 0; !nomem && j < fr->nstack; j++) {
        *sp++ = take(&m, vals[fr->nlocals + j]);
    }
    root->stackpointer = sp;
    root->instr = root->last = lu_code_start(fr->code) + fr->pc;
    root->jit_desc = NULL;
    root->owner = root->home;
    struct lu_frame *top = root;
    int unpushed = 0;
    /* Compiled code counts its inlined frames only across its calls
       (runtime/jit/x86_64/emit.c publish): pushing them counts them. */
    for (int k = 1; !nomem && k < d->nframes; k++) {
        struct lu_frame *f = push_inlined(ts, &m, k, top);
        if (f == NULL) {
            unpushed = 1;       /* its caller raises at its call */
            break;
        }
        fr = lu_deopt_frame(d, k);
        vals = d->unit->vals + fr->vals;
        sp = f->localsplus + fr->nlocals;
        for (int j = 0; j < fr->nstack; j++) {
            *sp++ = take(&m, vals[fr->nlocals + j]);
        }
        f->stackpointer = sp;
        top = f;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)top;
    /* The frames go on in the interpreter, which instruments a code only
       at RESUME: each is brought up to the tools as RESUME would (a tool
       turned on since the unit's build finds them instrumented). */
    int uninstrumented = 0;
    uintptr_t version = __atomic_load_n(&ts->interp->monitoring_version, __ATOMIC_RELAXED);
    for (struct lu_frame *f = top; !nomem && !unpushed && !uninstrumented; f = f->previous) {
        uninstrumented = __atomic_load_n(&f->code->_co_instrumentation_version, __ATOMIC_ACQUIRE) != version
            && lu_instrument(f->code, ts->interp) < 0;
        if (f == root) {
            break;
        }
    }
    int failed = nomem || unpushed || uninstrumented || m.failed;
    if (d->error && !unpushed && !nomem) {
        /* The raising instruction consumed its operands; the interpreter
           unwinds from the instruction after it. */
        for (int i = 0; i < d->drop && top->stackpointer > top->localsplus + top->code->co_nlocalsplus; i++) {
            PyObject *v = *--top->stackpointer;
            Py_XDECREF(v);
        }
        top->instr = top->last = lu_code_start(top->code)
            + lu_jit_next_pc(top->code, (int)(top->instr - lu_code_start(top->code)));
    }
    else if (failed && !unpushed) {
        /* MemoryError at the instruction the exit names, before it ran. */
        top->instr = top->last = lu_code_start(top->code)
            + lu_jit_next_pc(top->code, (int)(top->instr - lu_code_start(top->code)));
    }
    if (d->reason == EXIT_STRESS) {
        __atomic_store_n(d->unit->stress_counter, (long)_PyRuntime.stress.deopt, __ATOMIC_RELAXED);
    }
    /* Compiled code's own references go last: every slot has its own. */
    if (!nomem) {
        for (uint32_t i = 0; i < d->nowned; i++) {
            PyObject *o = (PyObject *)(uintptr_t)at(&m, d->unit->owned[d->owned + i]);
            Py_XDECREF(o);
        }
        maker_done(&m);
    }
    struct lu_forced *fo = root->jit_forced;
    root->jit_forced = NULL;
    if (fo != NULL) {
        /* A record no frame of this exit took (none in a correct unit). */
        for (int k = 0; k < fo->nframes; k++) {
            struct lu_frame *rec = fo->frames[k];
            if (rec != NULL) {
                for (PyObject **p = rec->localsplus; p < rec->stackpointer; p++) {
                    Py_XDECREF(*p);
                }
                Py_XDECREF(rec->locals);
                Py_DECREF(rec->code);
                Py_DECREF(rec->func);
                free(rec);
            }
        }
        lu_jit_forced_free(fo);
    }
    return d->error || failed ? LU_JIT_ERROR : LU_JIT_DEOPT;
}
