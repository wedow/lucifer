---
id: luc-5y28
status: open
deps: []
links: []
created: 2026-11-18T20:22:43Z
type: task
priority: 3
tags: [jit, perf]
---
# chaos: +0.9% instructions per iteration with direct calls (luc-tgqa)

Accepted loss of luc-tgqa: pyperformance chaos 59.2 M -> 59.8 M instructions per iteration on a core lease (1 rep), the only row of the twelve that grew (geomean 0.938). Find whether a call site declines the direct entry each time (a poll pending, a datastack chunk boundary, a nargs mismatch) and pays the dispatch and h_call both, or whether it is layout.

