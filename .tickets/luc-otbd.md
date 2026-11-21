---
id: luc-otbd
status: closed
deps: []
links: []
created: 2026-11-21T06:18:23Z
type: bug
priority: 2
tags: [threads, signals, fidelity]
---
# Condition.wait: KeyboardInterrupt while taking the lock back leaves it unheld, and the with block raises RuntimeError

Review of 62aaa6bf (pre-existing: the old _acquire_restore used Lock.acquire). RLock._acquire_restore waits with _PY_LOCK_HANDLE_SIGNALS, so a raising handler ends the wait without the lock; Condition.wait propagates the KeyboardInterrupt, and the with block's release then raises RuntimeError('cannot release un-acquired lock'). CPython's rlock_acquire_restore uses _PyRecursiveMutex_Lock (PyMutex_Lock, not interruptible): the handler runs after the lock is back, so KeyboardInterrupt leaves the with block cleanly.

Repro: main does 'with cond: cond.wait(10)'; a helper takes cond, notifies, sleeps 0.2 s, calls _thread.interrupt_main(), sleeps 0.2 s, releases. CPython: KeyboardInterrupt (no context). lucifer, all lanes: RuntimeError with KeyboardInterrupt as context.
Fix: _acquire_restore waits without handling signals.


## Notes

**2026-11-21T06:21:48Z**

Fixed by 8fb53ff2 (_acquire_restore waits without handling signals). tests/kernel/condition_wait_interrupt.py: RuntimeError before / KeyboardInterrupt after, 10/10 per lane.
