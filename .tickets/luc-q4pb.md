---
id: luc-q4pb
status: closed
deps: []
links: []
created: 2026-11-17T07:45:03Z
type: task
priority: 2
---
# JIT: cheaper unit builds (frame-state walks)

The JIT's first builds were 17.7% of import_stdlib instructions; about 27% of a build walked frame states: every exit re-described its whole frame chain (a frame was kept only while every outer frame named no virtual object), publishing calls walked every caller frame for the allocator and for holds, and the once-walks re-read the locals a state shares with the one before. Describe a frame that names no virtual object once per caller record, keep its owned locations with it, walk caller frames once per allocation, skip shared locals, and stamp the values a call is handed.


## Notes

**2026-11-17T07:47:55Z**

Landed 6d64950. instructions:u, master -> this (min of 5, core lease): import_stdlib 2314.6 -> 2277.7 M (-1.6%), isort 3907.4 -> 3855.5 M (-1.3%), markdown 4654.0 -> 4574.2 M (-1.7%), pygments 17181 -> 17046 M (-0.8%). Steady rows within noise (richards, deltablue, logging, deepcopy, go, json_dumps, mako, raytrace: -0.1..-1.1% instructions, go +/-1% run to run). Gates: floor quick jit PASS, deopt PASS; kernelcheck plain/jit/jit-deopt pass. Left: liveness and dce still walk each state's stack and unshared locals; walk_base re-walks callers (caching them per chain measured no gain); make_deopt self ~4.5% of a build.
