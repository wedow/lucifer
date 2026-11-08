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


## Notes

**2026-11-08T22:04:06Z**

luc-r1pk: str[i] (an exact str and an int) inlines str.__getitem__ in compiled code (AWFY Json 0.74x master's time). bool(x), int(str), float(str) and int.__repr__ are open.
