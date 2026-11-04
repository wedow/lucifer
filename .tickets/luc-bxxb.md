---
id: luc-bxxb
status: closed
deps: []
links: []
created: 2026-11-04T09:05:20Z
type: bug
priority: 2
assignee: Greg Wedow
---
# list mutators use stale bounds under concurrency

The kernel computes list bounds in Python before taking the lock: concurrent del L[a:] raises SystemError: list_splice() range out of bounds; extend and del L[i] have the same stale-index problem. Bounds must be clamped or computed under the list's critical section, as CPython's list_ass_slice does. Found in luc-rjiv review.


## Notes

**2026-11-04T20:42:47Z**

fixed: indices and ranges are clamped under the list lock; slice paths pass their length and retry when it changed
