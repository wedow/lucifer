---
id: luc-sj40
status: open
deps: []
links: []
created: 2026-11-11T03:34:57Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Buffered seek(): offsets beyond off_t pass, whence messages differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_buffered_diff, about 25 cases over seeds 1 and 2 at 1,000 cases each.

```python
import io
r = io.BytesIO(b"abc")
b = io.BufferedReader(r)
for args in ((2**63,), (-2**63 - 1,), (0, 3), (0, -1)):
    try:
        print(args, b.seek(*args))
    except Exception as e:
        print(args, type(e).__name__, e)
```
CPython 3.14t:
```
(9223372036854775808,) ValueError cannot fit 'int' into an offset-sized integer
(-9223372036854775809,) ValueError cannot fit 'int' into an offset-sized integer
(0, 3) ValueError invalid whence (3, should be 0, 1 or 2)
(0, -1) ValueError whence value -1 unsupported
```
Lucifer:
```
(9223372036854775808,) 9223372036854775808
(-9223372036854775809,) ValueError negative seek position -9223372036854775809
(0, 3) ValueError unsupported whence value
(0, -1) ValueError invalid whence value
```


