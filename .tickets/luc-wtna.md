---
id: luc-wtna
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array TypeError/OverflowError messages and checks differ (iadd, extend, indices, fromfile, pop, insert, reduce_ex, frombytes, repr of 'w')

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff, about 80 cases over seeds 1 and 2 at 1,000 cases each.

```python
from array import array
def t(label, f):
    try:
        print(label, f())
    except Exception as e:
        print(label, type(e).__name__, e)
a = array('b', [1])
t("iadd list", lambda: a.__iadd__([1]))
t("extend other typecode", lambda: a.extend(array('i', [2])))
t("getitem float", lambda: a[1.5])
class F:
    def read(self, n): raise ValueError("read")
t("fromfile(f, None)", lambda: a.fromfile(F(), None))
t("imul huge", lambda: array('w', "ab").__imul__(144115188075855871))
```
CPython 3.14t:
```
iadd list TypeError can only extend array with array (not "list")
extend other typecode TypeError can only extend with array of same kind
getitem float TypeError array indices must be integers
fromfile(f, None) TypeError 'NoneType' object cannot be interpreted as an integer
imul huge MemoryError 
```
Lucifer:
```
iadd list TypeError bad argument type for built-in operation
extend other typecode TypeError bad argument type for built-in operation
getitem float TypeError 'float' object cannot be interpreted as an integer
fromfile(f, None) TypeError '<' not supported between instances of 'NoneType' and 'int'
imul huge MemoryError 
```

```python
from array import array
def t(label, f):
    try:
        print(label, f())
    except Exception as e:
        print(label, type(e).__name__, e)
a = array('l', [1])
m = memoryview(a)
print("memoryview of array('l'):", m.format, m.itemsize, m.nbytes)
t("extend(5)", lambda: array('i').extend(5))
t("delitem 2**64-1", lambda: array('w', "ab").__delitem__(2**64 - 1))
t("pop(2**63)", lambda: array('I', [1]).pop(2**63))
t("insert(2**64, 1.5)", lambda: array('B', [1]).insert(2**64, 1.5))
t("__reduce_ex__(2**63)", lambda: array('f').__reduce_ex__(2**63))
t("frombytes(list)", lambda: array('H').frombytes([1, 2]))
t("repr, surrogate in 'w' array", lambda: repr(array('w', "\ud800")))
def partial():
    x = array('w', "e")
    def g():
        yield "x"; yield "y"; yield 1.5
    try:
        x.extend(g())
    except TypeError as e:
        print(type(e).__name__, e)
    return x.tounicode()
t("extend with a generator failing at item 3 keeps items 1-2", partial)
```
CPython 3.14t:
```
memoryview of array('l'): l 8 8
extend(5) TypeError 'int' object is not iterable
delitem 2**64-1 IndexError cannot fit 'int' into an index-sized integer
pop(2**63) OverflowError Python int too large to convert to C ssize_t
insert(2**64, 1.5) OverflowError Python int too large to convert to C ssize_t
__reduce_ex__(2**63) OverflowError Python int too large to convert to C long
frombytes(list) TypeError a bytes-like object is required, not 'list'
repr, surrogate in 'w' array UnicodeDecodeError 'utf-32-le' codec can't decode bytes in position 0-3: code point in surrogate code point range(0xd800, 0xe000)
TypeError array item must be a unicode character, not float
extend with a generator failing at item 3 keeps items 1-2 exy
```
Lucifer:
```
memoryview of array('l'): B 1 8
extend(5) TypeError can only extend with iterable
delitem 2**64-1 IndexError array assignment index out of range
pop(2**63) IndexError pop index out of range
insert(2**64, 1.5) TypeError 'float' object cannot be interpreted as an integer
__reduce_ex__(2**63) (<built-in function _array_reconstructor>, (<class 'array.array'>, 'f', 14, b''), None)
frombytes(list) TypeError memoryview: a bytes-like object is required, not 'list'
repr, surrogate in 'w' array array('w', '\ud800')
TypeError array item must be a unicode character, not float
extend with a generator failing at item 3 keeps items 1-2 e
```


