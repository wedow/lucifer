---
id: luc-1wbo
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# bytearray/list methods: argument conversion order and Py_ssize_t/C int overflow checks differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytesobj_diff bare.* and list/tuple fns, about 60 cases over seeds 1 and 2 at 1,000 cases each.

```python
def t(label, f):
    try:
        print(label, f(), flush=True)
    except Exception as e:
        print(label, type(e).__name__, e, flush=True)
BIG = 2**63
t("center width 2**63", lambda: bytearray(b"a").center(BIG, b"x"))
t("ljust width 2**63, bad fill", lambda: bytearray(b"a").ljust(BIG, memoryview(b"*")))
t("rjust width 2**63", lambda: bytearray(b"a").rjust(BIG, b"x"))
t("zfill 2**63", lambda: bytearray(b"a").zfill(BIG))
t("rsplit maxsplit 2**63", lambda: bytearray(b"a b").rsplit(None, BIG))
class NegLen(bytes):
    def __len__(self): return -1
t("hex sep with __len__ -1", lambda: bytes(b"ab").hex(NegLen(b":"), -2))
t("count start before sub", lambda: bytearray(b"ab").count(-1, b"\0", 3))
class EmptyLen(bytes):
    def __len__(self): return 0
t("rjust fill subclass", lambda: bytearray(b"ab").rjust(8, EmptyLen(b"x")))
t("ljust fill memoryview", lambda: bytearray(b"ab").ljust(8, memoryview(b"*")))
class LenOne(bytes):
    def __len__(self): return 1
t("hex sep: 2 bytes but __len__ 1", lambda: b"ab".hex(LenOne(b"::")))
t("expandtabs 2**31", lambda: bytearray(b"a\tb").expandtabs(2**31))
```
CPython 3.14t:
```
center width 2**63 OverflowError Python int too large to convert to C ssize_t
ljust width 2**63, bad fill OverflowError Python int too large to convert to C ssize_t
rjust width 2**63 OverflowError Python int too large to convert to C ssize_t
zfill 2**63 OverflowError Python int too large to convert to C ssize_t
rsplit maxsplit 2**63 OverflowError Python int too large to convert to C ssize_t
hex sep with __len__ -1 ValueError __len__() should return >= 0
count start before sub TypeError slice indices must be integers or None or have an __index__ method
rjust fill subclass bytearray(b'xxxxxxab')
ljust fill memoryview TypeError ljust() argument 2 must be a byte string of length 1, not memoryview
hex sep: 2 bytes but __len__ 1 61:62
expandtabs 2**31 OverflowError Python int too large to convert to C int
```
Lucifer:
```
center width 2**63 MemoryError 
ljust width 2**63, bad fill TypeError ljust() argument 2 must be a byte string of length 1, not memoryview
rjust width 2**63 OverflowError byte string is too large
zfill 2**63 OverflowError byte string is too large
rsplit maxsplit 2**63 [bytearray(b'a'), bytearray(b'b')]
hex sep with __len__ -1 6162
count start before sub ValueError byte must be in range(0, 256)
rjust fill subclass bytearray(b'xxxxxxab')
ljust fill memoryview TypeError ljust() argument 2 must be a byte string of length 1, not memoryview
hex sep: 2 bytes but __len__ 1 ValueError sep must be length 1.
expandtabs 2**31 bytearray(b'a                                                                                                                                                                                                                                                                               ...
```

```python
import sys
def t(label, f):
    try:
        print(label, f())
    except Exception as e:
        print(label, type(e).__name__, e)
class Ix:
    def __index__(self): return 2**64
t("list[Ix] where __index__ is out of range", lambda: [1][Ix()])
t("tuple[Ix]", lambda: (1,)[Ix()])
def iadd():
    l = [1]
    class It:
        def __iter__(self): return iter([0])
        def __length_hint__(self): return 2**60
    l += It()
    return l
t("list += iterator with length_hint 2**60", iadd)
class E(Exception): pass
t("tuple[x] with a long non-ASCII class name", lambda: (1,).__getitem__(type("é" * 100, (), {})()))
```
CPython 3.14t:
```
list[Ix] where __index__ is out of range IndexError cannot fit 'Ix' into an index-sized integer
tuple[Ix] IndexError cannot fit 'Ix' into an index-sized integer
list += iterator with length_hint 2**60 MemoryError 
tuple[x] with a long non-ASCII class name TypeError tuple indices must be integers or slices, not éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé
```
Lucifer:
```
list[Ix] where __index__ is out of range IndexError cannot fit 'int' into an index-sized integer
tuple[Ix] IndexError cannot fit 'int' into an index-sized integer
list += iterator with length_hint 2**60 [1, 0]
tuple[x] with a long non-ASCII class name TypeError tuple indices must be integers or slices, not éééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééééé
```

Companion of the str/bytes Py_ssize_t ticket; bytearray(b'\t').expandtabs(2**31) kills the process.


