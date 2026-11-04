---
id: luc-el03
status: closed
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


## Notes

**2026-11-04T16:08:21Z**

Done at 2ecc868 (master 7c56c6d merged at da30442, no conflicts after the lock-free list read was reconciled): the four modules at oracle parity through the libtest bridge - test_list 20/20, test_tuple 14/14, test_set 17/17, test_dict 28/28. container_refs added to the capi gate (concurrent *Ref reads), handoff lane identical. Whole-suite worker now passes everything up to test_abstract and dies at test_mapping_getitemstring:399 (NULL-family segfault) - next ticket.
