---
id: luc-wg2d
status: open
deps: []
links: []
created: 2026-11-03T09:23:30Z
type: bug
priority: 1
tags: [runtime, concurrency]
---
# Defer async exceptions and Python signal handlers while a kernel builtin frame runs (as for the GC bit), so builtins stay atomic like C; seq_cst store of t->carrier in bind_tstate

