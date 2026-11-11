---
id: luc-qyk8
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# collections._count_elements unhashable key message differs

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff fn count, 8 cases over seeds 1 and 2 at 1,000 cases each.

```python
import collections
try:
    collections._count_elements({}, [[]])
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError unhashable type: 'list'
```
Lucifer:
```
TypeError cannot use 'list' as a dict key (unhashable type: 'list')
```


