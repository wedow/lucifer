---
id: luc-5svj
status: open
deps: []
links: []
created: 2026-11-06T02:44:23Z
type: feature
priority: 2
---
# datetime C API types that are the datetime classes themselves, static as in CPython

The oracle's _testcapi test_datetime_capi asserts (assert-enabled: Modules/_testcapi/datetime.c:30, assert(!PyType_HasFeature(PyDateTimeAPI->DateType, Py_TPFLAGS_HEAPTYPE)), and likewise Time/DateTime/Delta/TZInfo) that the capsule's types are not heap types. lucifer's datetime classes are _pydatetime's heap classes, so the assert aborts the libtest worker in test_capi.test_misc Test_testcapi. A sweep stand-in that rebased the classes onto static C bases was rejected: it adds _date_base etc. to every datetime class's __mro__. The fix must keep datetime.__mro__ == (datetime, date, object) for every class, e.g. by building the datetime types as static types (kernel-style) that carry _pydatetime's behaviour.

## Acceptance Criteria

datetime class MROs equal the oracle's; _testcapi.test_datetime_capi() returns None; PyDate_Check and friends hold for instances.

