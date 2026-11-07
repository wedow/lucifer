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


## Notes

**2026-11-07**

Landed on the branch in five steps: per-unit census counts in -X lucifer_jit_stats; dependencies and invalidation at polls with the first consumer (guards on constant types, dict keys, global values and function versions leave loops and repeats go); the tests reworked so they fail without invalidation; value numbering over a value's type version, fixed facts (exact immutable type, not NULL, bits) and block-local loads; debug logging of invalidations.

Invariant (DESIGN 6.2 step 7). Old: guards are checked by the code itself each time they run; "Invalidation sets a flag that compiled code checks at guards and polls" (unimplemented). New: another thread's change to a type, dict keys or one key's value, or a function's code or defaults reaches compiled code at its next poll (back-edge, inlined call entry, first check after a call returns); the running thread's own change kills the facts before compiled code next relies on them; heap values are never kept across a poll or a call.

Census, guards per AWFY iteration (millions), master -> branch: CD 250.2 -> 147.2, DeltaBlue 39.7 -> 30.0, Richards 532.6 -> 222.5, Havlak 259.4 -> 191.6, NBody 140.5 -> 24.8. Liveness checks added: 37.0, 6.3, 159.9, 41.0, 54.5 (3 instructions each).

Instructions per iteration (a pinned core pair, 10 minus 5 iterations): CD -12.4%, NBody -11.3%, Richards -4.9%, Sieve -3.9%, Bounce -3.8%, others -0.1 to -2.4%, Mandelbrot +0.1%. AWFY time best-of-5: geomean 0.979 of master (CPython ratio 0.956 -> 0.935); CD 0.884, Towers 0.955, NBody 0.946, Richards 0.966; DeltaBlue 1.045 (1.034 over six alternating rounds, 1.019 with -falign-functions=64 -falign-loops=32 on both builds, inside that build's spread). Flask in-process routes unchanged (within 1%).

Left: DeltaBlue's residual (instructions -0.9%, time +2-4%); liveness checks after every release that may run a finalizer (decref-heavy loops keep one per release; item 1 removes STORE_LOCAL, ownership-based elision would remove more); global value guards inside loops whose bodies the facts pass cannot hoist (multiple outside predecessors); load elimination across blocks.

The guard removal brought check-jit's deopt lane to a latent exit bug (luc-8h3o, reproduced on master at deopt:11 and :21): folded float arithmetic of an operator's method resumed after the operator with its operands on the stack. Fixed on the branch, with tests/kernel/jit_float_island.py.

Gates on 5248d7a (master 4528888 merged, nothing newer): make check 0 (593/593 in plain, jit and jit-deopt), abi-audit 0, check-jit 0, check-harness 0, check-compiler 0.

**2026-11-07, after merging item 1 (master 730d384)**

Item 1 removed STORE_LOCAL, so the hot loop of tests/kernel/jit_guards.py runs no liveness checks at all now. A writer's kill clears the code's direct entry (master's own kill does; the entry's dead-unit refusal still covers a caller that read the entry first). Dict facts are watched and read inside the dict's critical section, which closes the compile race under the C11 model (argument in deps.c). The writers' global mutex is luc-fwnf.

AWFY best of 3, branch / master: geomean 0.973 (master 0.816 of CPython, branch 0.794). CD 0.907, Bounce 0.918, NBody 0.926, Richards 0.938, Havlak 0.965; DeltaBlue 1.038 (1.071 over six alternating rounds), Mandelbrot 1.036 (1.058), Storage 1.024 (0.990). Mandelbrot runs the same instructions as master (4,587.5M vs 4,587.9M per iteration) with 19% more cycles: its hot unit's IR is master's but for the DIV_F exit state, so it is code placement. DeltaBlue runs 0.4% more instructions than master, and 1.8% more than the branch with the facts pass off (14,894.8M vs 14,625.8M): with item 1 the pass now costs DeltaBlue instructions (hoisting 0.4%, repeat removal 0.8%, block loads 0.7%), likely from guards hoisted into short loops' preheaders and longer live ranges. Left for a follow-up.
