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
