---
id: luc-23yk
status: open
deps: []
links: []
created: 2026-11-07T05:56:29Z
type: task
priority: 2
---
# JIT: frame state where compiled code runs Python outside a publishing call

luc-cmel publishes the root frame's state (jit_desc) only around helpers marked IF_PY and TRUTH. A finalizer or weakref callback run by a release slow path (DECREF, a store's old value), or a task stopped in a non-Python helper (a critical-section wait), finds no state: lu_frame_slot reads the locals as unbound and lu_frame_sync raises RuntimeError. Also, instructions whose Python-running call is fused with a branch (TRUTH before POP_JUMP) have no GUARD_FRAMES after them, so a walker's write there is seen only at the unit's next guard or exit. Publish through a shared cold stub that saves every register (as the exits' common path does) and give the release slow paths a frame state.

