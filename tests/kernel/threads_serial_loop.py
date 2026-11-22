# While one thread alone has run Python, stores take no object locks; the
# start of a second thread switches every store to the locked protocol
# before that thread runs. A hot loop stores ints, floats, objects and list
# items into objects shared with a thread the loop itself starts halfway
# through, and that thread stores its own tokens into the same words. Each
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
    th = None
    for k in range(n):
        t = MINE[k & 15]
        p = ps[k & 7]
        p.i = k
        p.f = k * 0.5
        p.o = t
        xs[k & 31] = t
        if k == START:
            th = threading.Thread(target=other, args=(ps, xs, N))
            th.start()
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
