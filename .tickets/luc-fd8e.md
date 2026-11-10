---
id: luc-fd8e
status: open
deps: [luc-tgv3, luc-86vo, luc-00js]
links: []
created: 2026-11-03T02:15:01Z
type: feature
priority: 1
parent: luc-nsmh
tags: [runtime, subinterpreters]
---
# M1.6a: subinterpreters API: _interpreters, concurrent.interpreters, Py_mod_multiple_interpreters

Expose interpreter creation to Python over the K1.1 path; cross-interpreter data and ExecutionFailed; the import-time Py_mod_multiple_interpreters check. NumPy's subinterpreter teardown test is oracle PASS, so the public bar needs it (FOUNDATIONS 2.1).

## Acceptance Criteria

Oracle PASS rows of test__interpreters, test__interpchannels, test_crossinterp and test_concurrent_futures.test_interpreter_pool pass; a tests/capi module declaring Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED imported in a subinterpreter prints the oracle's ExecutionFailed message; numpy TestSubinterpreterTeardown::test_subinterpreter_import_fails_cleanly PASS.


## Notes

**2026-11-06T01:34:14Z**

From luc-dawf: Py_NewInterpreter is an inert stand-in until this lands. It re-attaches the caller's thread state and returns a static thread state whose interp is a zeroed _is, so the oracle's assert-enabled _testcapi (test_atexit, test_current_tstate_matches, run_in_subinterp) does not crash the worker; code handed to it runs in the main interpreter. Py_EndInterpreter on it runs its PyUnstable_AtExit callbacks. Replace both (runtime/pylifecycle.c) with the real thing. test_capi.test_misc cases waiting on it: InterpreterConfigTests (4), InterpreterIDTests (14), SubinterpreterTest (3 + an XPASS), TestPendingCalls.test_isolated_subinterpreter, TestThreadState.test_gilstate_matches_current, Test_testcapi.test_current_tstate_matches; test_getargs test_gh_119213.

**2026-11-06T02:44:31Z**

Superseding the earlier luc-dawf note: the inert stand-in is gone. Py_NewInterpreter returns NULL (no exception), Py_EndInterpreter is a fatal error. Two oracle _testcapi entry points do not check the NULL and call PyThreadState_Get() with no thread state, a fatal error that kills the libtest worker: test_atexit (Modules/_testcapimodule.c ~2524-2528: PyThreadState_Swap(NULL); Py_NewInterpreter(); PyThreadState_Get()) and test_current_tstate_matches (~1190-1193). run_in_subinterp and the other callers handle NULL.

**2026-11-09T09:18:51Z**

Full Lib/test run 2026-11-09: 634 cases need subinterpreters: test_concurrent_futures fails to import in most of its modules (test_process_pool 119, test_shutdown 51, test_deadlock 48, test_wait 33, test_as_completed 25, ...) because test_concurrent_futures/util.py reads futures.InterpreterPoolExecutor at import; test_crossinterp 101, test__interpchannels 68, test__interpreters 66, test_interpreter_pool 37, plus 28 'subinterpreters required' skips.
