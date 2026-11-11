---
id: luc-gwm5
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# C-API unsigned int conversions give 'too large' for negative values

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): longobj_diff fns as_ulong, as_size_t, as_ulonglong, void_ptr, 27 cases over seeds 1 and 2 at 1,000 cases each.

```python
import ctypes
A = ctypes.pythonapi
def t(name, res, v):
    f = getattr(A, name); f.restype = res; f.argtypes = [ctypes.py_object]
    try: f(v)
    except Exception as e: print(name, v, type(e).__name__, e)
for name, res in [("PyLong_AsUnsignedLong", ctypes.c_ulong), ("PyLong_AsSize_t", ctypes.c_size_t),
                  ("PyLong_AsUnsignedLongLong", ctypes.c_ulonglong), ("PyLong_AsVoidPtr", ctypes.c_void_p)]:
    for v in (-(1 << 64) - 1, -(1 << 64), -(1 << 100), 1 << 64, 1 << 100):
        t(name, res, v)
```
CPython 3.14t:
```
PyLong_AsUnsignedLong -18446744073709551617 OverflowError can't convert negative value to unsigned int
PyLong_AsUnsignedLong -18446744073709551616 OverflowError can't convert negative value to unsigned int
PyLong_AsUnsignedLong -1267650600228229401496703205376 OverflowError can't convert negative value to unsigned int
PyLong_AsUnsignedLong 18446744073709551616 OverflowError Python int too large to convert to C unsigned long
PyLong_AsUnsignedLong 1267650600228229401496703205376 OverflowError Python int too large to convert to C unsigned long
PyLong_AsSize_t -18446744073709551617 OverflowError can't convert negative value to size_t
PyLong_AsSize_t -18446744073709551616 OverflowError can't convert negative value to size_t
PyLong_AsSize_t -1267650600228229401496703205376 OverflowError can't convert negative value to size_t
PyLong_AsSize_t 18446744073709551616 OverflowError Python int too large to convert to C size_t
PyLong_AsSize_t 1267650600228229401496703205376 OverflowError Python int too large to convert to C size_t
PyLong_AsUnsignedLongLong -18446744073709551617 OverflowError can't convert negative int to unsigned
PyLong_AsUnsignedLongLong -18446744073709551616 OverflowError can't convert negative int to unsigned
PyLong_AsUnsignedLongLong -1267650600228229401496703205376 OverflowError can't convert negative int to unsigned
PyLong_AsUnsignedLongLong 18446744073709551616 OverflowError int too big to convert
... (6 more lines)
```
Lucifer:
```
PyLong_AsUnsignedLong -18446744073709551617 OverflowError Python int too large to convert to C unsigned long
PyLong_AsUnsignedLong -18446744073709551616 OverflowError Python int too large to convert to C unsigned long
PyLong_AsUnsignedLong -1267650600228229401496703205376 OverflowError Python int too large to convert to C unsigned long
PyLong_AsUnsignedLong 18446744073709551616 OverflowError Python int too large to convert to C unsigned long
PyLong_AsUnsignedLong 1267650600228229401496703205376 OverflowError Python int too large to convert to C unsigned long
PyLong_AsSize_t -18446744073709551617 OverflowError Python int too large to convert to C size_t
PyLong_AsSize_t -18446744073709551616 OverflowError Python int too large to convert to C size_t
PyLong_AsSize_t -1267650600228229401496703205376 OverflowError Python int too large to convert to C size_t
PyLong_AsSize_t 18446744073709551616 OverflowError Python int too large to convert to C size_t
PyLong_AsSize_t 1267650600228229401496703205376 OverflowError Python int too large to convert to C size_t
PyLong_AsUnsignedLongLong -18446744073709551617 OverflowError int too big to convert
PyLong_AsUnsignedLongLong -18446744073709551616 OverflowError int too big to convert
PyLong_AsUnsignedLongLong -1267650600228229401496703205376 OverflowError int too big to convert
PyLong_AsUnsignedLongLong 18446744073709551616 OverflowError int too big to convert
... (6 more lines)
```

runtime/capi/longs.c: CPython says "can't convert negative value to unsigned int" / "to size_t" / "negative int to unsigned".


