---
id: luc-3dgq
status: open
deps: []
links: []
created: 2026-11-11T02:42:33Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# StopIteration raised by map's function escapes map.__next__ instead of ending iteration

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): accel_diff fn merge, 1 case over seeds 1 and 2 at 1,000 cases each.

```python
class R:
    def __iter__(self): raise StopIteration
def f(x): raise StopIteration
def g():
    for x in map(iter, [R()]):
        print("body", x)
    return
    yield
def h():
    for x in map(f, [1]):
        print("body", x)
    yield 1
try: print(list(g()))
except Exception as e: print("g", type(e).__name__, e)
try: print(list(h()))
except Exception as e: print("h", type(e).__name__, e)
try:
    for x in map(f, [1]): print("body")
    print("loop ended")
except Exception as e: print("plain", type(e).__name__, e)
try: print(list(map(f, [1])))
except Exception as e: print("list", type(e).__name__, e)
```
CPython 3.14t:
```
[]
[1]
loop ended
[]
```
Lucifer:
```
g RuntimeError generator raised StopIteration
h RuntimeError generator raised StopIteration
plain StopIteration 
list StopIteration 
```


