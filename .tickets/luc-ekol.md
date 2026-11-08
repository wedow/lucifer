---
id: luc-ekol
status: open
deps: []
links: [luc-p2yt]
created: 2026-11-07T16:00:23Z
type: feature
priority: 2
tags: [jit, perf]
---
# jit: closures made in inlined frames (MAKE_FUNCTION, MAKE_CELL)

Found by luc-keyu's census. MAKE_FUNCTION and MAKE_CELL compile only in the root frame; an inlined callee that makes a closure side-exits and is marked noinline (DeltaBlue _Plan.execute's for_each(lambda), _AbstractConstraint.satisfy and inputs_known with cells). luc-keyu inlines a closure the unit made and handed on by its code (made_function), but only when the root made it. Making functions in inlined frames (the helper with the frames pushed, or the function made virtual when it does not escape) would let methods like execute inline whole: the Smalltalk VM's block inlining gave Havlak -23%, DeltaBlue -20% (roadmap row 7).


## Notes

**2026-11-08T14:34:27Z**

Done in luc-p2yt: MAKE_CELL compiles in inlined frames (a cell helper), and MAKE_FUNCTION anywhere (its helper declines when a function watcher is on, so it no longer may run Python). New jitcheck lane closures (jit_inlined_closures.py): master enters compiled code twice per iteration, the branch inlines the methods and their closures.

**2026-11-08T15:38:49Z**

Tried in luc-p2yt and reverted: MAKE_CELL (a cell helper) and MAKE_FUNCTION (declining when a function watcher is on) compiled in inlined frames, and the methods that make closures stayed inlined with their closures (a growth lane passed: master entered compiled code twice per iteration, the branch none). But AWFY DeltaBlue ran 7% more instructions with it than without (14.85G vs 13.83G for 3 x 12000; master 14.30G): each inlined copy makes the function and its cells, and for_each's call of it is not inlined where the copy is. pyperformance deltablue was unchanged (0.974 vs 0.975 of master). Worth doing only with the closure kept virtual when it does not escape (scalar replaced, made at an exit), so that no function or cell is made per call.
