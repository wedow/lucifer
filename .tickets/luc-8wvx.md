---
id: luc-8wvx
status: open
deps: []
links: []
created: 2026-11-21T06:18:23Z
type: bug
priority: 1
tags: [threads, race]
---
# RLock: a release by a thread that does not hold it rewrites the holder's count

Review of 62aaa6bf. rlock_release computes n = 0 for a caller that holds none, then still runs mutex->count -= n: a non-atomic read-modify-write of the holder's count from another thread. It races the holder's own count += 1 / -= 1 and loses updates: the holder later finds its count wrong (RuntimeError 'cannot release un-acquired lock' on its own second release, or the lock never unlocks). _release_save from a non-holder takes the same path.

Repro (CPython: 'bad [] 0 False'; lucifer all lanes: RuntimeError in holder, or a hang under jit-deopt):
    import threading
    lock = threading.RLock(); stop = False
    def foreign():
        while not stop:
            try: lock.release()
            except RuntimeError: pass
    ts = [threading.Thread(target=foreign) for _ in range(3)]
    for t in ts: t.start()
    for i in range(300000):
        lock.acquire(); lock.acquire(); lock.release(); lock.release()
    stop = True
Fix: write the count only when the caller gives up a hold.

