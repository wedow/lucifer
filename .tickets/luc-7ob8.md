---
id: luc-7ob8
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Setting an attribute on an object without __dict__ omits 'and no __dict__ for setting new attributes'

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 35 cases over seeds 1 and 2 at 1,000 cases each.

```python
def t(f):
    try: f()
    except Exception as e: print(type(e).__name__, e)
def s(o, n='a'):
    setattr(o, n, 1)
for o in [1, None, [], (), 'x', object()]:
    t(lambda: s(o))
class S:
    __slots__ = ()
t(lambda: s(S()))
t(lambda: delattr(1, 'a'))
```
CPython 3.14t:
```
AttributeError 'int' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'NoneType' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'list' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'tuple' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'str' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'object' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'S' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'int' object has no attribute 'a' and no __dict__ for setting new attributes
```
Lucifer:
```
AttributeError 'int' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'NoneType' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'list' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'tuple' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'str' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'object' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'S' object has no attribute 'a' and no __dict__ for setting new attributes
AttributeError 'int' object has no attribute 'a'
```


