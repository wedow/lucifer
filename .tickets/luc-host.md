---
id: luc-host
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO.__init__, close and __setstate__ ignore existing buffer exports

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_callback_diff, pyio_state_diff, pyio_share_diff, about 450 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
f = io.BytesIO(b"abc")
v = f.getbuffer()
for name, fn in [("write", lambda: f.write(b"z" * 100)), ("truncate", lambda: f.truncate(1)),
                 ("init", lambda: f.__init__(b"q")), ("setstate", lambda: f.__setstate__((b"x", 0, None))),
                 ("close", lambda: f.close())]:
    try: print(name, fn())
    except Exception as e: print(name, type(e).__name__, e)
print(f.closed)
```
CPython 3.14t:
```
write BufferError Existing exports of data: object cannot be re-sized
truncate BufferError Existing exports of data: object cannot be re-sized
init BufferError Existing exports of data: object cannot be re-sized
setstate BufferError Existing exports of data: object cannot be re-sized
close BufferError Existing exports of data: object cannot be re-sized
False
Exception ignored while finalizing file <_io.BytesIO object at 0x...>:
BufferError: Existing exports of data: object cannot be re-sized
```
Lucifer:
```
write BufferError Existing exports of data: object cannot be re-sized
truncate BufferError Existing exports of data: object cannot be re-sized
init None
setstate AttributeError 'BytesIO' object has no attribute '__setstate__'
close None
True
```

CPython raises BufferError "Existing exports of data: object cannot be re-sized".


