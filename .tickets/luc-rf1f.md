---
id: luc-rf1f
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# 'x in obj' and operator.countOf/indexOf on a non-iterable say "'int' object is not iterable"

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_abstract_diff, about 230 cases over seeds 1 and 2 at 1,000 cases each.

```python
import operator
class NI:
    __iter__ = None
class Bad:
    def __iter__(self): return self
class NoNext:
    def __iter__(self): return 5
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
for o in [3, None, NI(), Bad(), NoNext()]:
    t(operator.countOf, o, 1)
    t(operator.indexOf, o, 1)
t(lambda: 1 in NI())
t(lambda: 1 in Bad())
t(lambda: 1 in 3)
t(iter, NI())
```
CPython 3.14t:
```
TypeError argument of type 'int' is not iterable
TypeError argument of type 'int' is not iterable
TypeError argument of type 'NoneType' is not iterable
TypeError argument of type 'NoneType' is not iterable
TypeError argument of type 'NI' is not iterable
TypeError argument of type 'NI' is not iterable
TypeError argument of type 'Bad' is not iterable
TypeError argument of type 'Bad' is not iterable
TypeError argument of type 'NoNext' is not iterable
TypeError argument of type 'NoNext' is not iterable
TypeError argument of type 'NI' is not a container or iterable
TypeError argument of type 'Bad' is not a container or iterable
TypeError argument of type 'int' is not a container or iterable
TypeError 'NI' object is not iterable
```
Lucifer:
```
TypeError 'int' object is not iterable
TypeError 'int' object is not iterable
TypeError 'NoneType' object is not iterable
TypeError 'NoneType' object is not iterable
TypeError 'NI' object is not iterable
TypeError 'NI' object is not iterable
TypeError iter() returned non-iterator of type 'Bad'
TypeError iter() returned non-iterator of type 'Bad'
TypeError iter() returned non-iterator of type 'int'
TypeError iter() returned non-iterator of type 'int'
TypeError argument of type 'NI' is not a container or iterable
TypeError argument of type 'Bad' is not a container or iterable
TypeError argument of type 'int' is not a container or iterable
TypeError 'NI' object is not iterable
```

CPython: "argument of type 'int' is not iterable".


