---
id: luc-glgt
status: open
deps: [luc-t6y4, luc-bmfl]
links: []
created: 2026-11-03T01:39:45Z
type: epic
priority: 0
assignee: Greg Wedow
parent: luc-nsmh
tags: [milestone, numpy]
---
# M1.7: numpy suite parity, serial and parallel, GIL disabled (public bar)

The fixed public bar: numpy's own suite against the unmodified cp314t wheel.

## Acceptance Criteria

tools/numpytest/run.py run, serial and --parallel, --python bin/lucifer: 50,049 PASS each (oracle parity), no FAIL/ERROR/TIMEOUT/CRASH, GIL disabled after every shard.

