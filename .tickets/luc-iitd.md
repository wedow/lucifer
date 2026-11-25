---
id: luc-iitd
status: open
deps: []
links: []
created: 2026-11-25T15:55:30Z
type: bug
priority: 0
---
# jit: rebuilt inlined frames take their code from the function

When compiled code deopts with inlined frames, push_inlined (runtime/jit/exit.c) rebuilds each one with lu_frame_push(func), which takes the code from func->func_code. A __code__ assignment while those frames run (it kills the unit through its function dependency, so it deopts) leaves the function naming the new code: the rebuilt frame then carries the new code with an instruction of the old one (tracebacks name the new code's function with line None: tests/kernel/jit_direct_calls rebound case), and it is sized from the new code's co_framesize while the old code's locals are written into it: an overrun when the old code's frame is bigger.

Exposed on master by 2ec83fb3 (code with monitoring data compiles again once its tools have gone), which let the rebound case run compiled; 94e59f99 passes.

Fix: one frame-push path that takes the code explicitly (lu_frame_push its thin caller), used by push_inlined with the record's code; audit every other place that rebuilds or sizes a running frame from the function.

