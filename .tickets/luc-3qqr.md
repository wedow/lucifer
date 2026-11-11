---
id: luc-3qqr
status: open
deps: []
links: []
created: 2026-11-11T03:34:56Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Buffered I/O accepts a raw whose readable()/writable()/seekable() returns a truthy non-True value

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_buffered_diff, about 130 cases over seeds 1 and 2 at 1,000 cases each.

```python
# C buffered classes require raw.readable()/writable()/seekable() to return exactly True
import io
class Raw(io.RawIOBase):
    def readable(self): return 2
    def writable(self): return 1
    def seekable(self): return 2
    def readinto(self, b): return 0
for cls in (io.BufferedReader, io.BufferedWriter, io.BufferedRandom):
    try:
        cls(Raw()); print(cls.__name__, "ok")
    except Exception as e:
        print(cls.__name__, type(e).__name__, e)
```
CPython 3.14t:
```
BufferedReader UnsupportedOperation File or stream is not readable.
BufferedWriter UnsupportedOperation File or stream is not writable.
BufferedRandom UnsupportedOperation File or stream is not seekable.
```
Lucifer:
```
BufferedReader ok
BufferedWriter ok
BufferedRandom ok
```

CPython requires exactly True and raises UnsupportedOperation; where Lucifer rejects, it raises OSError with another message.


