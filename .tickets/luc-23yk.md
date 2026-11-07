---
id: luc-23yk
status: open
deps: []
links: []
created: 2026-11-07T05:56:29Z
type: task
priority: 1
---
# JIT: frame state where compiled code runs Python outside a publishing call

luc-cmel publishes the root frame's state (jit_desc) only around helpers marked IF_PY and TRUTH. A finalizer or weakref callback run by a release slow path (DECREF, a store's old value), or a task stopped in a non-Python helper (a critical-section wait), finds no state: lu_frame_slot reads the locals as unbound and lu_frame_sync raises RuntimeError. Also, instructions whose Python-running call is fused with a branch (TRUTH before POP_JUMP) have no GUARD_FRAMES after them, so a walker's write there is seen only at the unit's next guard or exit. Publish through a shared cold stub that saves every register (as the exits' common path does) and give the release slow paths a frame state.


## Notes

**2026-11-07T15:37:37Z**

Branch luc-23yk. Every call during which a walker may see the root frame publishes its state: Python-running helpers as before, releases that may run a finalizer (on their slow path, with the locals after their instruction), stores that release an old value, helpers that may park in a lock (IF_SEEN), and the truth test, which now has a frames guard after TO_BOOL. These ops are all IF_CALL, so every local the state names is in a spill word during the call and no register needs saving (the register-saving stub is needed only where a value may be in a register: INLINE_LOAD's slow path, luc-lgh7, and luc-15mf). A frames guard whose instruction's only such calls are releases and stores at its end is not emitted: each of those calls checks the owner after itself and leaves at the guard's state, releasing what the instruction had yet to release. Bools only frame states hold are made by the exits from their bit (virtual), and the published state carries the instruction (lu_jit_instr), so a publication is one immediate and one store. Instructions per AWFY iteration vs master (core pair, PYTHONHASHSEED=0, 10 minus 5 iterations, two runs each): Richards -0.6%, DeltaBlue -0.2%, CD +0.8% (master's own runs differ 0.5%; totals with the collector off are equal). Test: tests/kernel/jit_release_frames.py. Gates: check, abi-audit, check-jit, check-harness, check-compiler all 0.
