---
id: luc-00js
status: open
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
