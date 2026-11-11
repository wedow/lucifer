---
id: luc-gb0z
status: open
deps: []
links: []
created: 2026-11-11T03:34:57Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# A half-initialized buffered object raises AttributeError instead of 'I/O operation on uninitialized object'

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_buffered_diff, about 15 cases over seeds 1 and 2 at 1,000 cases each.

```python
# raw.readable() re-enters the half-built buffered object: C says "uninitialized object"
import io
holder = []
class Raw(io.RawIOBase):
    def readable(self):
        try:
            holder[0].read(1)
        except Exception as e:
            print("re", type(e).__name__, e)
        return True
    def writable(self): return True
    def seekable(self): return True
    def readinto(self, b): return 0
b = io.BufferedRandom.__new__(io.BufferedRandom)
holder.append(b)
b.__init__(Raw())
```
CPython 3.14t:
```
re ValueError I/O operation on uninitialized object
Exception ignored while finalizing file <_io.BufferedRandom>:
io.UnsupportedOperation: seek
```
Lucifer:
```
re AttributeError 'BufferedRandom' object has no attribute '_write_lock'
```


