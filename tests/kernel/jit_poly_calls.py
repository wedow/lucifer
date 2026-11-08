# Method calls at sites that meet several classes. Compiled code tests the
# receiver's class once and builds the call once per class, each with its
# own method inlined: through arguments with calls and attribute loads of
# their own, nested polymorphic calls, arguments with a branch, methods that
# raise into the caller's handler, then a fifth class, a method rebound on
# one class, a class changed to inherit another method, and receivers whose
# attributes move to a dict. Each result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Shape:
    def __init__(self, v):
        self.v = v

    def scale(self, k):
        return self.v * k

    def area(self, k):
        return k


class Square(Shape):
    def area(self, k):
        return self.v * self.v + k


class Circle(Shape):
    def area(self, k):
        return 3 * self.v * self.v - k


class Tri(Shape):
    def area(self, k):
        return self.v * k // 2


class Rect(Shape):
    def area(self, k):
        if k < 0:
            raise ValueError(k)
        return self.v + k


class Odd(Shape):
    def area(self, k):
        return -1


def total(objs, n):
    s = 0
    for i in range(n):
        o = objs[i % len(objs)]
        s += o.area(i)
    return s


def nested(objs, n):
    """Arguments that make calls of their own, polymorphic ones too."""
    s = 0
    for i in range(n):
        o = objs[i % len(objs)]
        p = objs[(i + 1) % len(objs)]
        s += o.area(p.area(i % 7) + o.scale(2))
    return s


def branchy(objs, n):
    """An argument with a branch in it: the call is built after the join."""
    s = 0
    for i in range(n):
        o = objs[i % len(objs)]
        s += o.area(i if i % 3 else i + 7 if i % 2 else 5)
    return s


def caught(objs, n):
    """A method that raises, into a handler of the caller."""
    s = 0
    for i in range(n):
        o = objs[i % len(objs)]
        try:
            s += o.area(-1 if i % 5 == 0 else i)
        except ValueError as e:
            s += 1000 + e.args[0]
    return s


def unused(objs, n):
    """A call whose result is dropped, and one in a condition."""
    hits = 0
    for i in range(n):
        o = objs[i % len(objs)]
        o.area(i)
        if o.area(i) > i:
            hits += 1
    return hits


objs = [Square(2), Circle(3), Tri(4), Rect(5)]
for f in (total, nested, branchy, unused):
    print(f.__name__, f(objs, N))
print('caught', caught([Rect(1), Square(2)], N))
print('fifth', total(objs + [Odd(1)], N))
Circle.area = lambda self, k: k + 1
print('rebound', total(objs, N))
del Circle.area
print('inherited', total(objs + [Circle(1)], N))
Tri.__bases__ = (Square,)
print('bases', total(objs, N))
objs[0].__dict__['extra'] = 1
print('dict', total(objs, N))
