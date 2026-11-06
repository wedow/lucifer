---
id: luc-byiv
status: open
deps: []
links: []
created: 2026-11-06T01:23:01Z
type: task
priority: 3
tags: [perf]
---
# perf: for over itertools.repeat runs ~40x slower than 3.14t

Found while closing luc-d5u1. `for _ in repeat(None, n): pass` with n = 40,000,000 takes 11 s on bin/lucifer (JIT on, off or -X perf alike) and 0.26 s on the 3.14t oracle: about 275 ns per iteration. lib/modules/itertools.py repeat.__next__ is Python; -X lucifer_jit_stats shows about two direct calls per iteration. test_perf_profiler's two Dwarf tests run this loop, so they take most of the module's 29 s.

