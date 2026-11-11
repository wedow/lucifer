---
id: luc-c6sa
status: closed
deps: []
links: []
created: 2026-11-11T04:20:08Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# int.to_bytes with a huge length: wrong message and argument check order; math.perm/comb with k beyond Py_ssize_t hang instead of OverflowError

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): mathint_diff fns to_bytes (21 cases), perm (9), comb (2) over seeds 1 and 2 at 500 cases each (perm/comb hit the 30 s bound).

```python
for length, order in ((2**63 - 2, 'little'), (2**63, 'big'), (-2**63 - 1, 'mid'), (2**63, 'mid')):
    try:
        print((20).to_bytes(length, order))
    except Exception as e:
        print(length, order, type(e).__name__, e)
```
CPython 3.14t:
```
9223372036854775806 little OverflowError byte string is too large
9223372036854775808 big OverflowError Python int too large to convert to C ssize_t
-9223372036854775809 mid OverflowError Python int too large to convert to C ssize_t
9223372036854775808 mid OverflowError Python int too large to convert to C ssize_t
```
Lucifer:
```
9223372036854775806 little OverflowError int too big to convert
9223372036854775808 big OverflowError Python int too large to convert to C long
-9223372036854775809 mid ValueError byteorder must be either 'little' or 'big'
9223372036854775808 mid ValueError byteorder must be either 'little' or 'big'
exit 0
```

```python
import math
for f in (math.perm, math.comb):
    try:
        print(f(2**64 + 1, 2**63))
    except Exception as e:
        print(f.__name__, type(e).__name__, e)
```
CPython 3.14t:
```
perm OverflowError k must not exceed 9223372036854775807
comb OverflowError min(n - k, k) must not exceed 9223372036854775807
```
Lucifer:
```
exit 124
```


