---
id: luc-pfc5
status: open
deps: []
links: []
created: 2026-11-07T05:56:29Z
type: task
priority: 3
---
# JIT: stop counting the code reference of direct frames

A direct call still increments and decrements the callee code's refcount (the function is borrowed since luc-cmel). Under threads that is a shared atomic per call on a code object another thread owns. It is counted because f.__code__ can be rebound during the call. Options: deferred references for code objects with the collector visiting frames that borrow (collector invariant 2), or func_set_code keeping the old code alive while frames run it.

