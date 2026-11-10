---
id: luc-chbr
status: closed
deps: []
links: []
created: 2026-11-10T14:19:17Z
type: bug
priority: 2
tags: [threads, signals]
---
# tests/kernel/park_locks.py hangs about one run in ten

On master (2b2069c's runtime) and on luc-custom2 alike, park_locks.py hangs in about 1 of 10 runs (master 9/10, luc-custom2 13/16 passing, the bench host loaded by other work); make check's plain lane failed on it once. A hung run's main thread is parked for good in prim_mutex_acquire -> _PyMutex_LockTimed -> _PyParkingLot_Park -> lu_park -> lu_futex_wait, called from compiled code (h_call). The test's only unbounded wait is SimpleQueue.get() that a 0.05 s SIGALRM handler is meant to interrupt, so the likely cause is a signal that arrives as the task parks on the queue's lock and does not wake it.


## Notes

**2026-11-10T16:43:49Z**

Root cause: a signal tripped before the lock wait's lu_park read the carrier's wake word was lost. Its C handler set the signals reason and bumped the wake word, but lu_park looked at the reason only after a sleep returned, and the sleep never returned: the wake was already spent. The window is wide in lucifer because SimpleQueue.get is a kernel builtin, whose frames defer Python handlers to the next poll outside the builtin, so a SIGALRM landing anywhere in get's body before the park (a loaded host stretches 0.1 ms to the 50 ms timer) went unseen. Hung core: signals.tripped=1, SIGALRM tripped=1, main carrier poll=SIGNALS_PENDING, carrier asleep in lu_futex_wait under prim_mutex_acquire. Fix: a wait whose caller runs handlers (lu_wait.signals: _PY_LOCK_HANDLE_SIGNALS through the parking lot, time.sleep) checks the reason before every sleep, the first included; other waits no longer end at signals at all; PyErr_CheckSignals clears the reason so sleep re-parks. Test: tests/capi/poll.c trips SIGUSR1 then waits on a held PyMutex with HANDLE_SIGNALS (timed out after 2 s before, PY_LOCK_INTR now). A 3000-iteration setitimer sweep around lock.acquire/SimpleQueue.get hung every run before and passes now; park_locks 200/200.
