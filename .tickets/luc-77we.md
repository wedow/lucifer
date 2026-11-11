---
id: luc-77we
status: open
deps: []
links: []
created: 2026-11-11T04:17:20Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# memoryview gaps: _from_flags, __buffer__, weakrefs, released-view check order, tuple index, cast/hex checks, array format

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytesobj_diff fn mem.script (about 100 cases), sequences_diff (about 16) over seeds 1 and 2 at 1,000 cases each.

```python
import weakref
def t(label, f):
    try:
        print(label, f())
    except Exception as e:
        print(label, type(e).__name__, e)
t("from_flags", lambda: memoryview._from_flags(b"ab", 0))
t("buffer_dunder", lambda: memoryview(b"ab").__buffer__(0))
def rel():
    m = memoryview(b"ab"); m.release(); return m.tobytes("x")
t("tobytes_released_bad_order", rel)
t("tuple_index_msg", lambda: memoryview(b"abc")[1, 2])
t("weakref", lambda: weakref.ref(memoryview(b"")))
t("cast_nonbyte", lambda: memoryview(bytearray(8)).cast("H").cast("I"))
t("cast_zero_shape", lambda: memoryview(b"").cast("B", (0, 2)))
t("hex_sep_bad", lambda: memoryview(b"ab").hex([1]))
t("hex_sep_big", lambda: memoryview(b"ab").hex(":", 2**70))
```
CPython 3.14t:
```
from_flags <memory at 0x...>
buffer_dunder <memory at 0x...>
tobytes_released_bad_order ValueError operation forbidden on released memoryview object
tuple_index_msg TypeError cannot index 1-dimension view with 2-element tuple
weakref <weakref at 0x...; dead>
cast_nonbyte TypeError memoryview: cannot cast between two non-byte formats
cast_zero_shape TypeError memoryview: cannot cast view with zeros in shape or strides
hex_sep_bad TypeError sep must be str or bytes.
hex_sep_big OverflowError Python int too large to convert to C int
```
Lucifer:
```
from_flags AttributeError type object 'memoryview' has no attribute '_from_flags'
buffer_dunder AttributeError 'memoryview' object has no attribute '__buffer__'
tobytes_released_bad_order ValueError order must be 'C', 'F' or 'A'
tuple_index_msg TypeError memoryview: invalid slice key
weakref TypeError cannot create weak reference to 'memoryview' object
cast_nonbyte TypeError memoryview: cannot cast between two non-byte formats
cast_zero_shape ValueError memoryview.cast(): elements of shape must be integers > 0
hex_sep_bad TypeError a bytes-like object is required, not 'list'
hex_sep_big 6162
```

memoryview(array("l", [1])) reports format B and itemsize 1 (s_array_misc).


