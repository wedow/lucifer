---
id: luc-fom3
status: closed
deps: []
links: []
created: 2026-11-03T07:36:04Z
type: bug
priority: 2
tags: [runtime, test]
---
# tests/capi/mutex.c timeout case depends on 20/50/70/100 ms sleeps; make it schedule-independent (no timing thresholds in tests)

## Acceptance Criteria

mutex passes 200/200 pinned to one CPU next to a busy loop

