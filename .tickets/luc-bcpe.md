---
id: luc-bcpe
status: open
deps: []
links: []
created: 2026-11-07T12:09:04Z
type: bug
priority: 3
assignee: Greg Wedow
---
# DeltaBlue: version-fact pass adds 1.8% instructions

After merging item 1, luc-x8q2's pass costs DeltaBlue 1.8% instructions compared with the pass off (hoisting 0.4%, repeat removal 0.8%, block-local load reuse 0.7%). Suspects: guards hoisted into the preheaders of short loops, and longer live ranges. Find the mechanism, then fix it generally (hoist only when the loop's trip count pays for it). Don't tune for the benchmark.

