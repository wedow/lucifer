---
id: luc-keyu
status: in_progress
deps: []
links: []
created: 2026-11-07T14:15:47Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: polymorphic splitting and callee feedback

Roadmap item 4. Duplicate the call after a load_attr_poly method load into each arm so each arm inlines its constant method (adaptive budget, no cliff). Record callee identity per CALL site (mono/polymorphic table in the CALL feedback) so calls through locals, attributes, closures and bound methods inline behind identity guards, with a polymorphic dispatch on 2-4 callees. Folds in luc-sl4y (order-dependent call feedback).

## Acceptance Criteria

Census of non-inlined calls per iteration by reason on DeltaBlue, Json, Havlak, CD and a Flask route, before and after. Inlined-call counts in -X lucifer_jit_stats. luc-sl4y repro as a jitcheck lane running both orders. Kernel tests: a site turning megamorphic after compile, a callee rebound after compile, bound methods, closures. AWFY DeltaBlue and Json; cycles agree with instructions. Gates green.


## Notes

**2026-11-07T19:49:15Z**

Landed on luc-poly in four steps: call census by reason in -X lucifer_jit_stats; polymorphic method loads build their call once per class (tail duplication over branch-free argument code, charged to an inlined frame's budget), plus class attributes behind a metaclass check and constant classes' plain values (Enum members) that blocked inlining; callee feedback (CALL cache words tag the last callee, a 4-way table per site of functions/builtins by identity and closures by code+globals; compiled code tests each, the last as a guard, and builds the call per callee; tables are dependencies, so a new callee recompiles; closures the unit made are known by code); callees that loop are called, not inlined, when only feedback names them (luc-9wov).

Census, non-inlined calls per AWFY iteration (inner: DeltaBlue 1000, Havlak 150, Richards 10), master -> branch: DeltaBlue poly 149K -> 3K, unknown 35K -> 16K; Havlak poly 669K -> 0; CD poly 703K -> 0; Richards unchanged (its closures are noinline from side exits, luc-n6yj). Instructions (cycles) per iteration, pinned: DeltaBlue -7.6% (-5.6%), CD -2.7% (-7.8%), Havlak -0.9% (-5.2%), Json 0 (-0.9%), Richards 0 (0), Flask 3 routes +2.1% (-6.8%).

AWFY time best of 3, branch/master: geomean 0.994 (CPython ratio 0.623 -> 0.620); DeltaBlue 0.900, Towers 0.974, Permute 0.982, Json 0.988; CD 1.081 (luc-8fns). luc-sl4y fixed (0.07 s both orders). Flask in process: 1-2% faster per route.

Tried and reverted: generic attribute loads/stores in inlined frames with frames pushed (Richards +30% instructions); feedback-made JIT state skipping warm-up (fixed: the state keeps the counts as waits).

**2026-11-07T23:45:11Z**

After luc-8fns's fix (a3ab2b7) and merging master 936c76f: gates 0/0/0/0/0; AWFY best of 3, branch/master geomean 0.833 (CPython ratio 0.623 -> 0.519): CD 0.316, Havlak 0.653, Json 0.740, DeltaBlue 0.742, Bounce 0.798, others 0.94-1.006. Flask in process: show/new/api/toggle 1-2% faster, index 6% slower in this run (one best-of-5 sample, not yet attributed).
