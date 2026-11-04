---
id: luc-ey0x
status: open
deps: []
links: []
created: 2026-11-04T02:21:57Z
type: bug
priority: 1
---
# Interpreter hot path pays ~4% for PEP 669 prev_instr tracking with no tool active

Review of luc-2vif: DISPATCH() now does prev_instr = cur_instr; cur_instr = next_instr on every instruction so INSTRUMENTED_LINE and the NOT_TAKEN/POP_ITER events know their predecessor. hot.py: 1.270 s -> 1.323 s, +3.8% instructions; removing the two moves restores the base. Fix: record only real control transfers (jumps, unwinds) and derive the sequential predecessor's line from the line table, as CPython does. Also note that opcode tracing is ~10x the oracle on one thread.

