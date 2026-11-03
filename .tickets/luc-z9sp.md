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

