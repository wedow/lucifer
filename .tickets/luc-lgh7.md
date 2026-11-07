---
id: luc-lgh7
status: open
deps: []
links: []
created: 2026-11-07T14:32:49Z
type: task
priority: 3
---
# JIT: INLINE_LOAD's unboxed slow path publishes no frame state

lu_inline_get can park in the object's critical section, where another task's stop-the-world walker finds the frame without a published state (locals read as unbound). INLINE_LOAD is not IF_CALL, so the locals may be in registers there: publishing needs the registers saved where the description can name them (a shared stub that saves every register, as the exits' common path does). luc-15mf (values in registers across releases) needs the same mechanism for the release slow paths once DECREF stops being IF_CALL.

