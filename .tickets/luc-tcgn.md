---
id: luc-tcgn
status: open
deps: []
links: []
created: 2026-11-10T05:15:16Z
type: task
priority: 3
tags: [libtest, stdlib]
---
# libtest: C-variant test classes need modules named _functools, _heapq, _asyncio, _zoneinfo

Owner decision (2026-11-10): the C accelerators of Python-level logic stay Python as JIT targets, so CPython's Modules/ C for _functools, _heapq, _asyncio, _zoneinfo (and _json, _struct, _io, _bisect, _datetime, _csv, list sort) is not vendored. Lib/test reaches the C variants by import_fresh_module(name, fresh=['_functools']) (test_functools 119 SKIP, test_heapq 34) or by the accelerator's own name (asyncio.tasks._CTask and _CFuture: test_asyncio.test_tasks 797, test_futures 123, test_eager_task_factory 48; test_zoneinfo 248 via _zoneinfo; test_datetime _Fast is luc-7aot). What a test needs is an importable module of that name exposing the accelerator's API: lib/modules/_functools.py (cmp_to_key, partial, reduce, _lru_cache_wrapper, the placeholder), _heapq.py, _asyncio.py (Task, Future, the current-task registry), _zoneinfo.py, each the Python implementation. Some C-only assertions (immutable types, exact reprs, sys.getsizeof) will then fail and show what differs.

