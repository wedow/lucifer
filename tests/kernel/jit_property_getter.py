# Property loads at a site of one class, compiled hot: the getter is called
# in line. Then what such a load relies on changes: the getter replaced in
# place by property.__init__ (no new class version), the class's property
# replaced by a plain value, a getter that raises, one that is a generator
# function, and a subclass whose property a __getattr__ stands behind.

N = 3000


class Point:
    def __init__(self, x):
        self._x = x

    # *args: CPython does not specialize the load, so it too reads the
    # getter each time and sees it replaced.
    @property
    def x(self, *args):
        return self._x * 2


def total(objs, n):
    s = 0
    for i in range(n):
        s += objs[i % len(objs)].x
    return s


pts = [Point(i) for i in range(10)]
print("hot", total(pts, N))

# The getter replaced in place: the same property object, the same class version.
prop = Point.__dict__["x"]
property.__init__(prop, lambda self, *args: self._x + 1000)
print("new getter", total(pts, N))

# The property replaced on the class by a plain value.
Point.x = 7
print("plain", total(pts, N))


class Fails:
    @property
    def v(self):
        if self.n == 3:
            raise ValueError("bad " + str(self.n))
        return self.n

    def __init__(self, n):
        self.n = n


def read_all(objs):
    out = []
    for o in objs:
        try:
            out.append(o.v)
        except ValueError as e:
            out.append(str(e))
    return out


objs = [Fails(i % 5) for i in range(50)]
for _ in range(100):
    r = read_all(objs)
print("raises", r[:6])


class Gen:
    @property
    def items(self):
        yield 1
        yield 2


def gen_sum(g, n):
    s = 0
    for _ in range(n):
        s += sum(g.items)
    return s


print("generator", gen_sum(Gen(), N))


class Lazy:
    @property
    def val(self):
        raise AttributeError("val")

    def __getattr__(self, name):
        return "fallback " + name


def read_val(o, n):
    out = None
    for _ in range(n):
        out = o.val
    return out


print("getattr", read_val(Lazy(), N))
