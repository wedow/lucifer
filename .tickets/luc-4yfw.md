---
id: luc-4yfw
status: closed
deps: []
links: [luc-j2vs, luc-uexr, luc-n6yj, luc-6ljp, luc-r1pk, luc-mltz, luc-m3wi, luc-xtmp, luc-9ags, luc-po0x, luc-o8i6]
created: 2026-11-07T13:14:15Z
type: bug
priority: 2
tags: [perf, scaling, types]
---
# type: class attribute writes run 2-4x slower than CPython 3.14t (lu_type_modified)

Measured with luc-fwnf's writer microbenchmark (N threads each run 'C.x += 1' 200000 times on a class of their own; best of 3, millions of writes/s in total): CPython 3.14t 7.48/12.51/10.11/4.04 at 1/2/4/8 threads, lucifer 1.68/2.53/3.61/4.35. The JIT's invalidation is no longer on the profile after luc-fwnf (a fence and two loads when nothing depends on the version); the cost is lu_type_modified itself: a subclasses snapshot list allocated per write, watcher notification and a fresh version per change.

## Acceptance Criteria

Class attribute writes on a class without subclasses within 1.5x of CPython 3.14t at 1-8 threads, with no process-wide lock on the path.


## Notes

**2026-11-08T22:04:06Z**

Done on luc-r1pk: a plain class attribute write is C's (type_setattr_native in descr.c: no function value, no dunder, metaclass keeps type's __setattr__ and has no data descriptor of the name), taking the tag back once instead of three times from kernel Python; lu_type_modified skips type_lock when the type has no tag (none of its subclasses then has one), with a fence ordering the change before the tag load, as assign_version's orders the tag before the walk. Writer microbenchmark (N threads, C.x += 1 on a class each, million writes/s total, best of 3): master 1.89 / 1.82 / 1.35 at 1 / 8 / 32 threads, branch 5.99 / 28.26 / 29.69, CPython 3.14t 7.51 / 3.95 / 2.88: 1.25x CPython's time at 1 thread, 7x and 10x its throughput at 8 and 32. A type that still has a tag takes type_lock to take it back, as CPython's TYPE_LOCK does; the benchmark's classes spend their 1000 tags early, after which no lock is taken.
