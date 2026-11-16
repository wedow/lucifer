---
id: luc-u47w
status: open
deps: []
links: []
created: 2026-11-16T11:32:48Z
type: bug
priority: 2
assignee: Greg Wedow
---
# JIT-on is 1.5x slower than JIT-off on run-once module bodies

import multiprocessing.pool: 138 ms JIT on vs 91 ms off. Run-once code should not pay compile; check tiering thresholds for module bodies.

