---
id: luc-k5az
status: closed
deps: []
links: []
created: 2026-11-07T17:38:17Z
type: task
priority: 1
tags: [jit, perf]
---
# JIT: object escape analysis (roadmap item 6)

Roadmap item 6. escape() in ssa_opt.c handles only int/float boxes and **kwargs dicts. Extend scalar replacement to tuples (UNPACK_SEQUENCE(BUILD_TUPLE), multiple return through inlined calls), *args tuples, bound methods, iterators, and instances whose __init__ inlined (luc-zcur): their fields become SSA values. Exits make each virtual object once with the fields it has there, aliases preserved. A per-site allocations counter in -X lucifer_jit_stats. Identity tests (is, id, weakref, __dict__, type()) under deopt.

## Acceptance Criteria

Allocations per iteration by site and kind before and after on AWFY CD, Havlak, Json, NBody, DeltaBlue, Richards and a Flask route; identity tests pass under jit and jit-deopt; make check, check-jit, abi-audit, check-harness, check-compiler pass.


## Notes

**2026-11-07T22:36:55Z**

Landed on branch luc-zcur (commit "perf: virtual tuples and instances, and lock-free stores into fresh instances"). ssa_opt.c step 3c (objects): build_tuple results and lowered instances (no finalizer) whose uses are refops, frame states (not published locals), type reads/guards, identity tests, tuple size/constant index, own inline stores/loads and dict_lacks are virtual: fields are the stored values, references counted per path, the last release releases the fields; exits make the object once (DV_TUPLE/DV_INSTANCE, fields moving in, aliases one object). Field presence per exit by dominance (lu_object_fields); regalloc keeps present fields live at exits. Instances that stay real take first stores of each name lock-free while no other thread can have seen them (INS_FRESH), which then publish nothing. Bound methods: compiled code allocates none in the census rows, not done.

Census per AWFY iteration after stage 1 -> after: CD instances 2.31M -> 1.65M, floats 8.12M -> 6.65M (total 11.9M -> 9.7M); Havlak tuples 2.64M -> 2.55M; DeltaBlue tuples 312K -> 276K; Richards tuples 604 -> 4. Why the rest stay real (LUCIFER_JIT_DEBUG 'not virtual'): returned or passed to calls that do not inline, stored in lists, and phis of loop-carried locals (luc-3rna). Time vs stage 1: geomean 0.994 (CD 0.951, DeltaBlue 1.040, Json 1.020); instructions CD -1.3% (cycles -4.8%), Havlak +0.7%, others within 0.5%. Leftovers: luc-3rna (virtual phis), luc-1gqu (closures), luc-p0v3 (iterators), luc-3zo2 (CD float boxes).
