---
id: luc-g4cx
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array: conversion callbacks that export or grow the array behave differently

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff, about 87 cases over seeds 1 and 2 at 1,000 cases each.

```python
from array import array
def t(label, f):
    try:
        print(label, f())
    except Exception as e:
        print(label, type(e).__name__, e)
keep = []
class Export:      # __index__ takes a buffer export of the array being filled
    def __init__(self, a): self.a = a
    def __index__(self): keep.append(memoryview(self.a)); return 5
a = array('l', [3]); t("fromlist with export taken in __index__", lambda: a.fromlist([Export(a)])); print(a.tolist())
keep.clear()
class Grow:        # __index__ grows the array by 20 items
    def __init__(self, a): self.a = a
    def __index__(self): self.a.frombytes(bytes(20 * self.a.itemsize)); return 7
a = array('h', [1]); t("append, __index__ grows the array", lambda: a.append(Grow(a))); print(len(a), a.tolist()[-3:])
```
CPython 3.14t:
```
fromlist with export taken in __index__ None
[3, 5]
append, __index__ grows the array None
22 [0, 0, 0]
```
Lucifer:
```
fromlist with export taken in __index__ BufferError Existing exports of data: object cannot be re-sized
[3]
append, __index__ grows the array None
22 [0, 0, 7]
```


