---
id: luc-pdtw
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Assorted call and name-error messages: NameError.name of a free variable, deleted __next__, positional-only keyword check

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_diff fns p_scope, p_virtual, p_binding, about 9 cases over seeds 1 and 2 at 1,000 cases each.

```python
def h():
    def k(): return w
    k()
    w = 1
try: h()
except NameError as e: print(type(e).__name__, e.name)
```
CPython 3.14t:
```
NameError w
```
Lucifer:
```
NameError None
```

```python
class I:
    def __iter__(self): return self
    def __next__(self):
        del I.__next__
        return 1
try:
    for x in I(): print('item', x)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
item 1
TypeError 'I' object is not iterable
```
Lucifer:
```
item 1
TypeError 'I' object is not an iterator
```

```python
class S(str):
    def __eq__(self, o): return []
    def __hash__(self): return hash(str(self))
def f(xx, /): pass
try: f(1, **{S('xx'): 2})
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError f() got an unexpected keyword argument 'xx'
```
Lucifer:
```
TypeError f() got some positional-only arguments passed as keyword arguments: 'xx'
```

Separate small causes, batched.


