---
id: luc-exj1
status: open
deps: []
links: []
created: 2026-11-06T05:05:21Z
type: feature
priority: 3
---
# Contexts in C: PyContext's layout and the thread state's context

lucifer's contextvars are Python classes (lib/modules/_contextvars.py) with each thread's current context in a module dict keyed by thread ident; CPython keeps it in tstate->context as a PyContext {ctx_prev, ctx_vars, ...}. C code that reads those (the oracle's _testcapi clear_context_stack, test_capi.test_watchers TestContextObjectWatchers.test_exit_base_context) sees no context, and the per-ident dict keeps a dead thread's context. Context watchers already report each switch (prim context_switched).

