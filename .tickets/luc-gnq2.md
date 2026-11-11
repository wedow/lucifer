---
id: luc-gnq2
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO.__init__ on a closed BytesIO leaves it closed

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_callback_diff, about 150 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
f = io.BytesIO(b"ab")
f.close()
f.__init__(b"cd")
print(f.closed)
try: print(f.getvalue(), f.tell())
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
False
b'cd' 0
```
Lucifer:
```
True
ValueError I/O operation on closed file.
```


