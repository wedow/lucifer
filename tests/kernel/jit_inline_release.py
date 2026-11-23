# Releases compiled code makes when an object's count reaches zero, in hot
# loops that replace list items and locals: exact ints (small, compact,
# wide), floats and tuples, which go back to their freelists, and strs; subclasses
# of int, float and tuple with __del__ and weak references, which take
# the dealloc; tuples whose items die with them (their finalizers run, in
# order) or come back to life in __del__; objects another thread made;
# more releases than a freelist keeps; and instances whose class gains __del__ or a weak reference in
# the middle of the loop. Finalizers, weakref callbacks and counts must
# be the interpreter's.
import sys
import threading
import weakref

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

log = []


def ints(n):
    xs = list(range(1000, 1300))
    for i in range(n):
        for j in range(300):
            xs[j] = xs[j] + 1
    return sum(xs)


def wide(n):
    xs = [2 ** 70 + i for i in range(50)]
    for i in range(n):
        for j in range(50):
            xs[j] = xs[j] + 1
    return sum(xs) % 1000003


def floats(n):
    xs = [0.0] * 300
    for i in range(n):
        for j in range(300):
            xs[j] = j * 0.5 + i
    return sum(xs)


def strs(n):
    xs = [''] * 50
    for i in range(n):
        for j in range(50):
            xs[j] = f'{i}:{j}' if j % 3 else f'\u00e9{i}\u4e2d{j}'
    return sum(map(len, xs)), xs[1], xs[3]


def tuples(n, a, b):
    s = 0
    for i in range(n):
        t = (a, b, i)
        s += len(t)
        t = None
    return s


class MyInt(int):
    def __del__(self):
        log.append(('int', int(self)))


class MyFloat(float):
    pass


class MyTuple(tuple):
    def __del__(self):
        log.append(('tuple', len(self)))


class Item:
    def __init__(self, k):
        self.k = k

    def __del__(self):
        log.append(('item', self.k))


saved = []


class Phoenix:
    def __del__(self):
        saved.append(self)


def subclasses(n):
    refs = []
    for i in range(n):
        x = MyInt(i)
        f = MyFloat(i)
        r = weakref.ref(f, lambda w: log.append(('float', 1)))
        refs.append(r)
        x = None
        f = None
        t = MyTuple((i, i))
        t = None
    return len(refs), sum(r() is None for r in refs)


def dying_items(n):
    for i in range(n):
        t = (Item(i), Item(-i))
        t = None
        u = (Phoenix(),)
        u = None
    return len(saved)


class Plain:
    def __init__(self, i):
        self.x = i
        self.y = i + 1


def instances(n, change):
    out = [None] * 10
    for i in range(n):
        for j in range(10):
            out[j] = Plain(i)
        if i == change:
            Plain.__del__ = lambda self: log.append(('plain', self.x))
        if i == change + 1:
            refs.append(weakref.ref(out[0], lambda w: log.append(('plain weak', 1))))
    return out[3].x


def foreign(n):
    # Objects another thread made, released here: not this thread's to free in line.
    made_there = []
    t = threading.Thread(target=lambda: made_there.extend(
        [float(i) + 0.5, (i, i + 1), Plain(i), 10 ** 6 + i] for i in range(n)))
    t.start()
    t.join()
    s = 0.0
    for row in made_there:
        for k in range(4):
            v = row[k]
            row[k] = None
            s += k
            v = None
    return s, len(made_there)


refs = []
a, b = object(), object()
ca, cb = sys.getrefcount(a), sys.getrefcount(b)
print('ints', ints(N // 10))
print('wide', wide(N // 10))
print('floats', floats(N // 10))
print('strs', strs(N // 10))
print('tuples', tuples(N * 10, a, b), sys.getrefcount(a) - ca, sys.getrefcount(b) - cb)
print('subclasses', subclasses(N))
print('log', len(log), log[:6], log[-6:])
del log[:]
print('dying', dying_items(N))
# Sorted: compiled code releases a virtual tuple's items in the other
# order (lu-ex68).
print('log', len(log), sorted(log)[:6], sorted(log)[-4:])
del log[:]
print('foreign', foreign(N))
print('instances', instances(N // 10, N // 20))
print('log', len(log), log[:4], log[-4:])
