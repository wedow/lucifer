# As threads_serial_loop, in the child of a fork made while the parent
# was still one thread: the child starts serial too (with its own barrier
# registration), and its hot loop starts the second thread. The parent, still
# one thread, runs the same loop with no thread after the child is done, and
# stays serial. The runtime's state is read where it exists (_lucifer); the
# oracle, which has none, reports what it should be.
import gc
import os
import sys
import threading

STARTED = []
try:
    from _lucifer import thread_serial
except ImportError:
    def thread_serial():
        return not STARTED

N = 400000
START = 150000
freed = []


class Tok:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        freed.append(self.name)


class P:
    def __init__(self):
        self.i = 0
        self.f = 0.0
        self.o = None


MINE = [Tok(k) for k in range(16)]
THEIRS = [Tok(100 + k) for k in range(16)]


def other(ps, xs, n):
    for k in range(n):
        t = THEIRS[k & 15]
        p = ps[k & 7]
        p.i = t
        p.f = t
        p.o = t
        xs[k & 31] = t


def hot(ps, xs, n, spawn=True):
    th = None
    for k in range(n):
        t = MINE[k & 15]
        p = ps[k & 7]
        p.i = k
        p.f = k * 0.5
        p.o = t
        xs[k & 31] = t
        if k == START and spawn:
            STARTED.append(k)
            th = threading.Thread(target=other, args=(ps, xs, N))
            th.start()
    return th


def main():
    ps = [P() for _ in range(8)]
    xs = [None] * 32
    print("child serial before", thread_serial())
    hot(ps, xs, N).join()
    print("child serial after", thread_serial())
    kinds = {(type(p.i).__name__, type(p.f).__name__, type(p.o).__name__) for p in ps}
    print(kinds <= {(a, b, "Tok") for a in ("int", "Tok") for b in ("float", "Tok")},
          all(type(x) is Tok for x in xs))
    for p in ps:
        p.i = p.f = p.o = None
    xs[:] = []
    gc.collect()
    print("early", sorted(freed))
    MINE.clear()
    THEIRS.clear()
    gc.collect()
    print("late", len(freed))
    sys.stdout.flush()


warm = [P() for _ in range(8)]
hot(warm, [None] * 32, N, spawn=False)     # the parent's loop compiled, unlocked
pid = os.fork()
if pid == 0:
    main()
    os._exit(0)
os.waitpid(pid, 0)
hot(warm, [None] * 32, N, spawn=False)
print("parent", threading.active_count(), type(warm[0].o).__name__, "serial", thread_serial())
warm.clear()
MINE.clear()
THEIRS.clear()
gc.collect()
print("parent freed", len(freed))
