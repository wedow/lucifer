"""Instance attributes of plain classes: insertion order, deletion, more
names than the class's methods assign, __dict__ in every form, __class__
assignment, shadowed methods and collection, each run warm so that the
specialized sites and compiled code meet every shape."""
import copy
import gc
import pickle
import weakref


class P:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def norm(self):
        return self.x * self.x + self.y * self.y


def order(o):
    return list(vars(o).items())


# 1. Instances fill their attributes in different orders.
for i in range(200):
    a = P(1, 2)
    b = P.__new__(P)
    b.y = 3
    b.x = 4
    if i % 50 == 0:
        print(1, order(a), order(b), a.norm(), b.norm())

# 2. Deletion, and a name stored again after it.
for i in range(200):
    p = P(i, 2)
    del p.x
    missing = not hasattr(p, 'x')
    p.x = 5
    if i % 50 == 0:
        print(2, missing, order(p), p.norm())
try:
    del p.zz
except AttributeError as e:
    print(2, 'AttributeError', e)

# 3. More names than the class assigns, set from outside, read back warm.
for i in range(200):
    p = P(1, 2)
    for k in range(40):
        setattr(p, 'a' + str(k), k)
    total = 0
    for k in range(40):
        total += getattr(p, 'a' + str(k))
    if i % 50 == 0:
        print(3, total, len(vars(p)), list(vars(p))[:4], p.norm())

# 4. __dict__ read, written, replaced and deleted.
for i in range(100):
    p = P(1, 2)
    d = p.__dict__
    d['z'] = 9
    p.w = 7
    if i % 50 == 0:
        print(4, p.z, d, p.norm(), p.__dict__ is d)
    p.__dict__ = {'x': 10, 'y': 0}
    if i % 50 == 0:
        print(4, order(p), p.norm(), hasattr(p, 'z'))
    del p.__dict__
    if i % 50 == 0:
        print(4, order(p), hasattr(p, 'x'))
    p.x = 3
    if i % 50 == 0:
        print(4, order(p))


# 5. __class__ assignment between classes that assign different names.
class Q:
    def __init__(self):
        self.a = 1
        self.b = 2
        self.c = 3

    def norm(self):
        return -1


class R:
    pass


for i in range(100):
    p = P(3, 4)
    p.__class__ = Q
    q = Q()
    q.__class__ = R
    r = R()
    r.k = 1
    r.__class__ = P
    r.x = 1
    r.y = 1
    if i % 50 == 0:
        print(5, type(p).__name__, order(p), p.norm(), order(q), order(r), r.norm())


# 6. An instance attribute shadows a method once warm, then goes away.
class M:
    def __init__(self):
        self.v = 1

    def f(self):
        return 'method'


ms = [M() for _ in range(10)]
seen = []
for i in range(3000):
    m = ms[i % 10]
    if i == 1500:
        ms[3].f = lambda: 'instance'
    if i == 2500:
        del ms[3].f
    seen.append(m.f())
print(6, seen.count('method'), seen.count('instance'))


# 7. A subclass that stores its own names before its base's.
class Base:
    def __init__(self):
        self.base = 'b'


class Sub(Base):
    def __init__(self):
        self.own = 'o'
        super().__init__()


for i in range(200):
    s = Sub()
    if i % 50 == 0:
        print(7, order(s), s.base, s.own)


# 8. copy, pickle, dir and repr of state.
p = P(1, 2)
p.extra = [1]
c = copy.copy(p)
dc = copy.deepcopy(p)
u = pickle.loads(pickle.dumps(p))
print(8, order(c), order(dc), order(u), c.extra is p.extra, dc.extra is p.extra)
print(8, [n for n in dir(p) if not n.startswith('__')])


# 9. Cycles through attributes are collected; weak references see it.
class Node:
    def __init__(self):
        self.other = None
        self.data = list(range(3))


def make_cycle():
    a, b = Node(), Node()
    a.other, b.other = b, a
    return weakref.ref(a)


refs = [make_cycle() for _ in range(50)]
gc.collect()
print(9, sum(r() is None for r in refs))
n = Node()
print(9, sorted(map(type, gc.get_referents(n)), key=lambda t: t.__name__))


# 10. Attribute names that are not interned.
p = P(1, 2)
key = ''.join(['dyn', 'amic'])
setattr(p, key, 1)
print(10, p.dynamic, getattr(p, 'dyn' + 'amic'), 'dynamic' in vars(p))
setattr(p, ''.join(['x']), 42)
print(10, p.x, order(p))


# 11. __getattr__ and properties beside inline values.
class G:
    def __init__(self):
        self.a = 1
        self._b = 2

    @property
    def b(self):
        return self._b

    @b.setter
    def b(self, v):
        self._b = v * 10

    def __getattr__(self, name):
        return 'missing ' + name


for i in range(200):
    g = G()
    g.b = i
    if i % 50 == 0:
        print(11, g.a, g.b, g.nope, order(g))


# 12. A site warmed on an instance whose attributes moved to its dict, then
# run on one that keeps them inline and shadows a class default.
class Dflt:
    flag = None

    def __init__(self):
        self.a = 1

    def get(self):
        return self.flag

    def set(self, v):
        self.flag = v


Dflt().set(0)
moved = Dflt()
vars(moved)
for _ in range(200):
    moved.get()
live = Dflt()
live.set('live')
print(12, live.get(), moved.get())
