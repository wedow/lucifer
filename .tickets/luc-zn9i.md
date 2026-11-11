---
id: luc-zn9i
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# vars(object) has another key order and an extra __static_attributes__

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): corelibs_diff fns collabc.*, 15 cases over seeds 1 and 2 at 1,000 cases each (the generator builds cases from object.__dict__).

```python
print(list(vars(object)))
```
CPython 3.14t:
```
['__new__', '__repr__', '__hash__', '__str__', '__getattribute__', '__setattr__', '__delattr__', '__lt__', '__le__', '__eq__', '__ne__', '__gt__', '__ge__', '__init__', '__reduce_ex__', '__reduce__', '__getstate__', '__subclasshook__', '__init_subclass__', '__format__', '__sizeof__', '__dir__', '__class__', '__doc__']
```
Lucifer:
```
['__doc__', '__sizeof__', '__getattribute__', '__setattr__', '__delattr__', '__new__', '__init__', '__dir__', '__repr__', '__str__', '__hash__', '__eq__', '__ne__', '__lt__', '__le__', '__gt__', '__ge__', '__getstate__', '__reduce__', '__reduce_ex__', '__init_subclass__', '__subclasshook__', '__class__', '__static_attributes__', '__format__']
```


