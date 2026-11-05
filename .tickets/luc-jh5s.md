---
id: luc-jh5s
status: open
deps: []
links: []
created: 2026-11-05T03:27:42Z
type: bug
priority: 2
parent: luc-d5u1
tags: [jit, perf]
---
# jit: havlak side exits dominate (op 79) and always mode runs 4x slower than jit=off


tests/diff/awfy/havlak.py (difftest bridge, 1 loop), idle host:
CPython 3.14: 17.4 s (t: 22.1 s). lucifer jit=off 12.5 s, default (on) 18.7 s, always 49.7 s.
perf (always): eval_frame 12%, lu_jit_enter 12%, h_call 6%, lu_frame_call 4%; compile functions absent.
-X lucifer_jit_stats: on: compiles 254, compile time 18.5 ms, entries 61.7M, side exits 11.4M.
always: compiles 1351, compile time 144.6 ms (0.3% of run), units recompiled 207 (no recompile loop),
entries 262M, side exits 20.3M (opcode 79 alone 15.6M, opcode 23 1.9M, 56 0.13M).
Cause: not compile time, not deopt churn. Compiled code side-exits to the interpreter millions of times
at a few opcodes, and each re-entry pays lu_jit_enter; default mode is already slower than jit=off.
Fix belongs to the tier (side-exit opcodes, entry cost), not the gate.

## Notes

**2026-11-05T05:08:15Z**

Still flaking the gate after the 8x-oracle timeout: havlak TIMEOUT at 97.5 s in difftest deopt:7 on the dawf checkpoint-4 merge under load. Fixing the side exits is what makes this lane reliable; raise priority.
