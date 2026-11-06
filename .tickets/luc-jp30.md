---
id: luc-jp30
status: closed
deps: []
links: []
created: 2026-11-06T19:47:24Z
type: task
priority: 2
assignee: Greg Wedow
---
# JIT: compile comprehensions, tuple GET_ITER and args[i] in inlined frames

From luc-tish: comprehensions (LOAD_FAST_AND_CLEAR, CALL_INTRINSIC_1) never compile; inlined frames compile neither GET_ITER over a tuple nor args[i], blocking kernel argument parsing (str.strip via _chars, str.startswith via _args). A module-level loop variable resets the globals keys version so LOAD_GLOBAL is unsupported until respecialised. A pruned branch that flips recovers only after 50 guard failures (recompile on hot side exit).

