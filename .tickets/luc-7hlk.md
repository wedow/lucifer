---
id: luc-7hlk
status: open
deps: []
links: []
created: 2026-11-07T05:56:29Z
type: task
priority: 2
---
# JIT: direct calls for keyword, star-argument and cell-making callees

After luc-cmel, compiled code enters a callee's direct entry only for positional calls (CALL) of codes without kw-only parameters, *args/**kwargs or MAKE_CELL in the prologue. CALL_KW, CALL_FUNCTION_EX and such callees still go h_call_kw/h_call_ex -> lu_call -> frame push -> lu_eval -> lu_jit_run. Flask/Jinja pass keywords on most calls. Bind constant kwnames at the call site against the callee code's names (the direct entry already fills defaults), and make cells in the entry.

