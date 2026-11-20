---
id: luc-g2r0
status: open
deps: []
links: []
created: 2026-11-20T02:35:46Z
type: bug
priority: 2
tags: [io, signals]
---
# TextIOWrapper flush lock: a signal handler landing between acquire and owner deadlocks its own thread

cb5fc0cf's _Pending flush lock (lib/modules/_io.py TextIOWrapper._writeflush) records its holder in a separate step: `lock.acquire(False)` succeeds, then `p.owner = me`; on the way out `p.owner = 0`, then `lock.release()`. A signal handler runs between those statements (CPython-style eval-breaker check after the call). If its handler writes to the same stream with a flush (write_through, line buffering, or a full chunk), _writeflush sees the lock held and owner != me, takes the blocking acquire, and waits for its own thread forever; every later signal nests another blocked handler. CPython does the flush in C under the stream's critical section, where no handler can run between the two, so its handler's write goes through or gets the buffer's reentrancy RuntimeError.

_BufferLock.__enter__/__exit__ has the same window (acquire, then _owner = me; _owner = 0, then release), pre-existing; the test below uses write_through so only the flush lock can block (main holds the flush lock with its owner recorded whenever it is inside the buffer lock).

Repro (CPython 3.14t prints `done`): kernelcheck --programs text_write_signal_reentry on master 702f52bf fails in plain, jit and jit-deopt; 3 of 3 bare runs hang (faulthandler after 20 s: 80-100 nested `handler` frames on the main thread).

```python
import os, signal, tempfile, time
with tempfile.TemporaryDirectory() as d:
    f = open(os.path.join(d, "t"), "w", encoding="utf-8")
    f.reconfigure(write_through=True)
    def handler(signum, frame):
        try:
            f.write("h\n")
        except RuntimeError:
            pass
    signal.signal(signal.SIGALRM, handler)
    signal.setitimer(signal.ITIMER_REAL, 0.002, 0.002)
    end = time.monotonic() + 3
    while time.monotonic() < end:
        try:
            f.write("y\n")
        except RuntimeError:
            pass
    signal.setitimer(signal.ITIMER_REAL, 0)
    f.close()
print("done")
```

Fix direction: make "held by this thread" readable without the gap (e.g. an owner check that tolerates the window, or an RLock-like primitive with an atomic owner), for both _Pending and _BufferLock.

