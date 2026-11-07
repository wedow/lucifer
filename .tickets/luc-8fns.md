---
id: luc-8fns
status: closed
deps: []
links: []
created: 2026-11-07T19:49:15Z
type: bug
priority: 1
tags: [jit, perf]
---
# jit: CD runs 8% slower in time on luc-keyu while its instructions and cycles fall

luc-keyu AWFY (canonical inner 250, median of iterations 3-6, best of 3, twice): CD branch/master 1.07-1.08 in time. Pinned counts at inner 100 (iterations 20 minus 10) say instructions -2.7%, cycles -7.8%. So the cost is outside the steady state the census measures: recompiles or exits that the larger inner count reaches, or compile time of a larger CD unit (split arms of the red-black tree's polymorphic compare). Attribute with -X lucifer_jit_stats at the canonical size and LUCIFER_JIT_DEBUG's compiled lines, then fix or limit the split there.


## Notes

**2026-11-07T22:39:26Z**

Mechanism (CD inner 250, LUCIFER_JIT_DEBUG and per-unit stats): not feedback recompiles (compiles 123 vs 127 on master, compile time 39 vs 55 ms, exits equal, invalidations 9 vs 5). Master's _RedBlackTree._tree_insert spends MAX_COMPILES (12) and is disabled, so it runs interpreted and makes _Node instances natively; on the branch the callee recording creates JIT state earlier and the count ends at 11, so it stays compiled and every class call goes through type.__call__ + object.__new__ (type.__call__ unit +35%). Isolation, instructions per iteration: master 50.6G, branch 55.2G, branch without recording 48.7G; without split, dispatch, made closures or class values each: 53.8-56.4G. Fixed generally in a3ab2b7: h_call makes instances as the interpreter's CALL does (lu_instance_plan). CD: 51.8G -> 15.2G instructions, 24.5G -> 8.4G cycles per iteration. The trajectory dependence itself is ticketed separately.
