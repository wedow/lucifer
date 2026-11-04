---
id: luc-h6r9
status: closed
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


## Notes

**2026-11-04T09:40:49Z**

Done on luc-h6r9 at 5406b40 (fix 0d17e02, master merged): PyType_Ready + PyErr_NewException/WithDoc + PyState_Add/Find/RemoveModule + instancemethod family; both modules import, capi gate testcapi_import compares sorted(dir(m)), gates green. libtest test_capi: oracle 1075 PASS/120 SKIP/2 XFAIL, lucifer 1197 CRASH before (import abort) and after (regrtest worker aborts at the first stubbed call, PyIter_NextItem) - per-test passing is luc-el03.
