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

