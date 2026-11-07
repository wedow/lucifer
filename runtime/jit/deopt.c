/* Leaving compiled code (DESIGN 6.2 step 6): the frames an exit describes
   are made real at their unexecuted boundary, and the interpreter runs on
   from there.

   The root frame is the one compiled code runs in. From the unit's entry
   it is LU_FRAME_JIT: compiled code owns its locals and its stack, and
   the slots hold nothing, but for the leading parameters it borrows that
   the slots below stackpointer keep. An exit fills every other local and
   every stack entry from its description and gives the frame back to the
   interpreter. Each inlined call becomes a frame pushed above it, as
   the interpreter would have pushed it at the call, and the innermost is
   the task's current frame. References: an entry compiled code owns moves
   into the frame; any other entry takes a new reference.

   While the root calls something that may run Python, it publishes the
   description of that call's state and its spill words (jit_desc,
   jit_spills), where the allocator keeps every local it names during the
   call. A walker then reads its locals from there (lu_jit_slot), and a
   write to them (f_locals, PEP 667) first gives the frame real slots
   (lu_jit_sync); compiled code sees that when the call returns and leaves,
   and the exit keeps the slots the walker wrote.

   A virtual box (an int or float compiled code never boxed) is built once
   per exit, so every slot that names the same value gets the same object.
   Released values go last: a finalizer then sees whole frames.

   A call from an inlined frame runs with the inlined frames pushed, lent
   their code, function and locals by compiled code (lu_jit_frames_push).
   A callee that made a frame object of one keeps them all pushed
   (lu_jit_frames_pop), and the exit taken next makes them ordinary frames
   and fills their stacks instead of pushing new ones: the frame object
   stays the frame's, and the frames keep the locals they have, which the
   callee may have written through f_locals. */
#include "jit/target.h"
#include "jit/jit.h"

#include <stdlib.h>
#include <string.h>

void lu_jit_note_exit(struct lu_deopt *d);
int lu_jit_handles(struct lu_deopt *d, int k);
int lu_jit_next_pc(PyCodeObject *code, int pc);

struct built {
    int32_t vid;
    PyObject *obj;
};

struct regs {
    const uint64_t *gpr, *fpr, *spills;
    struct built *built;
    int nbuilt, capbuilt;
};

static uint64_t
at(struct regs *r, int32_t loc)
{
    if (LU_LOC_IS_SPILL(loc)) {
        return *(const uint64_t *)((const char *)r->spills - 8 * LU_LOC_SPILL_INDEX(loc));
    }
    if (r->gpr == NULL) {
        Py_FatalError("lucifer jit: a published value in a register");
    }
    return loc >= LU_LOC_FPR ? r->fpr[loc - LU_LOC_FPR] : r->gpr[loc];
}

static uint64_t
raw(struct regs *r, const struct lu_dv *dv)
{
    return dv->loc == LU_LOC_NONE ? (uint64_t)dv->imm : at(r, dv->loc);
}

/* A new reference to what dv names (or the reference compiled code owns
   for it). */
static PyObject *
take(struct regs *r, const struct lu_dv *dv)
{
    switch (dv->kind) {
    case DV_NULL:
        return NULL;
    case DV_CONST:
        return Py_NewRef((PyObject *)(intptr_t)dv->imm);
    case DV_OBJ: {
        PyObject *o = (PyObject *)(uintptr_t)raw(r, dv);
        return dv->owned ? o : Py_XNewRef(o);
    }
    default:
        break;
    }
    if (dv->maybe_null && !(dv->bitloc == LU_LOC_NONE ? dv->bitimm : (at(r, dv->bitloc) & 1))) {
        return NULL;            /* a local still unbound on this path */
    }
    for (int i = 0; i < r->nbuilt; i++) {
        if (r->built[i].vid == dv->vid) {
            return Py_NewRef(r->built[i].obj);
        }
    }
    uint64_t bits = raw(r, dv);
    PyObject *o;
    if (dv->kind == DV_NEW_DICT) {
        o = PyDict_New();
    }
    else if (dv->kind == DV_BOX_I64) {
        o = PyLong_FromLongLong((long long)bits);
    }
    else {
        double d;
        memcpy(&d, &bits, 8);
        o = PyFloat_FromDouble(d);
    }
    if (o == NULL) {
        Py_FatalError("lucifer jit: out of memory rebuilding a frame");
    }
    if (r->nbuilt == r->capbuilt) {
        r->capbuilt = r->capbuilt ? r->capbuilt * 2 : 8;
        r->built = realloc(r->built, sizeof(struct built) * (size_t)r->capbuilt);
    }
    r->built[r->nbuilt++] = (struct built){ dv->vid, o };
    return Py_NewRef(o);
}

/* An inlined frame, pushed above top at pc with its locals and an empty
   stack; the references compiled code owns move into it. */
static struct lu_frame *
push_inlined(PyThreadState *ts, struct regs *r, struct lu_deopt_frame *fr, struct lu_frame *top)
{
    /* GUARD_DEPTH at entry left room for it. */
    struct lu_frame *f = lu_frame_push(ts, (PyFunctionObject *)fr->func, NULL);
    if (f == NULL) {
        Py_FatalError("lucifer jit: no room to rebuild an inlined frame");
    }
    for (int i = 0; i < fr->nlocals; i++) {
        f->localsplus[i] = take(r, &fr->vals[i]);
    }
    f->instr = f->last = lu_code_start(fr->code) + fr->pc;
    f->previous = top;
    return f;
}

/* Local i of a root frame compiled code runs, as a new reference: from
   the state it published at its call, or NULL (unbound) when it is in no
   such call (a finalizer run by a release in compiled code). */
PyObject *
lu_jit_slot(struct lu_frame *f, int i)
{
    struct lu_deopt *d = __atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE);
    if (d == NULL) {
        return NULL;
    }
    struct regs r = { NULL, NULL, f->jit_spills, NULL, 0, 0 };
    struct lu_dv dv = d->frames[0].vals[i];
    dv.owned = 0;
    PyObject *v = take(&r, &dv);
    for (int k = 0; k < r.nbuilt; k++) {
        Py_DECREF(r.built[k].obj);
    }
    free(r.built);
    return v;
}

/* The frame gets real slots, each with a reference of its own, and the
   interpreter's owner: compiled code leaves it when its call returns. */
int
lu_jit_sync(struct lu_frame *f)
{
    struct lu_deopt *d = __atomic_load_n(&f->jit_desc, __ATOMIC_ACQUIRE);
    if (d == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "cannot write the locals of a frame compiled code is running");
        return -1;
    }
    struct regs r = { NULL, NULL, f->jit_spills, NULL, 0, 0 };
    struct lu_deopt_frame *fr = &d->frames[0];
    for (int i = 0; i < fr->nlocals; i++) {
        struct lu_dv dv = fr->vals[i];
        dv.owned = 0;
        if (f->localsplus + i >= f->stackpointer) {
            f->localsplus[i] = take(&r, &dv);
        }
    }
    for (int k = 0; k < r.nbuilt; k++) {
        Py_DECREF(r.built[k].obj);
    }
    free(r.built);
    f->stackpointer = f->localsplus + fr->nlocals;
    __atomic_store_n(&f->owner, lu_frame_owner_for(f->code), __ATOMIC_RELEASE);
    return 0;
}

void
lu_jit_frames_push(PyThreadState *ts, struct lu_frame *root, struct lu_deopt *d,
                   const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills)
{
    struct regs r = { gpr, fpr, spills, NULL, 0, 0 };
    struct lu_frame *top = root;
    for (int k = 1; k < d->nframes; k++) {
        struct lu_deopt_frame *fr = &d->frames[k];
        struct lu_frame *f = lu_frame_push_lent(ts, (PyFunctionObject *)fr->func);
        if (f == NULL) {
            Py_FatalError("lucifer jit: no room for an inlined frame");
        }
        /* Locals are objects here (the optimizer boxes them): lent, as
           the code and function are. */
        for (int i = 0; i < fr->nlocals; i++) {
            const struct lu_dv *dv = &fr->vals[i];
            if (dv->kind >= DV_BOX_I64) {
                Py_FatalError("lucifer jit: a virtual local in a lent frame");
            }
            f->localsplus[i] = dv->kind == DV_NULL ? NULL : (PyObject *)(uintptr_t)raw(&r, dv);
        }
        /* Each caller is past its call; the innermost shows the helper's instruction as the one running. */
        f->instr = f->last = lu_code_start(fr->code) + (k < d->nframes - 1 ? fr->pc : d->call_pc);
        f->previous = top;
        top = f;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)top;
}

/* deopt:N keeps the frames of every Nth call too; their exit then counts
   as a stress exit, from which nothing is learned. */
static _Thread_local unsigned long stress_calls;
static _Thread_local int kept_by_stress;

void
lu_jit_frames_pop(PyThreadState *ts, struct lu_frame *root)
{
    struct lu_frame *top = (struct lu_frame *)ts->current_frame;
    int keep = 0;
    for (struct lu_frame *f = top; f != root; f = f->previous) {
        keep |= f->frame_obj != NULL;
    }
    unsigned long n = _PyRuntime.stress.deopt;
    kept_by_stress = !keep && n != 0 && ++stress_calls % n == 0;
    if (keep || kept_by_stress) {
        return;
    }
    while (top != root) {
        struct lu_frame *f = top;
        top = f->previous;
        lu_frame_pop_lent(ts, f);
    }
    ts->current_frame = (struct _PyInterpreterFrame *)root;
}

/* Whether an exception leaves every inlined frame of the exit as the
   interpreter would unwind them unseen: each is kernel code, which makes
   no traceback entry and no event, with no handler where it stands. */
static int
unwinds_unseen(struct lu_deopt *d)
{
    for (int k = 1; k < d->nframes; k++) {
        if (!(d->frames[k].code->co_flags & (LU_CO_KERNEL | LU_CO_HIDDEN)) || lu_jit_handles(d, k)) {
            return 0;
        }
    }
    return 1;
}

int
lu_jit_deopt(PyThreadState *ts, struct lu_frame *root, struct lu_deopt *d,
             const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills)
{
    struct regs r = { gpr, fpr, spills, NULL, 0, 0 };
    /* Frames a call kept pushed above the root, innermost first. */
    struct lu_frame *kept = (struct lu_frame *)ts->current_frame != root
        ? (struct lu_frame *)ts->current_frame : NULL;
    if (kept != NULL && kept_by_stress) {
        struct lu_deopt stressed = *d;
        stressed.reason = EXIT_STRESS;
        stressed.stress = NULL;
        lu_jit_note_exit(&stressed);
    }
    else {
        lu_jit_note_exit(d);
    }
    int depth = 0;
    for (struct lu_frame *f = kept; f != NULL && f != root; f = f->previous) {
        depth++;
    }
    if (kept != NULL && depth != d->nframes - 1) {
        Py_FatalError("lucifer jit: kept frames do not match the exit");
    }
    struct lu_deopt_frame *fr = &d->frames[0];
    int nvals = 1;
    for (int k = 0; k < d->nframes; k++) {
        nvals += d->frames[k].nlocals + d->frames[k].nstack;
    }
    PyObject **old = malloc(sizeof(PyObject *) * (size_t)nvals);
    int nold = 0;
    /* A walker that wrote the locals (lu_jit_sync) made the slots the
       frame's: compiled code's own references go. */
    int synced = root->owner != LU_FRAME_JIT;
    for (int i = 0; i < fr->nlocals; i++) {
        const struct lu_dv *dv = &fr->vals[i];
        if (!synced && root->localsplus + i >= root->stackpointer) {
            root->localsplus[i] = take(&r, dv);
        }
        else if (dv->kind == DV_OBJ && dv->owned) {
            old[nold++] = (PyObject *)(uintptr_t)raw(&r, dv);
        }
    }
    PyObject **sp = root->localsplus + fr->nlocals;
    for (int j = 0; j < fr->nstack; j++) {
        *sp++ = take(&r, &fr->vals[fr->nlocals + j]);
    }
    root->stackpointer = sp;
    root->instr = root->last = lu_code_start(fr->code) + fr->pc;
    root->jit_desc = NULL;
    root->owner = lu_frame_owner_for(root->code);
    struct lu_frame *top = root;
    /* An exception the inlined frames would pass on unseen is the root's
       at its call: they are not made, and what they held is released. */
    int unseen = d->error && kept == NULL && d->nframes > 1 && unwinds_unseen(d);
    for (int k = 1; unseen && k < d->nframes; k++) {
        fr = &d->frames[k];
        for (int i = 0; i < fr->nlocals + fr->nstack; i++) {
            if (fr->vals[i].kind == DV_OBJ && fr->vals[i].owned) {
                old[nold++] = (PyObject *)(uintptr_t)raw(&r, &fr->vals[i]);
            }
        }
    }
    for (int k = 1; !unseen && k < d->nframes; k++) {
        fr = &d->frames[k];
        struct lu_frame *f;
        if (kept != NULL) {
            f = kept;
            for (int j = d->nframes - 1; j > k; j--) {
                f = f->previous;
            }
            /* The frame takes what it was lent. Each local's reference goes
               to whoever holds the lent value now: the slot, or the frame
               object's list of values f_locals replaced. */
            Py_INCREF(f->code);
            Py_INCREF(f->func);
            for (int i = 0; i < fr->nlocals; i++) {
                (void)take(&r, &fr->vals[i]);
            }
            f->instr = f->last = lu_code_start(fr->code) + fr->pc;
        }
        else {
            f = push_inlined(ts, &r, fr, top);
        }
        sp = f->localsplus + fr->nlocals;
        for (int j = 0; j < fr->nstack; j++) {
            *sp++ = take(&r, &fr->vals[fr->nlocals + j]);
        }
        f->stackpointer = sp;
        top = f;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)top;
    if (d->error && !unseen) {
        /* The raising instruction consumed its operands; the interpreter
           unwinds from the instruction after it. */
        for (int i = 0; i < d->drop; i++) {
            PyObject *v = *--top->stackpointer;
            Py_XDECREF(v);
        }
        top->instr = top->last = lu_code_start(top->code)
            + lu_jit_next_pc(top->code, (int)(top->instr - lu_code_start(top->code)));
    }
    if (d->reason == EXIT_STRESS && d->stress != NULL) {
        __atomic_store_n(d->stress, (long)_PyRuntime.stress.deopt, __ATOMIC_RELAXED);
    }
    for (int i = 0; i < r.nbuilt; i++) {
        Py_DECREF(r.built[i].obj);      /* the build's own reference: each slot took one */
    }
    free(r.built);
    for (int i = 0; i < nold; i++) {
        Py_XDECREF(old[i]);
    }
    free(old);
    return d->error ? LU_JIT_ERROR : LU_JIT_DEOPT;
}
