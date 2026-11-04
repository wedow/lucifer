# Every timeout is a deadline of the park: it returns False/Empty/timeout no earlier than asked,
# and a wake before the deadline ends the wait at once. time.sleep sleeps at least its time, and
# a signal's handler runs during it without shortening it (PEP 475).
import queue
import signal
import threading
import time


def timed(f, *args, **kwargs):
    start = time.monotonic()
    result = f(*args, **kwargs)
    return result, time.monotonic() - start


def at_least(elapsed, wanted):
    return wanted <= elapsed < wanted + 2


lock = threading.Lock()
lock.acquire()
r, e = timed(lock.acquire, True, 0.1)
print("lock", r, at_least(e, 0.1))
print("lock nonblocking", lock.acquire(False), lock.acquire(timeout=0))

rlock = threading.RLock()
t = threading.Thread(target=rlock.acquire)
t.start()
t.join()
r, e = timed(rlock.acquire, True, 0.1)
print("rlock", r, at_least(e, 0.1))

cond = threading.Condition()
with cond:
    r, e = timed(cond.wait, 0.1)
print("condition", r, at_least(e, 0.1))

event = threading.Event()
r, e = timed(event.wait, 0.1)
print("event", r, at_least(e, 0.1))

sem = threading.Semaphore(0)
r, e = timed(sem.acquire, True, 0.1)
print("semaphore", r, at_least(e, 0.1))

for kind in (queue.Queue, queue.SimpleQueue):
    q = kind()
    try:
        timed(q.get, True, 0.1)
    except queue.Empty:
        r, e = "empty", None
    print(kind.__name__, r)

gate = threading.Event()
t = threading.Thread(target=gate.wait)
t.start()
r, e = timed(t.join, 0.1)
print("join", t.is_alive(), at_least(e, 0.1))
gate.set()
t.join()

r, e = timed(time.sleep, 0.1)
print("sleep", at_least(e, 0.1))

# A wake before the deadline ends the wait early.
woken = threading.Event()
t = threading.Timer(0.05, woken.set)
t.start()
r, e = timed(woken.wait, 30)
print("early wake", r, e < 5)
t.join()

# A signal during sleep: the handler runs, the sleep goes on for the time left.
hits = []
signal.signal(signal.SIGUSR1, lambda *a: hits.append(1))
threading.Timer(0.05, signal.pthread_kill, [threading.main_thread().ident, signal.SIGUSR1]).start()
r, e = timed(time.sleep, 0.2)
print("signal in sleep", hits, at_least(e, 0.2))

# A signal during a lock wait: the handler runs; one that raises ends the wait.
lock = threading.Lock()
lock.acquire()


def boom(*a):
    raise KeyboardInterrupt


signal.signal(signal.SIGUSR1, boom)
threading.Timer(0.05, signal.pthread_kill, [threading.main_thread().ident, signal.SIGUSR1]).start()
try:
    lock.acquire()
except KeyboardInterrupt:
    print("interrupted lock wait")
