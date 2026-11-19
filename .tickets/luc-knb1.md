---
id: luc-knb1
status: closed
deps: []
links: []
created: 2026-11-19T13:37:14Z
type: bug
priority: 1
assignee: Greg Wedow
---
# interpreter LOAD_ATTR_SLOT/INSTANCE_VALUE dereferences the cached index before checking the site is unchanged: SIGSEGV when another thread moves values to a dict

runtime/interp/ceval.c op_LOAD_ATTR_SLOT (also LOAD_ATTR_INSTANCE_VALUE) checks the cached type version, reads LA_INDEX and dereferences owner+index, and only after that calls lu_site_unchanged. Interleaving: thread A reads version V; thread B moves an instance's values to a dict, the site misses and respecializes to LOAD_ATTR_WITH_HINT with the same version V and dict entry index 1; A reads LA_INDEX=1 and dereferences owner+1 -> SIGSEGV in try_word_ref(word=obj+1) from lu_inline_get. Reproduces with -X lucifer_jit=off too. Found by the race review of a5b45591; code from 30f469cc/b2d23861.

Repro (argument 20):

    import sys, threading
    class C:
        closed = False
        def __init__(self): self.a = 1
    def reader(objs, stop):
        while not stop[0]:
            for o in objs: o.closed
    for r in range(int(sys.argv[1])):
        objs = [C() for _ in range(2000)]
        for o in objs: o.closed = "own"
        stop = [False]
        ts = [threading.Thread(target=reader, args=(objs, stop)) for _ in range(3)]
        for t in ts: t.start()
        for o in objs: vars(o)["closed"] = "own2"
        stop[0] = True
        for t in ts: t.join()

Fix direction: confirm the site is unchanged (opcode+version+index read consistently) before the dereference, or bounds-check the offset against the instance layout.


## Fix

Site rewrites advance a sequence (_PyRuntime.specialize_seq, odd while a rewrite writes) and every reader of a site's words, in the interpreter, in compiled code's LOAD_GLOBAL helper and in the compiler's cached_site, uses them only once the same even sequence and its own opcode are seen again (a seqlock, specialize.h); the slot and inline-value loads, the attribute stores and the property getter use the cached words only after that check. The ordering argument is in the commit. Regression: tests/kernel/threads_attr_site_flip.py (a load site turning between an inline offset and a dict hint under one version, a store site between two classes' offsets).

Runs exiting 139, master 1929ea93 / fixed: the repro above (argument 20) 40/40 / 0/40 with -X lucifer_jit=off, 40/40 / 0/40 with default flags; threads_attr_site_flip.py 40/40 / 0/40 with the tier off, 0/40 default, 0/20 jit-deopt.

Cost (core lease, steady instructions per iteration, fixed / master): default flags, 14 AWFY rows 0.993-1.000 (geomean 1.000), pyperformance raytrace 1.005, deltablue 1.008, richards 0.997, chaos 0.989, nbody 0.998, json_dumps 0.999, logging 1.037 (go swings run to run); interpreter alone (-X lucifer_jit=off) pyperformance richards 1.089, deltablue 1.041, raytrace 1.018, chaos 1.008 (medians of 3), AWFY Richards 0.993, DeltaBlue 1.003, Json 1.004, Havlak 1.007 (1 run): a few instructions per specialized attribute or global access.
