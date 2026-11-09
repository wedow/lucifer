---
id: luc-221o
status: open
deps: []
links: []
created: 2026-11-09T09:18:41Z
type: feature
priority: 2
tags: [libtest]
---
# libtest: C accelerator modules missing, so Lib/test's C-variant classes skip (1730 cases)

Full Lib/test run, 2026-11-09: cases CPython 3.14t passes and Lucifer does not. Lib/test runs C and pure-Python variants of these modules; the C variants skip or never get generated without the accelerator: _asyncio (test_asyncio.test_tasks 797, test_futures 123, test_eager_task_factory 48 via asyncio.tasks._CTask, test_free_threading 12, test_futures2 7), _decimal (test_decimal 341), _zoneinfo (test_zoneinfo 248), _functools (test_functools 120), _heapq (test_heapq 34). Build them from CPython's Modules/ like the other stdlib extensions, or provide them.

