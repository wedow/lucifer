---
id: luc-l0q2
status: open
deps: []
links: []
created: 2026-11-05T11:00:48Z
type: task
priority: 2
assignee: Greg Wedow
---
# perf: PyErr_SetObject instantiates every C-raised exception through Python

runtime/capi/errors.c PyErr_SetObject now calls the exception class for every C-raised error (except at the recursion limit), so StopIteration.value, OSError errno and SystemExit.code come out right. That runs Python for the AttributeError behind every failed hasattr/getattr default, and every StopIteration from C iterators. Measure: a microbench of hasattr-miss and C-iterator exhaustion, before/after this change, JIT on. If it costs, follow CPython: store the bare value and normalize lazily, or instantiate directly in C for exact builtin classes whose init is known (AttributeError, StopIteration, KeyError, IndexError). The numpy suite tests from luc-h4gc must stay green.

