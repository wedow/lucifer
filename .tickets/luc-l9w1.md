---
id: luc-l9w1
status: open
deps: []
links: []
created: 2026-11-04T16:37:14Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-bmfl
---
# M1.3c: test_capi.test_abstract at oracle parity

After luc-el03 the whole test_capi run gets through test_dict and the worker dies in test_abstract (test_mapping_getitemstring, a NULL-argument segfault). Implement/fix the abstract object, mapping, sequence, number and iterator C-API (Objects/abstract.c) that test_capi.test_abstract exercises, matching CPython 3.14 on NULL arguments, errors and references.

Acceptance: every oracle-PASS test in test_capi.test_abstract passes; the whole-suite tools/libtest run --modules test_capi gets past test_abstract (report where it dies next, or the PASS count if it completes); abi-audit stub count drops.

