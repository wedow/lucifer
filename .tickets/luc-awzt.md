---
id: luc-awzt
status: open
deps: [luc-a0sq, luc-ahq7, luc-abar]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, sonnet]
---
# K1u: opcode closure, remaining programs and every lane green

Close K1: kernelcheck --opcodes lists every base opcode (< 129, excluding instrumented) that occurs in the C0 kernel, closure and difftest corpora and checks each has a handler; the missing ones land here (MATCH_CLASS, MATCH_MAPPING, MATCH_SEQUENCE, MATCH_KEYS, EXTENDED_ARG, ENTER_EXECUTOR placeholder, LOAD_FAST_BORROW forms, anything left). Fill tests/kernel to at least 30 programs covering every DESIGN 9.3 family; programs added here are written first and shown failing.
Implements: DESIGN 6.1 (all base opcodes), 8 (K1 rung), 9.3 (tests/kernel families).
Files: tools/kernelcheck (--opcodes).
Adds tests/kernel: match_stmt.py, scoping.py (global/nonlocal/del/walrus/class scope), control_flow.py (chained comparisons, assert, loops with else, break/continue in try/finally).

Builder: sonnet.

## Acceptance Criteria

make check-kernel: at least 30 programs, all matching the oracle plain, with LANE=handoff and with LANE=handoff:1 ... :8; kernelcheck --opcodes reports 0 unhandled base opcodes; make check passes.

