---
id: luc-r1pk
status: in_progress
deps: [luc-9ags, luc-n6yj, luc-4yfw, luc-xtmp, luc-m3wi, luc-uexr, luc-o8i6, luc-j2vs]
links: []
created: 2026-11-08T18:09:49Z
type: epic
priority: 2
tags: [jit, perf, scaling]
---
# attribute lookup and object creation: census, then general fixes

Corpus census 2026-11-08 mechanisms 7 (attribute/type lookup, 6.9% of log-weighted time) and 8 (object creation through type.__call__, 5.6%). Census on Flask, AWFY Json/DeltaBlue/Havlak and attribute-heavy pyperformance programs, rank by time, fix in general form. Folds in luc-9ags, luc-n6yj, luc-4yfw, luc-xtmp, luc-m3wi, luc-uexr, luc-o8i6, luc-j2vs.

