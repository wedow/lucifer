---
id: luc-5u20
status: open
deps: [luc-a5u6]
links: []
created: 2026-11-03T01:39:45Z
type: task
priority: 2
assignee: Greg Wedow
parent: luc-nsmh
tags: [compiler, build]
---
# selfhost: stage-2 bundle byte-identical to stage 0

make selfhost runs tools/freeze with bin/lucifer as the bootstrap compiler (docs/DESIGN.md 3.4).

## Acceptance Criteria

make selfhost: the frozen bundle and every build/lib .pyc produced by bin/lucifer are byte-identical to the host-produced ones.

