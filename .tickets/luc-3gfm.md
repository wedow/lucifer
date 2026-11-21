---
id: luc-3gfm
status: open
deps: []
links: []
created: 2026-11-04T13:19:51Z
type: bug
priority: 2
parent: luc-h4gc
---
# LOAD_FAST_BORROW takes a strong reference: Py_REFCNT of a local passed to a call is one too high

CPython 3.14 lends a fast local to the call that uses it, so d.resize(50) sees Py_REFCNT(d) == 1. Extensions inline that test (PyUnstable_Object_IsUniquelyReferenced is a static inline in the cp314t headers), so no runtime hook can answer it: NumPy's ndarray.resize refuses with 'may be referenced by another object' (10 numpytest rows: TestResize, TestSizeOf resize). Needs borrowed stack references in the interpreter, or a deferred-count design that keeps a loaned local invisible.


## Notes

**2026-11-06T01:34:14Z**

From luc-dawf: test_capi.test_object CAPITest.test_is_unique_temporary asserts sys.getrefcount(x) == 1 for a parameter (gh-130704); lucifer gives 2. PyUnstable_Object_IsUniqueReferencedTemporary itself now scans the calling frame's value stack.

## Review 11-21: blocked, branch luc-3gfm kept
Refcount balance is right, but lu_frame_has_borrows refuses compiled entry for any frame holding a borrowed slot (jit.c lu_jit_enter/lu_jit_run), and lu_frame_call keeps borrowed args borrowed, so any f(x) with x a caller local never runs compiled; h_call/h_call_kw exit on borrowed args every call. Measured: call loop 0.037 s -> 1.717 s (46x), JIT entries 4732 -> 27. Rework: the JIT must accept borrowed slots (take ownership at entry or track them), then a perf A/B.
