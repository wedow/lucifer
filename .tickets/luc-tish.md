---
id: luc-tish
status: closed
deps: []
links: []
created: 2026-11-06T16:45:21Z
type: task
priority: 1
assignee: Greg Wedow
---
# JIT: direct compiled-to-compiled calls and partial inlining of large callees

After luc-n05c, non-inlined calls between compiled units are ~30% of _sre/json time: ~70 ns per call vs ~22 ns in CPython (h_call -> frame push -> lu_eval -> lu_jit_run -> pop -> release_refs). _sre's _charset (598 units), _count (1055), _match (6649) exceed the 400-unit inline limit. Also: one opaque call two+ frames deep or in a loop cancels a whole inline; varargs (*args/**kwargs) functions never inline (str.is* ~440 ns vs 95 ns). Measure _sre/json microbenchmarks, Flask routes, AWFY.

