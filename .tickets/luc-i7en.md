---
id: luc-i7en
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Assigning a non-tuple to BaseException.args does not fall back to iteration

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 60 cases over seeds 1 and 2 at 1,000 cases each.

```python
class Sq:
    def __getitem__(self, i):
        print("getitem", i)
        if i >= 2: raise IndexError
        return 'n'
e = ValueError()
for v in [Sq(), {'k': 1}, 5, 'ab']:
    try: e.args = v; print(e.args)
    except Exception as x: print(type(x).__name__, x)
```
CPython 3.14t:
```
getitem 0
getitem 1
getitem 2
('n', 'n')
('k',)
TypeError 'int' object is not iterable
('a', 'b')
```
Lucifer:
```
TypeError 'Sq' object is not iterable
TypeError 'dict' object is not iterable
TypeError 'int' object is not iterable
TypeError 'str' object is not iterable
```


