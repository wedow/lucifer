---
id: luc-74bf
status: open
deps: [luc-a5u6]
links: []
created: 2026-11-03T01:39:45Z
type: feature
priority: 1
parent: luc-nsmh
tags: [milestone]
---
# M0.2: difftest 164/164

Cycle collector (trial deletion over GC heaps), weakrefs and finalizers, async/coroutines, float formatting, traceback printing, metaclasses, memoryview, pattern matching, generated and AWFY lanes.

## Acceptance Criteria

tools/difftest run --python bin/lucifer: 164/164 PASS.
- Foundations: check-kernel passes under -X lucifer_stress=gc:1 and difftest 164/164 under gc:100; tests/kernel/resurrect.py (single-run __del__ resurrection, weakref callback order vs __del__, WeakValueDictionary under gc.collect) matches the oracle (docs/FOUNDATIONS.md 2.7, 2.14).
