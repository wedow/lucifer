---
id: luc-uawj
status: closed
deps: []
links: []
created: 2026-11-18T03:06:54Z
type: task
priority: 1
---
# jit: a truth test learns its operand's type

Gap report 2026-11-18 mechanism #1. TO_BOOL on a value of unknown type is an OP_TRUTH call (it may run __bool__), whose release of a bool afterwards is a noisy DECREF with a GUARD_FRAMES after it. Learn the operand's type (lu_ev_learn_type) at TO_BOOL: a bool's truth is IS True, None's is 0, and a value known to be a bool or None is not released (immortal).


## Result

Landed. Executed counts, 3 iterations, before -> after: GUARD_FRAMES Richards 794 M -> 592 M, Json 165 M -> 134 M,
Queens 169 M -> 87 M; DECREF Richards 741 M -> 566 M, Queens 155 M -> 110 M (bool guards rise: Richards 69 M -> 223 M).
Steady instructions per iteration on a core lease: Mandelbrot 0.878, Queens 0.955, Sieve 0.954, Richards 0.976; AWFY
cycles geomean 0.992 (one pass; cycles on the host swing by 10% between runs of the same binary).
