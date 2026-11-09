---
id: luc-pfc5
status: closed
deps: []
links: []
created: 2026-11-07T05:56:29Z
type: task
priority: 3
---
# JIT: stop counting the code reference of direct frames

A direct call still increments and decrements the callee code's refcount (the function is borrowed since luc-cmel). Under threads that is a shared atomic per call on a code object another thread owns. It is counted because f.__code__ can be rebound during the call. Options: deferred references for code objects with the collector visiting frames that borrow (collector invariant 2), or func_set_code keeping the old code alive while frames run it.


## Notes

**2026-11-08T23:57:07Z**

Done in luc-hez4: direct frames (jit_lent) borrow their code; the collector counts the code of every jit_lent frame in every task's chain as an outside reference (collector invariant 2), and an exit that hands the frame to the interpreter takes the reference. tests/kernel/jit_direct_calls.py rebinds a running callee's __code__ and collects. Direct call: 258 -> 236 instructions.
