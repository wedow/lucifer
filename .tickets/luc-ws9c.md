---
id: luc-ws9c
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array: empty buffer_info() address is nonzero

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff, about 118 cases over seeds 1 and 2 at 1,000 cases each.

```python
from array import array
print(array('q').buffer_info()[0] != 0)
```
CPython 3.14t:
```
False
```
Lucifer:
```
True
```

An implementation detail; capacity differences in the same cases come from __sizeof__.


