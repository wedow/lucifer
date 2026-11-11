---
id: luc-9uoy
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array('f') rejects a double beyond float range instead of storing inf

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff, 31 cases plus about 54 where the generator lost sync over seeds 1 and 2 at 1,000 cases each.

```python
from array import array
for v in (1e300, -1e300, 5.47e253):
    try:
        print(array('f', [v]).tolist())
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
[inf]
[-inf]
[inf]
```
Lucifer:
```
OverflowError float too large to pack with f format
OverflowError float too large to pack with f format
OverflowError float too large to pack with f format
```


