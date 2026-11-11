---
id: luc-mpzr
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# generator.close() on a delegating generator propagates an error from the sub-iterator's close

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_gen_diff fn gen_drive, about 8 cases over seeds 1 and 2 at 1,000 cases each.

```python
class It:
    def __iter__(self): return self
    def __next__(self): return 1
    @property
    def close(self): raise ValueError('attr')
import sys
sys.unraisablehook = lambda u: print('unraisable', type(u.exc_value).__name__, u.exc_value)
def g():
    yield from It()
x = g(); next(x)
try: print(x.close())
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
unraisable ValueError attr
None
```
Lucifer:
```
ValueError attr
```

CPython reports it as unraisable and continues.


