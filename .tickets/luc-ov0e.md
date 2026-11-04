---
id: luc-ov0e
status: open
deps: []
links: []
created: 2026-11-04T23:08:35Z
type: task
priority: 3
assignee: Greg Wedow
---
# faulthandler: lucifer-only check that threads arming and cancelling dump_traceback_later at once leave one watchdog and none after the last cancel

faulthandler_later_threads now serializes each arm+cancel because the free-threaded oracle deadlocks in join about 1 run in 25. The concurrent case needs a check that does not compare with the oracle (assert the invariants in the program and run it on lucifer alone).

