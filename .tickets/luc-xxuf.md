---
id: luc-xxuf
status: open
deps: []
links: []
created: 2026-11-16T11:32:48Z
type: feature
priority: 1
assignee: Greg Wedow
---
# forkserver start method for multiprocessing

concurrent_imap 58x CPython: forkserver is unavailable so Pool defaults to spawn; each worker boots Lucifer and re-imports pyperf (453 ms vs 96 ms). CPython 3.14 defaults to forkserver on Linux; implement it rather than switching the default to fork.

