---
id: luc-wha8
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# A code object's generator flags are not honoured like _Py_MakeCoro

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_diff fn p_genflags, 11 cases over seeds 1 and 2 at 1,000 cases each.

```python
def mk(mask):
    def flags_f():
        yield 1
    return flags_f.__code__.replace(co_flags=(flags_f.__code__.co_flags & ~0x2a0) | mask)
import types
for mask in (0, 0x220, 0x280, 0xa0):
    f = types.FunctionType(mk(mask), {})
    print(hex(mask), type(f()).__name__)
```
CPython 3.14t:
```
build/repro/bytecode/genflags.py:8: RuntimeWarning: coroutine 'mk.<locals>.flags_f' was never awaited
  print(hex(mask), type(f()).__name__)
RuntimeWarning: Enable tracemalloc to get the object allocation traceback
0x0 coroutine
0x220 coroutine
0x280 coroutine
0xa0 coroutine
```
Lucifer:
```
0x0 generator
0x220 async_generator
0x280 coroutine
0xa0 coroutine
```

CPython makes a generator or async generator only for exactly CO_GENERATOR or CO_ASYNC_GENERATOR, a coroutine otherwise.


