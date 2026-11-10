---
id: luc-kc4d
status: open
deps: []
links: []
created: 2026-11-09T06:05:00Z
type: feature
priority: 2
tags: [stdlib, capi]
---
# _lsprof (cProfile) is missing

import cProfile fails with ModuleNotFoundError: No module named '_lsprof'. cadence's benchmarks/settle_threads.py imports cProfile at module level, so tests/test_thread_benchmark.py fails to collect on Lucifer (2 tests CPython passes with numba installed). CPython's Modules/_lsprof.c (with rotatingtree.c) builds on sys.monitoring callbacks in 3.14 (PY_START, PY_RETURN, CALL, C_RETURN, C_RAISE ...).

## Design

Vendor Modules/_lsprof.c and rotatingtree.c from the tag and build them into lib-dynload as STDLIB_EXTS does for the other stdlib C modules, adding the C-API it needs; a kernel test that profiles a small program with cProfile and checks the call counts against the oracle (timings vary).

