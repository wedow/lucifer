---
id: luc-86qb
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# set probing for keys with hash -1/-2 calls __eq__ fewer times than CPython

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_sethooks_diff, about 60 cases over seeds 1 and 2 at 1,000 cases each.

```python
class K:
    def __init__(self, n):
        self.n = n
    def __hash__(self):
        return -2
    def __eq__(self, o):
        calls.append((self.n, o.n))
        return False
calls = []
s = {K(1)}
s.add(K(2))
print(len(calls))
calls.clear()
d = {K(1): 0}
d[K(2)] = 0
print(len(calls))
```
CPython 3.14t:
```
13
13
```
Lucifer:
```
1
13
```

The dict probe matches; only sets differ.


