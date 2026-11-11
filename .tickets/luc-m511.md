---
id: luc-m511
status: closed
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 1
tags: [conformance, fuzzdiff]
---
# Segfault: throw() into a generator whose exception constructor re-enters it while finally does yield from

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_gen_diff fn gen_drive, 1 case over seeds 1 and 2 at 1,000 cases each, reduced.

```python
class ReInit(Exception):
    def __init__(self, *a):
        try: next(g)
        except BaseException as e: print('inner', type(e).__name__, e)
        super().__init__(*a)
def sub():
    yield 1
def f():
    try:
        yield 3
    finally:
        yield from sub()
g = f()
for i in range(2):
    try: print(g.throw(ReInit))
    except BaseException as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
1
inner ValueError generator already executing
ReInit 
```
Lucifer:
```
timeout: the monitored command dumped core
```

A memory-safety bug: CPython raises ValueError "generator already executing" inside the constructor. Likely the gen_throw primitive (runtime/interp/gen.c).


