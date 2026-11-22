---
id: luc-3s7n
status: open
deps: []
links: []
created: 2026-11-22T22:12:37Z
type: task
priority: 0
parent: luc-orn7
tags: [perf, jit]
---
# jit: stay in compiled code (keyword and star calls bound at compile time, exceptions as control flow, cells as loads, small polymorphic sites inlined)

Measured idiom rows that leave compiled code and run at or below CPython's speed: f(j, c=i) 530 cycles (PyPy 12), forwarding f(*args, **kw) 1418 (8), an exception raised and caught in the same function 1509 (37; a deopt and interpreter run per raise), isinstance(b, (C, B)) 210 (8; a tuple built and released per test, then a C subtype walk), a closure call 66 (4; each cell read is a C call), a call over three receiver classes 178 (21; a helper call and a full return instead of three guarded inlines), sum(genexpr) 268 per 100 items (39). In real programs the call path outside units (lu_jit_direct, call_argv, lu_jit_run, h_call, frame push/pop) is about 5% of steady state and type_lookup 2.8%.

Stages, one landing each, each gated on its idiom rows:
1. Calls: a call whose target is a known Python function binds positional, keyword, default and star parameters at compile time and inlines or calls the unit directly; no dict, tuple or frame is made.
2. Exceptions: a raise whose handler is in the same unit (after inlining) is a jump to the handler with the exception object made only if the handler reads it; a prim that fails inside a try reaches the handler without a deopt.
3. Cells: a cell read or write is a load or store on the cell object.
4. Polymorphic sites: a site with 2-4 receiver classes inlines each under its guard.
5. isinstance and issubclass against constant classes or a tuple of them fold to class guards.
6. Generator expressions and comprehensions consumed in the same unit run as loops.

Acceptance: each row within 3x of PyPy when its stage lands (1.5x is the epic's bar); jitcheck lanes for each shape.

