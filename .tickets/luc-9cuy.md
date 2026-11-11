---
id: luc-9cuy
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# SystemError from bad C-API arguments lacks CPython's 'file:line:' prefix

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff, 5 cases over seeds 1 and 2 at 1,000 cases each (the generator parses the prefix).

```python
import _testlimitedcapi as tl
try:
    tl.dict_setitem([], 1, 2)
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
SystemError Objects/dictobject.c:2722: bad argument to internal function
```
Lucifer:
```
SystemError bad argument to internal function
exit 0
```

An implementation detail; low priority.


