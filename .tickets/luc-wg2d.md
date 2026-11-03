---
id: luc-wg2d
status: closed
deps: []
links: []
created: 2026-11-03T09:23:30Z
type: bug
priority: 1
tags: [runtime, concurrency]
---
# Defer async exceptions and Python signal handlers while a kernel builtin frame runs (as for the GC bit), so builtins stay atomic like C; seq_cst store of t->carrier in bind_tstate


## Notes

**2026-11-03T09:36:01Z**

done: poll.c defers signals, calls, GC, async exc in builtin frames; seq_cst carrier store; capi test (no kernel threads yet)
