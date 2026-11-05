---
id: luc-6e6n
status: open
deps: []
links: []
created: 2026-11-05T22:34:07Z
type: feature
priority: 1
assignee: Greg Wedow
---
# perf: let inlined frames make calls so hot callees inline

P0b met the bar but Richards sits at 1.52-1.54x CPython with no margin. Calls from inside inlined frames would let run_task inline. General mechanism, not Richards-specific; show it across AWFY.

