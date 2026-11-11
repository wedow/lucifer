---
id: luc-bed5
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# OSError treats filename=None as unset

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 60 cases over seeds 1 and 2 at 1,000 cases each.

```python
e = OSError(9, 's', 'old')
e.filename = None
print(str(e))
e = OSError(9, 's', 'old', None, 'old2')
e.filename = None
print(str(e))
e = OSError(9, 's', 'old', None, 'old2')
e.filename2 = None
print(str(e))
print(repr(e), e.filename, e.filename2)
```
CPython 3.14t:
```
[Errno 9] s: None
[Errno 9] s: None -> 'old2'
[Errno 9] s: 'old' -> None
OSError(9, 's') old None
```
Lucifer:
```
[Errno 9] s
[Errno 9] s
[Errno 9] s: 'old'
OSError(9, 's') old None
```

CPython tests the field for NULL, not None.


