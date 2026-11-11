---
id: luc-oaxn
status: open
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# bytes % with %e/%f/%g on a non-float, and join with a huge __length_hint__, raise other errors

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): bytesobj_diff, 21 cases over seeds 1 and 2 at 1,000 cases each.

```python
class O: pass
for conv in "dixeEfFgGc":
    try:
        print(conv, b"%" + conv.encode() % O() if False else (b"%" + conv.encode()) % O())
    except Exception as e:
        print(conv, type(e).__name__, e)
class Hint:
    def __init__(self): self.it = iter([1])
    def __iter__(self): return self
    def __next__(self): return next(self.it)
    def __length_hint__(self): return 2**70
try:
    bytearray(b"ab").join(Hint())
except Exception as e:
    print("join", type(e).__name__, e)
```
CPython 3.14t:
```
d TypeError %d format: a real number is required, not O
i TypeError %i format: a real number is required, not O
x TypeError %x format: an integer is required, not O
e TypeError float argument required, not O
E TypeError float argument required, not O
f TypeError float argument required, not O
F TypeError float argument required, not O
g TypeError float argument required, not O
G TypeError float argument required, not O
c TypeError %c requires an integer in range(256) or a single byte, not O
join OverflowError Python int too large to convert to C ssize_t
```
Lucifer:
```
d TypeError %d format: a real number is required, not O
i TypeError %i format: a real number is required, not O
x TypeError %x format: an integer is required, not O
e TypeError must be real number, not O
E TypeError must be real number, not O
f TypeError must be real number, not O
F TypeError must be real number, not O
g TypeError must be real number, not O
G TypeError must be real number, not O
c TypeError %c requires an integer in range(256) or a single byte, not O
join TypeError sequence item 0: expected a bytes-like object, int found
```


