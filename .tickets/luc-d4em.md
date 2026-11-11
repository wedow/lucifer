---
id: luc-d4em
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# StringIO.seek(pos, whence) does not raise OverflowError for a whence outside C int

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_diff fn stringio_c, 84 cases over seeds 1 and 2 at 1,000 cases each.

```python
import io
s = io.StringIO("abc")
for w in (2**31, 2**31 - 1, -2**31 - 1, 2**40, 5):
    try:
        s.seek(0, w)
    except Exception as e:
        print(w, type(e).__name__, e)
```
CPython 3.14t:
```
2147483648 OverflowError Python int too large to convert to C int
2147483647 ValueError Invalid whence (2147483647, should be 0, 1 or 2)
-2147483649 OverflowError Python int too large to convert to C int
1099511627776 OverflowError Python int too large to convert to C int
5 ValueError Invalid whence (5, should be 0, 1 or 2)
```
Lucifer:
```
2147483648 ValueError Invalid whence (2147483648, should be 0, 1 or 2)
2147483647 ValueError Invalid whence (2147483647, should be 0, 1 or 2)
-2147483649 ValueError Invalid whence (-2147483649, should be 0, 1 or 2)
1099511627776 ValueError Invalid whence (1099511627776, should be 0, 1 or 2)
5 ValueError Invalid whence (5, should be 0, 1 or 2)
```

C _io converts whence with the "i" format before testing its value.


