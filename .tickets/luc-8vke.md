---
id: luc-8vke
status: open
deps: [luc-r6ny, luc-00js]
links: []
created: 2026-11-03T02:15:01Z
type: feature
priority: 2
parent: luc-nsmh
tags: [runtime, observability]
---
# M1.1a: tracemalloc over the allocator domains

_tracemalloc as a native module wrapping the obj/mem/raw domain tables, traceback capture through the one frame walker, PyTraceMalloc_Track/Untrack (FOUNDATIONS 2.6).

## Acceptance Criteria

tools/libtest run --modules test_tracemalloc at oracle parity (51 PASS); numpy _core/tests/test_multiarray.py::TestCreation::test_tracemalloc PASS.

