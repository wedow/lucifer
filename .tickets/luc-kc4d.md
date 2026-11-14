---
id: luc-kc4d
status: closed
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


## Notes

**2026-11-14T03:32:17Z**

Done on luc-kc4d. Modules/_lsprof.c, rotatingtree.c/.h and clinic/_lsprof.c.h of v3.14.10 build unmodified into lib-dynload (STDLIB_EXTS). Runtime gaps fixed: _Py_CheckFunctionResult exported with CPython's messages (shared by the C-function vectorcalls); _PyTime_FromSeconds/FromLong/FromSecondsObject; reprs of method/classmethod/wrapper/member/getset descriptors and method-wrapper; _imp's frames hidden like the other builtin ports (profile.py's Bad call assertion); marshal.dump. Kernel tests cprofile_stats (whole pstats report under a counting timer, identical to the oracle), descriptor_repr, profile_builtin_port. test_profile 7/7, test_cprofile 25/27 JIT off and on: the two left are forkserver (luc-bxi0) and spawn (site never imported, luc-g4vm). ft-packages JIT off: 163604 ERROR before; after, TIMEOUT 96507, NOTRUN 62972, PASS 758, ERROR 3202, CRASH 161, FAIL 2, SKIP 2, on a loaded host. Causes filed: luc-x3m8 (timeouts: random.seed, compile), luc-vavj (RLock subclass, joblib), luc-0rr8 (str.format self, propcache, setuptools-rust), luc-gdfk (msgpack abort), luc-4qfg (marshal.load). Leftover profiler differences not filed: a builtin port's module body is a visible frame on import, with-statements on locks fire no C CALL for __enter__/__exit__, _thread.allocate_lock calls fire no C call event.
