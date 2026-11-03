---
id: luc-pfq6
status: open
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

