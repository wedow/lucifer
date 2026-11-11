---
id: luc-jfmv
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# type.__new__ argument checks differ; type.__new__(type, 1) returns int

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 52 cases over seeds 1 and 2 at 1,000 cases each.

```python
def t(f, *a, **k):
    try: print(f(*a, **k))
    except Exception as e: print(type(e).__name__, e)
t(type.__new__, type)
t(type.__new__, type, 1)
t(type.__new__, type, 1, 2)
t(type, 1, 2)
t(type)
t(type, 1, 2, 3, 4)
t(type.__call__)
class M(type): pass
t(M.__new__, M)
t(M, 'a')
```
CPython 3.14t:
```
TypeError type.__new__() takes exactly 3 arguments (0 given)
TypeError type.__new__() takes exactly 3 arguments (1 given)
TypeError type.__new__() takes exactly 3 arguments (2 given)
TypeError type() takes 1 or 3 arguments
TypeError type() takes 1 or 3 arguments
TypeError type() takes 1 or 3 arguments
TypeError descriptor '__call__' of 'type' object needs an argument
TypeError type.__new__() takes exactly 3 arguments (0 given)
TypeError type.__new__() takes exactly 3 arguments (1 given)
```
Lucifer:
```
TypeError type() takes 1 or 3 arguments
<class 'int'>
TypeError type() takes 1 or 3 arguments
TypeError type() takes 1 or 3 arguments
TypeError type() takes 1 or 3 arguments
TypeError type() takes 1 or 3 arguments
TypeError descriptor '__call__' of 'type' object needs an argument
TypeError type() takes 1 or 3 arguments
TypeError type() takes 1 or 3 arguments
```


