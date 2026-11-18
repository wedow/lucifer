---
id: luc-3yh7
status: closed
deps: []
links: []
created: 2026-11-18T03:49:20Z
type: task
priority: 1
---
# jit: borrow attribute reads released before anything can free them

Gap report 2026-11-18 mechanism #1 (refcount elision). An INLINE_LOAD takes a reference that the evaluator drops at the end of the instruction or soon after; when nothing between could free the value (no call, no release of anything that holds others, no store, no park), the instance's reference keeps it alive and the read borrows it. A GUARD_FRAMES no published call reaches goes too.


## Result

Landed. opt.c borrowed_loads makes such an INLINE_LOAD unowned (emit.c checks the value is immortal or this thread's;
the unboxed slow path uses lu_inline_peek); facts.c frames_checks drops a GUARD_FRAMES no published call reaches.
Executed counts, 3 iterations, before -> after: DECREF Havlak 376 M -> 353 M, Richards 566 M -> 534 M, Json 196 M ->
186 M, NBody 123 M -> 119 M, CD 270 M -> 266 M, DeltaBlue 104 M -> 102 M (GUARD_FRAMES alike). Steady instructions per
iteration: Havlak 0.985, Richards 0.992, NBody 0.991; AWFY cycles geomean 0.997 (within the host's noise).
Windows stay narrow: an INLINE_LOAD inside one may park on its slow path, where a stop-the-world collection merges
queued counts and could free a borrowed value, so it ends the window.
