---
id: luc-iqi8
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array.tofile/fromfile: block size, empty writes and short reads differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff, about 14 cases over seeds 1 and 2 at 1,000 cases each.

```python
from array import array
class F:
    def __init__(self): self.sizes = []
    def write(self, b): self.sizes.append(len(b))
for n in (8193, 20000):
    f = F()
    array('d', bytes(8 * n)).tofile(f)
    print(n, f.sizes)
```
CPython 3.14t:
```
8193 [65536, 8]
20000 [65536, 65536, 28928]
```
Lucifer:
```
8193 [65544]
20000 [160000]
```

```python
from array import array
class F:
    def read(self, n): return bytes(7)
a = array('H')
try:
    a.fromfile(F(), 4)
except Exception as e:
    print(type(e).__name__, e)
print(len(a))
class W:
    def write(self, b): print("write", len(b))
array('i').tofile(W())
```
CPython 3.14t:
```
ValueError bytes length not a multiple of item size
0
```
Lucifer:
```
EOFError read() didn't return enough bytes
3
write 0
```


