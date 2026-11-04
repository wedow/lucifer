---
id: luc-rao5
status: open
deps: []
links: []
created: 2026-11-04T12:49:18Z
type: bug
priority: 2
assignee: Greg Wedow
---
# QSBR retire frees immediately during finalization

lu_qsbr_retire frees at once while finalizing, but daemon threads are still running until they are stopped (CPython order restored in luc-o8b8), so a daemon reading a list/dict/set during finalization can touch freed memory. Defer until all other tasks are stopped. From the luc-vb3n review.

