---
id: luc-a0sq
status: open
deps: [luc-8dqq, luc-at0e, luc-dlik]
links: []
created: 2026-11-03T02:47:56Z
type: feature
priority: 0
parent: luc-vvqv
tags: [runtime, kernel, opus]
---
# K1r: -X lucifer_handoff: two carriers, run queue, park at safepoints

The production M1.8 path in miniature: under -X lucifer_handoff[=SEED] the runtime starts a second carrier and lifts the main task's lifetime pin; at every safepoint with pin == 0 the task parks with its frames published, the eval loop returns to the carrier loop, and lu_unpark queues it on the other carrier, which binds it (K1p) and re-enters the loop at its top frame. With =SEED each safepoint parks or continues by a seeded choice. Unpinned lu_park returns to the carrier loop instead of sleeping in place; a pinned task (nested eval, foreign C, lifetime pin) still blocks its carrier. Migration-point invariants asserted: no C frame, no critical section, no open QSBR read interval. _thread.get_native_id() reads the current carrier. kernelcheck LANE=handoff and LANE=handoff:1..8 run every program.
Implements: DESIGN 5.4 (migration points), 5.5 (unpinned park), 5.8; FOUNDATIONS 2.7 (switch/sched are this flag).
Files: runtime/thread/sched.c (run queue, carrier loop), runtime/thread/park.c (unpinned path), tools/kernelcheck (lanes).
Adds tests/kernel: handoff_witness.py (distinct get_native_id() over 1,000 loop iterations; the runner's one documented exception expects 2 under the handoff lanes).

Builder: opus.

## Acceptance Criteria

make check-kernel LANE=handoff and LANE=handoff:1 ... handoff:8 match the oracle for every program present; handoff_witness prints 1 plain and 2 under each handoff lane.


- From K1t: specialized LOAD_ATTR caches keep a descriptor pointer alive only through the type version guard. With two carriers, reclaim it through QSBR, or hold a reference.
