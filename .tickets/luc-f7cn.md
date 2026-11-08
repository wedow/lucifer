---
id: luc-f7cn
status: open
deps: []
links: []
created: 2026-11-08T04:20:11Z
type: task
priority: 3
tags: [jit, perf]
---
# jit: background compilation, if first-run latency still shows compile time

Split from luc-32pn. After luc-xboo (size-scaled thresholds and waits, linear regalloc liveness, no recompile loops) compile time per process on the 30 census programs fell from 0.3-2.0 s to 0.1-0.35 s (sum 24.5 s -> 7.7 s): cli.isort 158 ms of 0.75 s, cli.black 286 ms of 2.8 s, import black ~140 ms. Moving compiles to a thread would take at most that off wall time. It needs: a build that does not read the live frame (lu_ssa_build takes the frame), publication by ticket (deps.c already refuses a stale compile), and units entered only at the next entry. Measure first-request latency (Flask) and the cli wall share before building it.


## Notes

**2026-11-08T20:43:09Z**

luc-ci4f (census 2026-11-08 mechanism 2): after pruning untaken branches in large roots, compile time per cli process is black 119 ms of 2.24 s (5%), isort 45 of 0.66 (7%), markdown 182 of 1.12 (16%), pycodestyle 343 of 3.78 (9%), pyflakes 221 of 2.83 (8%), pygments 220 of 2.49 (9%): about 8% of cli wall time, geomean. pyperformance times best-of after a warm-up, so it gains little. Not built in luc-ci4f: a compile thread must be an attached thread that polls for stop-the-world (the builder is long C code without polls; a collection would wait for the compile), and lu_ssa_build reads the live frame (c->live: entry locals' types for OSR, unboxed entry, the stack depth), which needs a snapshot. A lower threshold alone (4 -> 1 per unit) cut interpreted instructions 25-35% but cost as much compile as it saved (cli geomean +2.5%); with compiles off the main thread it would be free, so the two belong together.
