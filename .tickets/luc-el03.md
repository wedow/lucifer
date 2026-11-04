---
id: luc-el03
status: open
deps: [luc-h6r9]
links: []
created: 2026-11-04T08:00:42Z
type: feature
priority: 1
assignee: Greg Wedow
parent: luc-bmfl
---
# M1.3b: test_capi list, tuple, set, dict at oracle parity

Implement the C-API that test_capi.test_list, test_tuple, test_set and test_dict exercise (PyList_*, PyTuple_*, PySet_*/PyFrozenSet_*, PyDict_*), matching CPython 3.14t source on references, errors and free-threaded critical sections.

Acceptance: every test the oracle passes in those four modules passes under tools/libtest; no oracle-PASS row elsewhere regresses; abi-audit stub count drops.

