---
id: luc-w977
status: open
deps: []
links: []
created: 2026-11-10T16:16:57Z
type: task
priority: 3
parent: luc-zelv
tags: [perf]
---
# cadence walking programs 3-4% slower on luc-custom2

Two timed runs on the bench host, ratio to CPython 3.14t, master -> luc-custom2: cadence.walking_rhythm 2.58/2.54 -> 2.66/2.65, cadence.walking_for_reward 2.88/2.85 -> 2.97/3.00, while continuing_brain went 3.61/3.58 -> 3.44/3.46 and the cadence geomean 3.08/3.03 -> 3.09/3.10. Find which luc-custom2 change (class notes only where a build asked, call arms only outside kernel-rooted units, genexpr resumption, extension slot fast path) costs the walking programs, by instruction counts per stage.

