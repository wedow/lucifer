---
id: luc-0rr8
status: open
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 1
tags: [kernel]
---
# str.format(self=...) raises TypeError

lib/kernel/str.py defines format(self, *args, **kwargs), so '{self}'.format(self=1) raises 'got multiple values for argument self'; CPython's self is positional-only. Fails collection of propcache (122 ft-packages cases) and setuptools-rust (12). Other kernel methods with (self, *args, **kwargs) may share it.

