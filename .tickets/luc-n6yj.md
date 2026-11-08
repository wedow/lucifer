---
id: luc-n6yj
status: open
deps: []
links: []
created: 2026-11-07T16:00:10Z
type: feature
priority: 2
tags: [jit, perf]
---
# jit: polymorphic attribute stores, and generic attribute sites in inlined frames

Found by luc-keyu's census. A STORE_ATTR site that meets two classes (Richards _TaskState.running: plain _TaskState at setup, _TaskControlBlock after) fails its one-shape guard 50 times, goes generic, and a generic attribute load or store in an inlined frame side-exits (root_only), so the method is marked noinline and its callers lose the inline (Richards run_task, DeltaBlue execute/get_output/inputs_do with an attribute site first run after the compile). Compiling generic loads and stores in inlined frames with the frames pushed (as calls are) was tried on luc-keyu: Richards +30% instructions from frame pushes on hot paths, reverted. Wanted: a store PIC like load_attr_poly, then the generic path only for real megamorphism; with item 5 for sites cold at compile time.

