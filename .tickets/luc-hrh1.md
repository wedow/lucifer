---
id: luc-hrh1
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# deque iterator type does not accept index= and skips its overflow check

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff, 47 cases over seeds 1 and 2 at 1,000 cases each.

```python
from collections import deque
d = deque([0, 3])
for t in (type(iter(d)), type(reversed(d))):
    for args, kw in (((d,), {"index": 1}), ((d, 1), {"index": 1})):
        try:
            print(t.__name__, list(t(*args, **kw)))
        except Exception as e:
            print(t.__name__, type(e).__name__, e)
```
CPython 3.14t:
```
_deque_iterator [0, 3]
_deque_iterator [3]
_deque_reverse_iterator [3, 0]
_deque_reverse_iterator [0]
```
Lucifer:
```
_deque_iterator TypeError _DequeIterator.__init__() got some positional-only arguments passed as keyword arguments: 'index'
_deque_iterator TypeError _DequeIterator.__init__() got some positional-only arguments passed as keyword arguments: 'index'
_deque_reverse_iterator TypeError _DequeIterator.__init__() got some positional-only arguments passed as keyword arguments: 'index'
_deque_reverse_iterator TypeError _DequeIterator.__init__() got some positional-only arguments passed as keyword arguments: 'index'
```


