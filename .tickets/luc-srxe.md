---
id: luc-srxe
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# _pyio.StringIO.tell() raises UnicodeDecodeError after readline on a lone surrogate

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_diff fn stringio_py, 1 case over seeds 1 and 2 at 1,000 cases each.

```python
import _pyio
s = _pyio.StringIO("\xe9\xe9\ud800\0\0\0b\r\n€€", "\n")
print(s.getvalue() == "\xe9\xe9\ud800\0\0\0b\r\n€€")
print(s.readline(2) == "\xe9\xe9", s.readline(2) == "\ud800\0")
try:
    print(s.tell())
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
True
True True
8
```
Lucifer:
```
True
True True
UnicodeDecodeError 'utf-8' codec can't decode bytes in position 4-5: unexpected end of data
```


