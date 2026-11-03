---
id: luc-tpwz
status: open
deps: []
links: []
created: 2026-11-03T06:19:17Z
type: task
priority: 2
tags: [runtime]
---
# Remove the fixed 200-entry static type table limit (no hard caps); new tasks inherit sys.setrecursionlimit


## Notes

**2026-11-03T06:19:24Z**

LU_MAX_STATIC_TYPES mirrors CPython's _Py_MAX_MANAGED_STATIC_BUILTIN_TYPES (200), which sizes per-interpreter arrays. It only matters if Lucifer needs more managed static types than CPython does; heap types are not limited. Low priority; the recursion-limit inheritance is the real fix here.
