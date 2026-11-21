---
id: luc-livv
status: open
deps: []
links: []
created: 2026-11-21T10:14:21Z
type: bug
priority: 1
tags: [fork, signals]
---
# fork: a signal pending at fork skips the parent's after_in_parent hooks

Review of 6127bd00. When a signal (here _thread.interrupt_main from another thread) is pending as os.fork returns, Lucifer raises the KeyboardInterrupt at the first poll in posix.fork's kernel code, before _run_at_fork('after_in_parent'): the hooks never run, so logging's lock taken by its before hook stays held (same class as luc-t6nh). CPython runs the hooks in C (PyOS_AfterFork_Parent) and the handler fires at the first eval breaker, inside the first Python hook, which reports it as 'Exception ignored in atfork callback'; with no hooks the exception reaches the caller after os.fork returns. Repro: register_at_fork(after_in_parent=lambda: log.append(1)); a thread holds _imp.acquire_lock(), sleeps 0.2, calls _thread.interrupt_main(), sleeps 0.2, releases; main does os.fork() (child os._exit(0)) in try/except KeyboardInterrupt and prints log. Oracle: unraisable KeyboardInterrupt in the hook, 'no interrupt', log empty (the lambda raised); Lucifer: 'interrupted', hook never called. Fix direction: no poll between os_fork returning and the parent's hooks starting (run the hooks from the primitive's return path, or defer signal polls in hidden frames until the first hook frame).

