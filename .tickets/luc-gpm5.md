---
id: luc-gpm5
status: open
deps: []
links: []
created: 2026-11-11T03:34:57Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Buffered I/O construction does not call raw.tell()

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_buffered_diff, about 330 cases over seeds 1 and 2 at 1,000 cases each (every raw-call log differs).

```python
# C _buffered_init calls raw.tell() once at construction
import io
log = []
class Raw(io.RawIOBase):
    def readable(self): log.append("readable"); return True
    def writable(self): return True
    def seekable(self): return True
    def tell(self): log.append("tell"); return 0
    def readinto(self, b): return 0
for cls in (io.BufferedReader, io.BufferedWriter, io.BufferedRandom):
    del log[:]
    cls(Raw(), 4); print(cls.__name__, log)
```
CPython 3.14t:
```
Exception ignored while finalizing file <_io.BufferedRandom>:
Traceback (most recent call last):
  File "build/repro/late/io_init_calls_tell.py", line 12, in <module>
    cls(Raw(), 4); print(cls.__name__, log)
io.UnsupportedOperation: seek
BufferedReader ['readable', 'tell']
BufferedWriter ['tell']
BufferedRandom ['readable', 'tell']
```
Lucifer:
```
BufferedReader ['readable']
BufferedWriter []
BufferedRandom ['readable']
```


