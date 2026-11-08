---
id: luc-0yfw
status: open
deps: []
links: []
created: 2026-11-08T04:20:11Z
type: bug
priority: 2
tags: [jit, perf]
---
# jit: Havlak leaves compiled code at polls ~675 times per iteration

Found by luc-xboo's AWFY exit census: Havlak's poll exits grow with iterations (13.5k at 20 iterations, 40.5k at 60; master the same, 14.5k at 20). CD grows 190 -> 555. Every other exit kind on all 14 rows is flat after warm-up. Find what raises the poll bit each iteration (collections stopping the world, invalidation signals from writers, or the poll word's other events) and whether a poll that only needs a safepoint can stay in compiled code.

