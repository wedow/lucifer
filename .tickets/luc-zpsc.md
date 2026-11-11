---
id: luc-zpsc
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO.write and __init__ of a non-buffer: 'memoryview:' prefix and "can't concat" messages

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_callback_diff, pyio_base_diff, about 200 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
f = io.BytesIO()
for x in (None, 1, 1.5, [1], "s"):
    try: f.write(x)
    except Exception as e: print(type(e).__name__, e)
class B:
    def __buffer__(self, flags): raise ValueError("boom")
try: f.write(B())
except Exception as e: print(type(e).__name__, e)
try: f.__init__(B())
except Exception as e: print(type(e).__name__, e)
try: f.__init__(1)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError a bytes-like object is required, not 'NoneType'
TypeError a bytes-like object is required, not 'int'
TypeError a bytes-like object is required, not 'float'
TypeError a bytes-like object is required, not 'list'
TypeError a bytes-like object is required, not 'str'
ValueError boom
ValueError boom
TypeError a bytes-like object is required, not 'int'
```
Lucifer:
```
TypeError memoryview: a bytes-like object is required, not 'NoneType'
TypeError memoryview: a bytes-like object is required, not 'int'
TypeError memoryview: a bytes-like object is required, not 'float'
TypeError memoryview: a bytes-like object is required, not 'list'
TypeError a bytes-like object is required, not 'str'
ValueError boom
ValueError boom
TypeError can't concat int to bytearray
```


