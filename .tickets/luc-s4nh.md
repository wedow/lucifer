---
id: luc-s4nh
status: open
deps: []
links: []
created: 2026-11-11T03:08:31Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# coroutine.cr_origin stays None with origin tracking enabled

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytecode_gen_diff fn gen_origin, 19 cases over seeds 1 and 2 at 1,000 cases each.

```python
import sys, threading
async def co(): pass
def f(n):
    return f(n - 1) if n > 1 else co()
def target():
    sys.set_coroutine_origin_tracking_depth(4)
    c = f(4)
    print(c.cr_origin and [x[2] for x in c.cr_origin])
    c.close()
t = threading.Thread(target=target); t.start(); t.join()
```
CPython 3.14t:
```
['f', 'f', 'f', 'f']
```
Lucifer:
```
None
```

sys.set_coroutine_origin_tracking_depth only stores the depth.


