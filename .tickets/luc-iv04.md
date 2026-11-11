---
id: luc-iv04
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# defaultdict.__init__ with too many arguments leaves default_factory unset

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff fn defdict, 12 cases over seeds 1 and 2 at 1,000 cases each.

```python
from collections import defaultdict
d = defaultdict()
try:
    d.__init__(list, {}, {})
except TypeError as e:
    print(type(e).__name__, e)
print(d.default_factory)
```
CPython 3.14t:
```
TypeError dict expected at most 1 argument, got 2
<class 'list'>
```
Lucifer:
```
TypeError dict expected at most 1 argument, got 2
None
```


