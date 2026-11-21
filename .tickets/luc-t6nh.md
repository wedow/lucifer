---
id: luc-t6nh
status: closed
deps: []
links: []
created: 2026-11-21T07:04:47Z
type: bug
priority: 3
tags: [fork, signals, threads]
---
# os.fork: a signal handler that raises between _imp.acquire_lock() and the fork leaks the import lock

Review of 48331397 (the structure predates it). posix.fork does _imp.acquire_lock(), then 'held = _imp._lock_depth() - 1', then 'try: pid = os_fork()'. A signal handler runs at the poll between those statements; one that raises (KeyboardInterrupt) leaves the import lock held by the forking thread with no release, and every later import in another thread blocks for ever. CPython takes and drops the lock in C (PyOS_BeforeFork / PyOS_AfterFork_Parent), with no poll between. Fix direction: take the lock, read its depth and fork in one primitive call, or put the acquire inside the try with a release that only runs if this call took it (rlock_release returns the holds given up). Repro needs a timer signal raising in a fork loop with another thread importing; not written.


## Notes

**2026-11-21T08:55:37Z**

Fixed: os_fork(lock) is one primitive that takes the import lock uninterruptibly, forks and gives the parent's hold back (child: the lock keeps only its own holds), as PyOS_BeforeFork/AfterFork_*; _imp._lock_depth and _reinit_lock are gone. The child also clears pending signals as _PySignal_AfterFork does: without it a KeyboardInterrupt pending at fork ran in the child too. Regression tests/kernel/fork_import_lock_interrupt.py: base fails (interrupt before the fork, lock left held), candidate passes plain, jit, jit-deopt with the fork, import-lock and condition neighbours (jit_reclaim_fork's jit-deopt miss is luc-rf7k, same on base).
