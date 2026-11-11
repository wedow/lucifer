---
id: luc-ej0i
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array._array_reconstructor ignores the machine-format code and validates typecodes differently

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff fn reconstruct, 89 cases over seeds 1 and 2 at 1,000 cases each.

```python
import array
def t(*a):
    try:
        r = array.array._array_reconstructor if False else array._array_reconstructor(array.array, *a)
        print(a, r.typecode, list(r.tobytes()))
    except Exception as e:
        print(a, type(e).__name__, e)
t("ab", 14, b"")
t(5, 1, b"abcde")
t("w", 18, b"\xac\x20")
t("l", 18, b"\x3e\x18\xbb\x74")
t("b", 21, bytes(12))
t("B", 8, b"\x64\x8a\x73\xff")
t("u", 7, bytes(range(8)))
t("h", 18, b"\xdd\xdf\x23\xcd")
```
CPython 3.14t:
```
('ab', 14, b'') TypeError _array_reconstructor(): argument 2 must be a unicode character, not a string of length 2
(5, 1, b'abcde') TypeError _array_reconstructor() argument 2 must be a unicode character, not int
('w', 18, b'\xac ') ValueError string length not a multiple of item size
('l', 18, b'>\x18\xbbt') TypeError cannot use a str to initialize an array with typecode 'l'
('b', 21, b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00') ValueError string length not a multiple of item size
('B', 8, b'd\x8as\xff') i [100, 138, 115, 255]
('u', 7, b'\x00\x01\x02\x03\x04\x05\x06\x07') I [3, 2, 1, 0, 7, 6, 5, 4]
('h', 18, b'\xdd\xdf#\xcd') UnicodeDecodeError 'utf-16-le' codec can't decode bytes in position 0-1: illegal encoding
```
Lucifer:
```
build/repro/seqs/s_array_reconstructor.py:14: DeprecationWarning: The 'u' type code is deprecated and will be removed in Python 3.16
  t("u", 7, bytes(range(8)))
('ab', 14, b'') ValueError second argument must be a valid type code
(5, 1, b'abcde') TypeError 'in <string>' requires string as left operand, not int
('w', 18, b'\xac ') w [172, 32, 0, 0]
('l', 18, b'>\x18\xbbt') l [62, 24, 0, 0, 0, 0, 0, 0, 187, 116, 0, 0, 0, 0, 0, 0]
('b', 21, b'\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00') b [0, 0, 0]
('B', 8, b'd\x8as\xff') OverflowError unsigned byte integer is less than minimum
('u', 7, b'\x00\x01\x02\x03\x04\x05\x06\x07') ValueError chr() arg not in range(0x110000)
('h', 18, b'\xdd\xdf#\xcd') OverflowError signed short integer is greater than maximum
```


