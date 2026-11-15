# What compiled code reads from a class once (a special method, a base)
# is the class's own answer every time: an operator's method replaced,
# added or removed while a loop that uses it runs; a base class changed;
# a method added to a base; an instance's class reassigned; a class whose
# special methods keep changing.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    def __init__(self, v):
        self.v = v


class B(A):
    pass


class Eq:
    def __init__(self, v):
        self.v = v

    def __eq__(self, other):
        return self.v == other.v


def plain_eq(n):
    a, b = A(1), A(1)
    hits = 0
    for i in range(n):
        if a == b:
            hits += 1
        if a == a:
            hits += 2
        if a != b:
            hits += 4
    return hits


def eq_added(n):
    a, b = A(1), A(1)
    out = []
    for i in range(n):
        if i == n - 30:
            A.__eq__ = lambda self, other: self.v == other.v
        if i == n - 20:
            A.__eq__ = lambda self, other: False
        if i == n - 10:
            del A.__eq__
        out.append(a == b)
    return out[-35:], out.count(True)


def base_changed(n):
    class X:
        pass

    class Y:
        def __lt__(self, other):
            return True

    class Z(X):
        pass

    p, q = Z(), Z()
    out = []
    for i in range(n):
        if i == n - 6:
            Z.__bases__ = (Y,)
        try:
            out.append(p < q)
        except TypeError:
            out.append('no')
    return out[-8:]


def base_method(n):
    b1, b2 = B(1), B(2)
    out = []
    for i in range(n):
        if i == n - 4:
            A.__add__ = lambda self, other: self.v + other.v
        try:
            out.append(b1 + b2)
        except TypeError:
            out.append('no')
    del A.__add__
    return out[-6:]


def reclassed(n):
    x, y = A(3), Eq(3)
    out = []
    for i in range(n):
        if i == n - 5:
            x.__class__ = Eq
        if i == n - 2:
            x.__class__ = A
        out.append(x == y)
        out.append(y == x)
    return out[-12:]


def churn(n):
    a, b = Eq(1), Eq(1)
    hits = 0
    for i in range(n):
        if i % 7 == 0:
            Eq.__ne__ = (lambda self, other: True) if i % 2 else (lambda self, other: False)
        hits += a == b
        hits += (a != b) * 3
    del Eq.__ne__
    return hits


def reflected(n):
    class L:
        def __eq__(self, other):
            return NotImplemented

    class R(L):
        def __eq__(self, other):
            return 'r'

    l, r = L(), R()
    out = []
    for i in range(n):
        out.append(l == r)
        if i == n - 3:
            del R.__eq__
    return out[-5:]


import enum


class Direction(enum.Enum):
    FORWARD = 1
    BACKWARD = 2


class Limits:
    LOW = 3
    HIGH = 10 ** 6
    NAME = 'limits'


class Narrow(Limits):
    LOW = 5


class Meta(type):
    def __getattr__(cls, name):
        if name == 'MISSING':
            return 'made up'
        raise AttributeError(name)


class Dynamic(metaclass=Meta):
    PRESENT = 'here'


def class_constants(n):
    hits = 0
    for i in range(n):
        d = Direction.FORWARD if i % 3 else Direction.BACKWARD
        if d is Direction.FORWARD:
            hits += Limits.LOW
        elif Narrow.LOW < Narrow.HIGH:
            hits += Narrow.LOW + len(Limits.NAME)
    return hits, Direction.FORWARD.name, Direction.BACKWARD.value


def class_constants_change(n):
    out = []
    for i in range(n):
        if i == n - 12:
            Limits.LOW = 30
        if i == n - 9:
            Narrow.LOW = 50
        if i == n - 6:
            del Narrow.LOW
        if i == n - 3:
            Limits.LOW = property(lambda self: 1)
        out.append((Limits.LOW if not isinstance(Limits.LOW, property) else 'prop', Narrow.LOW if not isinstance(Narrow.LOW, property) else 'prop'))
    Limits.LOW = 3
    Narrow.LOW = 5
    return out[-14:]


def metaclass_attrs(n):
    out = []
    for i in range(n):
        if i == n - 4:
            Meta.PRESENT = 'meta data'          # no descriptor: the class's own still wins
        if i == n - 2:
            Meta.PRESENT = property(lambda cls: 'meta property')
        out.append((Dynamic.PRESENT, Dynamic.MISSING))
    del Meta.PRESENT
    return out[-6:]


for f in (plain_eq, eq_added, base_changed, base_method, reclassed, churn, reflected, class_constants,
          class_constants_change, metaclass_attrs):
    print(f.__name__, f(N))
    print(f.__name__, f(N // 4))
