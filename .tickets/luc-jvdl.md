---
id: luc-jvdl
status: closed
deps: []
links: []
created: 2026-11-21T10:14:21Z
type: bug
priority: 3
tags: [fork, tests]
---
# tests/kernel/fork_import_lock_interrupt.py depends on timing

Review of 6127bd00. The holder thread calls interrupt_main after a fixed 0.2 s sleep, assuming main is already waiting in os.fork for the import lock; on a loaded host under jit-deopt main is still in t.start() (threading.py _started.wait) and the KeyboardInterrupt lands there, uncaught (exit -2). Seen once in jit-deopt on 11-21, passed on rerun. Make the hand-off deterministic: the holder interrupts only once main is known to wait (e.g. main sets an Event right before os.fork and the holder waits on it, then a short sleep for the lock wait to start).


## Notes

**2026-11-21T13:02:35Z**

A one-second pause before os.fork reproduced the uncaught KeyboardInterrupt on CPython 3.14t and on Lucifer with JIT deoptimization. The lock holder now waits for an Event set by a before-fork callback. The existing short delay then gives fork time to enter the native import-lock wait. The delayed case passed on both interpreters after the change. The 15 targeted kernel checks passed across plain, jit and jit-deopt.
