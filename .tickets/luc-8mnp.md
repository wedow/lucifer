---
id: luc-8mnp
status: open
deps: []
links: []
created: 2026-11-03T01:38:06Z
type: task
priority: 2
assignee: Greg Wedow
tags: [validation]
---
# Harness: verify parallel lanes really run in parallel; classify 128+N shell-wrapper exits as CRASH

## Acceptance Criteria

a fake runtime that makes pytest-run-parallel fall back to serial scores below the oracle in the parallel lanes; a wrapper exit 128+11 records CRASH; checks added to make check-harness

