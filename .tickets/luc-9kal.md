---
id: luc-9kal
status: open
deps: []
links: []
created: 2026-11-06T21:49:51Z
type: task
priority: 2
assignee: Greg Wedow
---
# Kernel _args padding allocates on every call

From luc-jp30: (*args, MISSING) padding in the kernel's _args dominates str.startswith once inlined (alloc, free, locks in perf). Remove the allocation generally (scalar replacement or no padding tuple).

