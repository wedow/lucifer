# Reference counts through compiled code's fast and slow paths: objects
# read from lists and tuples and attributes, stored, boxed and released in
# hot loops, some of them dropped by a finalizer that runs Python while
# the loop's other values are held, or by __bool__, which a truth test
# runs while the frame names the object it tests. Each object's count after the loops
# must be what it was before (the deltas printed are 0), and every object
# the loops made must be gone once they return.
import sys
import weakref

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000

made = []


class Obj:
    def __init__(self, v):
        self.v = v
        self.w = v + 1


class Probe:
    def __del__(self):
        pass


def items(xs, n):
    s = 0
    for _ in range(n):
        for b in xs:
            s += b.v
    return s


def stores(xs, n):
    for i in range(n):
        for b in xs:
            b.v = i
            b.w = b
            b.w = None
    return xs[0].v


def tuples(ps, n):
    s = 0
    for _ in range(n):
        for a, b in ps:
            s += a.v - b.v
    return s


def released(xs, n):
    s = 0
    keep = xs[0]
    for i in range(n):
        p = Probe()
        made.append(weakref.ref(p))
        if len(made) > 50:
            del made[:]
        p = None
        s += keep.v
    return s


class Truthy:
    def __init__(self, v):
        self.v = v

    def __bool__(self):
        return self.v > 0


def truth(xs, n):
    s = 0
    t = Truthy(1)
    c = sys.getrefcount(t)
    for i in range(n):
        if t:
            s += xs[i % 20].v
    return s, sys.getrefcount(t) - c


def fresh(n):
    out = None
    for i in range(n):
        o = Obj(i)
        made.append(weakref.ref(o))
        if len(made) > 50:
            del made[:]
        out = o
    return out.v


xs = [Obj(i) for i in range(20)]
ps = [(xs[i], xs[i + 1]) for i in range(19)]


def counts():
    return [sys.getrefcount(o) for o in xs]


before = counts()
for fn, args in ((items, (xs,)), (stores, (xs,)), (tuples, (ps,)), (released, (xs,)), (truth, (xs,))):
    fn(*args, N)
    print(fn.__name__, fn(*args, 3), [c - b for c, b in zip(counts(), before)])
print('fresh', fresh(N), fresh(3))
refs = [weakref.ref(o) for o in (Obj(1), Obj(2))]
made.extend(refs)
print('alive', sum(r() is not None for r in made))
