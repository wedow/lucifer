# Shapes a release compiled code makes in line must get right, each in a
# hot loop that drops the last reference by replacing a list item: one
# object held twice by what dies (a tuple (x, x), an instance with two
# attributes on one value, an instance holding its own class), so that the
# second reference is the last; an instance whose attributes moved to a
# dict (__class__ assigned to a class with more names) and whose dict was
# then deleted; an instance whose dict was made real, one with __slots__,
# one of a subclass of a builtin, one whose attribute dies with it, one
# whose class dies with it; tuples longer than any freelist keeps and the
# empty tuple; an object another thread referenced; and more releases than
# a freelist keeps. Finalizers, weakref callbacks and
# counts must be the interpreter's.
import gc
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000

log = []


class D:
    def __init__(self, k):
        self.k = k

    def __del__(self):
        log.append(self.k)


def twice_in_tuple(n):
    out = [None]
    for i in range(n):
        d = D(i)
        out[0] = (d, d)
        d = None
    out[0] = None
    return len(log)


class Pair:
    def __init__(self, v):
        self.a = v
        self.b = v


def twice_in_attrs(n):
    out = [None]
    for i in range(n):
        out[0] = Pair(D(i))
    out[0] = None
    return len(log)


class Selfish:
    def __init__(self):
        self.k = type(self)
        self.j = type(self)


def own_class(n):
    out = [None]
    c = sys.getrefcount(Selfish)
    for i in range(n):
        out[0] = Selfish()
    out[0] = None
    return sys.getrefcount(Selfish) - c >= 0      # each instance gives its class back exactly once


def floats_twice(n):
    out = [None]
    for i in range(n):
        x = float(i) + 0.5
        out[0] = (x, x, x)
        x = None
    return out[0][0]


class Wide:
    def __init__(self):
        for k in range(40):
            setattr(self, 'a%d' % k, k)


Wide()


class Narrow:
    def __init__(self, v):
        self.v = v


def moved(n):
    out = [None]
    for i in range(n):
        o = Narrow(D(i))
        o.__class__ = Wide
        del o.__dict__
        out[0] = o
        o = None
    out[0] = None
    gc.collect()
    return 'done'


class Slotted:
    __slots__ = ('a', 'b')

    def __init__(self, v):
        self.a = v
        self.b = i_of(v)


def i_of(v):
    return v


class IntSub(int):
    pass


class Based(IntSub):
    def __init__(self, v):
        self.v = v


def shapes(n):
    out = [None] * 6
    for i in range(n):
        o = Narrow(D(i))
        vars(o)
        out[0] = o
        out[1] = Slotted(D(-i))
        out[2] = Based(i)
        out[3] = Narrow(D(i + 0.5))
        out[4] = tuple(range(i % 30))
        out[5] = ()
    out[:] = [None] * 6
    gc.collect()
    return 'done'


def dying_class(n):
    out = [None]
    for i in range(n):
        C = type('C', (), {'__init__': lambda s: setattr(s, 'v', i)})
        out[0] = C()
        C = None
    out[0] = None
    gc.collect()
    return out


def foreign_refs(n):
    box = []
    out = [None]
    for i in range(n):
        x = float(i) + 0.25
        if i % 100 == 0:
            t = threading.Thread(target=lambda: box.append(x))
            t.start()
            t.join()
            box.clear()
        out[0] = x
        x = None
    return out[0]


def overflow(n):
    # More releases than a freelist keeps, before anything is made again.
    s = 0.0
    for i in range(n // 100):
        xs = [float(k) + i for k in range(500)]
        for j in range(500):
            s += xs[j]
            xs[j] = None
    return s


print('twice in tuple', twice_in_tuple(N))
del log[:]
print('twice in attrs', twice_in_attrs(N))
del log[:]
print('own class', own_class(N))
print('floats twice', floats_twice(N))
print('moved', moved(N // 4))
del log[:]
print('shapes', shapes(N))
del log[:]
print('dying class', dying_class(N // 10))
print('foreign', foreign_refs(N))
print('overflow', overflow(N))
