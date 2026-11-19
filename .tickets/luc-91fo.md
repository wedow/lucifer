---
id: luc-91fo
status: open
deps: []
links: []
created: 2026-11-19T09:03:45Z
type: task
priority: 3
---
# pickle_pure_python +2.3% instructions after the element intrinsics

Accepted loss of luc-cxwo/luc-lfnr (net-win rule). Core-lease steady instructions per iteration, master 7937512e vs the element intrinsics: pickle_pure_python 1.019 (1 rep) and 1.023 (3 reps; cycles 0.92), unpickle 1.009 (both). Symbol profile (instructions:u): prim_str_cmp gone, but lu_frame_call 0.53 -> 0.93%, call_function 0.49 -> 0.80%, lu_frame_push and lu_jit_run up ~0.7%: calls the tier made in line or directly before now go through frames, an inline-budget shift. Find the call sites (LUCIFER_JIT_DEBUG whynot / refused counts per unit, base vs branch) and whether the budget charges the new in-line ops differently.

