---
id: luc-ryb7
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# ctypes: a function returning a struct of two doubles by value yields zeros or garbage

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): longobj_complex_diff fn cx_as_c, 38 cases over seeds 1 and 2 at 1,000 cases each (PyComplex_AsCComplex through ctypes.pythonapi).

```python
import ctypes
class CC(ctypes.Structure):
    _fields_ = [("real", ctypes.c_double), ("imag", ctypes.c_double)]
f = ctypes.pythonapi.PyComplex_AsCComplex
f.restype, f.argtypes = CC, [ctypes.py_object]
for o in (1.5, -1, 3+4j, 10**20):
    r = f(o)
    print(r.real, r.imag)
```
CPython 3.14t:
```
1.5 0.0
-1.0 0.0
3.0 4.0
1e+20 0.0
```
Lucifer:
```
0.0 0.0
0.0 0.0
0.0 0.0
2025163840.0 0.0
```

Either ctypes struct-by-value returns (SysV: xmm0:xmm1) or the PyComplex_AsCComplex export; not isolated.


