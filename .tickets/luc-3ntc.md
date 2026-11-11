---
id: luc-3ntc
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# UnicodeError subclasses check constructor arguments differently

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 70 cases over seeds 1 and 2 at 1,000 cases each.

```python
class Ix:
    def __index__(self): print("Ix.__index__"); return 1
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
t(UnicodeEncodeError, 1, 'a', 0, 1, 'r')
t(UnicodeEncodeError, 'e', 'a', Ix(), 1, 'r')
t(UnicodeTranslateError, 1, 0, 1, 'r')
t(UnicodeTranslateError, 'a', Ix(), 1, 'r')
t(UnicodeTranslateError, 'a', None, 1, 'r')
t(UnicodeDecodeError, 'e', 1, 0, 1, 'r')
t(UnicodeDecodeError, 'e', 'a', 0, 1, 'r')
t(UnicodeDecodeError, 'e', b'a', 0, 1, 1)
e = UnicodeEncodeError('e', 'a', 0, 1, 'r')
e.object = 5
t(str, e)
```
CPython 3.14t:
```
TypeError argument 1 must be str, not int
Ix.__index__
'e' codec can't encode characters in position 1-0: r
TypeError argument 1 must be str, not int
Ix.__index__
can't translate characters in position 1-0: r
TypeError 'NoneType' object cannot be interpreted as an integer
TypeError a bytes-like object is required, not 'int'
TypeError a bytes-like object is required, not 'str'
TypeError argument 5 must be str, not int
TypeError UnicodeError 'object' attribute must be a string
```
Lucifer:
```
TypeError UnicodeEncodeError() argument 1 must be str, not int
TypeError UnicodeEncodeError() argument 3 must be int, not Ix
TypeError UnicodeTranslateError() argument 1 must be str, not int
TypeError UnicodeTranslateError() argument 2 must be int, not Ix
TypeError UnicodeTranslateError() argument 2 must be int, not NoneType
'e' codec can't decode byte 0x00 in position 0: r
TypeError string argument without an encoding
TypeError UnicodeDecodeError() argument 5 must be str, not int
TypeError UnicodeError 'object' attribute must be a string
```

```python
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
e = UnicodeDecodeError('enc', b'abc', 1, 2, 'r')
t(e.__init__, 'enc', 'a', 5, 4, '')
t(UnicodeDecodeError, 'enc', 1, 5, 2, '')
t(UnicodeDecodeError, 'enc', 'str', 0, 1, '')
```
CPython 3.14t:
```
TypeError a bytes-like object is required, not 'str'
TypeError a bytes-like object is required, not 'int'
TypeError a bytes-like object is required, not 'str'
```
Lucifer:
```
TypeError string argument without an encoding
'enc' codec can't decode bytes in position 5-1: 
TypeError string argument without an encoding
```


