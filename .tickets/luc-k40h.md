---
id: luc-k40h
status: closed
deps: []
links: []
created: 2026-11-08T02:17:51Z
type: task
priority: 2
parent: luc-0ivh
tags: [jit, perf]
---
# JIT: generator units recompile to MAX_COMPILES on polymorphic guards (cli.black compile time)

From luc-0ivh timing: cli.black 1.13x master's time, genshi 1.09x. black's compile time went 690 -> 1148 ms (819 -> 919 compiles, recompiles 353 -> 450): generator units (Node.pre_order, Visitor.visit, LineGenerator.visit_default, transform_line) now compile and, like many plain functions in black, hit 12 compiles from LOAD_ATTR guards that keep failing on Leaf/Node shapes (GUARD_FAILS 50 -> NOSPEC -> every unit of the code recompiled). A whole resume unit recompiles per site. Item 5's refusal state per site and recompile policy would cut it; a cap on recompiles of large resume units is the stopgap.


## Notes

**2026-11-08T04:41:12Z**

Fixed by luc-xboo after merging luc-0ivh: cli.black compile 1997 -> 295 ms, 952 -> 586 compiles, 22 -> 0 disabled, wall 4.18 -> 2.74 s; genshi 930 -> 274 ms, wall 3.62 -> 2.79 s (census, test host).
