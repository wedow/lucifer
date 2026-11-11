---
id: luc-qgzl
status: open
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# _random.Random.__new__ seeds the generator; setstate messages differ

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): corelibs_diff fns rand.new, csetstate, sys_getrandbits, 8 cases over seeds 1 and 2 at 1,000 cases each.

```python
import _random
r = _random.Random.__new__(_random.Random)
print(r.getrandbits(32), r.getstate()[-1])
```
CPython 3.14t:
```
0 1
```
Lucifer:
```
228275485 1
```

```python
import random
r = random.Random(1)
v, st, g = r.getstate()
for bad in [None, 2**63]:
    s = list(st); s[3] = bad
    try: random.Random.setstate(r, (3, tuple(s), g))
    except Exception as e: print(type(e).__name__, e)
try: random.Random.getrandbits(r, 2**70)
except Exception as e: print(type(e).__name__, e)
```
CPython 3.14t:
```
TypeError an integer is required
OverflowError Python int too large for C uint64_t
```
Lucifer:
```
TypeError state vector items must be integers
OverflowError Python int too large to convert to C unsigned long
OverflowError Python int too large for C uint64_t
```

CPython leaves the state zeroed until __init__ seeds it.


