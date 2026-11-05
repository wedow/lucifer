# Every timeout is a deadline of the park: it returns False/Empty/timeout no earlier than asked,
# and a wake before the deadline ends the wait at once. time.sleep sleeps at least its time, and
# a signal's handler runs during it without shortening it (PEP 475).
import _thread
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

# A signal during sleep: the handler runs, the sleep goes on for the time left. The signalling thread
# waits for `ready` (set just before the sleep) and is joined after it, so the handler has run by the
# print wherever the signal landed.
def signal_main_after(ready):
    t = threading.Thread(target=lambda: (ready.wait(), signal.pthread_kill(threading.main_thread().ident, signal.SIGUSR1)))
    t.start()
    return t


hits = []
signal.signal(signal.SIGUSR1, lambda *a: hits.append(1))
ready = threading.Event()
t = signal_main_after(ready)
ready.set()
r, e = timed(time.sleep, 0.2)
t.join()
print("signal in sleep", hits, at_least(e, 0.2))

# A signal during a lock wait: the handler runs; one that raises ends the wait. A signal that lands
# just before the wait begins is only noticed at the next safepoint, which a wait that has begun does
# not reach (PEP 475), so the signalling thread repeats the signal until main has been interrupted.
# The handler raises once; `ready` is released inside the try, so the exception is raised in it. It is a
# bare lock: an exception landing inside Event.set could leave its condition locked.
lock = threading.Lock()
lock.acquire()
raised = []


def boom(*a):
    if not raised:
        raised.append(1)
        raise KeyboardInterrupt


def signal_until(ready, done):
    ready.acquire()
    while not done.is_set():
        signal.pthread_kill(threading.main_thread().ident, signal.SIGUSR1)
        done.wait(0.01)


signal.signal(signal.SIGUSR1, boom)
ready = _thread.allocate_lock()
ready.acquire()
done = threading.Event()
t = threading.Thread(target=signal_until, args=(ready, done))
t.start()
try:
    ready.release()
    lock.acquire()
except KeyboardInterrupt:
    print("interrupted lock wait")
done.set()
t.join()
