---
id: luc-4yfw
status: open
deps: []
links: []
created: 2026-11-07T13:14:15Z
type: bug
priority: 2
tags: [perf, scaling, types]
---
# type: class attribute writes run 2-4x slower than CPython 3.14t (lu_type_modified)

Measured with luc-fwnf's writer microbenchmark (N threads each run 'C.x += 1' 200000 times on a class of their own; best of 3, millions of writes/s in total): CPython 3.14t 7.48/12.51/10.11/4.04 at 1/2/4/8 threads, lucifer 1.68/2.53/3.61/4.35. The JIT's invalidation is no longer on the profile after luc-fwnf (a fence and two loads when nothing depends on the version); the cost is lu_type_modified itself: a subclasses snapshot list allocated per write, watcher notification and a fresh version per change.

## Acceptance Criteria

Class attribute writes on a class without subclasses within 1.5x of CPython 3.14t at 1-8 threads, with no process-wide lock on the path.

