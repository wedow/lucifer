---
id: luc-6m9j
status: open
deps: []
links: []
created: 2026-11-03T08:34:12Z
type: bug
priority: 2
tags: [kernel]
---
# generator close(): skip resuming when the suspended yield has no enclosing handler, as CPython 3.12+ gen_close does, so a surviving frame's f_back is None; frame objects and tracebacks need tp_traverse

