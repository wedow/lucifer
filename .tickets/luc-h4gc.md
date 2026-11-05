---
id: luc-h4gc
status: closed
deps: [luc-pfq6]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 1
parent: luc-nsmh
tags: [milestone, numpy]
---
# M1.5: numpytest 25%

Close numpy suite failures by census.

## Acceptance Criteria

tools/numpytest/run.py run --python bin/lucifer: at least 13,069 PASS of 52,273.


## Notes

**2026-11-04T19:29:19Z**

numpytest 50,835 PASS of 52,273 (97.25%), from 2,184; shard recipe in tools/numpytest/runall.py; open causes ticketed (typing/mypyc, cython compile, resize refcount, port functions that bind, cmath, release_buffer, ctypes formats)
