---
id: luc-0u5x
status: open
deps: []
links: []
created: 2026-11-03T09:49:14Z
type: task
priority: 2
tags: [runtime, kernel, opus]
---
# Unpinned parks for blocking primitives, timers and idle-carrier merges

K1r parks the main task only at eval-loop safepoints. lu_park is a C function, so os.sleep, mutex acquisition and the parking lot still sleep in place on the carrier futex (detached). To move them off the carrier, make them frame primitives that return a park request to the loop, give unpinned waits a deadline queue per carrier (lu_wait.deadline is unused for them), and merge what was queued to a carrier just before it went idle (dealloc needs a tstate; those objects wait for the next task bound there, while later frees of an idle carrier's objects merge in the queuing thread).

