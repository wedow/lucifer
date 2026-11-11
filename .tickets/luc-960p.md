---
id: luc-960p
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# itertools.tee: no weakref support, no _tee_dataobject, re-entry not detected

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): itertools_diff fns teeweak (54), teedata (48), tee/tee_wrap (42) over seeds 1 and 2 at 1,000 cases each.

```python
import itertools, weakref
a, b = itertools.tee([1, 2, 3])
log = []
r = weakref.ref(a, lambda ref: log.append('called'))
del a
print(log, type(b).__name__)
```
CPython 3.14t:
```
['called'] _tee
```
Lucifer:
```
Traceback (most recent call last):
  File "build/repro/dicts/tee_weakref.py", line 4, in <module>
    r = weakref.ref(a, lambda ref: log.append('called'))
TypeError: cannot create weak reference to '_tee' object
```

```python
import itertools
for args in ((iter([]), [], None), (iter([]), [0], None), (iter([]), [0], 3)):
    try:
        print(itertools._tee_dataobject(*args))
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
<itertools._tee_dataobject object at 0x...>
<itertools._tee_dataobject object at 0x...>
ValueError Invalid arguments
```
Lucifer:
```
AttributeError module 'itertools' has no attribute '_tee_dataobject'
AttributeError module 'itertools' has no attribute '_tee_dataobject'
AttributeError module 'itertools' has no attribute '_tee_dataobject'
```

```python
import itertools
class Src:
    def __iter__(self):
        return self
    def __next__(self):
        try:
            next(t)
        except Exception as e:
            print(type(e).__name__, e)
        return 1
t, u = itertools.tee(Src())
print(next(t))
```
CPython 3.14t:
```
RuntimeError cannot re-enter the tee iterator
1
```
Lucifer:
```
RecursionError maximum recursion depth exceeded
1
```


