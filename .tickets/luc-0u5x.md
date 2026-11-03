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

K1r parks the main task only at eval-loop safepoints. lu_park is a C function, so os.sleep, mutex acquisition and the parking lot still sleep in place on the carrier futex (detached). To move them off the carrier, make them frame primitives that return a park request to the loop, give unpinned waits a deadline queue per carrier (lu_wait.deadline is unused for them), and let an idle carrier merge its biased-refcount queue (dealloc needs a tstate; today the merge waits for the next task bound there and the idle loop only runs QSBR frees).

