---
id: luc-mfd7
status: open
deps: []
links: []
created: 2026-11-11T08:30:07Z
type: bug
priority: 2
assignee: Greg Wedow
---
# web: 1-CPU p99 latency is ~8 s while p50 is 42 ms

Two dailies in a row (11-10 7874 ms, 11-11 7946 ms): at 1 CPU Lucifer's Flask p99 is ~8 s; at 2-8 CPUs it is 53-119 ms. Likely a starved connection or a stall (compile, collection, accept backlog) at 1 carrier. Find which requests take seconds and why.

