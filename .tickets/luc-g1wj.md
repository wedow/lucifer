---
id: luc-g1wj
status: open
deps: []
links: []
created: 2026-11-03T02:15:02Z
type: bug
priority: 2
tags: [runtime]
---
# Initialize mimalloc from .preinit_array so allocations in extension constructors on worker threads cannot share the main heap

## Acceptance Criteria

a capi test that first allocates on worker threads from a preloaded extension constructor passes 15/15; today it fails 11 of 15

