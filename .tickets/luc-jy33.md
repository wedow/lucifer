---
id: luc-jy33
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# bench: idiom table (cycles and instructions per iteration against PyPy and CPython)

tools/bench/idioms: a set of small loops, one ordinary idiom each (the 25 of the parent's table to start, growing toward the language and the built-in types: attribute and item access, calls of every shape, allocation, iteration protocols, str/bytes/dict/set/list/tuple methods, exceptions, closures, generators, comprehensions, with, unpacking, arithmetic towers, class creation, descriptors, async). Each runs at two loop counts under perf stat; the row is (cycles, instructions) per inner iteration from the difference, for each runtime given. One JSON object out; a table printer; a compare of two runs. The daily status run adds the table and the geomean Lucifer/PyPy over rows to summary.md.

An idiom is a mechanism, not a program: no row may copy a benchmark's code.

Acceptance: `tools/bench/idioms time` reproduces the parent's table within 10% on a quiet core; summary.md carries it.

