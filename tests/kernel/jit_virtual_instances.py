# Instances of plain classes made and used in hot code: with __slots__ and
# with their attributes inline, read, written, holding one another and
# calling their methods, then dropped, kept, or handed to code that sees
# them whole (vars, a list, a slot read while unset, a class changed while
# they are in use). Each result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class P:
    __slots__ = ('x', 'y')

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def norm1(self):
        return abs(self.x) + abs(self.y)

    def plus(self, o):
        return P(self.x + o.x, self.y + o.y)


class Q(P):
    __slots__ = ('z',)

    def __init__(self, x, y, z):
        P.__init__(self, x, y)
        self.z = z


class V:
    scale = 2

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def dot(self, o):
        return self.x * o.x + self.y * o.y

    def times(self, k):
        return V(self.x * k, self.y * k)


class Box:
    def __init__(self, item):
        self.item = item
        self.count = 0

    def bump(self):
        self.count += 1
        return self


class Dying:
    log = []

    def __init__(self, k):
        self.k = k

    def __del__(self):
        Dying.log.append(self.k)


class Guarded:
    def __init__(self, k):
        self.k = k

    def __setattr__(self, name, value):
        object.__setattr__(self, name, value * 10 if name == 'k' else value)


class Unset:
    __slots__ = ('a', 'b')

    def __init__(self, a):
        self.a = a


def slots_sum(n):
    s = 0
    for i in range(n):
        p = P(i, -i)
        q = p.plus(P(1, 2))
        s += q.norm1() + q.x
    return s


def subclass_slots(n):
    s = 0
    for i in range(n):
        q = Q(i, 2, 3)
        q.z += q.x
        s += q.z - q.y
    return s


def inline_values(n):
    s = 0
    for i in range(n):
        v = V(i, i + 1).times(V.scale)
        s += v.dot(V(1, -1)) + v.scale
    return s


def nested(n):
    s = 0
    for i in range(n):
        b = Box(P(i, 1)).bump().bump()
        s += b.item.x * b.count
    return s


def kept(n):
    out = []
    for i in range(n):
        v = V(i, 0)
        if i % 97 == 0:
            v.extra = i
            out.append(v)
        elif i % 89 == 0:
            out.append(P(i, i))
    return [sorted(vars(o)) if isinstance(o, V) else (o.x, o.y) for o in out]


def order(n):
    seen = set()
    for i in range(n):
        v = V(0, 0)
        if i % 2:
            v.b = 1
            v.a = 2
        else:
            v.a = 2
            v.b = 1
        v.x = i
        if i % 500 == 0:
            seen.add(tuple(vars(v)))
    return sorted(seen)


def unset(n):
    hits = 0
    for i in range(n):
        u = Unset(i)
        try:
            u.b
        except AttributeError:
            hits += 1
        u.b = u.a
        hits += u.b - i
    return hits


def finalized(n):
    Dying.log.clear()
    for i in range(n):
        Dying(i)
    return len(Dying.log), Dying.log[:3], Dying.log[-1]


def custom_setattr(n):
    s = 0
    for i in range(n):
        s += Guarded(i).k
    return s


def class_changes(n):
    s = 0
    dot = V.dot
    for i in range(n):
        if i == n // 2:
            V.dot = lambda self, o: 1000
        s += V(i, 1).dot(V(1, 1))
    V.dot = dot
    return s


def identity(n):
    same = 0
    for i in range(n):
        p = P(i, i)
        r = p
        same += (r is p) + (P(i, i) is p) + isinstance(p, P) + (type(p) is P)
    return same


print(slots_sum(N))
print(subclass_slots(N))
print(inline_values(N))
print(nested(N))
print(kept(N)[:5], len(kept(N)))
print(order(N))
print(unset(N))
print(finalized(N))
print(custom_setattr(N))
print(class_changes(N))
print(identity(N))
