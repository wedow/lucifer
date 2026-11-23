---
id: luc-5xk0
status: open
deps: []
links: []
created: 2026-11-23T05:25:51Z
type: bug
priority: 3
tags: [compat]
---
# compat: tracemalloc looks like a stub

tracemalloc.start() mid-run appears to trace nothing (noted by the luc-uqyj builder; not examined). Establish what is implemented (start/stop, get_traced_memory, snapshots), what Lib/test's test_tracemalloc passes, and whether the allocator hooks the in-line allocation and release paths check are ever set by it.

