---
id: luc-qyzm
status: closed
deps: []
links: []
created: 2026-11-17T10:50:20Z
type: task
priority: 1
---
# compiled code resumes generators it did not make without leaving

Kernel consumers (sum/list/set/dict/any via next_item -> generators._next -> gen_send) side-exited at gen_send in compiled code: _next@35 CALL, the unit then retired (sum(genexpr) in _io FileIO.__init__ ran interpreted every open). Fix: gen_send runs nested out of line like other frame primitives; a call of the kernel's generator step on a virtual or shown generator steps it in line as FOR_ITER does (LU_RETURN_NEXT, MISSING at the end). Test: tools/jitcheck lane consumers (tests/kernel/jit_gen_consumers.py).

