---
id: luc-0pbt
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# collections._tuplegetter: repr and index overflow differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff fn tuplegetter, 147 cases over seeds 1 and 2 at 1,000 cases each.

```python
import collections
g = collections._tuplegetter(1, 0)
print(repr(g))
try:
    collections._tuplegetter(2**64, 0)
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
_tuplegetter(1, 0)
OverflowError Python int too large to convert to C ssize_t
```
Lucifer:
```
<collections._tuplegetter object at 0x...>
```


