---
id: luc-6nyq
status: open
deps: []
links: []
created: 2026-11-08T23:57:15Z
type: task
priority: 2
parent: luc-hez4
tags: [jit, perf]
---
# JIT: bind star parameters and keywords in the direct entry, not lu_jit_bind

After luc-hez4 a positional direct call is ~222 instructions and a keyword call ~481; the difference is lu_jit_bind (C: zeroes every local, scans names, looks kw-only defaults up in __kwdefaults__). lu_jit_bind is 1.5% of telco, 0.6% of AWFY DeltaBlue. Its callers there are positional calls of kernel functions with *args/**kwargs (telco per run: int.__new__ 353K, object.__new__ 184K, type.__call__ 177K, call_special 52K; DeltaBlue: call_special, set.__init__, list.__new__), each making an empty tuple reference and a fresh **kwargs dict. Bind in line: the empty tuple is a constant; a **kwargs dict nothing stores into or lets escape could stay virtual; constant kwnames could be mapped to slots at the call site when the callee's code is known.

