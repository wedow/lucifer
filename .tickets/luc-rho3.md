---
id: luc-rho3
status: open
deps: []
links: []
created: 2026-11-05T03:33:12Z
type: bug
priority: 2
tags: [runtime, threads, numpy]
---
# test_multithreaded_array_printing: a thread's repr sees another's recursion guard

numpy/_core/tests/test_arrayprint.py::test_multithreaded_array_printing fails about half the runs, with -X lucifer_jit=on and off alike: repr(z) in a worker returns 'array(..., dtype=float16)', the fill value numpy's _recursive_guard returns when (id(self), get_ident()) is already in its shared set. Live threads have distinct get_ident() values, so suspect the set (add/discard/contains racing under free threading) or id() reuse. CPython 3.14t passes.

