---
id: luc-41e4
status: open
deps: []
links: []
created: 2026-11-11T03:34:57Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Buffered I/O messages: buffer size, read/flush on a closed raw

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_buffered_diff, about 25 cases over seeds 1 and 2 at 1,000 cases each.

```python
import io
for size in (0, -1):
    try:
        io.BufferedReader(io.BytesIO(), size)
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError buffer size must be strictly positive
ValueError buffer size must be strictly positive
```
Lucifer:
```
ValueError invalid buffer size
ValueError invalid buffer size
```

```python
# readline on a buffered object whose raw.closed is True
import io
class Raw(io.RawIOBase):
    closed = True
    def readable(self): return True
    def readinto(self, b): return 0
b = io.BufferedReader(Raw())
for op in (lambda: b.readline(), lambda: b.read(1), lambda: b.peek(1), lambda: b.flush()):
    try:
        print(op())
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError readline of closed file
ValueError read of closed file
ValueError peek of closed file
None
```
Lucifer:
```
ValueError readline of closed file
b''
ValueError peek of closed file
ValueError flush of closed file
```


