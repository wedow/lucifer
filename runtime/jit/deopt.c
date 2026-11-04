/* Leaving compiled code (DESIGN 6.2 step 6): the frames an exit describes
   are made real at their unexecuted boundary, and the interpreter runs on
   from there.

   The root frame is the one compiled code ran in: each local whose slot
   does not already hold its value gets it (the slot may hold an older
   object while compiled code kept an int raw), and its stack gets every
   entry. Each inlined call becomes a frame pushed above it, as the
   interpreter would have pushed it at the call, and the innermost is the
   task's current frame. References: an entry compiled code owns moves
   into the frame; any other entry takes a new reference.

   A virtual box (an int or float compiled code never boxed) is built once
   per exit, so every slot that names the same value gets the same object.
   Old slot values are released last: a finalizer then sees whole frames. */
#include "jit/target.h"
#include "jit/jit.h"

#include <stdlib.h>
#include <string.h>

void lu_jit_note_exit(struct lu_deopt *d);
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
raw(struct regs *r, const struct lu_dv *dv)
{
    if (dv->loc == LU_LOC_NONE) {
        return (uint64_t)dv->imm;
    }
    if (LU_LOC_IS_SPILL(dv->loc)) {
        return *(const uint64_t *)((const char *)r->spills - 8 * LU_LOC_SPILL_INDEX(dv->loc));
    }
    return dv->loc >= LU_LOC_FPR ? r->fpr[dv->loc - LU_LOC_FPR] : r->gpr[dv->loc];
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
    for (int i = 0; i < r->nbuilt; i++) {
        if (r->built[i].vid == dv->vid) {
            return Py_NewRef(r->built[i].obj);
        }
    }
    uint64_t bits = raw(r, dv);
    PyObject *o;
    if (dv->kind == DV_BOX_I64) {
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

/* Whether the root slot already holds what dv names. */
static int
holds(struct regs *r, const struct lu_dv *dv, PyObject *cur)
{
    switch (dv->kind) {
    case DV_NULL: return cur == NULL;
    case DV_CONST: return cur == (PyObject *)(intptr_t)dv->imm;
    case DV_OBJ: return cur == (PyObject *)(uintptr_t)raw(r, dv);
    default: return 0;
    }
}

int
lu_jit_deopt(PyThreadState *ts, struct lu_frame *root, struct lu_deopt *d,
             const uint64_t *gpr, const uint64_t *fpr, const uint64_t *spills)
{
    struct regs r = { gpr, fpr, spills, NULL, 0, 0 };
    lu_jit_note_exit(d);
    struct lu_deopt_frame *fr = &d->frames[0];
    PyObject **old = malloc(sizeof(PyObject *) * (size_t)(fr->nlocals + 1));
    int nold = 0;
    for (int i = 0; i < fr->nlocals; i++) {
        PyObject *cur = root->localsplus[i];
        if (holds(&r, &fr->vals[i], cur)) {
            continue;
        }
        struct lu_dv dv = fr->vals[i];
        dv.owned = 0;           /* the slot takes its own reference */
        root->localsplus[i] = take(&r, &dv);
        old[nold++] = cur;
    }
    PyObject **sp = root->localsplus + fr->nlocals;
    for (int j = 0; j < fr->nstack; j++) {
        *sp++ = take(&r, &fr->vals[fr->nlocals + j]);
    }
    root->stackpointer = sp;
    root->instr = root->last = lu_code_start(fr->code) + fr->pc;
    struct lu_frame *top = root;
    for (int k = 1; k < d->nframes; k++) {
        fr = &d->frames[k];
        struct lu_frame *f = lu_frame_push(ts, (PyFunctionObject *)fr->func, NULL);
        if (f == NULL) {
            Py_FatalError("lucifer jit: no room to rebuild an inlined frame");
        }
        for (int i = 0; i < fr->nlocals; i++) {
            f->localsplus[i] = take(&r, &fr->vals[i]);
        }
        sp = f->localsplus + fr->nlocals;
        for (int j = 0; j < fr->nstack; j++) {
            *sp++ = take(&r, &fr->vals[fr->nlocals + j]);
        }
        f->stackpointer = sp;
        f->instr = f->last = lu_code_start(fr->code) + fr->pc;
        f->previous = top;
        f->ret = LU_RETURN_PUSH;
        top = f;
    }
    ts->current_frame = (struct _PyInterpreterFrame *)top;
    if (d->error) {
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
