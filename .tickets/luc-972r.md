---
id: luc-972r
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# json encoder ignores a list subclass's __iter__/__len__; dict.items() non-iterable message differs

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): json_diff fn dumpsx_c, about 12 cases over seeds 1 and 2 at 1,000 cases each.

```python
import json
class L(list):
    def __len__(self): return 1
    def __iter__(self):
        raise ValueError("it")
        yield
class D(dict):
    def __len__(self): return 1
    def items(self): return 5
class L2(list):
    def __len__(self): return 0
    def __iter__(self): return iter([1])
for v in (L(), D(a=1), L2([9])):
    for kw in ({}, {"indent": 1}):
        try: print(type(v).__name__, kw, json.dumps(v, **kw))
        except Exception as e: print(type(v).__name__, kw, type(e).__name__, e)
```
CPython 3.14t:
```
L {} ValueError it
L {'indent': 1} ValueError it
D {} TypeError D.items() returned a non-iterable (type int)
D {'indent': 1} TypeError D.items() returned a non-iterable (type int)
L2 {} [1]
L2 {'indent': 1} [
 1
]
```
Lucifer:
```
L {} []
L {'indent': 1} []
D {} TypeError 'int' object is not iterable
D {'indent': 1} TypeError 'int' object is not iterable
L2 {} [9]
L2 {'indent': 1} [
 9
]
exit 0
```


