---
id: luc-mmi9
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# itertools.pairwise re-entrancy differs

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): itertools_diff fn pairwise_r, 19 cases over seeds 1 and 2 at 1,000 cases each.

```python
import itertools
out = []
class Src:
    n = 0
    def __iter__(self):
        return self
    def __next__(self):
        self.n += 1
        if self.n == 2:
            for _ in range(3):
                try:
                    out.append(next(p))
                except Exception as e:
                    out.append((type(e).__name__, str(e)))
        if self.n > 5:
            raise StopIteration
        return self.n
p = itertools.pairwise(Src())
print(next(p))
print(out)
```
CPython 3.14t:
```
(1, 5)
[(1, 3), (3, 4), (4, 5)]
```
Lucifer:
```
(5, 5)
[(1, 3), (3, 4), (4, 5)]
```


