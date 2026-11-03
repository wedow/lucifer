---
id: luc-a8sh
status: open
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

