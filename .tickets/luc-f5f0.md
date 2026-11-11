---
id: luc-f5f0
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO.seek/truncate: _pyio messages and no C-int/ssize_t overflow checks

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_share_diff, pyio_state_diff, pyio_callback_diff, pyio_base_diff, about 600 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
f = io.BytesIO(b"abc")
for a in [(0, 3), (0, -1), (0, 2**31), (-1, 0), (-2, 0)]:
    try: f.seek(*a)
    except Exception as e: print("seek", a, type(e).__name__, e)
for n in (-1, -2, 2**63):
    try: print("truncate", n, f.truncate(n))
    except Exception as e: print("truncate", n, type(e).__name__, e)
```
CPython 3.14t:
```
seek (0, 3) ValueError invalid whence (3, should be 0, 1 or 2)
seek (0, -1) ValueError invalid whence (-1, should be 0, 1 or 2)
seek (0, 2147483648) OverflowError Python int too large to convert to C int
seek (-1, 0) ValueError negative seek value -1
seek (-2, 0) ValueError negative seek value -2
truncate -1 ValueError negative size value -1
truncate -2 ValueError negative size value -2
truncate 9223372036854775808 OverflowError Python int too large to convert to C long
```
Lucifer:
```
seek (0, 3) ValueError unsupported whence value
seek (0, -1) ValueError unsupported whence value
seek (0, 2147483648) ValueError unsupported whence value
seek (-1, 0) ValueError negative seek position -1
seek (-2, 0) ValueError negative seek position -2
truncate -1 ValueError negative truncate position -1
truncate -2 ValueError negative truncate position -2
truncate 9223372036854775808 9223372036854775808
```

Companion of the BytesIO.truncate message ticket filed from pyio_diff.


