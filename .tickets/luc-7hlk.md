---
id: luc-7hlk
status: closed
deps: []
links: []
created: 2026-11-07T05:56:29Z
type: task
priority: 2
---
# JIT: direct calls for keyword, star-argument and cell-making callees

After luc-cmel, compiled code enters a callee's direct entry only for positional calls (CALL) of codes without kw-only parameters, *args/**kwargs or MAKE_CELL in the prologue. CALL_KW, CALL_FUNCTION_EX and such callees still go h_call_kw/h_call_ex -> lu_call -> frame push -> lu_eval -> lu_jit_run. Flask/Jinja pass keywords on most calls. Bind constant kwnames at the call site against the callee code's names (the direct entry already fills defaults), and make cells in the entry.


## Notes

**2026-11-08T23:34:05Z**

Verified done by luc-yikq/luc-p2yt: the direct entry has a keyword entry (LU_DIRECT_KW before it) that compiled code (h_call_kw -> call_function -> lu_jit_call) and C (function_vectorcall) take; keywords, keyword-only defaults, star parameters and the prologue's cells bind in lu_jit_bind, positional arguments and defaults in line (emit.c push_in_line). tests/kernel/jit_call_shapes.py and the jitcheck shapes lane hold it. What remains is cost, not reach: a keyword call is ~610 instructions against ~236 for a positional one (luc-hez4 microbenchmark), the C binder's share.
