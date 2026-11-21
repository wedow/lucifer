---
id: luc-0rr8
status: closed
deps: []
links: []
created: 2026-11-14T03:32:01Z
type: bug
priority: 1
tags: [kernel]
---
# str.format(self=...) raises TypeError

lib/kernel/str.py defines format(self, *args, **kwargs), so '{self}'.format(self=1) raises 'got multiple values for argument self'; CPython's self is positional-only. Fails collection of propcache (122 ft-packages cases) and setuptools-rust (12). Other kernel methods with (self, *args, **kwargs) may share it.


## Notes

**2026-11-21T03:22:36Z**

tests/kernel/builtin_self_keyword.py reproduced the duplicate-argument TypeError in str.format, dict.update, GenericAlias.__call__, staticmethod.__call__, and SimpleNamespace.__replace__. Positional-only receivers let these methods collect self with the other keywords. The regression failed before the fix and matched CPython 3.14t afterward. The targeted kernel gate passed 39/39 in each of plain, jit, and jit-deopt.
