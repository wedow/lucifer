---
id: luc-x8q2
status: open
deps: []
links: []
created: 2026-11-07T02:48:42Z
type: feature
priority: 1
tags: [jit, perf]
---
# jit: dependencies and invalidation at polls, then GVN, load elimination and loop-invariant guards

Roadmap item 3. Compiled code relies on type, dict-key, global-value and function-version facts between polls: internal watchers record each unit's dependencies; a writer marks dependent units dead and raises every task's poll bit, so another thread's change reaches compiled code at its next poll and the running thread's own change kills the facts it touches at once. Then a dominator-scoped value-numbering pass over guards and ob_type/ma_keys/ob_size and same-object attribute loads between effects, and hoisting of version and type guards out of loops.

## Acceptance Criteria

Census of guards and loads per iteration on CD, DeltaBlue, Richards, Havlak, NBody. Difftest: another thread monkeypatches a method, a global and a class's base mid-loop; the loop sees each at its next iteration. A test of the running thread's own mutations. Per-unit guard counts in -X lucifer_jit_stats asserted by a tools/jitcheck lane. Invariant written in DESIGN 6.2 and FOUNDATIONS 2.4. AWFY, Flask routes, guard and instruction counts vs master and CPython 3.14t; cycles agree with instructions. Gates green.

