---
id: luc-o8b8
status: closed
deps: []
links: []
created: 2026-11-04T09:32:04Z
type: bug
priority: 1
assignee: Greg Wedow
---
# finalization: stop daemon threads before module teardown

luc-c702 tried CPython's order (daemons stopped before module teardown) but it deadlocked ~7% when daemons were stopping the world, so it shipped the other order: daemons stop after teardown and see torn-down globals; test_io test_daemon_threads_shutdown_*_deadlock fails intermittently. Fix the deadlock between finalization and a daemon's stop-the-world and restore CPython's order.


## Notes

**2026-11-04T10:18:28Z**

Fixed on luc-c702: the hang was a daemon parked on the stop-the-world mutex being handed it as finalization shut it down, then hanging at attach holding it; the shutdown collection's stop waited for ever (4 daemons calling setrecursionlimit hung 30/30). After the shutdown the finalizing task keeps the mutex; held stream locks are taken over. threads_shutdown covers both.
