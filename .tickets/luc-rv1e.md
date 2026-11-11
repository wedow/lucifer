---
id: luc-rv1e
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# dict probing when a key's __eq__ mutates the dict differs from CPython

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff ops set, popd, sdef, del, ior, in, about 25 cases over seeds 1 and 2 at 1,000 cases each; one case hung Lucifer past 300 s.

```python
class K:
    def __init__(self, n, evil=False):
        self.n, self.evil = n, evil
    def __hash__(self):
        return 1
    def __eq__(self, o):
        if not self.evil:
            return NotImplemented
        calls.append((self.n, o.n))
        d.popitem()
        return False
calls = []
d = {K(1): 0, K(2): 0, K(3): 0}
d[K(9, True)] = 1
print(calls, [k.n for k in d])
```
CPython 3.14t:
```
[(9, 1), (9, 2), (9, 1)] [9]
```
Lucifer:
```
[(9, 1), (9, 1), (9, 1)] [9]
```


