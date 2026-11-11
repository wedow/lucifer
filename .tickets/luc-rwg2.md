---
id: luc-rwg2
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# struct: complex formats F and D are not supported

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): textlibs_diff, about 6 cases over seeds 1 and 2 at 1,000 cases each.

```python
import struct
for f in ("@3F", "=3D0b2H", ">0xF", "<D"):
    try: print(f, struct.calcsize(f))
    except Exception as e: print(f, type(e).__name__, e)
```
CPython 3.14t:
```
@3F 24
=3D0b2H 52
>0xF 8
<D 16
```
Lucifer:
```
@3F error bad char in struct format
=3D0b2H error bad char in struct format
>0xF error bad char in struct format
<D error bad char in struct format
```

A 3.14 feature.


