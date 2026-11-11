---
id: luc-mpye
status: closed
deps: []
links: []
created: 2026-11-10T22:13:48Z
type: task
priority: 2
tags: [perf, jit]
---
# Code size tripled with the deeper opaque-call bounds

luc-custom4's opaque-call bounds (three frames deep, four in kernel code, four calls per inlined frame) took mako's compiled code from 4.3 MB to 15.8 MB, 394 compiles and 136 recompiles, 416 ms of compile time in 6 loops, with 568k entries waiting on a unit meanwhile; the instruction counts per loop still fell (629M -> 525M). Check the wall time of short runs (cli, startup) and the background compiler's queue, and bound the inlining by the code it adds where it does not pay.


## Notes

**2026-11-11T00:09:20Z**

Resolved on luc-custom4 by measurement: four calls per inlined frame cost AWFY Json 30% in its steady state (not compile time: 81M -> 105M instructions an iteration at 20 and 100 iterations alike) and cli 8% wall; the bound is back to one call per frame, one frame deeper than master.
