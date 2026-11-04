---
id: luc-dawf
status: open
deps: []
links: []
created: 2026-11-04T18:03:10Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-bmfl
---
# M1.3d: sweep test_capi until the whole suite runs to completion

After luc-l9w1 the whole test_capi run dies in test_build (stub _Py_GetBuiltWithAssert). Sweep onward module by module in the order the worker reaches them: clear what kills the worker, then bring each module to oracle parity, matching CPython 3.14 source.

Acceptance: tools/libtest run --python bin/lucifer --modules test_capi runs to completion with no worker death; report its PASS count against the oracle's 1075 (M1.3 target 538); per-module counts; abi-audit stub count drops.

