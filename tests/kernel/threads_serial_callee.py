# As threads_serial_loop, but the second thread starts in a function the
# hot loop calls, deeper than the loop's frame, and that function goes on
# storing into the shared objects after the start, as does the loop when
# it returns: no store from before the switch may run unlocked after it.
# Each token is released exactly once: none while its pool still holds it,
# none kept after the pool lets it go.
import gc
import threading

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


def spawn(ps, xs, k):
    th = threading.Thread(target=other, args=(ps, xs, N))
    th.start()
    for j in range(20000):
        p = ps[j & 7]
        p.i = j
        p.f = j * 0.25
        p.o = MINE[j & 15]
        xs[j & 31] = MINE[(j + k) & 15]
    return th


def step(ps, xs, k, th):
    t = MINE[k & 15]
    p = ps[k & 7]
    p.i = k
    p.f = k * 0.5
    p.o = t
    xs[k & 31] = t
    if k == START:
        return spawn(ps, xs, k)
    return th


def hot(ps, xs, n):
    th = None
    for k in range(n):
        th = step(ps, xs, k, th)
    return th


def main():
    ps = [P() for _ in range(8)]
    xs = [None] * 32
    hot(ps, xs, N).join()
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


main()
