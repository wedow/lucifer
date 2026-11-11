---
id: luc-bxbc
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# iter() does not fall back to __getitem__ when looking up __iter__ raises AttributeError

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): typeobj_diff, about 25 cases over seeds 1 and 2 at 1,000 cases each.

```python
class G:
    def __get__(self, o, t): print("G.__get__"); raise AttributeError('g')
class It:
    __getitem__ = G()
    @property
    def __iter__(self): print("it"); raise AttributeError('i')
def t(f):
    try: print(f())
    except Exception as e: print(type(e).__name__, e)
t(lambda: iter(It()))
t(lambda: list(It()))
t(lambda: 1 in It())
```
CPython 3.14t:
```
it
G.__get__
TypeError 'It' object is not iterable
it
G.__get__
TypeError 'It' object is not iterable
it
G.__get__
TypeError argument of type 'It' is not a container or iterable
```
Lucifer:
```
it
AttributeError i
it
AttributeError i
it
AttributeError i
```


