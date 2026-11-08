---
id: luc-p0v3
status: open
deps: []
links: []
created: 2026-11-07T21:09:55Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: loops over range and list iterators in inlined callees allocate the iterator

Census (luc-k5az): compiled code allocates 2.87M iterators per AWFY Havlak iteration (Vector.for_each and has_some: for i in range(self._first_idx, self._last_idx)), 1.75M per NBody iteration and 0.23M per Richards iteration. H_range_iter makes the range iterator in C each time a loop starts; the iterator is a loop-carried stack entry, so the virtual-object pass cannot remove it. Either allocate it in line as instances are (cheap), or scalar replace it (its index as a phi, made at exits), which needs the virtual phis of luc-3rna.

