---
id: luc-pfq6
status: closed
deps: [luc-86vo, luc-00js]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 1
parent: luc-nsmh
tags: [milestone, numpy]
---
# M1.4: import numpy and collect its suite

The 411 Py symbols the NumPy 2.5.4 cp314t wheel imports, ctypes/_ctypes, math, struct, pickle, datetime, the 376-module pytest+numpy.testing closure.

## Acceptance Criteria

tools/numpytest/run.py run --collect --python bin/lucifer: import numpy and pytest collection succeed, rows leave NOTRUN, at least 1 PASS.


## Notes

**2026-11-04T10:42:05Z**

done: import numpy (unmodified cp314t wheel) works; tools/numpytest/run.py run --collect --python bin/lucifer collects the oracle's 52,273 IDs with the same hash (first run 25 min, pytest rewrites every test file; cached runs minutes); six shard groups: 2,184 PASS, 16 FAIL, 1 TIMEOUT of 52,273 (4.18%). C-API stubs 1058 -> 850 (numpy's 346 imported functions: 100 stubs -> 0). New: PyType_Ready slot wrappers, zlib/_ctypes/mmap/pwd/grp/fcntl/_operator/_tracemalloc, -m.
