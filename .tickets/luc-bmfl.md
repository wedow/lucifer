---
id: luc-bmfl
status: open
deps: [luc-00js, luc-86vo]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 2
parent: luc-nsmh
tags: [milestone, capi]
---
# M1.3: test_capi at 50% with oracle-built _testcapi

Load the oracle-built _testcapi and _testlimitedcapi (547 and 361 imported Py symbols) and implement the C-API they exercise.

## Acceptance Criteria

tools/libtest run --python bin/lucifer --modules test_capi: at least 538 PASS (50% of the oracle's 1,075).

