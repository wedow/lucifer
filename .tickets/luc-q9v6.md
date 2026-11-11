---
id: luc-q9v6
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# getattr/setattr/delattr reject str-subclass names; instance dict keys of a str subclass are not found

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff fns i_set, i_get, i_del, i_d, about 96 cases over seeds 1 and 2 at 1,000 cases each.

```python
class S(str):
    pass
class C:
    pass
o = C()
for f in (lambda: setattr(o, S('a'), 1), lambda: getattr(o, S('a')),
          lambda: getattr(o, S('b')), lambda: delattr(o, S('a')),
          lambda: delattr(o, S('a'))):
    try:
        print(f())
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
None
1
AttributeError 'C' object has no attribute 'b'
None
AttributeError 'C' object has no attribute 'a'
```
Lucifer:
```
TypeError attribute name must be string, not 'S'
TypeError attribute name must be string, not 'S'
TypeError attribute name must be string, not 'S'
TypeError attribute name must be string, not 'S'
TypeError attribute name must be string, not 'S'
```

```python
class S(str):
    pass
class C:
    pass
o = C()
o.__dict__[S('a')] = 1
print(getattr(o, 'a', None), list(o.__dict__), o.__dict__['a'])
```
CPython 3.14t:
```
1 ['a'] 1
```
Lucifer:
```
None ['a'] 1
```


