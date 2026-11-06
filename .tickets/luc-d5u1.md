---
id: luc-d5u1
status: open
deps: [luc-74bf, luc-2vif]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 1
parent: luc-nsmh
tags: [jit, perf]
---
# P0: block-SSA optimizing tier; AWFY 14/14 pass

runtime/jit ported from the donor block-SSA backend: SSA from bytecode and inline caches, kernel inlining, guards, unboxing, deopt to the interpreter, background compile, perf map (docs/DESIGN.md 6.2, 6.3).

## Acceptance Criteria

tools/bench/awfy --python bin/lucifer with the SSA tier on: 14/14 PASS; difftest stays 164/164.
- Foundations (docs/FOUNDATIONS.md 2): AWFY 14/14 and difftest 164/164 under -X lucifer_jit=always -X lucifer_stress=deopt:7,gc:1000; tests/kernel frames.py, invalidate.py and the M0.3 settrace/monitoring PASS sets unchanged with -X lucifer_jit=always.
- test_perf_profiler and test_perfmaps at oracle parity with the JIT on; perf record -g on AWFY richards resolves at least 95% of samples in JIT code to py:: symbols.
- runtime/jit/ssa*.c and regalloc.c include only jit/target.h; an aarch64 cross build compiles runtime/jit with a stub target and -X lucifer_jit=always there reports no backend and runs interpreted.

## Notes

**2026-11-06T01:23:22Z**

Acceptance check on branch luc-d5u1. AWFY 14/14 match the oracle under -X lucifer_jit=always -X lucifer_stress=deopt:7,gc:1000 (2 iterations, smallest verifiable inner counts; Havlak takes 770 s because each collection scans the heap). difftest 164/164 under the same flags; havlak needs --timeout 2400 (415 s). tests/kernel frames.py and invalidate.py pass in check-kernel-jit's jit and jit-deopt lanes. libtest test_sys_settrace, test_sys_setprofile, test_monitoring, test_bdb, test_cprofile and test_pdb: identical PASS sets under jit=off, on and always (below the oracle there: luc-gskg). test_perf_profiler 12 PASS / 3 SKIP and test_perfmaps 1 PASS under jit on and always, the oracle's rows: -X perf, -X perf_jit and sys.activate_stack_trampoline now run each frame through a per-code trampoline (runtime/interp/perf.c), with compiled code still entered and named. check-jit's perf lane: 100% of Richards' compiled-code samples resolve to py:: (0% before: -X perf was not parsed). ssa*.c and regalloc.c include only jit/target.h plus libc headers. No aarch64 cross compiler or sysroot on this host, so the cross build is not done; JIT_TARGET=stub builds natively, and -X lucifer_jit=always there prints one no-backend line and runs interpreted.
