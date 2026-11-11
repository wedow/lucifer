---
id: luc-ngyp
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# A class defining only __delitem__ (or __setitem__) fails with TypeError, not AttributeError from the shared slot

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 20 cases over seeds 1 and 2 at 1,000 cases each.

```python
class A:
    def __delitem__(self, k): print("del")
class B:
    def __setitem__(self, k, v): print("set")
class C:
    def __getitem__(self, k): return 1
def t(f):
    try: f()
    except Exception as e: print(type(e).__name__, e)
def s(o):
    o[0] = 1
def d(o):
    del o[0]
t(lambda: s(A())); t(lambda: d(B())); t(lambda: s(C())); t(lambda: d(C()))
class A2(A): pass
t(lambda: s(A2()))
```
CPython 3.14t:
```
AttributeError __setitem__
AttributeError __delitem__
TypeError 'C' object does not support item assignment
TypeError 'C' object doesn't support item deletion
AttributeError __setitem__
```
Lucifer:
```
TypeError 'A' object does not support item assignment
TypeError 'B' object doesn't support item deletion
TypeError 'C' object does not support item assignment
TypeError 'C' object doesn't support item deletion
TypeError 'A2' object does not support item assignment
```


