---
id: luc-hpml
status: closed
deps: []
links: []
created: 2026-11-11T04:01:07Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# x in range(...) with a non-int probe counts every match instead of stopping at the first, and hangs on huge ranges

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): rangeslice_diff fns search, contains, 6 cases over seeds 1 and 2 at 500 cases each (one hung past 300 s).

```python
class TrueAt:
    def __init__(self, m): self.m, self.calls = m, 0
    def __eq__(self, o):
        n = self.calls; self.calls += 1
        return n == self.m
class AlwaysEq(int):
    calls = 0
    def __eq__(self, o):
        self.calls += 1
        return True
    __hash__ = int.__hash__
for r, p in [(range(0, -14, -2), TrueAt(0)), (range(1, 39, 2), AlwaysEq(39))]:
    print(p in r, p.calls)
```
CPython 3.14t:
```
True 1
True 1
```
Lucifer:
```
True 7
True 19
```

```python
class AlwaysEq(int):
    def __eq__(self, o): return True
    __hash__ = int.__hash__
print(AlwaysEq(5) in range(-2**63 - 1, 2**64 + 2, 2))
```
CPython 3.14t:
```
True
```
Lucifer:
```

```

lib/kernel/range.py __contains__ calls _search(self, ob, 'count') > 0; CPython's range_contains stops at the first equal item.


