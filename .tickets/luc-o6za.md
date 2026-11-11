---
id: luc-o6za
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# Async generator finalizer hook not called when a never-started async generator is collected

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_gen_diff fn gen_drive, 7 cases over seeds 1 and 2 at 1,000 cases each.

```python
import sys, gc
gc.disable()
log = []
sys.set_asyncgen_hooks(firstiter=lambda g: log.append('first'), finalizer=lambda g: log.append('fin'))
async def ag():
    yield 1
    return
g = ag()
held = [g.asend(1)]
for _ in range(2):
    try: held[0].send(1)
    except BaseException as e: print(type(e).__name__, e)
held = []
g = None
gc.collect()
print(log)
```
CPython 3.14t:
```
TypeError can't send non-None value to a just-started async generator
RuntimeError cannot reuse already awaited __anext__()/asend()
['first', 'fin']
```
Lucifer:
```
TypeError can't send non-None value to a just-started async generator
RuntimeError cannot reuse already awaited __anext__()/asend()
['first']
```


