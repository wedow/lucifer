---
id: luc-z7c9
status: open
deps: []
links: []
created: 2026-11-11T04:17:20Z
type: bug
priority: 1
tags: [conformance, fuzzdiff]
---
# Segfault: iterating a memoryview over a PIL buffer whose pointer table holds NULL

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytesobj_diff fn mem.script, 6 cases over seeds 1 and 2 at 1,000 cases each.

```python
# iterate a 1-d PIL memoryview (suboffsets [0]) whose pointer table has a NULL entry
import ctypes as ct
class Buf(ct.Structure):
    _fields_ = [("buf", ct.c_void_p), ("obj", ct.c_void_p), ("len", ct.c_ssize_t),
                ("itemsize", ct.c_ssize_t), ("readonly", ct.c_int), ("ndim", ct.c_int),
                ("format", ct.c_char_p), ("shape", ct.c_void_p), ("strides", ct.c_void_p),
                ("suboffsets", ct.c_void_p), ("internal", ct.c_void_p)]
data = (ct.c_char * 3).from_buffer_copy(bytes([3, 0, 2]))
table = (ct.c_void_p * 3)(None, ct.addressof(data) + 1, ct.addressof(data) + 2)
shape = (ct.c_ssize_t * 1)(3)
strides = (ct.c_ssize_t * 1)(8)
suboffsets = (ct.c_ssize_t * 1)(0)
fmt = ct.create_string_buffer(b"B")
info = Buf(ct.addressof(table), None, 3, 1, 1, 1, ct.cast(fmt, ct.c_char_p),
           ct.addressof(shape), ct.addressof(strides), ct.addressof(suboffsets), None)
f = ct.pythonapi.PyMemoryView_FromBuffer
f.restype = ct.py_object
f.argtypes = [ct.POINTER(Buf)]
m = f(ct.byref(info))
it = iter(m)
for _ in range(4):
    try:
        print(next(it), flush=True)
    except StopIteration:
        print("stop", flush=True)
```
CPython 3.14t:
```
stop
0
2
stop
```
Lucifer:
```
timeout: the monitored command dumped core
```

CPython's memoryiter_next checks the suboffset pointer; Lucifer dereferences it.


