---
id: luc-r9to
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# SyntaxError argument handling and str() differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_exc_diff, about 50 cases over seeds 1 and 2 at 1,000 cases each.

```python
class Ii(int): pass
for a in [('m', 5), ('m', (1,)), ('m', 'abcde')]:
    try: print(SyntaxError(*a).args)
    except Exception as e: print(type(e).__name__, e)
print(str(SyntaxError('a', ('a/b', 3, 1, 't'))))
print(str(SyntaxError('a', ('a/b', Ii(3), 1, 't'))))
print(str(SyntaxError('a', (type('S', (str,), {})('a/b'), 3, 1, 't'))))
```
CPython 3.14t:
```
TypeError 'int' object is not iterable
TypeError function takes at least 4 arguments (1 given)
TypeError end_offset must be provided when end_lineno is provided
a (b, line 3)
a (b)
a (b, line 3)
```
Lucifer:
```
TypeError Value after * must be an iterable, not int
TypeError function takes at least 4 arguments (1 given)
TypeError end_offset must be provided when end_lineno is provided
a (b, line 3)
a (b, line 3)
a (b, line 3)
```


