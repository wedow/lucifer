---
id: luc-a8sh
status: closed
deps: []
links: []
created: 2026-11-03T06:16:17Z
type: bug
priority: 0
tags: [runtime, concurrency]
---
# check-capi refcount (and once mutex) fails intermittently, about 1 in 20-40 runs, on master

## Acceptance Criteria

root cause found by bisect plus a reliable reproducer, fixed; check-capi passes 200 consecutive runs of refcount and mutex


## Notes

**2026-11-03T07:32:32Z**

Test race, not a runtime regression. check_shared_refcount checked that main's queued decref had not freed the object while the owner thread was free to run PyGILState_Release, whose unbind merges the queue and frees it. Present since the check was written: under one-CPU contention it fails 24/100 before K1p, 21/100 at K1p, 19/100 at K1c and 11/100 on master; on a quiet host it fails 0-1% at every point, so 15-run passes said nothing. A 10 ms sleep before the check fails 5/5. Fix: the owner drops its reference before the second barrier and waits on a third, so main checks before the merge. After: 0/100 under contention, 0/200 quiet, 0/20 with the sleep; mutex 0/200; make check passes. The single mutex failure did not reproduce and has no captured output; its timeout check depends on 20-100 ms sleeps.
