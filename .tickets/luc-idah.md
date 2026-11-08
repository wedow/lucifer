---
id: luc-idah
status: open
deps: []
links: []
created: 2026-11-07T22:39:26Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: steady state depends on which code spends MAX_COMPILES

Found by luc-8fns: CD's speed on master came from _tree_insert exhausting its 12 compiles (guard failures at two attribute sites after hoisting) and running interpreted; a one-compile shift in the trajectory changed CD's time by 8% before a3ab2b7. Item 5 (exit and recompile policy): a code that keeps failing the same guards should settle on a unit that keeps them in place, not be disabled by a count, and whether interpreting or compiling a code is faster should not hang on that count.

