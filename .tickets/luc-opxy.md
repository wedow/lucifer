---
id: luc-opxy
status: open
deps: []
links: []
created: 2026-11-07T03:07:55Z
type: task
priority: 2
assignee: Greg Wedow
---
# Build with function and loop alignment to cut layout noise

From luc-5ds3: -falign-functions=64 -falign-loops=32 made both base and master 3-4% faster on DeltaBlue and ~6% on Json; layout-only changes swing AWFY rows 3-4% between builds, producing false regressions. Measure the flags across the full AWFY panel, Flask and kernel; adopt if broadly positive, and check the spread between layout-only builds shrinks.

