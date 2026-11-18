---
id: luc-knag
status: closed
deps: []
links: []
created: 2026-11-18T22:03:00Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: units entered directly grow their inline budget

lu_jit_enter kills a unit that is still hot at HOT_ENTRIES entries with calls left out of line for want of budget, so its code's next build gets twice the budget (up to MAX_GROWN times). Since luc-tgqa most hot callees are entered through their direct entry, which never passed there: a hot callee kept its first budget and its own calls out of line. The direct entry now counts toward the same HOT_ENTRIES and calls lu_jit_grow there (the running activation finishes in the dead unit).

## Acceptance Criteria

AWFY rows whose hot callees are entered directly lose instructions on a core lease (Json, Havlak, DeltaBlue, Richards); kernel lanes, floor quick and check-jit pass.


## Result

Landed with luc-mrr0 and luc-ihjn. Firing: tests/kernel/jit_grow_direct.py's mid is built twice (once
before). Direct entries per run (calls not taken in line), master 538654a -> landing: Json 7.82 M -> 4.66 M,
Havlak 15.3 M -> 8.8 M, CD 23.9 M -> 19.3 M, Richards 22.5 M -> 19.7 M (3 iterations); pyperformance (3+3
values) pickle_pure_python 4.73 M -> 2.61 M, json_loads 3.77 M -> 2.68 M, unpickle_pure_python 2.55 M ->
2.00 M. Growth alone (1 rep, on a stack that also had the since-dropped luc-c31r/luc-yzbg): Json 0.882,
Havlak 0.943, DeltaBlue 0.958, Richards 0.973, CD 0.975, pickle_pure_python 0.760, json_loads 0.917.

The landing, master 538654a -> 49035d4, instructions on a core lease (AWFY steady per iteration, 1 rep;
pyperformance 3 reps, medians; cycles unreadable on the loaded host):
AWFY Json 0.895, Havlak 0.951, Bounce 0.960, DeltaBlue 0.972, Richards 0.984, CD 0.996; Towers, Storage,
Permute, Queens, List, Sieve, NBody, Mandelbrot 1.000; geomean 0.982.
Pyperformance hexiom 0.579, pickle_pure_python 0.773, unpickle_pure_python 0.864, json_loads 0.922, chaos 0.935,
deltablue 0.972, raytrace 0.973, nqueens 0.976, richards 0.982, nbody 1.001, float 1.007, go 1.091 (go spreads
+-10% across runs of one binary); geomean 0.912.
Gates: tools/floor quick lost 2 (test_re's anchor timing test in jit, a multiprocessing_spawn Barrier timeout
in deopt; each passes alone, 5/5 and 3/3), check-jit PASS, kernelcheck plain/jit/jit-deopt 778/778.
