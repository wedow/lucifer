---
id: luc-mrr0
status: closed
deps: []
links: []
created: 2026-11-18T20:07:40Z
type: task
priority: 2
tags: [jit, perf]
---
# jit: C calls of Python functions take the direct entry

function_vectorcall (lu_call, slot wrappers, kernel helpers calling kernel Python: setitem, binary_op, negative, compare) bound a frame in C and ran it through lu_eval and lu_jit_run. With no PEP 523 hook, a frame below and the JIT on, a plain positional call now takes the callee code's direct entry as compiled code does (lu_jit_direct, which returns a done flag now), the task pinned around it. Follow-up of luc-tgqa: raytrace, nqueens and the pickle rows still entered 0.2-2 M callees per run from C.


## Result

Landed. Calls compiled code entered from C per run (-X lucifer_jit_stats, 3+3 pyperformance values), master
538654a -> this stage: raytrace 549,533 -> 107,186, deltablue 456,438 -> 251,649, chaos 204,595 -> 85,608,
unpickle_pure_python 1,033,667 -> 933,428, json_loads 196,740 -> 142,430, go 33,899 -> 22,582. check-jit's
ccalls lane (a dict lookup calling compiled __hash__ and __eq__) holds flat now and left NOT_BUILT.
AWFY steady instructions (core lease, 1 rep) master -> this stage: Json 0.992, Richards 0.995, the rest within
0.1% but DeltaBlue (1.013, its 1-rep spread). Totals of the landing it went in with: luc-knag.
