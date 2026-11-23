---
id: luc-my3i
status: open
deps: []
links: []
created: 2026-11-23T05:25:51Z
type: bug
priority: 3
tags: [compat]
---
# compat: sys.getallocatedblocks() always answers 0

CPython reports the number of memory blocks the interpreter has allocated; here it is constant 0, so leak checks written against it see nothing. Found while writing release tests (luc-uqyj), which had to fall back to peak RSS.

