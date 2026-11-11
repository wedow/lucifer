---
id: luc-odpl
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Call errors name bound methods and classes '<method object>'/'<type object>' instead of _PyObject_FunctionStr

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_diff fns p_builders, p_call_generic, p_messages, about 110 cases over seeds 1 and 2 at 1,000 cases each.

```python
class C:
    def m(self, **k): return k
    def n(self): pass
class K:
    def __init__(self, **k): pass
def f(**k): return k
def t(fn):
    try: fn()
    except Exception as e: print(type(e).__name__, e)
t(lambda: C().m(**{'a': 1}, **{'a': 2}))
t(lambda: f(**{'a': 1}, **{'a': 2}))
t(lambda: K(**{'a': 1}, **{'a': 2}))
t(lambda: C().m(*5))
t(lambda: f(*5))
t(lambda: K(*5))
```
CPython 3.14t:
```
TypeError __main__.C.m() got multiple values for keyword argument 'a'
TypeError __main__.f() got multiple values for keyword argument 'a'
TypeError __main__.K() got multiple values for keyword argument 'a'
TypeError __main__.C.m() argument after * must be an iterable, not int
TypeError __main__.f() argument after * must be an iterable, not int
TypeError __main__.K() argument after * must be an iterable, not int
```
Lucifer:
```
TypeError <method object> got multiple values for keyword argument 'a'
TypeError __main__.f() got multiple values for keyword argument 'a'
TypeError <type object> got multiple values for keyword argument 'a'
TypeError <method object> argument after * must be an iterable, not int
TypeError __main__.f() argument after * must be an iterable, not int
TypeError <type object> argument after * must be an iterable, not int
```

```python
class NoQ:
    def __getattribute__(self, n):
        if n == '__qualname__': raise AttributeError('q')
        return object.__getattribute__(self, n)
    def __call__(self, **k): return k
    def __str__(self): return 'NOQ'
class E:
    def __call__(self, *a, **k): return a
def t(fn):
    try: fn()
    except Exception as e: print(type(e).__name__, e)
t(lambda: NoQ()(**{'a': 1}, **{'a': 2}))
t(lambda: E()(*5))
```
CPython 3.14t:
```
TypeError NOQ got multiple values for keyword argument 'a'
TypeError <__main__.E object at 0x...> argument after * must be an iterable, not int
```
Lucifer:
```
TypeError <NoQ object> got multiple values for keyword argument 'a'
TypeError <E object> argument after * must be an iterable, not int
```

```python
class FQ:
    def __getattribute__(self, n):
        if n == '__qualname__': raise ValueError('q')
        return object.__getattribute__(self, n)
    def __call__(self, **k): return k
try: FQ()(**{'a': 1}, **{'a': 2})
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
ValueError q
```
Lucifer:
```
TypeError <FQ object> got multiple values for keyword argument 'a'
```

runtime/interp/call.c lu_function_str handles only functions; CPython looks up __module__/__qualname__ on any object and propagates non-AttributeError errors.


