---
id: luc-d8pq
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# BytesIO.getvalue() copies instead of sharing the bytes object

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_share_diff, about 36 cases over seeds 1 and 2 at 1,000 cases each.

```python
import io
f = io.BytesIO(b"abc")
a = f.getvalue(); b = f.getvalue()
print(a is b)
c = io.BytesIO(a).getvalue()
print(c is a)
```
CPython 3.14t:
```
True
True
```
Lucifer:
```
False
False
```

C BytesIO shares its bytes object copy-on-write; observable through identity.


