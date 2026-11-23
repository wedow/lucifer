/* Guards the unit need not run (docs/design/jit-architecture.md 2,
   feedback and invalidation): a guard on a constant whose fact only a
   writer changes (a dict's keys, the value of one key, a function's code
   and defaults, a class's version), or of a type's version its path read
   already (INS_PROVEN), becomes a dependency of the unit (runtime/jit/deps.c). A writer that changes the fact kills the unit,
   which then leaves at its next poll; the running thread's own change
   must reach it at once: a unit with dependencies checks that it lives
   where it checks its frames after an instruction that may have run code
   (GUARD_FRAMES), and where it may have run code or mutated since it
   last knew itself alive, the guard becomes such a check; elsewhere it
   goes.

   A site whose fact a writer took back before (lu_jit_nohoist) keeps its
   guard in place: a global rebound again and again must not kill the
   unit each time. */
#include "jit/target.h"

#include <stdlib.h>
#include <string.h>

/* The dependency a guard stands for, watched so that its writers report
   it, when its fact holds now; 0 for a guard that stays. */
static int
fact_of(struct lu_func *f, struct lu_ins *in, struct lu_fdep *dep)
{
    if (in->op == OP_GUARD_VERSION && in->imm2 == GV_TYPE_OF && (in->flags & INS_PROVEN)
        && !lu_jit_nohoist(in->fs->code, in->fs->pc)) {
        /* Its path read the version already: only a change to the type takes it back. */
        *dep = (struct lu_fdep){ .kind = LU_DEP_TYPE, .obj = (uint32_t)in->imm, .code = in->fs->code, .pc = in->fs->pc };
        return 1;
    }
    if (in->fs == NULL || in->a == LU_NOV || f->ins[in->a].op != OP_CONST || lu_jit_nohoist(in->fs->code, in->fs->pc)) {
        return 0;
    }
    PyObject *o = (PyObject *)(intptr_t)f->ins[in->a].imm;
    *dep = (struct lu_fdep){ .code = in->fs->code, .pc = in->fs->pc, .obj = (uintptr_t)o };
    if (in->op == OP_GUARD_VERSION && in->imm2 == GV_TYPE_ITSELF) {
        dep->kind = LU_DEP_TYPE;
        dep->obj = (uint32_t)in->imm;
        return __atomic_load_n(&((PyTypeObject *)o)->tp_version_tag, __ATOMIC_ACQUIRE) == (uint32_t)in->imm;
    }
    if (in->op == OP_GUARD_VERSION && in->imm2 == GV_FUNC) {
        dep->kind = LU_DEP_FUNC;
        return __atomic_load_n(&((PyFunctionObject *)o)->func_version, __ATOMIC_ACQUIRE) == (uint32_t)in->imm;
    }
    int keys = in->op == OP_GUARD_VERSION && in->imm2 == GV_DICT_KEYS;
    int value = in->op == OP_GUARD_IS && GI_MODE(in->imm2) == 2;
    if (!keys && !value) {
        return 0;
    }
    /* Watched and read in the dict's critical section, where every change
       to it is made and reported (dict.c): a change the compile does not
       read is one made after, which sees the watch and the compile's
       ticket (runtime/jit/deps.c). */
    int holds;
    dep->kind = keys ? LU_DEP_KEYS : LU_DEP_VALUE;
    dep->sub = keys ? 0 : (uint32_t)GI_OFFSET(in->imm2);
    Py_BEGIN_CRITICAL_SECTION(o);
    __atomic_fetch_or(&((PyDictObject *)o)->_ma_watcher_tag, 1, __ATOMIC_RELAXED);
    PyDictKeysObject *dk = ((PyDictObject *)o)->ma_keys;
    char *word = (char *)dk + GI_OFFSET(in->imm2);
    holds = keys ? dk->dk_version == (uint32_t)in->imm
                 : lu_dk_is_unicode(dk) && dk->dk_nentries > 0 && word <= (char *)lu_dk_value_slot(dk, dk->dk_nentries - 1)
                   && *(PyObject **)word == (PyObject *)(intptr_t)in->imm;
    Py_END_CRITICAL_SECTION();
    return holds;
}

/* Whether the instruction may run code of the program's, or mutate what
   a dependency names, in this thread: after it the unit may be dead
   without having met a poll. */
static int
unsettles(const struct lu_ins *in)
{
    switch (in->op) {
    case OP_HELPER:
        return (lu_helpers[in->imm].fx & (FX_PY | FX_SEEN)) != 0;
    case OP_TRUTH:
        return in->imm != 1;
    case OP_DECREF:
        return 0;               /* a release that runs code checks that the unit lives (emit.c settle) */
    case OP_LIST_STORE: case OP_INLINE_STORE: case OP_INLINE_STORE_F: case OP_INLINE_STORE_I:
    case OP_ATTR_STORE: case OP_STORE:
        return 1;
    default:
        return 0;
    }
}

/* A check of the frames (GUARD_FRAMES) that no published call can have
   reached since the last check, on any path, goes: only such a call lets
   a walker see the frames. A release the optimizer took out leaves one so,
   and a release that runs C checks the frames itself (emit.c settle). */
static void
frames_checks(struct lu_func *f)
{
    int nb = f->nblocks;
    char *seen = calloc((size_t)nb, 1);
    for (int changed = 1, final = 0; changed || !final;) {
        final = !changed;
        changed = 0;
        for (int k = 0; k < f->norder; k++) {
            int b = f->order[k];
            struct lu_block *bl = &f->blocks[b];
            int any = 0;
            for (int p = 0; p < bl->npreds; p++) {
                any |= seen[bl->preds[p]];
            }
            for (int i = 0; i < bl->nins; i++) {
                struct lu_ins *in = &f->ins[bl->ins[i]];
                if (in->flags & INS_DEAD) {
                    continue;
                }
                if (in->op == OP_GUARD_FRAMES) {
                    if (final && !any && in->imm == 0) {
                        in->flags |= INS_DEAD;
                    }
                    any = 0;
                }
                else if (lu_publishes(in) && in->op != OP_DECREF) {
                    any = 1;           /* a release checks the frames itself (emit.c) */
                }
            }
            if (seen[b] != any) {
                seen[b] = (char)any;
                changed = 1;
            }
        }
        if (final) {
            break;
        }
    }
    free(seen);
}

/* A call's poll (POLL_CALL) goes where every path polled since the last
   instruction that published its state (lu_publishes: it may run code or
   park, where another task may stop the world): what the poll would see,
   nothing since can have set, and the code between is no loop (a
   back-edge polls). A slow path that parks leaves when the poll word
   changed meanwhile (emit.c parked_end), so it needs no poll after it. */
static void
call_polls(struct lu_func *f)
{
    int nb = f->nblocks;
    char *polled = malloc((size_t)nb);
    memset(polled, 1, (size_t)nb);
    for (int changed = 1, final = 0; changed || !final;) {
        final = !changed;
        changed = 0;
        for (int k = 0; k < f->norder; k++) {
            int b = f->order[k];
            struct lu_block *bl = &f->blocks[b];
            int ok = k > 0 && bl->npreds > 0;
            for (int p = 0; p < bl->npreds; p++) {
                ok &= polled[bl->preds[p]];
            }
            for (int i = 0; i < bl->nins; i++) {
                struct lu_ins *in = &f->ins[bl->ins[i]];
                if (in->flags & INS_DEAD) {
                    continue;
                }
                if (in->op == OP_POLL) {
                    if (final && ok && in->imm == POLL_CALL) {
                        in->flags |= INS_DEAD;
                    }
                    ok = 1;
                }
                else if (lu_publishes(in)) {
                    ok = 0;
                }
            }
            if (polled[b] != ok) {
                polled[b] = (char)ok;
                changed = 1;
            }
        }
        if (final) {
            break;
        }
    }
    free(polled);
}

/* A store that takes its object's lock byte in line (emit.c lock_object). */
static int
locks_in_line(const struct lu_ins *in)
{
    switch (in->op) {
    case OP_INLINE_STORE: case OP_INLINE_STORE_I: case OP_INLINE_STORE_F:
    case OP_LIST_STORE: case OP_TYPED_LOAD: case OP_TYPED_STORE:
    case OP_INLINE_LOAD: case OP_INLINE_LOAD_I: case OP_INLINE_LOAD_F: case OP_INLINE_ORIGIN:
        return !(in->flags & INS_FRESH);
    default:
        return 0;
    }
}

/* While serial (lu_serial), such a store takes no lock, an inline load or
   store does not test the in-dict flag before the word (emit.c
   in_dict_jump: an instance another thread gave the class by __class__
   assignment could otherwise reach a unit built before the class was
   marked, and no other thread exists), and the unit depends on the
   serial state: making a second task kills the unit, then
   waits until the first task is detached or shows no unit that may store
   unlocked before its next check (its unlocked_jit, runtime/thread/task.c).
   The word is 0 only inside a published call: 1 from the unit's entry and
   from the end of each published call, 0 from each publish, before the
   call, so it is 1 before any check reads the unit. The switch therefore
   goes on only where the task is out of the unit, in a published call, or
   detached (which happens only in an instruction that may run code, park
   or detach: lu_publishes, unsettles); after those, the next unlocked store
   checks first. Checks are CHECK_LIVE, a GUARD_FRAMES that checks the unit
   (with a dependency every one does), a release's or a park's check after
   its C, and such a store's own: a store some path reaches with no check
   since checks first (INS_RECHECK), and leaves before storing if the unit
   died. A release and a poll change nothing: their C ends with a check
   (emit.c settle and parked_end), and their other paths run no code; but a
   release with no state to publish (a returning frame's) has no check after
   its C, so it counts as code run. A poll is no check: a destructor's own
   polls may have taken the reason it would leave for. C the unit calls
   that may run Python without publishing (such a release, a helper with no
   state) has the word at 0 too (emit.c unlocked_pause), so Python never
   runs with it at 1 but in the callee of an instruction the helper table
   says runs none. */
static void
serial_stores(struct lu_func *f)
{
    int any = 0;
    for (int v = 0; v < f->nins && !any; v++) {
        any = !(f->ins[v].flags & INS_DEAD) && locks_in_line(&f->ins[v]);
    }
    if (!any || !lu_serial()) {
        return;
    }
    struct lu_fdep dep = { .kind = LU_DEP_SERIAL, .code = f->code };
    lu_ir_dep(f, &dep);
    int nb = f->nblocks;
    char *alive = malloc((size_t)nb);
    memset(alive, 1, (size_t)nb);
    for (int changed = 1, final = 0; changed || !final;) {
        final = !changed;
        changed = 0;
        for (int k = 0; k < f->norder; k++) {
            int b = f->order[k];
            struct lu_block *bl = &f->blocks[b];
            int ok = 1;
            for (int p = 0; p < bl->npreds; p++) {
                ok &= alive[bl->preds[p]];
            }
            for (int i = 0; i < bl->nins; i++) {
                struct lu_ins *in = &f->ins[bl->ins[i]];
                if (in->flags & INS_DEAD) {
                    continue;
                }
                if (locks_in_line(in)) {
                    if (final) {
                        in->flags |= INS_UNLOCKED | (ok ? 0 : INS_RECHECK);
                    }
                    ok = 1;
                }
                if (in->op == OP_CHECK_LIVE || (in->op == OP_GUARD_FRAMES && !(in->imm & 1))) {
                    ok = 1;
                }
                else if (in->op == OP_DECREF ? in->imm == 1 && !lu_publishes(in)     /* a return's: no check after */
                         : in->op != OP_POLL && (lu_publishes(in) || unsettles(in))) {
                    ok = 0;
                }
            }
            if (alive[b] != ok) {
                alive[b] = (char)ok;
                changed = 1;
            }
        }
        if (final) {
            break;
        }
    }
    free(alive);
}

int
lu_ssa_facts(struct lu_func *f)
{
    frames_checks(f);
    call_polls(f);
    int nb = f->nblocks;
    /* alive[b]: the unit is known alive where block b ends (it was at its
       entry, and a poll leaves a dead one). Optimistic, then to a fixpoint. */
    char *alive = malloc((size_t)nb), *is_fact = calloc((size_t)f->nins + 1, 1);
    for (int v = 0; v < f->nins; v++) {
        struct lu_ins *in = &f->ins[v];
        struct lu_fdep dep;
        if (!(in->flags & INS_DEAD) && (in->op == OP_GUARD_VERSION || in->op == OP_GUARD_IS) && fact_of(f, in, &dep)) {
            is_fact[v] = 1;
            lu_ir_dep(f, &dep);
        }
    }
    for (int b = 0; b < nb; b++) {
        alive[b] = 1;
    }
    for (int changed = 1, final = 0; changed || !final;) {
        final = !changed;       /* one more walk, which rewrites */
        changed = 0;
        for (int k = 0; k < f->norder; k++) {
            int b = f->order[k];
            struct lu_block *bl = &f->blocks[b];
            int ok = 1;
            for (int p = 0; p < bl->npreds; p++) {
                ok &= alive[bl->preds[p]];
            }
            for (int i = 0; i < bl->nins; i++) {
                struct lu_ins *in = &f->ins[bl->ins[i]];
                if (in->flags & INS_DEAD) {
                    continue;
                }
                if (is_fact[bl->ins[i]]) {
                    if (final) {
                        if (ok) {
                            in->flags |= INS_DEAD;
                        }
                        else {
                            in->op = OP_CHECK_LIVE;
                            in->a = LU_NOV;
                        }
                    }
                    ok = 1;
                }
                else if (in->op == OP_POLL || in->op == OP_CHECK_LIVE || (in->op == OP_GUARD_FRAMES && !(in->imm & 1))) {
                    ok = 1;
                }
                else if (unsettles(in)) {
                    ok = 0;
                }
            }
            if (alive[b] != ok) {
                alive[b] = (char)ok;
                changed = 1;
            }
        }
        if (final) {
            break;
        }
    }
    free(alive);
    free(is_fact);
    serial_stores(f);
    return 0;
}
