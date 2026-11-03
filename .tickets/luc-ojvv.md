---
id: luc-ojvv
status: open
deps: [luc-a0sq]
links: []
created: 2026-11-03T09:39:23Z
type: feature
priority: 0
tags: [runtime, threads]
---
# _thread.start_new_thread, _thread.lock/RLock over PyMutex, and threading.Thread on one task per carrier (1:1 until M1.8)

Found by luc-wg2d: lucifer cannot start a Python thread. Each new thread is a task bound to its own new carrier via the K1p/K1r binding path. Joining, daemon threads, threading.local per task, and _thread.get_native_id follow CPython 3.14t. Signal handlers stay on the main thread.

## Acceptance Criteria

tests/kernel threads_basic.py (start, join, locks, Condition, Event, Barrier, threading.local, daemon exit, exceptions in threads via threading.excepthook) matches the oracle plain and under handoff; the luc-wg2d signal-during-sort case gets a kernel test that matches the oracle

