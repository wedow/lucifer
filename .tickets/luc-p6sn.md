---
id: luc-p6sn
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO.truncate(-n) says 'negative truncate position' instead of 'negative size value'

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_diff fn bytesio_c, 154 cases over seeds 1 and 2 at 1,000 cases each.

```python
import io
try:
    io.BytesIO().truncate(-5)
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError negative size value -5
```
Lucifer:
```
ValueError negative truncate position -5
```

lib/modules/_io.py copies the _pyio text; C _io.BytesIO says "negative size value".


