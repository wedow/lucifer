# Classes called from compiled code, which makes the instance as the
# interpreter does (allocated, then __init__ called) when type.__call__ has
# nothing to decide: an __init__ that raises, one that returns a value, a
# class without __init__, and classes whose __init__ or __new__ changes or
# whose metaclass is not type. Each result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class P:
    def __init__(self, x, y):
        self.x = x
        self.y = y


class Bare:
    pass


class Bad:
    def __init__(self, v):
        if v % 7 == 0:
            raise ValueError(v)
        self.v = v


class Returns:
    def __init__(self, v):
        if v == 5:
            return 1


def make(n):
    s = 0
    for i in range(n):
        p = P(i, i + 1)
        b = Bare()
        s += p.x + p.y + (b is not None)
        try:
            s += Bad(i).v
        except ValueError as e:
            s -= e.args[0]
    return s


def returns(n):
    out = []
    for i in range(n):
        try:
            Returns(i % 10)
        except TypeError as e:
            out.append(str(e))
    return len(out), out[:1]


class Meta(type):
    def __call__(cls, *a):
        return 'meta'


class M(metaclass=Meta):
    def __init__(self, v):
        self.v = v


def metas(n):
    return sum(M(i) == 'meta' for i in range(n))


print(make(N))
print(returns(N))
print(metas(N))
P.__init__ = lambda self, x, y: setattr(self, 'x', -x) or setattr(self, 'y', 0)
print(make(N))
P.__new__ = lambda cls, x, y: Bare()
print(make(N))
