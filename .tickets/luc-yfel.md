---
id: luc-yfel
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# IOBase.readline/readlines: result and hint argument checks differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pyio_base_diff, about 130 operations over seeds 1 and 2 at 1,000 cases each.

```python
import io
class S(io.IOBase):
    def readable(self): return True
    def read(self, n=-1): return self.r
s = S()
for r in (1, None):
    s.r = r
    try: print(repr(r), s.readline())
    except Exception as e: print(repr(r), type(e).__name__, e)
s.r = b"a"
for n in (b"x", 1.5, "s"):
    try: print(io.IOBase.readline(s, n))
    except Exception as e: print(repr(n), type(e).__name__, e)
for h in (0.5, 1.5, float("nan"), float("inf")):
    try: print(h, io.BytesIO(b"a\nb\n").readlines(h))
    except Exception as e: print(h, type(e).__name__, e)
```
CPython 3.14t:
```
1 OSError read() should have returned a bytes object, not 'int'
None OSError read() should have returned a bytes object, not 'NoneType'
b'x' TypeError argument should be integer or None, not 'bytes'
1.5 TypeError argument should be integer or None, not 'float'
's' TypeError argument should be integer or None, not 'str'
0.5 TypeError integer argument expected, got 'float'
1.5 TypeError integer argument expected, got 'float'
nan TypeError integer argument expected, got 'float'
inf TypeError integer argument expected, got 'float'
```
Lucifer:
```
1 TypeError can't concat int to bytearray
None b''
b'x' TypeError b'x' is not an integer
1.5 TypeError 1.5 is not an integer
's' TypeError 's' is not an integer
0.5 [b'a\n']
1.5 [b'a\n']
nan [b'a\n', b'b\n']
inf [b'a\n', b'b\n']
```


