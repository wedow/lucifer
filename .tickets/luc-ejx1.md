---
id: luc-ejx1
status: open
deps: []
links: []
created: 2026-11-04T05:56:58Z
type: task
priority: 2
---
# libtest: fail loudly when the oracle test extensions are not linked (make oracle-exts)

Review of luc-00js: on a clean tree nothing creates the _testinternalcapi/_testlimitedcapi links, so test_class silently drops 31 PASS. Either make the libtest runner refuse to run without them, or have the run create them.

