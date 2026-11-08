---
id: luc-p0v3
status: closed
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: loops over range and list iterators in inlined callees allocate the iterator

Census (luc-k5az): compiled code allocates 2.87M iterators per AWFY Havlak iteration (Vector.for_each and has_some: for i in range(self._first_idx, self._last_idx)), 1.75M per NBody iteration and 0.23M per Richards iteration. H_range_iter makes the range iterator in C each time a loop starts; the iterator is a loop-carried stack entry, so the virtual-object pass cannot remove it. Either allocate it in line as instances are (cheap), or scalar replace it (its index as a phi, made at exits), which needs the virtual phis of luc-3rna.


## Notes

**2026-11-08T09:03:51Z**

Done on branch luc-esc2 (6a3d1f7, 56bfd63). A loop over range(a[, b]), a list or a tuple made in the loop's code keeps its iterator only on the stack, so each step makes a new iterator from the last one's fields (listiter_new, tupleiter_new, rangeiter_new) instead of changing it; a list's length is read each step. Phis of such iterators are virtual (luc-3rna), so the iterator is never made, its index is a loop phi, and an exit makes it where the loop stands. A generator's loops do the same when they contain no yield. Iterators per run, master -> branch: AWFY Havlak (10 x 1500) 28.7M -> 0.12M, NBody 35.1M -> 3.7K, CD 24.5M -> 3.1K, DeltaBlue 5.3M -> 64, Richards 4.7M -> 64; pyperformance raytrace 170M -> 4.3M (the kernel operator dispatch's loops), chaos 6.5M -> 0.17M, go 4.0M -> 14K, nqueens 4.3M -> 2.0M (generators that yield in their loops).
