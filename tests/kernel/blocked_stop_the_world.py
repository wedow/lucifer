# A task blocked in a wait does not hold up a stop-the-world: gc.collect() completes while threads
# sleep in lu_park (locks, conditions, events, queues, join, sleep) and inside the syscall bracket
# (pipe read, select, poll, waitpid, flock, socket receive). A wait that stayed attached would
# never answer the stop request and this program would hang.
import fcntl
import gc
import os
import queue
import select
import socket
import subprocess
import threading
import time

release = []      # one callable per blocked thread: lets it go
names = []


def blocked(name, wait, let_go):
    names.append(name)
    t = threading.Thread(target=wait)
    t.start()
    release.append((t, let_go))


lock = threading.Lock()
lock.acquire()
blocked("lock", lock.acquire, lock.release)

rlock = threading.RLock()
rlock.acquire()
blocked("rlock", rlock.acquire, rlock.release)

cond = threading.Condition()


def cond_wait():
    with cond:
        cond.wait()


blocked("condition", cond_wait, lambda: (cond.acquire(), cond.notify_all(), cond.release()))

event = threading.Event()
blocked("event", event.wait, event.set)

q = queue.Queue()
blocked("queue", q.get, lambda: q.put(1))

sq = queue.SimpleQueue()
blocked("simplequeue", sq.get, lambda: sq.put(1))

gate = threading.Event()
sleeper = threading.Thread(target=gate.wait)
sleeper.start()
blocked("join", sleeper.join, gate.set)

blocked("sleep", lambda: time.sleep(0.7), lambda: None)

r, w = os.pipe()
blocked("pipe read", lambda: os.read(r, 1), lambda: os.write(w, b"x"))

r2, w2 = os.pipe()
blocked("select", lambda: select.select([r2], [], []), lambda: os.write(w2, b"x"))

r3, w3 = os.pipe()


def poll_wait():
    p = select.poll()
    p.register(r3, select.POLLIN)
    p.poll()


blocked("poll", poll_wait, lambda: os.write(w3, b"x"))

proc = subprocess.Popen(["sleep", "0.7"])
blocked("waitpid", lambda: os.waitpid(proc.pid, 0), lambda: None)

path = "/tmp/lucifer-blocked-stw.%d" % os.getpid()
fd1 = os.open(path, os.O_CREAT | os.O_RDWR)
fd2 = os.open(path, os.O_RDWR)
fcntl.flock(fd1, fcntl.LOCK_EX)
blocked("flock", lambda: fcntl.flock(fd2, fcntl.LOCK_EX), lambda: fcntl.flock(fd1, fcntl.LOCK_UN))

a, b = socket.socketpair()
blocked("socket recv", lambda: a.recv(1), lambda: b.send(b"x"))

time.sleep(0.3)                                    # let them all block
alive = sum(t.is_alive() for t, _ in release)
for i in range(3):
    gc.collect()
print("collected with", alive, "of", len(release), "blocked")

for (t, let_go), name in zip(release, names):
    let_go()
for t, _ in release:
    if t.is_alive():
        t.join(5)
print(sorted(n for (t, _), n in zip(release, names) if t.is_alive()))
os.unlink(path)
