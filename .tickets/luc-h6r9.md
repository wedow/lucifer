---
id: luc-h6r9
status: open
deps: []
links: []
created: 2026-11-04T08:00:42Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-bmfl
---
# M1.3a: import oracle-built _testcapi and _testlimitedcapi

Loading the oracle-built _testcapi aborts in PyType_Ready (luc-00js notes). Make both modules import unmodified under bin/lucifer.

Acceptance: a gate check imports _testcapi and _testlimitedcapi under oracle and bin/lucifer and compares sorted(dir(m)) (oracle-exts built); tools/libtest run --modules test_capi.test_list test_capi.test_tuple reports per-test results instead of an import error. Report the libtest PASS counts for all of test_capi before/after, and abi-audit stub counts.

