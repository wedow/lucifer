---
id: luc-sd3n
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# TextIOWrapper._CHUNK_SIZE is a plain attribute without validation

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_text_diff, about 145 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
t = io.TextIOWrapper(io.BytesIO())
for v in (0, -1, 2**64):
    try: t._CHUNK_SIZE = v; print(v, "ok", t._CHUNK_SIZE)
    except Exception as e: print(v, type(e).__name__, e)
try: del t._CHUNK_SIZE
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
0 ValueError a strictly positive integer is required
-1 ValueError a strictly positive integer is required
18446744073709551616 ValueError cannot fit 'int' into an index-sized integer
AttributeError attribute '_CHUNK_SIZE' of '_io.TextIOWrapper' objects cannot be deleted
```
Lucifer:
```
0 ok 0
-1 ok -1
18446744073709551616 ok 18446744073709551616
```


