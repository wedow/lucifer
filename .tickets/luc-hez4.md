---
id: luc-hez4
status: in_progress
deps: [luc-v0kt, luc-pfc5, luc-15mf, luc-t0dg, luc-14n0, luc-7hlk]
links: []
created: 2026-11-08T22:55:49Z
type: epic
priority: 2
---
# JIT: per-call cost and allocation/refcount on the call path

Call protocol is 7.9% of corpus time (+2.6% JIT helper calls), allocation/refcount 7.3% (census 2026-11-08, mechanisms 3 and 5). Umbrella for luc-v0kt, luc-pfc5, luc-15mf, luc-t0dg, luc-14n0, luc-7hlk and a cheaper compiled-to-compiled frame push/pop.


## Notes

**2026-11-09T07:41:38Z**

Wrapped up early (coordinator). Branch: ce69ea6 (code borrowed by direct frames + collector counts it, luc-pfc5; inlined frames pushed/popped in line as one block; values across calls in rbx/r12/r13; non-publishing release not a call to the allocator, luc-15mf; keyword calls enter the keyword entry from compiled code; raw-int slice bounds, luc-14n0) and 779267b (fix: values an exit after a call reads live across the call; nqueens/sqlglot_v2_optimize miscompiled without it, test jit_generator_calls). Gates on 779267b: check, abi-audit, check-jit, check-harness, check-compiler all 0. Measured: instructions per call (microbenchmark) positional 258->227 (CPython 593), from an inlined frame 497->296, keyword 632->481; ns/call positional 22.6->17.6 (CPython 43.3), keyword 50.6->38.7, from inlined frame 38.9->23.6. AWFY instructions Richards -11%, DeltaBlue -4%. Timing below is for ce69ea6 (before the fix, which adds spills at exits after calls): pyperformance geomean -5.0% vs master, cli -2.0%, AWFY time geomean -2.2% (Richards -9%; Mandelbrot/Permute/Towers +1-2%, unconfirmed noise), telco -11%, dulwich_log -17%; web req/s +5% at 1 and 8 CPUs measured on an earlier snapshot. Half-done: final timing and census of 779267b never ran; AWFY no-regression unconfirmed. Leftovers: luc-v0kt (open, describe inlined frames for the luc-23yk correctness half), luc-6nyq (bind star/kw in line), luc-t0dg (conformance only), luc-un7i (threads_set_race flake on master).
