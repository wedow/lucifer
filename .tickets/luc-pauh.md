---
id: luc-pauh
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# complex(): TypeError names the bare class instead of module.qualname; no DeprecationWarning for a __complex__ returning a complex subclass

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): longobj_complex_diff fns cx_new (35 cases) and cx_from_number (1 case) over seeds 1 and 2 at 1,000 cases each.

```python
import warnings
class C3: __module__ = "m"
class C1(complex): __module__ = "m"
class C0(int):
    def __complex__(self): return C1(1, 2)
for a in (C3(), None):
    try:
        complex(a)
    except Exception as e:
        print(type(e).__name__, e)
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter("always")
    r = complex.from_number(C0(3))
    print(type(r).__name__, r, [str(x.message) for x in w])
    r = complex(C0(3))
    print(type(r).__name__, r, [str(x.message) for x in w])
```
CPython 3.14t:
```
TypeError complex() argument must be a string or a number, not m.C3
TypeError complex() argument must be a string or a number, not NoneType
complex (1+2j) ['__complex__ returned non-complex (type C1).  The ability to return an instance of a strict subclass of complex is deprecated, and may be removed in a future version of Python.']
complex (1+2j) ['__complex__ returned non-complex (type C1).  The ability to return an instance of a strict subclass of complex is deprecated, and may be removed in a future version of Python.', '__complex__ returned non-complex (type C1).  The ability to return an instance of a strict subclass of complex is deprecated, and may be removed in a future version of Python.']
```
Lucifer:
```
TypeError complex() argument must be a string or a number, not C3
TypeError complex() argument must be a string or a number, not NoneType
complex (1+2j) []
complex (1+2j) []
```


