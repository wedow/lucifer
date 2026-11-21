---
id: luc-4n3y
status: closed
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# functools.partial flattens a nested partial whose __dict__ is not empty

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): corelibs_diff fn functools.partial, 3 cases over seeds 1 and 2 at 1,000 cases each.

```python
import functools
def f(*a, **k): return a, k
inner = functools.partial(f, 1)
inner.x = 1
outer = functools.partial(inner, 2)
print(outer.func is inner, outer.args)
```
CPython 3.14t:
```
True (2,)
```
Lucifer:
```
False (1, 2)
```

CPython flattens only when the inner partial has an empty __dict__.



## Review 11-21: rework before landing, branch luc-4n3y kept
Behaviour matches CPython's partial_new. Two changes needed: (1) do not edit vendored Lib/functools.py; put the fix in a lib/modules/_functools accelerator as CPython does (DESIGN says vendored Lib/ is unchanged); (2) LU_ATTRS_DELETED takes ob_flags bit 3 in CPython's low byte and is defined locally in attrs.c; define it in lucifer.h beside the other LU_ATTRS_* bits in a free bit.

## Notes

**2026-11-21T22:46:58Z**

Reworked the fix in lib/modules/_functools.py. Only a partial without an instance dictionary is flattened. An existing empty dictionary still prevents flattening, including after clearing, deleting the last attribute, or restoring from pickle. The inline deletion flag uses the runtime-owned upper byte of ob_flags. Classes with more than 31 inline names use the existing fixed-capacity layout. The regression failed on the base against CPython 3.14t. The regression and seven nearby kernel tests passed in plain, jit, and jit-deopt.
