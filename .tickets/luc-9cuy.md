---
id: luc-9cuy
status: closed
deps: []
links: []
created: 2026-11-11T02:46:58Z
type: bug
priority: 2
tags: [conformance, fuzzdiff]
---
# SystemError from bad C-API arguments lacks CPython's 'file:line:' prefix

Found by `make fuzzdiff` (tools/fuzzdiff, python-formalization case generators): dictset_diff, 5 cases over seeds 1 and 2 at 1,000 cases each (the generator parses the prefix).

```python
import _testlimitedcapi as tl
try:
    tl.dict_setitem([], 1, 2)
except Exception as e:
    print(type(e).__name__, e)
```
CPython 3.14t:
```
SystemError Objects/dictobject.c:2722: bad argument to internal function
```
Lucifer:
```
SystemError bad argument to internal function
exit 0
```

An implementation detail; low priority.



## Notes

**2026-11-21T11:35:51Z**

The dict item setter and deleter used a bare SystemError for a non-dict target. They now use the C-API error formatter with the call sites from CPython 3.14.10. The deleter reuses dict key hashing to report a hash failure before rejecting the target, as CPython does. [Objects/dictobject.c](https://github.com/python/cpython/blob/v3.14.10/Objects/dictobject.c#L2719-L2856)

The kernel regression exercises both entry points and their string-key wrappers. It removes the oracle's absolute build-directory prefix while comparing the source file, line number, and exception text. It failed before the fix and passed afterward in plain, jit, and jit-deopt along with four neighboring dict checks in each lane. The ABI audit passed.
