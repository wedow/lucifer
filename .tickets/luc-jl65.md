---
id: luc-jl65
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# No 'coroutine method ... was never awaited' RuntimeWarning for an unawaited athrow()

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_gen_diff fn gen_drive, about 6 cases over seeds 1 and 2 at 1,000 cases each.

```python
import warnings, gc
warnings.simplefilter('always')
warnings.showwarning = lambda m, c, *a, **k: print('warn', c.__name__, m)
async def ag():
    yield 1
g = ag()
x = g.athrow(RuntimeError)
del x
gc.collect()
print('done')
```
CPython 3.14t:
```
warn RuntimeWarning coroutine method 'athrow' of 'ag' was never awaited
done
```
Lucifer:
```
done
```


