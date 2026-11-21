"""A thread that releases an RLock it does not hold gets RuntimeError and
leaves the holder's count alone (luc-8wvx)."""
import threading
lock = threading.RLock()
stop = False
bad = []
def holder():
    for i in range(300000):
        lock.acquire(); lock.acquire()
        if lock._recursion_count() != 2:
            bad.append(lock._recursion_count())
        lock.release(); lock.release()
def foreign():
    n = 0
    while not stop:
        try: lock.release()
        except RuntimeError: n += 1
        try: lock._release_save()
        except RuntimeError: n += 1
ts = [threading.Thread(target=foreign) for _ in range(3)]
for t in ts: t.start()
try:
    holder()
finally:
    stop = True
    for t in ts: t.join()
print("bad", bad[:5], len(bad), lock.locked())
