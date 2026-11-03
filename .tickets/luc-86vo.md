---
id: luc-86vo
status: open
deps: [luc-a5u6]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 1
parent: luc-nsmh
tags: [milestone, capi]
---
# M1.2: first unmodified C extensions (_statistics, _bisect, _heapq)

dlopen of unmodified cp314t oracle lib-dynload extensions, PyModuleDef_Init multi-phase init, the 8/28/19 imported C-API functions implemented, tests/capi lane comparing against the oracle.

## Acceptance Criteria

make check-capi: _statistics, _bisect and _heapq from the oracle's lib-dynload import into bin/lucifer and every tests/capi program matches the oracle's output.
- Foundations: tests/capi/exc_cross.c (C calls Python that raises, Python catches a C raise, PyErr_GetRaisedException/SetRaisedException round trip, traceback lists only user frames) matches the oracle; a tests/capi module with Py_mod_multiple_interpreters and Py_mod_gil slots loads, and an unknown slot fails as in the oracle (docs/FOUNDATIONS.md 2.8, 2.15).
