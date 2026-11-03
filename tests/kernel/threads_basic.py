# Python threads: start, join, locks, events, queues, thread-locals, daemon threads, errors.
import queue
import sys
import threading
import time

results = []
lock = threading.Lock()


def worker(n):
    for i in range(200):
        with lock:
            results.append((n, i))


threads = [threading.Thread(target=worker, args=(n,)) for n in range(6)]
for t in threads:
    t.start()
for t in threads:
    t.join()
print(len(results), sorted(results) == [(n, i) for n in range(6) for i in range(200)], [t.is_alive() for t in threads])
for n in range(6):
    assert [i for m, i in results if m == n] == list(range(200))

event = threading.Event()
box = []


def waiter():
    box.append(event.wait(5))


t = threading.Thread(target=waiter, name='waiter')
t.start()
time.sleep(0.05)
print(t.is_alive(), t.name, t.ident is not None)
event.set()
t.join()
print(box, t.is_alive())

q = queue.Queue()
out = []


def consumer():
    while True:
        item = q.get()
        if item is None:
            break
        out.append(item * 2)


c = threading.Thread(target=consumer)
c.start()
for i in range(50):
    q.put(i)
q.put(None)
c.join()
print(out == [i * 2 for i in range(50)])

local = threading.local()
local.v = 'main'
seen = []


def use_local():
    seen.append(hasattr(local, 'v'))
    local.v = 'thread'
    seen.append(local.v)


t = threading.Thread(target=use_local)
t.start()
t.join()
print(seen, local.v)

counter = 0
rlock = threading.RLock()


def bump():
    global counter
    for _ in range(500):
        with rlock:
            with rlock:
                counter += 1


ts = [threading.Thread(target=bump) for _ in range(4)]
[t.start() for t in ts]
[t.join() for t in ts]
print(counter)


def failing():
    raise ValueError('boom')


old = threading.excepthook
caught = []
threading.excepthook = lambda args: caught.append((args.exc_type.__name__, str(args.exc_value), args.thread.name))
t = threading.Thread(target=failing, name='failing')
t.start()
t.join()
threading.excepthook = old
print(caught)

cond = threading.Condition()
items = []


def producer():
    for i in range(5):
        with cond:
            items.append(i)
            cond.notify()
        time.sleep(0.005)


p = threading.Thread(target=producer)
p.start()
got = []
while len(got) < 5:
    with cond:
        while not items:
            cond.wait(1)
        got.append(items.pop(0))
p.join()
print(got, threading.active_count(), threading.current_thread() is threading.main_thread())
sem = threading.Semaphore(2)
active = []
peak = []


def limited():
    with sem:
        active.append(1)
        peak.append(len(active))
        time.sleep(0.01)
        active.pop()


ts = [threading.Thread(target=limited) for _ in range(6)]
[t.start() for t in ts]
[t.join() for t in ts]
print(max(peak) <= 2)
bad = threading.Thread(target=lambda: None)
try:
    bad.join()
except RuntimeError as e:
    print(e)
bad.start()
bad.join()
try:
    bad.start()
except RuntimeError as e:
    print(e)
late = []
d = threading.Thread(target=lambda: (time.sleep(0.05), late.append(1)), daemon=False)
d.start()
t0 = threading.Timer(0.01, lambda: late.append(2))
t0.start()
t0.join()
print(late)
