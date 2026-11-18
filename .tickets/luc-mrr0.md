---
id: luc-mrr0
status: open
deps: []
links: []
created: 2026-11-18T20:07:40Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: C calls of Python functions take the direct entry

function_vectorcall (lu_call, slot wrappers, kernel helpers calling kernel Python: setitem, binary_op, negative, compare) bound a frame in C and ran it through lu_eval and lu_jit_run. With no PEP 523 hook, a frame below and the JIT on, a plain positional call now takes the callee code's direct entry as compiled code does (lu_jit_direct, which returns a done flag now), the task pinned around it. Follow-up of luc-tgqa: raytrace, nqueens and the pickle rows still entered 0.2-2 M callees per run from C.

