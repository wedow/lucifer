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
    # The class's count after n instances died: a constant difference from
    # CPython is someone's reference (lu-z0xi); one that grows with n is
    # each instance giving its class back other than once.
    out = [None]
    c = sys.getrefcount(Selfish)
    for i in range(n):
        out[0] = Selfish()
    out[0] = None
    return sys.getrefcount(Selfish) - c


def own_class_growth(n):
    # Small and fixed on both runtimes (Lucifer's differs a little, lu-z0xi);
    # an instance giving its class back other than once grows it by n.
    return own_class(2 * n) - own_class(n) < n // 10


def max_rss():
    import resource
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss       # KiB


def floats_twice(n):
    # A float three times in each dying tuple: none may leak. Two million
    # floats leaked would be 64 MB; none leaked stays within the noise.
    out = [None]
    before = max_rss()
    for i in range(n):
        x = float(i) + 0.5
        out[0] = (x, x, x)
        x = None
    return out[0][0], max_rss() - before < 32 * 1024


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
    # Before any collection: the finalizers of the values no dict held
    # (the slotted instance's and the in-line one's), each once, at once.
    seen = sorted(k for k in log if isinstance(k, (int, float)) and (k < 0 or k != int(k)))
    gc.collect()
    return len(seen), len(set(seen)), seen[:2]


def dying_class(n):
    # Each class dies once its last instance does (a cycle through its
    # dict and MRO, so at the collection): none may be left alive.
    import weakref
    out = [None]
    classes = []
    for i in range(n):
        C = type('C', (), {'__init__': lambda s: setattr(s, 'v', i)})
        classes.append(weakref.ref(C))
        out[0] = C()
        C = None
    out[0] = None
    gc.collect()
    return sum(r() is not None for r in classes)


def foreign_refs(n):
    # Objects another thread made (and attributes it set), released here:
    # each finalizer runs once, when its last reference goes.
    made = []
    t = threading.Thread(target=lambda: made.extend((D(('f', k)), Narrow(D(('g', k)))) for k in range(n)))
    t.start()
    t.join()
    del log[:]
    out = [None]
    for i in range(n):
        out[0] = made[i]
        made[i] = None
    out[0] = None
    keys = [k for k in log if isinstance(k, tuple)]
    return len(keys), len(set(keys))


def many_releases(n):
    # Instances released with nothing made between them, more than the
    # collector's allocation count batches (it flushes every 512).
    batch = [Narrow(i & 7) for i in range(4 * n)]
    for i in range(4 * n):
        batch[i] = None
    return len(batch)


def traced(n):
    # Allocation tracing started in the middle of a loop that releases.
    import tracemalloc
    out = [None]
    for i in range(n):
        if i == n // 2:
            tracemalloc.start()
        out[0] = (float(i), Narrow(i & 7), 'k' + str(i))
    tracemalloc.stop()
    return out[0][0]


exec('''
class Big:
    def __init__(self, v):
''' + ''.join(f"        self.a{k} = v\n" for k in range(36)))


def fixed_room(n):
    # A class whose methods name more attributes than an instance's room
    # field counts: its instances' room is the table's capacity.
    out = [None]
    for i in range(n):
        out[0] = Big(i & 7)
    big = Big(D(('big', 1)))
    out[0] = big
    big = None
    out[0] = None
    return [k for k in log if isinstance(k, tuple) and k[0] == 'big']


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
print('own class growth', own_class_growth(N))
print('floats twice', floats_twice(1000 * N))
print('moved', moved(N // 4))
del log[:]
print('shapes', shapes(N))
del log[:]
print('dying class', dying_class(N // 10))
print('foreign', foreign_refs(N))
print('many releases', many_releases(N))
print('traced', traced(N))
print('fixed room', fixed_room(N))
print('overflow', overflow(N))
