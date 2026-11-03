---
id: luc-z9sp
status: open
deps: []
links: []
created: 2026-11-03T15:17:11Z
type: bug
priority: 2
tags: [runtime, threads]
---
# Finalization order: tear down __main__ only after daemon threads are stopped (CPython order), with the final std-stream flush not blocking on a buffer lock a stopped daemon holds (CPython's timed acquire and fatal 'could not acquire lock ... possibly due to daemon threads')

## Acceptance Criteria

shutdown.py and threads_daemon_io pass plain and handoff with finalize_modules after lu_task_shutdown_others


## Notes

**2026-11-03T23:40:11Z**

luc-74bf adds the final shutdown collection, which also runs before lu_task_shutdown_others: daemons keep running during it, and cycle __del__ output differs from the oracle. Probe in the review notes: a daemon spinning on a global plus a __main__ cycle with __del__.
