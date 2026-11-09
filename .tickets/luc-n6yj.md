---
id: luc-n6yj
status: closed
deps: []
links: [luc-j2vs, luc-uexr, luc-4yfw, luc-6ljp, luc-r1pk, luc-mltz, luc-m3wi, luc-xtmp, luc-9ags, luc-po0x, luc-o8i6]
created: 2026-11-07T16:00:10Z
type: feature
priority: 2
tags: [jit, perf]
---
# jit: polymorphic attribute stores, and generic attribute sites in inlined frames

Found by luc-keyu's census. A STORE_ATTR site that meets two classes (Richards _TaskState.running: plain _TaskState at setup, _TaskControlBlock after) fails its one-shape guard 50 times, goes generic, and a generic attribute load or store in an inlined frame side-exits (root_only), so the method is marked noinline and its callers lose the inline (Richards run_task, DeltaBlue execute/get_output/inputs_do with an attribute site first run after the compile). Compiling generic loads and stores in inlined frames with the frames pushed (as calls are) was tried on luc-keyu: Richards +30% instructions from frame pushes on hot paths, reverted. Wanted: a store PIC like load_attr_poly, then the generic path only for real megamorphism; with item 5 for sites cold at compile time.


## Notes

**2026-11-08T22:04:06Z**

Done on luc-r1pk: STORE_ATTR sites keep a polymorphic cache like loads (store_attr_poly: an arm per shape storing the inline value, else the generic helper in a root frame or an exit in an inlined one), fed by the root generic store (H_store_attr now takes the cache) and by the interpreter's generic stores; the interpreter's generic loads feed load caches too (luc-9ags), so an inlined frame whose generic site left recompiles with arms. pyperformance richards 0.60x master's time, richards_super 0.74x, deltablue 0.88x (with the other luc-r1pk changes). Lane jitcheck inlinedpoly.
