---
id: luc-yi7s
status: open
deps: []
links: []
created: 2026-11-21T07:30:59Z
type: bug
priority: 3
tags: [imports, signals, fidelity]
---
# _imp.acquire_lock: a raising signal handler ends the wait without the import lock

Review of 48331397. acquire_lock calls rlock_acquire(_lock, -1, 1), which waits with _PY_LOCK_HANDLE_SIGNALS: a KeyboardInterrupt while another thread holds the import lock leaves acquire_lock without it. CPython's _PyImport_AcquireLock is _PyRecursiveMutex_Lock (PyMutex_Lock, not interrupted): the handler runs once the lock is held. Repro: tests/kernel/import_lock_interrupt.py (a holder thread calls _thread.interrupt_main() while main waits in acquire_lock). CPython: 'held when interrupted True False'; lucifer: held False.

