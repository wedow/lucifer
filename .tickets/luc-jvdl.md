---
id: luc-jvdl
status: open
deps: []
links: []
created: 2026-11-21T10:14:21Z
type: bug
priority: 3
tags: [fork, tests]
---
# tests/kernel/fork_import_lock_interrupt.py depends on timing

Review of 6127bd00. The holder thread calls interrupt_main after a fixed 0.2 s sleep, assuming main is already waiting in os.fork for the import lock; on a loaded host under jit-deopt main is still in t.start() (threading.py _started.wait) and the KeyboardInterrupt lands there, uncaught (exit -2). Seen once in jit-deopt on 11-21, passed on rerun. Make the hand-off deterministic: the holder interrupts only once main is known to wait (e.g. main sets an Event right before os.fork and the holder waits on it, then a short sleep for the lock wait to start).

