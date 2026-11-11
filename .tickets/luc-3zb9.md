---
id: luc-3zb9
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# itertools.count converts bool start/step to int and ignores an int subclass step past sys.maxsize

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): itertools_diff, 17 cases over seeds 1 and 2 at 1,000 cases each.

```python
import itertools
print(repr(itertools.count(False, 2)), repr(itertools.count(1, False)), repr(itertools.count(True)))
c = itertools.count(False, 2)
print(next(c), repr(c))
```
CPython 3.14t:
```
count(False, 2) count(1, False) count(1)
False count(2, 2)
```
Lucifer:
```
count(0, 2) count(1, 0) count(1)
0 count(2, 2)
```

```python
import itertools, sys
class R(int):
    def __add__(self, other):
        raise ValueError('add')
    __radd__ = __add__
c = itertools.count(sys.maxsize - 1, R(1))
for _ in range(4):
    try:
        print(next(c))
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
9223372036854775806
ValueError add
ValueError add
ValueError add
```
Lucifer:
```
9223372036854775806
9223372036854775807
9223372036854775808
9223372036854775809
```


