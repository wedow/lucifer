---
id: imp-j1nc
status: closed
deps: []
links: []
created: 2026-11-17T07:04:14Z
type: bug
priority: 1
---
# compiled generator leaks the items of a tuple it makes real to yield

A generator unit that yields a tuple holding what an inlined generator yielded (a tuple compiled code never made) leaks a reference to its items: yield_value made the values real and then dropped the pending releases. multiprocessing.pool._guarded_task_generation leaked every task's arguments (ThreadPool.map kept 9 of 10 objects alive). Fix: settle releases before the yield. Test: tests/kernel/jit_yield_releases.py.

