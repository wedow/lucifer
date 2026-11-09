---
id: luc-1yki
status: in_progress
deps: []
links: []
created: 2026-11-09T09:30:06Z
type: bug
priority: 1
tags: [jit, perf]
---
# jit: unpack_sequence runs interpreted after its hoisted guard fails

Pure-python gap lever 7. do_unpacking is called with a tuple and then a list by turns. The tuple type guard is hoisted out of the loop; when it fails no lasting replacement unit is built, the loop runs in the interpreter, and per-value time degrades from 88 to 120 ns. Its unit is about 1 MB. Fix the missing rebuild, bound unit size adaptively if huge units are the cause, and add a jitcheck growth lane that fails on master.

