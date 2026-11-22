# As threads_serial_loop, but the second thread starts in a destructor that
# a store in the hot loop runs when it releases the value it replaces: the
# loop's next stores, in the same turn, must already take the locks. Each
# token is released exactly once: none while its pool still holds it, none
# kept after the pool lets it go.
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


class Starter:
    def __init__(self, ps, xs):
        self.args = (ps, xs, N)

    def __del__(self):
        th = threading.Thread(target=other, args=self.args)
        th.start()
        started.append(th)


started = []


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


def hot(ps, xs, n):
    for k in range(n):
        t = MINE[k & 15]
        p = ps[k & 7]
        p.o = t
        p.i = k
        p.f = k * 0.5
        xs[k & 31] = t
        if k == START:
            p.o = Starter(ps, xs)       # released by the first store of turn k + 8
    return started[0]


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
