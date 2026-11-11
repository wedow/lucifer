---
id: luc-1iof
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array() and extend consume iterators and validate typecodes in another order

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff fns array_new, extend_raise and others, about 94 cases over seeds 1 and 2 at 1,000 cases each.

```python
from array import array
def t(label, f):
    try:
        print(label, f())
    except Exception as e:
        print(label, type(e).__name__, e)
t("bad init, valid tc", lambda: array('H', 5))
t("bad init, bad tc", lambda: array('Z', 5))
def gen():
    print("pulled 1"); yield -2**64
    print("pulled 2"); yield 2
t("overflow while iterating", lambda: array('q', gen()))
```
CPython 3.14t:
```
bad init, valid tc TypeError 'int' object is not iterable
bad init, bad tc TypeError 'int' object is not iterable
pulled 1
overflow while iterating OverflowError int too big to convert
```
Lucifer:
```
bad init, valid tc TypeError can only extend with iterable
bad init, bad tc ValueError bad typecode (must be b, B, u, w, h, H, i, I, l, L, q, Q, f or d)
pulled 1
pulled 2
overflow while iterating OverflowError int too big to convert
```


