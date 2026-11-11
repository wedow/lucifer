---
id: luc-2r3w
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# isinstance()/issubclass(): no abstract-class protocol (__bases__), and issubclass checks argument 2 first

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_abstract_diff (about 300 cases), typeobj_diff (about 140 cases) over seeds 1 and 2 at 1,000 cases each.

```python
class Abs:
    @property
    def __bases__(self):
        print("bases"); return ()
a = Abs()
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
t(isinstance, 1, (int, a))
t(isinstance, 1, a)
t(issubclass, a, int)
t(issubclass, int, a)
t(issubclass, a, a)
t(isinstance, a, a)
```
CPython 3.14t:
```
True
bases
False
bases
bases
False
bases
False
bases
bases
True
bases
False
```
Lucifer:
```
True
TypeError isinstance() arg 2 must be a type, a tuple of types, or a union
TypeError issubclass() arg 1 must be a class
TypeError issubclass() arg 2 must be a class, a tuple of classes, or a union
TypeError issubclass() arg 2 must be a class, a tuple of classes, or a union
TypeError isinstance() arg 2 must be a type, a tuple of types, or a union
```

```python
def t(f, *a):
    try: print(f(*a))
    except Exception as e: print(type(e).__name__, e)
t(issubclass, None, 's')
t(issubclass, 1, 2)
t(issubclass, int, 's')
t(issubclass, 1, int)
t(isinstance, 1, 's')
```
CPython 3.14t:
```
TypeError issubclass() arg 1 must be a class
TypeError issubclass() arg 1 must be a class
TypeError issubclass() arg 2 must be a class, a tuple of classes, or a union
TypeError issubclass() arg 1 must be a class
TypeError isinstance() arg 2 must be a type, a tuple of types, or a union
```
Lucifer:
```
TypeError issubclass() arg 2 must be a class, a tuple of classes, or a union
TypeError issubclass() arg 2 must be a class, a tuple of classes, or a union
TypeError issubclass() arg 2 must be a class, a tuple of classes, or a union
TypeError issubclass() arg 1 must be a class
TypeError isinstance() arg 2 must be a type, a tuple of types, or a union
```


