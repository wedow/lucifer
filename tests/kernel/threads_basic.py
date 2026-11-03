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
d.join()

# Barrier: every party gets a distinct index, the action runs once, an abort breaks the others.
actions = []
barrier = threading.Barrier(4, action=lambda: actions.append(1))
indexes = []
ts = [threading.Thread(target=lambda: indexes.append(barrier.wait())) for _ in range(4)]
[t.start() for t in ts]
[t.join() for t in ts]
print(sorted(indexes), actions, barrier.broken)
broken = []
barrier = threading.Barrier(3)


def party():
    try:
        barrier.wait()
    except threading.BrokenBarrierError:
        broken.append(1)


ts = [threading.Thread(target=party) for _ in range(2)]
[t.start() for t in ts]
while barrier.n_waiting < 2:
    time.sleep(0.001)
barrier.abort()
[t.join() for t in ts]
print(broken, barrier.broken)

# Event and Condition with several waiters.
event = threading.Event()
woke = []
ts = [threading.Thread(target=lambda n=n: (event.wait(), woke.append(n))) for n in range(5)]
[t.start() for t in ts]
event.set()
[t.join() for t in ts]
print(sorted(woke), event.is_set())
cond = threading.Condition()
ready = []
passed = []


def sleeper(n):
    with cond:
        ready.append(n)
        cond.wait_for(lambda: 'go' in passed)
        passed.append(n)


ts = [threading.Thread(target=sleeper, args=(n,)) for n in range(3)]
[t.start() for t in ts]
while True:
    with cond:
        if len(ready) == 3:
            passed.append('go')
            cond.notify_all()
            break
[t.join() for t in ts]
print(sorted(map(str, passed)))
with cond:
    print(cond.wait(0.01))

# Thread objects: names, ident, native_id, daemon inheritance, enumerate, Timer.
info = []


def describe():
    me = threading.current_thread()
    info.append((me.name, me.ident == threading.get_ident(), me.native_id == threading.get_native_id(),
                 me.daemon, me.is_alive()))
    child = threading.Thread(target=lambda: info.append(('child', threading.current_thread().daemon)))
    child.start()
    child.join()


t = threading.Thread(target=describe, name='named', daemon=True)
print(t.is_alive(), t.ident, t.native_id, repr(t))
t.start()
t.join()
print(info, [x.name for x in threading.enumerate()], threading.active_count())
timer = threading.Timer(60, lambda: info.append('never'))
timer.start()
timer.cancel()
timer.join()
print(timer.finished.is_set(), 'never' in info)
main = threading.main_thread()
print(main.name, main.is_alive(), main.ident == threading.get_ident(), main.daemon)

# threading.local, subclassed with defaults, one value per thread; a thread's values go with it.
class Mine(threading.local):
    def __init__(self, start):
        self.value = start


mine = Mine(10)
mine.value += 1
seen = []
ts = [threading.Thread(target=lambda n=n: (setattr(mine, 'value', mine.value + n), seen.append(mine.value)))
      for n in range(3)]
[t.start() for t in ts]
[t.join() for t in ts]
print(sorted(seen), mine.value)

# Exits: a lock handed to a daemon that never ends, a daemon asleep for ever, and a non-daemon thread
# that finishes after the main program: threading joins it, then atexit runs, then the process exits
# with the daemons still blocked.
import atexit

forever = threading.Event()
for n in range(3):
    threading.Thread(target=forever.wait, daemon=True).start()
threading.Thread(target=time.sleep, args=(3600,), daemon=True).start()
held = threading.Lock()
held.acquire()
threading.Thread(target=held.acquire, daemon=True).start()
threading.Thread(target=lambda: [None for _ in iter(int, 1)], daemon=True).start()      # busy for ever
threading.Thread(target=lambda: (time.sleep(2), print('daemon woke')), daemon=True).start()
gate = threading.Event()
atexit.register(print, 'atexit')


def after_main():
    gate.wait()
    print('non-daemon done')


threading.Thread(target=after_main).start()
print('main done', threading.active_count())

gate.set()
