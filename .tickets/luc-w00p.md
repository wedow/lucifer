---
id: luc-w00p
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Small type-object checks: __slotnames__ type, __class__ slot comparison, __kwdefaults__ lookup, NUL in __name__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 30 cases over seeds 1 and 2 at 1,000 cases each.

```python
class A:
    __slotnames__ = 1
a = A(); a.x = 1
for p in (0, 1, 2):
    try: print(a.__reduce_ex__(p))
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError A.__slotnames__ should be a list or None, not int
TypeError A.__slotnames__ should be a list or None, not int
TypeError A.__slotnames__ should be a list or None, not int
```
Lucifer:
```
TypeError 'int' object is not iterable
TypeError 'int' object is not iterable
TypeError object of type 'int' has no len()
```

```python
class S(str):
    def __eq__(self, o): print("S.__eq__"); return False
    __hash__ = str.__hash__
class A: __slots__ = (S('x'),)
class B: __slots__ = (S('y'),)
class C: __slots__ = ('x',)
class D: __slots__ = ('x',)
a = A()
try: a.__class__ = B
except Exception as e: print(type(e).__name__, e)
c = C()
try: c.__class__ = D; print(type(c).__name__)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
S.__eq__
TypeError __class__ assignment: 'B' object layout differs from 'A'
D
```
Lucifer:
```
TypeError __class__ assignment: 'B' object layout differs from 'A'
D
```

```python
class K:
    def __hash__(s): return hash('k')
    def __eq__(s, o): print("K.__eq__"); raise ValueError('k')
def f(a, *, k): return (a, k)
f.__kwdefaults__ = {K(): 4}
try: print(f(1))
except Exception as e: print(type(e).__name__, e)
try: print(f(1, k=2))
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
K.__eq__
ValueError k
(1, 2)
```
Lucifer:
```
TypeError f() missing 1 required keyword-only argument: 'k'
(1, 2)
```

```python
class B: pass
try: B.__name__ = 'a\x00'
except Exception as e: print(type(e).__name__, e)
try: B.__qualname__ = 'a\x00'; print(B.__qualname__ == 'a\x00')
except Exception as e: print(type(e).__name__, e)
try: type('a\x00b', (), {})
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError type name must not contain null characters
True
ValueError type name must not contain null characters
```
Lucifer:
```
True
ValueError type name must not contain null characters
```


