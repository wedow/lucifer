---
id: luc-p7vb
status: open
deps: []
links: []
created: 2026-11-10T03:18:18Z
type: task
priority: 2
assignee: Greg Wedow
tags: [jit, startup]
---
# jit: persistent code cache across runs

Startup and short CLI runs pay JIT compile again in every process: import black 0.85 s vs 3.14t 0.14 s, cli suite ~7x, compile is about 8% of cli wall time even after luc-xboo and luc-ci4f. CPython caches its per-file work in .pyc, and serc shows how much cheap per-file work matters at scale. Design a cache of compiled units keyed by code object hash + runtime build id + the guard and feedback facts the unit assumed, stored next to __pycache__ or in a user cache dir. A cached unit is used only after its assumptions are re-validated (type versions are per-process, so key on structural facts and re-establish dependencies at load). Also cache warm feedback so hot code compiles on first entry. Measure -c pass, import black and the cli suite; decide from data whether this beats lower thresholds plus background compile (luc-f7cn).

