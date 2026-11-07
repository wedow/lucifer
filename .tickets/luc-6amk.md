---
id: luc-6amk
status: open
deps: []
links: []
created: 2026-11-07T22:27:53Z
type: task
priority: 3
assignee: Greg Wedow
tags: [perf, scaling]
---
# runtime: critical sections on C paths cost 4% of corpus time

Corpus census 2026-11-07: PyCriticalSection_* on C paths is 3.9% of corpus time, 12% of raytrace and 11% of spectral_norm. Find the hot sites and take the owner-thread or lock-free fast path where the object is private or immortal, as list reads do since luc-vzka.

