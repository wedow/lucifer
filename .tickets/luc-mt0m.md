---
id: luc-mt0m
status: open
deps: []
links: []
created: 2026-11-06T01:34:28Z
type: bug
priority: 3
---
# A heap type's refcount seen from a C tp_finalize that reassigns __class__ is one too high

test_capi.test_misc CAPITest.test_c_subclass_of_heap_ctype_with_del_modifying_dunder_class_only_decrefs_once: the oracle's _testcapi HeapCTypeSubclassWithFinalizer finalizer reads Py_REFCNT of the old type after setting __class__ and records 7 on the free-threaded oracle, 8 on lucifer. Something in lucifer's dealloc/finalize path (subtype_dealloc, PyObject_CallFinalizerFromDealloc, or the __class__ setter) holds an extra reference to the old type while the finalizer runs.


## Notes

**2026-11-06T05:42:09Z**

Measured: the free-threaded oracle's heap types are deferred (refcount near 2**60) and an instance's reference to its type does not show in sys.getrefcount (3.14's per-thread heap-type refcounts, _Py_INCREF_TYPE). In the oracle: before 1152921504606846982, with an instance the same, in the finalizer one less. lucifer counts the instance's reference (7 -> 8 with an instance). Parity needs per-thread type refcounting; the test compares deltas, so the instance's +1 is what differs.
