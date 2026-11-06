---
id: luc-ik2m
status: open
deps: []
links: []
created: 2026-11-06T08:49:22Z
type: task
priority: 2
tags: [perf, threads]
---
# thread exit purges mimalloc segments: madvise + refault on every short-lived thread

Found by the web benchmark (luc-pywv) while the server still made one thread per request (werkzeug closes each connection). perf at 8 server CPUs: ~7% in page faults (kernel_init_pages, __zap_vma_range) and ~2% in madvise, from carrier retire() -> lu_mimalloc_abandon -> _mi_heap_collect_abandon -> mi_segment_try_purge -> _mi_os_purge_ex; the next thread then refaults the pages. Kernel memory calls on a hot path (a server spawning a thread per connection, a thread pool growing and shrinking). Keep abandoned segments committed and purge lazily (delay or only on memory pressure), as with the no-munmap-on-hot-paths rule. Measure with tools/bench/web/app.py under a connection-per-request client (wrk -H 'Connection: close').

