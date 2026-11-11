---
id: luc-inbg
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# OrderedDict (Python implementation) differs from C OrderedDict: messages, update, iterator mutation checks, pop on empty

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff ops o_*, about 75 cases over seeds 1 and 2 at 1,000 cases each.

```python
from collections import OrderedDict
od = OrderedDict()
for f in (lambda: od.__setitem__([], 1), lambda: od.move_to_end([]),
          lambda: OrderedDict.fromkeys([[]]), lambda: od.pop([], 1),
          lambda: od.__delitem__([])):
    try:
        f()
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError unhashable type: 'list'
KeyError []
TypeError unhashable type: 'list'
TypeError unhashable type: 'list'
TypeError unhashable type: 'list'
```
Lucifer:
```
TypeError cannot use 'list' as a dict key (unhashable type: 'list')
TypeError cannot use 'list' as a dict key (unhashable type: 'list')
TypeError cannot use 'list' as a dict key (unhashable type: 'list')
TypeError cannot use 'list' as a dict key (unhashable type: 'list')
```

```python
from collections import OrderedDict
for arg in ([5], [[1]], [[]], [[1, 2, 3]]):
    try:
        OrderedDict().update(arg)
    except Exception as e:
        print(arg, type(e).__name__, e)
```
CPython 3.14t:
```
[5] TypeError 'int' object is not iterable
[[1]] ValueError need more than 1 value to unpack
[[]] ValueError need more than 0 values to unpack
[[1, 2, 3]] ValueError too many values to unpack (expected 2)
```
Lucifer:
```
[5] TypeError cannot unpack non-iterable int object
[[1]] ValueError not enough values to unpack (expected 2, got 1)
[[]] ValueError not enough values to unpack (expected 2, got 0)
[[1, 2, 3]] ValueError too many values to unpack (expected 2, got 3)
```

```python
from collections import OrderedDict
class ItemsOnly:
    def items(self):
        return [('a', 1)]
    def __iter__(self):
        return iter([('b', 2)])
od = OrderedDict()
od.update(ItemsOnly())
print(list(od.items()))
```
CPython 3.14t:
```
[('a', 1)]
```
Lucifer:
```
[('b', 2)]
```

```python
from collections import OrderedDict
for name, mut in (('move_to_end', lambda od: od.move_to_end('a')),
                  ('popitem', lambda od: od.popitem()),
                  ('del+add', lambda od: (od.pop('b'), od.__setitem__('b', 9)))):
    od = OrderedDict(a=1, b=2, c=3)
    it = iter(od)
    next(it)
    mut(od)
    try:
        print(name, next(it))
    except Exception as e:
        print(name, type(e).__name__, e)
```
CPython 3.14t:
```
move_to_end RuntimeError OrderedDict mutated during iteration
popitem RuntimeError OrderedDict mutated during iteration
del+add RuntimeError OrderedDict mutated during iteration
```
Lucifer:
```
move_to_end StopIteration 
popitem b
del+add c
```

```python
from collections import OrderedDict
class H:
    def __hash__(self):
        raise ValueError('hash')
for od in (OrderedDict(), OrderedDict(a=1)):
    try:
        print(od.pop(H(), 5))
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError hash
ValueError hash
```
Lucifer:
```
5
ValueError hash
```

```python
from collections import OrderedDict
od = OrderedDict(a=1, b=2, c=3)
dict.clear(od)
print(len(od), list(dict.keys(od)))
try:
    print(list(od))
except Exception as e:
    print(type(e).__name__, e)
od['z'] = 0
try:
    print(list(od), list(dict.keys(od)))
except Exception as e:
    print(type(e).__name__, e)
od = OrderedDict(a=1, b=2)
dict.popitem(od)
try:
    print(list(od), list(od.items()))
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
0 []
['a']
['a', 'z'] ['z']
KeyError 'b'
```
Lucifer:
```
0 []
['a', 'b', 'c']
['a', 'b', 'c', 'z'] ['z']
KeyError 'b'
```

odict_raw_dict_ops follows CPython stale-node behaviour that is itself odd; low priority.


