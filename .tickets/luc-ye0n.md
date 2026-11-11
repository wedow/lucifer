---
id: luc-ye0n
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# OSError errno-to-subclass lookup scans with == instead of hashing

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 25 cases over seeds 1 and 2 at 1,000 cases each.

```python
class Hh(int):
    def __hash__(self): print("hash"); return 2
    def __eq__(self, o): print("eq"); return False
print(type(OSError(Hh(32), 'x')).__name__)
print(type(OSError(Hh(2), 'x')).__name__)
```
CPython 3.14t:
```
hash
eq
OSError
hash
eq
OSError
```
Lucifer:
```
eq
eq
eq
eq
eq
eq
eq
eq
eq
eq
eq
eq
eq
eq
... (24 more lines)
```


