---
id: luc-2vif
status: open
deps: [luc-a5u6]
links: []
created: 2026-11-03T02:15:01Z
type: feature
priority: 1
parent: luc-nsmh
tags: [runtime, tracing]
---
# M0.3: PEP 669 instrumentation in the interpreter; settrace/setprofile over it

Instrumented opcodes, per-code monitoring data and version checked at RESUME, legacy tracing as Python/legacy_tracing.c, global event changes by stop-the-world, per-task tracing as a targeted event (FOUNDATIONS 2.3).

## Acceptance Criteria

tools/libtest run --modules test_sys_settrace test_sys_setprofile test_monitoring: at least 90% of the oracle's PASS rows (448/29/89). tests/kernel/trace_midloop.py (settrace and sys.monitoring LINE/BRANCH enabled mid-loop from the same and another thread) matches the oracle.


## Notes

**2026-11-03T23:49:32Z**

Progress (branch luc-2vif). Done: PEP 669 in the interpreter (instrumented opcodes, per-code monitoring data, version check at RESUME, global changes under stop-the-world with executing code re-instrumented), sys.monitoring, settrace/setprofile over it (legacy_tracing.c), f_lineno setter, monitoring C-API (EnterScope/Fire*), co_branches. trace_midloop.py matches the oracle plain and under handoff; check-kernel plain 478/478 and handoff (6 shards) green; check-capi (with the new capi_monitoring extension), check-globals, abi-audit, check-embed, check-compiler green. Libtest: test_sys_setprofile 27/29 (93%); test_sys_settrace 418/448 (93%) only with --timeout 1100, because test_jump_extended_args_for_iter execs a 100k-line function that the Python compiler takes ~12 min to compile (oracle 1.8 s), so the default 180 s module timeout marks all 448 rows TIMEOUT; test_monitoring 42/89: the 9 rows that need _testcapi/_testinternalcapi cannot run, and the first TestCApiEventGeneration test skips in setUp after taking the tool ids, so every later class fails its setUp (needs M1.3, loading the oracle _testcapi). Not closing.
