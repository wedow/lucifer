---
id: luc-idah
status: closed
deps: []
links: []
created: 2026-11-07T22:39:26Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: steady state depends on which code spends MAX_COMPILES

Found by luc-8fns: CD's speed on master came from _tree_insert exhausting its 12 compiles (guard failures at two attribute sites after hoisting) and running interpreted; a one-compile shift in the trajectory changed CD's time by 8% before a3ab2b7. Item 5 (exit and recompile policy): a code that keeps failing the same guards should settle on a unit that keeps them in place, not be disabled by a count, and whether interpreting or compiling a code is faster should not hang on that count.


## Notes

**2026-11-08T04:41:12Z**

Fixed on luc-policy (luc-xboo): MAX_COMPILES and disabling are gone. A death makes the entry wait (50 + code size) << level entries; recompiles raise the level, a unit that served longer than its wait lowers it. Census: disabled units 102 -> 0 over the 30 programs; tests/kernel/jit_phases.py (20 phases, one more failing site each) passes the jitcheck phases lane.
