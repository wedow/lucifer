---
id: luc-00js
status: closed
deps: [luc-74bf]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 1
parent: luc-nsmh
tags: [milestone, stdlib]
---
# M1.1: libtest core-type modules at 90% of oracle passes

Run regrtest under bin/lucifer with its -j 1 worker: subprocess via a native fork_exec primitive, unittest, faulthandler, tempfile; then fix core-type failures.

## Acceptance Criteria

tools/libtest/run.py run --python bin/lucifer --modules test_int test_long test_float test_bool test_list test_tuple test_dict test_set test_str test_bytes test_types test_class test_descr test_exceptions test_generators test_scope test_grammar: at least 1,902 PASS (90% of the oracle's 2,113).
- Foundations: tools/libtest run --modules test_faulthandler at least 90% of the oracle's 44 PASS; faulthandler.dump_traceback_later(0.2) during a busy loop prints the loop's frame (docs/FOUNDATIONS.md 2.12).

## Notes

**2026-11-04T04:43:03Z**

libtest core-type modules: 1953 PASS of the oracle's 2113 (92.4%, bar 1902); test_faulthandler 40 of 44 (the 4 others need _testcapi). test_bytes and test_class need make oracle-exts. Remaining failures are per-test (descr 40, bytes 23, class 9, types 9, str 12, exceptions 6, generators 8); loading _testcapi aborts on PyType_Ready, left to luc-bmfl.
