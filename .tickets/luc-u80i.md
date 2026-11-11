---
id: luc-u80i
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# MemoryError.__new__ keeps args where CPython's freelist path drops them

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 20 cases over seeds 1 and 2 at 1,000 cases each.

```python
print(MemoryError('x').args)
print(MemoryError.__new__(MemoryError, 'x').args)
print(MemoryError.__new__(MemoryError, 'x', 'y').args)
class M(MemoryError): pass
print(M.__new__(M, 'x').args)
```
CPython 3.14t:
```
('x',)
()
()
('x',)
```
Lucifer:
```
('x',)
('x',)
('x', 'y')
('x',)
```

An implementation detail of CPython; low priority.


