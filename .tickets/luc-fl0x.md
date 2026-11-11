---
id: luc-fl0x
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# object.__reduce_ex__ does not validate the protocol argument as a C int

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, 22 cases over seeds 1 and 2 at 1,000 cases each.

```python
class A: pass
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
a = A()
t(a.__reduce_ex__, 's')
t(a.__reduce_ex__, 1099511627776)
t(a.__reduce_ex__, None)
t(a.__reduce_ex__, 1.5)
```
CPython 3.14t:
```
TypeError 'str' object cannot be interpreted as an integer
OverflowError Python int too large to convert to C int
TypeError 'NoneType' object cannot be interpreted as an integer
TypeError 'float' object cannot be interpreted as an integer
```
Lucifer:
```
TypeError '<' not supported between instances of 'str' and 'int'
(<function __newobj__ at 0x...>, (<class '__main__.A'>,), None, None, None)
TypeError '<' not supported between instances of 'NoneType' and 'int'
(<function _reconstructor at 0x...>, (<class '__main__.A'>, <class 'object'>, None))
```


