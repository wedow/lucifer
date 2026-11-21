---
id: luc-igt3
status: closed
deps: []
links: []
created: 2026-11-20T21:58:12Z
type: bug
priority: 2
tags: [threads, signals]
---
# RLock: a signal handler that takes an RLock its thread is acquiring or releasing deadlocks

Review of 7003c94f, which moved the stream locks' holder into the mutex primitive but left _thread.RLock with its own _owner/_count set in statements after mutex_acquire and cleared before mutex_release. A signal handler that runs at the poll in either gap finds the RLock not owned by its thread and waits on a mutex its own thread holds; under _PY_LOCK_HANDLE_SIGNALS the next signal nests another handler that waits too, and the program dies of RecursionError. logging's handler lock is an RLock, so a handler that logs hits it.

Repro (CPython 3.14t: 'ok True'; Lucifer, plain, jit and jit-deopt: RecursionError from the handler at 'with lock'):

    import signal, threading
    lock = threading.RLock(); hits = 0
    def handler(signum, frame):
        global hits
        with lock:
            hits += 1
    signal.signal(signal.SIGALRM, handler)
    signal.setitimer(signal.ITIMER_REAL, 0.0003, 0.0003)
    for i in range(300000):
        with lock:
            pass
    signal.setitimer(signal.ITIMER_REAL, 0)
    print('ok', hits > 0)

Fix direction: keep the RLock's count with the mutex too (one primitive per acquire/release that sets owner and count together), so RLock drops _owner like the stream locks did.


## Notes

**2026-11-21T03:13:58Z**

Fixed by 62aaa6bf: rlock_acquire/rlock_release keep the holder and its count with the PyMutex in one primitive call, so a handler at a poll never sees the lock without them. tests/kernel/rlock_signal_reentry.py, 20 runs per lane failing before / after: plain, jit, jit-deopt 20/20 / 0/20. Kernel threading/RLock/signal programs (10) pass in all three lanes. Instructions, 1M nested with-RLock pairs on a core lease, median of 3: 10.25G before, 7.68G after (-25%).
