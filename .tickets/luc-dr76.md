---
id: luc-dr76
status: open
deps: [luc-1dl8]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1b: functions, calls, closures, cells

Port from Python/bytecodes.c and ceval.c (initialize_locals, _PyEval_MakeFrameVectorcall): MAKE_FUNCTION, SET_FUNCTION_ATTRIBUTE (defaults, kwdefaults, annotations, closure), CALL, CALL_KW, CALL_FUNCTION_EX, positional/keyword/default/kw-only/*args/**kwargs binding with CPython's TypeError texts, MAKE_CELL, LOAD_DEREF, STORE_DEREF, DELETE_DEREF, LOAD_FROM_DICT_OR_DEREF, COPY_FREE_VARS, LOAD_CLOSURE, DELETE_FAST, LOAD_FAST_AND_CLEAR, STORE_GLOBAL, DELETE_GLOBAL, DELETE_NAME, COPY, SWAP, POP_JUMP_IF_* and JUMP_FORWARD/BACKWARD over the exact True/False/None singletons, NOT_TAKEN, RETURN_VALUE across frames. Python-to-Python calls push a frame in the same activation and never recurse in C; py_recursion_remaining is task state.
Implements: DESIGN 5.2 (calls), 6.1.
Files: runtime/interp/call.c, runtime/object/function.c.
Adds tests/kernel: calls.py, closures.py (written first; arguments are str/None/bool and small-int constants only).

Builder: sonnet.

## Acceptance Criteria

make check-kernel PROGRAMS="calls closures" (bin/lucifer -I -S on the host-compiled .pyc, 512 KiB RLIMIT_STACK, stdout+stderr+exit equal to cpython-3.14.10t).

