---
id: luc-fe77
status: open
deps: [luc-k5qz]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 2
parent: luc-nsmh
tags: [jit, perf]
---
# P2: AWFY 2x faster than CPython 3.14t

Optimize by mechanism across the panel; pyperformance panel passing.

## Acceptance Criteria

tools/bench/compare --suite awfy: geomean time/CPython at most 0.594; tools/bench/pyperformance: 9/9 PASS.

