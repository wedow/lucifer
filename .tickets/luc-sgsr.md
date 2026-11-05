---
id: luc-sgsr
status: closed
deps: []
links: []
created: 2026-11-05T04:06:43Z
type: task
priority: 2
---
# threads_signal_sort, park_timeouts and fork_threads depend on thread timing; a redirected signal runs its Python handler twice


## Notes

**2026-11-05T12:11:05Z**

threads_signal_sort/park_timeouts/fork_threads: gates are bare locks released inside the try, park repeats its signal, fork runs single-threaded first. Runtime: redirected signal tripped twice (handler ran twice under handoff); join after an interrupted acquire/release left the handle lock held (hang). 40 runs x 6 concurrent, before/after: sort handoff:3 10/40 -> 0/40; others 0 after on plain, jit, jit-deopt, handoff:3.
