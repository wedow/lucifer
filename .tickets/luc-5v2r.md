---
id: luc-5v2r
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# f(**obj) and {**obj}: AttributeError from keys() and KeyError from __getitem__ are not converted

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_diff fns p_builders, p_messages, about 37 cases over seeds 1 and 2 at 1,000 cases each.

```python
class N:
    def keys(self): raise AttributeError('inner')
def f(**k): return k
def t(fn):
    try: fn()
    except Exception as e: print(type(e).__name__, e)
t(lambda: f(**N()))
t(lambda: {**N()})
```
CPython 3.14t:
```
TypeError __main__.f() argument after ** must be a mapping, not N
TypeError 'N' object is not a mapping
```
Lucifer:
```
AttributeError inner
AttributeError inner
```

```python
class M:
    def keys(self): return ['a']
    def __getitem__(self, k): raise KeyError('zz')
def f(**k): return k
def t(fn):
    try: fn()
    except Exception as e: print(type(e).__name__, e)
t(lambda: f(**M()))
t(lambda: f(a=1, **M()))
t(lambda: {**M()})
```
CPython 3.14t:
```
TypeError __main__.f() got multiple values for keyword argument 'zz'
TypeError __main__.f() got multiple values for keyword argument 'a'
KeyError 'zz'
```
Lucifer:
```
KeyError 'zz'
KeyError 'zz'
KeyError 'zz'
```


