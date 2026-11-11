---
id: luc-3bdd
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# UTF-8 decode error through the C API says 'invalid continuation byte' for a leading continuation byte

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): pickle_diff fn loads, 1 case over seeds 1 and 2 at 1,000 cases each.

```python
for b in (b'ab\x85', b'ab\x85(K', b'ab\x85(K\xe9L1', b'\x85', b'a\x85(\xe9'):
    try:
        b.decode()
    except Exception as e:
        print(b, e)
import pickle
try:
    pickle.loads(b'X\t\0\0\0ab\x85(K\xe9L1.')
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
b'ab\x85' 'utf-8' codec can't decode byte 0x85 in position 2: invalid start byte
b'ab\x85(K' 'utf-8' codec can't decode byte 0x85 in position 2: invalid start byte
b'ab\x85(K\xe9L1' 'utf-8' codec can't decode byte 0x85 in position 2: invalid start byte
b'\x85' 'utf-8' codec can't decode byte 0x85 in position 0: invalid start byte
b'a\x85(\xe9' 'utf-8' codec can't decode byte 0x85 in position 1: invalid start byte
UnicodeDecodeError 'utf-8' codec can't decode byte 0x85 in position 2: invalid start byte
```
Lucifer:
```
b'ab\x85' 'utf-8' codec can't decode byte 0x85 in position 2: invalid start byte
b'ab\x85(K' 'utf-8' codec can't decode byte 0x85 in position 2: invalid start byte
b'ab\x85(K\xe9L1' 'utf-8' codec can't decode byte 0x85 in position 2: invalid start byte
b'\x85' 'utf-8' codec can't decode byte 0x85 in position 0: invalid start byte
b'a\x85(\xe9' 'utf-8' codec can't decode byte 0x85 in position 1: invalid start byte
UnicodeDecodeError 'utf-8' codec can't decode byte 0x85 in position 2: invalid continuation byte
```

bytes.decode matches; only the C-API decoder path differs.


