---
id: luc-qhs1
status: open
deps: []
links: []
created: 2026-11-11T02:47:16Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# json.dumps hits RecursionError at nesting depth 521 where CPython reaches tens of thousands

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): json_diff fn nestx_c (56 cases) over seeds 1 and 2 at 1,000 cases each; the generator also calibrates its cases from these limits, so on Lucifer its calibration raises and about 190 later cases differ only in a counter.

```python
import json
def deepest(build):
    lo, hi = 1, 100000
    while lo < hi:
        mid = (lo + hi + 1) // 2
        try: json.dumps(build(mid)); lo = mid
        except RecursionError: hi = mid - 1
    return lo
def lst(n):
    o = []
    for _ in range(n - 1): o = [o]
    return o
def dct(n):
    o = {}
    for _ in range(n - 1): o = {"a": o}
    return o
print("list", deepest(lst), "dict", deepest(dct))
```
CPython 3.14t:
```
list 47438 dict 28990
```
Lucifer:
```
list 521 dict 521
exit 0
```


