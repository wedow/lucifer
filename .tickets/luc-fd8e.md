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

