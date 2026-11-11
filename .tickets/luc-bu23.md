---
id: luc-bu23
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# deque: concatenation, __getstate__ override, imul/index overflow checks differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff fn deque, about 45 cases over seeds 1 and 2 at 1,000 cases each.

```python
from collections import deque
def t(label, f):
    try:
        print(label, f())
    except Exception as e:
        print(label, type(e).__name__, e)
class H:
    @property
    def __class__(self):
        raise AttributeError("a")
t("add object whose __class__ raises", lambda: deque([1]) + H())
class D(deque): pass
def gs():
    raise ValueError("gs")
def red():
    d = D([1]); d.__getstate__ = gs; return d.__reduce__()
t("__reduce__ uses instance __getstate__", red)
t("mul 2**64", lambda: deque([1]) * 2**64)
t("imul 2**64", lambda: deque([1], 65).__imul__(2**64))
t("iterator ctor index 2**64", lambda: type(iter(deque([1])))(deque([1]), 2**64))
def setitem():
    d = deque([1]); d[-2**63 - 1] = 3
t("d[-2**63-1] = 3", setitem)
t("d[2**64]", lambda: deque([1])[2**64])
```
CPython 3.14t:
```
add object whose __class__ raises TypeError can only concatenate deque (not "H") to deque
__reduce__ uses instance __getstate__ ValueError gs
mul 2**64 OverflowError cannot fit 'int' into an index-sized integer
imul 2**64 OverflowError cannot fit 'int' into an index-sized integer
iterator ctor index 2**64 OverflowError Python int too large to convert to C ssize_t
d[-2**63-1] = 3 IndexError cannot fit 'int' into an index-sized integer
d[2**64] IndexError cannot fit 'int' into an index-sized integer
```
Lucifer:
```
add object whose __class__ raises AttributeError a
__reduce__ uses instance __getstate__ (<class '__main__.D'>, (), {'__getstate__': <function gs at 0x...>}, <collections._deque_iterator object at 0x...>)
mul 2**64 OverflowError cannot fit 'int' into an index-sized integer
imul 2**64 deque([1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1], maxlen=65)
iterator ctor index 2**64 <collections._deque_iterator object at 0x...>
d[-2**63-1] = 3 OverflowError cannot fit 'int' into an index-sized integer
d[2**64] IndexError cannot fit 'int' into an index-sized integer
```


