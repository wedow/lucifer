---
id: luc-7hk7
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# generator/coroutine throw(): no DeprecationWarning for the 3-argument form; argument errors and reuse checks differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_gen_diff fn gen_drive, about 525 cases over seeds 1 and 2 at 1,000 cases each.

```python
import warnings
warnings.simplefilter('always')
warnings.showwarning = lambda m, c, *a, **k: print('warn', c.__name__, m)
def g():
    yield 1
x = g(); next(x)
try: x.throw(ValueError, ValueError('v'), None)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
warn DeprecationWarning the (type, exc, tb) signature of throw() is deprecated, use the single-arg signature instead.
ValueError v
```
Lucifer:
```
ValueError v
```

```python
def g():
    yield 1
async def c():
    pass
async def ag():
    yield 1
def t(fn):
    try: fn()
    except Exception as e: print(type(e).__name__, e)
x = g(); next(x)
t(lambda: x.throw())
t(lambda: x.throw(1, 2, 3, 4))
co = c()
t(lambda: co.throw())
co.close()
a = ag()
aw = a.athrow(ValueError)
t(lambda: aw.throw())
```
CPython 3.14t:
```
TypeError throw expected at least 1 argument, got 0
TypeError throw expected at most 3 arguments, got 4
TypeError throw expected at least 1 argument, got 0
TypeError throw expected at least 1 argument, got 0
```
Lucifer:
```
TypeError generator.throw() missing 1 required positional argument: 'typ'
TypeError generator.throw() takes from 2 to 4 positional arguments but 5 were given
TypeError coroutine.throw() missing 1 required positional argument: 'typ'
TypeError async_generator_athrow.throw() missing 1 required positional argument: 'typ'
```

```python
async def ag():
    yield 1
a = ag()
aw = a.aclose()
try: aw.send(None)
except StopIteration: pass
for args in ((), (1, 2, 3, 4)):
    try: aw.throw(*args)
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
RuntimeError cannot reuse already awaited aclose()/athrow()
RuntimeError cannot reuse already awaited aclose()/athrow()
```
Lucifer:
```
TypeError async_generator_athrow.throw() missing 1 required positional argument: 'typ'
TypeError async_generator_athrow.throw() takes from 2 to 4 positional arguments but 5 were given
```


