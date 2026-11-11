---
id: luc-s8gs
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Bound-method equality compares receivers with == instead of identity

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 28 cases over seeds 1 and 2 at 1,000 cases each.

```python
import types
class A:
    def m(self): pass
    def __eq__(self, o): print("A.__eq__"); return True
    __hash__ = object.__hash__
a, b = A(), A()
print(a.m == a.m)
print(a.m == b.m)
print(types.MethodType(A.m, a) == types.MethodType(A.m, b))
print(a.__str__ == a.__str__)
print([].append == [].append)
x = []; print(x.append == x.append)
```
CPython 3.14t:
```
True
False
False
True
False
True
```
Lucifer:
```
True
False
False
False
False
False
```


