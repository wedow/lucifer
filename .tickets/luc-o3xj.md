---
id: luc-o3xj
status: closed
deps: []
links: []
created: 2026-11-03T20:37:59Z
type: task
priority: 1
---
# Bound every gate step with a timeout

check-embed hung 4h on a second Py_Initialize (fixed in K1u) and blocked two gates. Every make check step needs a per-program timeout that fails the step, as kernelcheck already does. An orphaned threads_daemon_io child also outlived its run, so the runners must kill the process group on timeout.


## Notes

**2026-11-03T20:41:11Z**

Makefile gate steps wrapped in timeout (TMO/STEP_TMO); branch luc-o3xj
