---
id: luc-4uge
status: open
deps: []
links: []
created: 2026-11-17T16:04:22Z
type: bug
priority: 2
---
# test_racing_set_dict and test_local_refs fail in every mode on master

test.test_free_threading.test_dict TestDict.test_racing_set_dict (a replaced f.__dict__ outlives gc.collect(): AssertionError {} is not None) and test.test_threading_local PyThreadingLocalTest.test_local_refs (0 of 20 locals dead) fail on master 1d79772 with -X lucifer_jit=off as well as on, jit and deopt, through tools/floor libtest and ft-cpython units and through -m unittest. results/floor/off has both PASS, and an older build (Nov 14) passes them, so an interpreter change since then lets replaced objects live past their last reference (delayed frees?); 6389c80 'process delayed frees only with a task attached' is a suspect, unverified. Not the JIT constant-reference bug luc-eih1 named them as (they fail with the JIT off).

