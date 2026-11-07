---
id: luc-8fns
status: open
deps: []
links: []
created: 2026-11-07T19:49:15Z
type: bug
priority: 1
tags: [jit, perf]
---
# jit: CD runs 8% slower in time on luc-keyu while its instructions and cycles fall

luc-keyu AWFY (canonical inner 250, median of iterations 3-6, best of 3, twice): CD branch/master 1.07-1.08 in time. Pinned counts at inner 100 (iterations 20 minus 10) say instructions -2.7%, cycles -7.8%. So the cost is outside the steady state the census measures: recompiles or exits that the larger inner count reaches, or compile time of a larger CD unit (split arms of the red-black tree's polymorphic compare). Attribute with -X lucifer_jit_stats at the canonical size and LUCIFER_JIT_DEBUG's compiled lines, then fix or limit the split there.

