---
id: luc-33vj
status: open
deps: []
links: []
created: 2026-11-11T03:34:57Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BufferedReader.truncate() truncates the raw stream instead of raising UnsupportedOperation

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_buffered_diff, about 8 cases over seeds 1 and 2 at 1,000 cases each.

```python
# BufferedReader has no truncate(); the port inherits the mixin's flush+truncate
import io
class Raw(io.RawIOBase):
    def readable(self): return True
    def writable(self): print("writable called"); return True
    def readinto(self, b): return 0
    def truncate(self, n=None): print("raw truncate", n); return n
b = io.BufferedReader(Raw())
try:
    print(b.truncate(5))
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
UnsupportedOperation truncate
```
Lucifer:
```
writable called
raw truncate 5
5
```


