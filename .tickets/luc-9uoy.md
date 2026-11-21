---
id: luc-9uoy
status: closed
deps: []
links: []
created: 2026-11-11T04:18:01Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# array('f') rejects a double beyond float range instead of storing inf

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): sequences_diff, 31 cases plus about 54 where the generator lost sync over seeds 1 and 2 at 1,000 cases each.

```python
from array import array
for v in (1e300, -1e300, 5.47e253):
    try:
        print(array('f', [v]).tolist())
    except Exception as e:
        print(type(e).__name__, e)
```
CPython 3.14t:
```
[inf]
[-inf]
[inf]
```
Lucifer:
```
OverflowError float too large to pack with f format
OverflowError float too large to pack with f format
OverflowError float too large to pack with f format
```



## Notes

**2026-11-21T16:41:41Z**

CPython narrows these values with a native C float cast (Modules/arraymodule.c and Python/getargs.c). Array construction and mutation now use that cast when packing float32 items. Standard-size struct packing and overflow during conversion to double retain their exceptions. The new tests/kernel/array_float_overflow.py check reproduced the old failure, then passed with the six neighboring checks in plain, jit, and jit-deopt.
