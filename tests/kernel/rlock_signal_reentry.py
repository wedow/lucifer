"""A signal handler that takes the RLock its thread is acquiring or
releasing finds it its own: the handler's acquire nests instead of waiting
for its own thread (luc-igt3). Condition.wait's release and restore of the
lock's recursion count is interrupted the same way."""
import signal
import threading
import time

lock = threading.RLock()
hits = 0


def handler(signum, frame):
    global hits
    with lock:
        hits += 1


signal.signal(signal.SIGALRM, handler)
signal.setitimer(signal.ITIMER_REAL, 0.0003, 0.0003)
for i in range(300000):
    with lock:
        pass
cond = threading.Condition(lock)
end = time.monotonic() + 0.5
while time.monotonic() < end:
    with lock, lock:
        cond.wait(0.0001)
        if lock._recursion_count() != 2:
            print("count", lock._recursion_count())
signal.setitimer(signal.ITIMER_REAL, 0)
print("ok", hits > 0, lock._is_owned(), lock.locked())
