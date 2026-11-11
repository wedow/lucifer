---
id: luc-9bti
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# functools without _functools: cmp_to_key, Placeholder type and reduce messages differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): corelibs_diff fns functools.*, 11 cases over seeds 1 and 2 at 1,000 cases each.

```python
import functools
try: functools.cmp_to_key(lambda a, b: 0)()
except Exception as e: print(type(e).__name__, e)
K = functools.cmp_to_key(lambda a, b: 0)
print(type(K).__module__, type(K).__qualname__)
```
CPython 3.14t:
```
TypeError K() missing required argument 'obj' (pos 1)
functools KeyWrapper
```
Lucifer:
```
TypeError cmp_to_key.<locals>.K.__init__() missing 1 required positional argument: 'obj'
builtins type
```

```python
import functools
try:
    class X(type(functools.Placeholder)): pass
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError type 'functools._PlaceholderType' is not an acceptable base type
```
Lucifer:
```
TypeError type 'X' is not an acceptable base type
```

```python
import functools
class A:
    def __iter__(self): raise TypeError("mine")
try: functools.reduce(lambda a, b: a, A())
except Exception as e: print(type(e).__name__, e)
try: functools.reduce(lambda a, b: a, 5)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError reduce() arg 2 must support iteration
TypeError reduce() arg 2 must support iteration
```
Lucifer:
```
TypeError mine
TypeError 'int' object is not iterable
```


