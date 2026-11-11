---
id: luc-omz5
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# dict.copy() of a subclass ignores an overridden keys/__iter__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff op copy_ks, about 35 cases over seeds 1 and 2 at 1,000 cases each.

```python
class KS(dict):
    def __iter__(self):
        return iter(list(dict.keys(self))[::-1])
    def keys(self):
        return list(dict.keys(self))[::-1]
d = KS({1: 'a', 2: 'b', 3: 'c'})
print(list(d.copy()), list(dict(d)), list({}.__class__(d)))
```
CPython 3.14t:
```
[3, 2, 1] [3, 2, 1] [3, 2, 1]
```
Lucifer:
```
[1, 2, 3] [3, 2, 1] [3, 2, 1]
```


