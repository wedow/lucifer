# An attribute site that meets objects of several classes: data
# attributes at different places, methods, class attributes, properties,
# slots, instances with a __dict__ entry shadowing a method; a fifth and
# a sixth class turning up late; a class changed while the loop runs; and
# an attribute one of the classes lacks.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    kind = 'a'

    def __init__(self, i):
        self.x = i
        self.y = -i

    def area(self):
        return self.x * 2


class B:
    kind = 'b'

    def __init__(self, i):
        self.pad = None
        self.y = i * 3
        self.x = i + 1

    def area(self):
        return self.x + self.y


class C(A):
    kind = 'c'

    def area(self):
        return 7


class D:
    __slots__ = ('x', 'y')
    kind = 'd'

    def __init__(self, i):
        self.x = i * 5
        self.y = 1

    @property
    def area(self):
        return lambda: self.x


class E:
    def __init__(self, i):
        self.x = float(i)
        self.y = 'y'
        self.kind = 'e%d' % (i % 2)
        self.area = lambda: 11


class F:
    kind = 'f'

    def __init__(self, i):
        self.x = i

    def __getattr__(self, name):
        if name == 'y':
            return 42
        raise AttributeError(name)

    def area(self):
        return 0


def mix(n, classes):
    objs = [classes[i % len(classes)](i) for i in range(64)]
    total = 0
    kinds = {}
    for i in range(n):
        o = objs[i % 64]
        total += o.x
        total += len(str(o.y))
        total += o.area()
        k = o.kind
        kinds[k] = kinds.get(k, 0) + 1
    return total, sorted(kinds.items())


def late(n):
    objs = [A(1), B(2)] * (n // 2) + [C(3), D(4), E(5), F(6), A(7)]
    out = []
    for o in objs:
        out.append((o.x, o.y, o.area(), o.kind))
    return out[-7:], len(out)


def changed(n):
    objs = [A(i) if i % 2 else B(i) for i in range(8)]
    out = []
    for i in range(n):
        o = objs[i % 8]
        if i == n - 20:
            A.area = lambda self: -1
        if i == n - 10:
            B.kind = 'B!'
        if i == n - 5:
            del A.area
        try:
            out.append((o.area(), o.kind))
        except AttributeError as e:
            out.append(str(e))
    A.area = lambda self: self.x * 2
    B.kind = 'b'
    return out[-24:]


def missing(n):
    objs = [A(1), B(2), D(3)]
    hits = 0
    for i in range(n):
        o = objs[i % 3]
        try:
            hits += o.x + (o.pad is None)
        except AttributeError:
            hits += 100
    return hits


print('two', mix(N, [A, B]))
print('three', mix(N, [A, B, C]))
print('five', mix(N, [A, B, C, D, E]))
print('six', mix(N, [A, B, C, D, E, F]))
for f in (late, changed, missing):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 5))
