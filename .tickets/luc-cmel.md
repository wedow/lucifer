---
id: luc-cmel
status: open
deps: []
links: []
created: 2026-11-06T19:47:24Z
type: task
priority: 2
assignee: Greg Wedow
---
# JIT: compiled code pushes frames and enters compiled callees directly

After luc-tish a non-inlined call costs ~55 ns vs ~43 ns in CPython; trimming the C path saves only a few ns. Needs the frame pushed by compiled code and the callee's compiled entry jumped to. json makes ~2M calls/run, Flask ~8M per request batch. Also: the per-unit entries counter is written on every direct call (shared cache line under threads).

