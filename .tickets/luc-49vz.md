---
id: luc-49vz
status: closed
deps: []
links: []
created: 2026-11-09T09:30:06Z
type: bug
priority: 1
tags: [jit, perf]
---
# jit: a marked site whose rebuilt unit keeps its failing guard exits every iteration (abs(-v) storm)

Pure-python gap lever 7. abs(-v) over floats leaves its loop unit at UNARY_NEGATIVE's guard on every iteration and never recompiles. The policy marks the site generic after GUARD_FAILS, but a unit built after the mark keeps the same guard; that exit counts as a leave, and a unit with loops is never retired, so the storm is permanent. Find why the mark has no effect, fix it in general form, and add a jitcheck growth lane that fails on master.


## Notes

**2026-11-10T02:06:23Z**

Root cause: UNARY_NEGATIVE guessed an int for any operand of unknown type (it has no feedback) and never read the site's generic mark, so after GUARD_FAILS marked the site the rebuilt unit kept the same guard. The policy counts a guard kept after the mark as a leave, and leave() never retired a unit with loops, so the loop paid an exit and an entry per iteration forever. Fix: a marked negation calls the kernel's negation (H_unary, unused until now); and a loop unit that keeps leaving at a kept guard on most entries is retired with backoff like a loop-free one (alone, without the builder fix, it ends the storm: 672k exits -> 651, loop interpreted). Probe of ten other unary/binary/call shapes over int/float/Fraction/str/tuple found no other storm. micro.py k_abs_neg: exits 672,710 -> 521 per run; 651 -> 45 ns per op (3.14t 53). tests/kernel/jit_marked_guards.py, jitcheck lane kept (fails on master: 71851 -> 111851).
