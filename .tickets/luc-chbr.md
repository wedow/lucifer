---
id: luc-chbr
status: open
deps: []
links: []
created: 2026-11-10T14:19:17Z
type: bug
priority: 2
tags: [threads, signals]
---
# tests/kernel/park_locks.py hangs about one run in ten

On master (2b2069c's runtime) and on luc-custom2 alike, park_locks.py hangs in about 1 of 10 runs (master 9/10, luc-custom2 13/16 passing, the bench host loaded by other work); make check's plain lane failed on it once. A hung run's main thread is parked for good in prim_mutex_acquire -> _PyMutex_LockTimed -> _PyParkingLot_Park -> lu_park -> lu_futex_wait, called from compiled code (h_call). The test's only unbounded wait is SimpleQueue.get() that a 0.05 s SIGALRM handler is meant to interrupt, so the likely cause is a signal that arrives as the task parks on the queue's lock and does not wake it.

