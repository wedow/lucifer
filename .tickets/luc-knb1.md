---
id: luc-knb1
status: open
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

