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


## Notes

**2026-11-07T08:53:02Z**

Landed on branch luc-cmel (roadmap item 1). Compiled code owns its root frame (LU_FRAME_JIT): locals are counted SSA values, STORE_LOCAL is gone, Python-running calls publish the call's exit description and the spill words, walkers read locals through lu_frame_slot (another task under a stop-the-world), a walker write syncs the slots and a guard after the call leaves. Calls of Python functions and bound methods enter the callee code's direct entry (frame pushed and popped by compiled code; defaults, free variables, borrowed parameters and function), from compiled code and from h_call's C path. LOAD_SUPER_ATTR compiles at the root. Call microbenchmark (unknown callee, two object arguments): 66.5 -> 20.1 ns/call (CPython 3.14t 38.1); 858 -> 234 instructions and 290 -> 77 cycles per iteration. AWFY time geomean vs master 0.841, every row faster; vs CPython 0.964 -> 0.811. Flask in process -1.5..-4%. Leftovers: luc-7hlk (keyword/star/cell callees), luc-23yk (frame state outside publishing calls), luc-v0kt (inlined frames at calls), luc-pfc5 (code reference), luc-15mf (registers across releases).
