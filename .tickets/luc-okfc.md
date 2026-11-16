---
id: luc-okfc
status: open
deps: []
links: []
created: 2026-11-16T11:32:48Z
type: bug
priority: 1
assignee: Greg Wedow
---
# BytesIO has no readline; per-byte read(1) fallback

dulwich_log 12.5x CPython: io.BytesIO lacks readline so line iteration falls back to read(1) per byte; _parse_message is 47% of the profile, iterating 1000 small BytesIO is 420x slower. Add readline/readlines/__next__ on BytesIO.

