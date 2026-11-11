---
id: luc-9onn
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO pickle state: __getstate__ returns the instance dict, no __setstate__, internals visible in __dict__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_state_diff (about 1,900 cases), pyio_base_diff (about 40) over seeds 1 and 2 at 1,000 cases each.

```python
import io
f = io.BytesIO(b"ab")
f.seek(1)
try: print(f.__getstate__())
except Exception as e: print(type(e).__name__, e)
try: print(f.__setstate__((b"xyz", 2, None)), f.getvalue(), f.tell())
except Exception as e: print(type(e).__name__, e)
try: f.__setstate__([b"", 0, None])
except Exception as e: print(type(e).__name__, e)
try: f.__setstate__((b"", -1, None))
except Exception as e: print(type(e).__name__, e)
g = io.BytesIO(b"q")
print(g.__dict__)
g.a = 1
print(g.__dict__)
```
CPython 3.14t:
```
(b'ab', 1, None)
None b'xyz' 2
TypeError _io.BytesIO.__setstate__ argument should be 3-tuple, got list
ValueError position value cannot be negative
{}
{'a': 1}
```
Lucifer:
```
{'_buffer': bytearray(b'ab'), '_pos': 1}
AttributeError 'BytesIO' object has no attribute '__setstate__'
AttributeError 'BytesIO' object has no attribute '__setstate__'
AttributeError 'BytesIO' object has no attribute '__setstate__'
{'_buffer': bytearray(b'q'), '_pos': 0}
{'_buffer': bytearray(b'q'), '_pos': 0, 'a': 1}
```

C _io.BytesIO state is (value, pos, dict) with a validated __setstate__.


