---
id: luc-twwi
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# TextIOWrapper.seek messages and whence overflow differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_text_diff, about 30 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
t = io.TextIOWrapper(io.BytesIO(b"abc"))
for a in [(1, 1), (1, 2), (0, 3), (0, 2**31), (0, -2**31 - 1)]:
    try: t.seek(*a)
    except Exception as e: print(a, type(e).__name__, e)
```
CPython 3.14t:
```
(1, 1) UnsupportedOperation can't do nonzero cur-relative seeks
(1, 2) UnsupportedOperation can't do nonzero end-relative seeks
(0, 3) ValueError invalid whence (3, should be 0, 1 or 2)
(0, 2147483648) OverflowError Python int too large to convert to C int
(0, -2147483649) OverflowError Python int too large to convert to C int
```
Lucifer:
```
(1, 1) UnsupportedOperation can't do nonzero cur-relative seeks
(1, 2) UnsupportedOperation can't do nonzero end-relative seeks
(0, 3) ValueError unsupported whence (3)
(0, 2147483648) ValueError unsupported whence (2147483648)
(0, -2147483649) ValueError unsupported whence (-2147483649)
```


