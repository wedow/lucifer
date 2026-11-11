---
id: luc-ol93
status: open
deps: []
links: []
created: 2026-11-11T02:47:17Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# json C-scanner recursion budget depends on Python frames and on JIT warm-up

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): json_diff fns loads_rec_c (63 cases), loads_rec_py (6) over seeds 1 and 2 at 1,000 cases each.

```python
import json, sys
def frame_depth():
    f, n = sys._getframe(1), 0
    while f is not None:
        n, f = n + 1, f.f_back
    return n
def run(s, frames):
    dec = json.JSONDecoder(strict=True)
    d = frame_depth()
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(d + 2 + frames)
    try:
        return dec.decode(s)
    except Exception as e:
        return type(e).__name__
    finally:
        sys.setrecursionlimit(old)
for warm in (0, 5000):
    for _ in range(warm): run("[1]", 100)
    print(warm, [run("[1]", f) for f in (0, 1, 2, 3)], run('"', 2))
```
CPython 3.14t:
```
0 [[1], [1], [1], [1]] JSONDecodeError
5000 [[1], [1], [1], [1]] JSONDecodeError
```
Lucifer:
```
0 [[1], [1], [1], [1]] RecursionError
5000 ['RecursionError', 'RecursionError', [1], [1]] RecursionError
exit 0
```

The pure-Python scanner path matches (json_loads_frames); the _json stand-in uses frames where C uses none.


