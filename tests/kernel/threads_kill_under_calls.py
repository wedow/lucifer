# os.fork inside a callee, and other threads acting on hot calls: a thread
# started inside a callee, sys._current_frames() read from another thread
# across a chain of calls, and a global the callee reads rebound by another
# thread under the calling loop (each call sees the old value or the new one,
# and the new one after the join).
import os
import sys
import threading

K = 1


def reads(x):
    return x * K


def loop(n):
    seen = set()
    for i in range(n):
        seen.add(reads(1))
    return seen


# os.fork inside a callee, before any other thread exists (one still
# exiting would make CPython warn, at a time that varies).


def forks(i):
    if i == 150:
        pid = os.fork()
        if pid == 0:
            os._exit(sum(loop(100)))
        return os.waitpid(pid, 0)[1] >> 8
    return 0


def calls_forks(n):
    s = 0
    for i in range(n):
        s += forks(i)
    return s


print('fork', calls_forks(300))


def starter(i):
    if i == 150:
        t = threading.Thread(target=lambda: out.append(loop(200)))
        t.start()
        return t
    return None


def starts(n):
    started = []
    for i in range(n):
        t = starter(i)
        if t is not None:
            started.append(t)
    return started


out = []
for t in starts(300):
    t.join()
print('started inside a callee', out)

# The chain of a worker in nested calls, read from the main thread.
ready = threading.Event()
done = threading.Event()


def c3(n):
    if n == 0:
        ready.set()
        done.wait()
        return 0
    return c3(n - 1) + 1


def c2(n):
    return c3(n)


def c1(n):
    return c2(n)


done.set()
for _ in range(300):
    c1(3)
ready.clear()
done.clear()
w = threading.Thread(target=c1, args=(5,))
w.start()
ready.wait()
f = sys._current_frames().get(w.ident)
chain = []
while f is not None:
    chain.append(f.f_code.co_name)
    f = f.f_back
done.set()
w.join()
print('current frames', chain[:9])

# A global rebound under the calling loop.
loop(300)
results = []
w = threading.Thread(target=lambda: results.append(loop(200000)))
w.start()
K = 2
w.join()
print('rebound', results[0] <= {1, 2}, loop(5))
