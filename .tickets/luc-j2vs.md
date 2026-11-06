---
id: luc-j2vs
status: open
deps: []
links: []
created: 2026-11-06T15:16:01Z
type: task
priority: 2
assignee: Greg Wedow
---
# Slow builtin calls: bool(x), int(str), float(str), int.__repr__, str[i]

Measured in luc-v1cz: bool(x) ~1.3us, int(str)/float(str) ~1.6us, int.__repr__(x) ~0.9us vs repr(x) ~0.15us, str[i] ~0.4us. Find the shared mechanism (call path, arg unpacking) and fix it generally.

