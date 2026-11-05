---
id: luc-ovg2
status: open
deps: []
links: []
created: 2026-11-05T14:47:28Z
type: task
priority: 2
assignee: Greg Wedow
---
# PYTHONMALLOC debug layer and an allocation-free MemoryError

From luc-dawf (test_capi.test_mem: 45 run / 18 fail / 27 skip). 16 failures need CPython's PYTHONMALLOC=debug layer over our PEP 445 tables: _PyMem_Debug* wrappers with head/tail markers and serial numbers, 0xCD/0xDD/0xFD fills, API-misuse detection (wrong-domain free), _PyObject_DebugDumpAddress's exact stderr, _PyObject_IsFreed. 2 (test_set_nomemory) need raising MemoryError without allocating: CPython raises a preallocated instance and skips context chaining on that path; ours recurses through chaining. Estimated 450-650 lines; all 18 should pass together.

