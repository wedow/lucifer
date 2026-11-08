---
id: luc-pj0a
status: closed
deps: []
links: []
created: 2026-11-08T08:03:52Z
type: task
priority: 3
tags: [jit, perf]
---
# JIT: forward f(a, *args, **kwargs) without allocation in user code

luc-esc2 compiles the CALL_FUNCTION_EX forwarding sequence (BUILD_LIST, LIST_EXTEND, list_to_tuple, BUILD_MAP, DICT_MERGE) as one call (h_call_forward) only in hidden code (type.__call__, call_special), since its exits leave from the sequence's first instruction and a user frame's traceback would show that instruction's position. User wrappers and decorators (def w(*a, **k): return f(*a, **k)) still make a list, a tuple and a dict per call. Either exit at CALL_FUNCTION_EX's position with the tuple and dict made from the parts, or record the position the traceback shows.


## Notes

**2026-11-08T14:34:27Z**

Done in luc-p2yt: forward_call applies to user code too. An exception from the call is CALL_FUNCTION_EX's (its snapshot, with the operands that remain consumed), so tb_lasti and the traceback's positions match; a declined call still leaves from the first instruction, where nothing is made. jit_forward_calls.py checks a decorator's wrapper, its traceback positions and tb_lasti.
