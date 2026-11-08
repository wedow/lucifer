---
id: luc-pj0a
status: open
deps: []
links: []
created: 2026-11-08T08:03:52Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: forward f(a, *args, **kwargs) without allocation in user code

luc-esc2 compiles the CALL_FUNCTION_EX forwarding sequence (BUILD_LIST, LIST_EXTEND, list_to_tuple, BUILD_MAP, DICT_MERGE) as one call (h_call_forward) only in hidden code (type.__call__, call_special), since its exits leave from the sequence's first instruction and a user frame's traceback would show that instruction's position. User wrappers and decorators (def w(*a, **k): return f(*a, **k)) still make a list, a tuple and a dict per call. Either exit at CALL_FUNCTION_EX's position with the tuple and dict made from the parts, or record the position the traceback shows.

