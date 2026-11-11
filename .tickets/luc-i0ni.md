---
id: luc-i0ni
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# with-statement TypeError: bare type name, and the async-protocol hint added too eagerly

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_diff fn p_with, about 21 cases over seeds 1 and 2 at 1,000 cases each.

```python
def mk():
    class L: pass
    return L
class O:
    class I: pass
for x in (mk()(), O.I()):
    try:
        with x: pass
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError 'mk.<locals>.L' object does not support the context manager protocol (missed __exit__ method)
TypeError 'O.I' object does not support the context manager protocol (missed __exit__ method)
```
Lucifer:
```
TypeError 'L' object does not support the context manager protocol (missed __exit__ method)
TypeError 'I' object does not support the context manager protocol (missed __exit__ method)
```

```python
class NS:
    __aenter__ = 1
    async def __aexit__(self, *a): pass
class NA:
    __enter__ = len
    def __exit__(self, *a): pass
class Plain:
    async def __aenter__(self): pass
    async def __aexit__(self, *a): pass
for cls in (NS, Plain):
    try:
        with cls(): pass
    except Exception as e: print(type(e).__name__, e)
async def f(c):
    async with c(): pass
for cls in (NA,):
    try: f(cls).send(None)
    except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError 'NS' object does not support the context manager protocol (missed __exit__ method)
TypeError 'Plain' object does not support the context manager protocol (missed __exit__ method) but it supports the asynchronous context manager protocol. Did you mean to use 'async with'?
TypeError 'NA' object does not support the asynchronous context manager protocol (missed __aexit__ method)
```
Lucifer:
```
TypeError 'NS' object does not support the context manager protocol (missed __exit__ method) but it supports the asynchronous context manager protocol. Did you mean to use 'async with'?
TypeError 'Plain' object does not support the context manager protocol (missed __exit__ method) but it supports the asynchronous context manager protocol. Did you mean to use 'async with'?
TypeError 'NA' object does not support the asynchronous context manager protocol (missed __aexit__ method) but it supports the context manager protocol. Did you mean to use 'with'?
```

lib/kernel/exceptions.py _special_error.


