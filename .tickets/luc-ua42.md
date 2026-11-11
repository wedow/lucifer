---
id: luc-ua42
status: open
deps: []
links: []
created: 2026-11-11T02:46:59Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# struct: offset __index__, format text checks, uninitialized Struct, iter_unpack type and argument messages differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): textlibs_diff, about 30 cases over seeds 1 and 2 at 1,000 cases each.

```python
import struct
class I:
    def __index__(self): return 1
class R:
    def __index__(self): raise ValueError("boom")
class Big:
    def __index__(self): return 2**70
for off in (I(), R(), Big(), "s", 2**63, -2**63-1):
    try: print(struct.unpack_from("@b", b"abc", off))
    except Exception as e: print(type(off).__name__, type(e).__name__, e)
for off in (Big(), -2**63-1):
    try: struct.pack_into("b", bytearray(3), off, 1)
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
(98,)
R ValueError boom
Big OverflowError Python int too large to convert to C ssize_t
str TypeError 'str' object cannot be interpreted as an integer
int OverflowError Python int too large to convert to C ssize_t
int OverflowError Python int too large to convert to C ssize_t
IndexError cannot fit 'Big' into an index-sized integer
IndexError cannot fit 'int' into an index-sized integer
```
Lucifer:
```
I TypeError '<' not supported between instances of 'I' and 'int'
R TypeError '<' not supported between instances of 'R' and 'int'
Big TypeError '<' not supported between instances of 'Big' and 'int'
str TypeError '<' not supported between instances of 'str' and 'int'
int error offset 9223372036854775808 out of range for 3-byte buffer
int error offset -9223372036854775809 out of range for 3-byte buffer
TypeError '<' not supported between instances of 'Big' and 'int'
error offset -9223372036854775809 out of range for 3-byte buffer
```

```python
import struct
for f in ("\xe9", "ąi", b"\xff", ">3 i"):
    try: print(repr(f), struct.calcsize(f))
    except Exception as e: print(repr(f), type(e).__name__, e)
try: struct.pack(">3 i")
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
'é' UnicodeEncodeError 'ascii' codec can't encode character '\xe9' in position 0: ordinal not in range(128)
'ąi' UnicodeEncodeError 'ascii' codec can't encode character '\u0105' in position 0: ordinal not in range(128)
b'\xff' UnicodeEncodeError 'ascii' codec can't encode character '\udcff' in position 0: ordinal not in range(128)
'>3 i' error bad char in struct format
error bad char in struct format
```
Lucifer:
```
'é' error bad char in struct format
'ąi' error bad char in struct format
b'\xff' UnicodeDecodeError 'ascii' codec can't decode byte 0xff in position 0: ordinal not in range(128)
'>3 i' 12
error pack expected 3 items for packing (got 0)
```

```python
import struct
s = struct.Struct.__new__(struct.Struct)
for a in ("format", "size"):
    try: print(getattr(s, a))
    except Exception as e: print(type(e).__name__, e)
try: s.pack()
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
RuntimeError Struct object is not initialized
-1
RuntimeError Struct object is not initialized
```
Lucifer:
```
AttributeError 'struct.Struct' object has no attribute '_format'
AttributeError 'struct.Struct' object has no attribute '_size'
AttributeError 'struct.Struct' object has no attribute '_items'
```

```python
import struct, operator
it = struct.iter_unpack("b", b"abc")
print(type(it).__name__, hasattr(it, "__length_hint__"))
try: print(operator.length_hint(it, -1), it.__length_hint__())
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
unpack_iterator True
3 3
```
Lucifer:
```
generator False
AttributeError 'generator' object has no attribute '__length_hint__'
```

```python
import struct
for f, a in ((struct.pack_into, ("b",)), (struct.pack_into, ("b", bytearray(2))), (struct.unpack_from, ()), (struct.Struct("b").pack_into, ())):
    try: f(*a)
    except Exception as e: print(type(e).__name__, e)
try: struct.iter_unpack("b", [])
except Exception as e: print(type(e).__name__, e)
class BF:
    def __buffer__(self, flags): raise ValueError("boom")
try: struct.pack_into("b", BF(), 0, 1)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
error pack_into expected buffer argument
error pack_into expected offset argument
TypeError unpack_from() takes at least 1 positional argument (0 given)
error pack_into expected buffer argument
TypeError a bytes-like object is required, not 'list'
TypeError argument must be read-write bytes-like object, not BF
```
Lucifer:
```
TypeError pack_into() missing 2 required positional arguments: 'buffer' and 'offset'
TypeError pack_into() missing 1 required positional argument: 'offset'
TypeError unpack_from() missing 2 required positional arguments: 'format' and 'buffer'
TypeError Struct.pack_into() missing 2 required positional arguments: 'buffer' and 'offset'
TypeError memoryview: a bytes-like object is required, not 'list'
ValueError boom
```


