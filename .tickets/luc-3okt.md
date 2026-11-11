---
id: luc-3okt
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# itertools.groupby with a manually built _grouper differs

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): itertools_diff, 12 cases over seeds 1 and 2 at 1,000 cases each.

```python
import itertools
g = itertools.groupby([1, 0, 0, 0], lambda x: x)
k, grp = next(g)
g2 = itertools._grouper(g, 1)
print(k, next(g2, None), next(g2, None), next(g2, None))
print(next(grp, None), next(g)[0])
```
CPython 3.14t:
```
1 1 None None
None 0
```
Lucifer:
```
1 None None None
1 0
```


