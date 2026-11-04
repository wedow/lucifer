# Every lock-shaped wait: Lock handoff between threads, RLock ownership by the thread (the task
# ident), Condition, Event, Semaphore, Barrier, the queue classes, and join.
import queue
import threading

results = []

# A Lock is released by a thread that did not take it.
lock = threading.Lock()
lock.acquire()
t = threading.Thread(target=lock.release)
t.start()
t.join()
results.append(("lock handed back", lock.acquire(False)))
lock.release()

# An RLock is owned by the thread: another thread cannot release it, the owner can nest.
rlock = threading.RLock()
with rlock:
    with rlock:
        seen = []

        def other():
            seen.append(rlock.acquire(False))
            try:
                rlock.release()
            except RuntimeError as e:
                seen.append(str(e))

        t = threading.Thread(target=other)
        t.start()
        t.join()
results.append(("rlock owner", seen))
results.append(("rlock free", rlock.acquire(False)))
rlock.release()

# Condition: ping-pong through wait/notify, 200 turns.
cond = threading.Condition()
turn = [0]
log = []


def player(me, other_player):
    for _ in range(200):
        with cond:
            while turn[0] != me:
                cond.wait()
            log.append(me)
            turn[0] = other_player
            cond.notify_all()


ts = [threading.Thread(target=player, args=(0, 1)), threading.Thread(target=player, args=(1, 0))]
for t in ts:
    t.start()
for t in ts:
    t.join()
results.append(("condition", log == [0, 1] * 200))

# Event wakes every waiter.
event = threading.Event()
woke = []
ts = [threading.Thread(target=lambda: woke.append(event.wait())) for _ in range(8)]
for t in ts:
    t.start()
event.set()
for t in ts:
    t.join()
results.append(("event", woke == [True] * 8))

# Semaphore bounds the threads inside it.
sem = threading.BoundedSemaphore(3)
inside = [0]
peak = [0]
guard = threading.Lock()


def use():
    for _ in range(50):
        with sem:
            with guard:
                inside[0] += 1
                peak[0] = max(peak[0], inside[0])
            with guard:
                inside[0] -= 1


ts = [threading.Thread(target=use) for _ in range(8)]
for t in ts:
    t.start()
for t in ts:
    t.join()
results.append(("semaphore", peak[0] <= 3, inside[0]))

# Barrier releases all parties together, three times.
barrier = threading.Barrier(5)
arrived = []


def meet():
    for round_ in range(3):
        arrived.append((round_, barrier.wait() >= 0))


ts = [threading.Thread(target=meet) for _ in range(5)]
for t in ts:
    t.start()
for t in ts:
    t.join()
results.append(("barrier", sorted(arrived) == [(r, True) for r in range(3) for _ in range(5)]))

# Queues: a producer and three consumers; the stops go in once every item is consumed.
STOP = 10**9
for kind in (queue.Queue, queue.LifoQueue, queue.PriorityQueue, queue.SimpleQueue):
    q = kind()
    got = []
    all_in = threading.Event()

    def consume():
        while True:
            item = q.get()
            if item == STOP:
                return
            got.append(item)
            if len(got) == 100:
                all_in.set()

    ts = [threading.Thread(target=consume) for _ in range(3)]
    for t in ts:
        t.start()
    for i in range(100):
        q.put(i)
    all_in.wait()
    for _ in ts:
        q.put(STOP)
    for t in ts:
        t.join()
    results.append((kind.__name__, sorted(got) == list(range(100))))

# A bounded queue blocks put until a get makes room; task_done and join pair up.
q = queue.Queue(maxsize=2)
done = []


def drain():
    for _ in range(10):
        done.append(q.get())
        q.task_done()


t = threading.Thread(target=drain)
t.start()
for i in range(10):
    q.put(i)
q.join()
t.join()
results.append(("bounded", done))

# join() of a thread that has not finished waits for it; of a finished one returns at once.
gate = threading.Event()
t = threading.Thread(target=gate.wait)
t.start()
joiners = [threading.Thread(target=t.join) for _ in range(4)]
for j in joiners:
    j.start()
gate.set()
for j in joiners:
    j.join()
results.append(("join", t.is_alive(), [j.is_alive() for j in joiners]))

# A SimpleQueue.get that a signal handler's exception ends leaves no waiter behind: the next put
# wakes the getter that is still waiting.
import signal
import time


class Interrupted(Exception):
    pass


def interrupt(signum, frame):
    raise Interrupted


q = queue.SimpleQueue()
old = signal.signal(signal.SIGALRM, interrupt)
signal.setitimer(signal.ITIMER_REAL, 0.05)
try:
    q.get()
except Interrupted:
    results.append(("get interrupted",))
signal.signal(signal.SIGALRM, old)
got = []
t = threading.Thread(target=lambda: got.append(q.get(timeout=10)))
t.start()
time.sleep(0.2)                  # the getter waits by now, or finds the item when it looks
q.put("item")
t.join()
results.append(("woken getter", got))

for r in results:
    print(*r)
