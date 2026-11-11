---
id: luc-w118
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# PyLong_FromString leaves the end pointer past an invalid underscore

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): longobj_str_diff fn intcstr, 6 cases over seeds 1 and 2 at 1,000 cases each.

```python
import ctypes
F = ctypes.PYFUNCTYPE(ctypes.py_object, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int)(("PyLong_FromString", ctypes.pythonapi))
def t(data, base):
    buf = ctypes.create_string_buffer(data)
    end = ctypes.c_void_p(0)
    start = ctypes.addressof(buf)
    try: r = F(start, ctypes.addressof(end), base)
    except Exception as e: r = (type(e).__name__, str(e))
    print(data, base, r, None if end.value is None else end.value - start)
for d, b in [(b"0__1", 10), (b"1__2", 10), (b"12_", 10), (b"12__3", 10), (b"-0__5", 10), (b"\r0__0686", 10), (b"1_", 10), (b"12 ", 10), (b"12x", 10), (b"0x1_", 16), (b"ff_", 16)]:
    t(d, b)
```
CPython 3.14t:
```
b'0__1' 10 ('ValueError', "invalid literal for int() with base 10: '0__1'") 1
b'1__2' 10 ('ValueError', "invalid literal for int() with base 10: '1__2'") 1
b'12_' 10 ('ValueError', "invalid literal for int() with base 10: '12_'") 2
b'12__3' 10 ('ValueError', "invalid literal for int() with base 10: '12__3'") 2
b'-0__5' 10 ('ValueError', "invalid literal for int() with base 10: '-0__5'") 2
b'\r0__0686' 10 ('ValueError', "invalid literal for int() with base 10: '\\r0__0686'") 2
b'1_' 10 ('ValueError', "invalid literal for int() with base 10: '1_'") 1
b'12 ' 10 12 3
b'12x' 10 ('ValueError', "invalid literal for int() with base 10: '12x'") 2
b'0x1_' 16 ('ValueError', "invalid literal for int() with base 16: '0x1_'") 3
b'ff_' 16 ('ValueError', "invalid literal for int() with base 16: 'ff_'") 2
```
Lucifer:
```
b'0__1' 10 ('ValueError', "invalid literal for int() with base 10: '0__1'") 2
b'1__2' 10 ('ValueError', "invalid literal for int() with base 10: '1__2'") 2
b'12_' 10 ('ValueError', "invalid literal for int() with base 10: '12_'") 3
b'12__3' 10 ('ValueError', "invalid literal for int() with base 10: '12__3'") 3
b'-0__5' 10 ('ValueError', "invalid literal for int() with base 10: '-0__5'") 3
b'\r0__0686' 10 ('ValueError', "invalid literal for int() with base 10: '\\r0__0686'") 3
b'1_' 10 ('ValueError', "invalid literal for int() with base 10: '1_'") 2
b'12 ' 10 12 3
b'12x' 10 ('ValueError', "invalid literal for int() with base 10: '12x'") 2
b'0x1_' 16 ('ValueError', "invalid literal for int() with base 16: '0x1_'") 4
b'ff_' 16 ('ValueError', "invalid literal for int() with base 16: 'ff_'") 3
```


