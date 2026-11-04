---
id: luc-o8b8
status: open
deps: []
links: []
created: 2026-11-04T09:32:04Z
type: bug
priority: 1
assignee: Greg Wedow
---
# finalization: stop daemon threads before module teardown

luc-c702 tried CPython's order (daemons stopped before module teardown) but it deadlocked ~7% when daemons were stopping the world, so it shipped the other order: daemons stop after teardown and see torn-down globals; test_io test_daemon_threads_shutdown_*_deadlock fails intermittently. Fix the deadlock between finalization and a daemon's stop-the-world and restore CPython's order.

