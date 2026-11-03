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

