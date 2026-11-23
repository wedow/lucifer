# What every instance holds under each attribute name, recorded per class
# (int, float, or any object), kept by every writer: the interpreter's
# stores, setattr and object.__setattr__, compiled stores of numbers and
# of objects, a str or a bool or a big int or a subclass's instance into a
# name that held ints, a float into one, deletion, vars() and assigning
# or deleting __dict__, __class__ assignment, copy and pickle. After each,
# every instance's words must agree with its class's record (a check the
# runtime offers; elsewhere it is always true), and every value read back
# must be the interpreter's.
import copy
import pickle
import sys

try:
    from _lucifer import inline_kinds_ok
except ImportError:
    def inline_kinds_ok(o):
        return True

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000


class P:
    def __init__(self, x, y):
        self.x = x
        self.y = y


class Q:
    def __init__(self, x):
        self.x = x


class MyInt(int):
    pass


def ok(objs):
    return all(inline_kinds_ok(o) for o in objs)


def ints(n):
    ps = [P(i, i * 0.5) for i in range(20)]
    for i in range(n):
        p = ps[i % 20]
        p.x = i
        p.y = i * 0.25
    return ok(ps), sum(p.x for p in ps), sum(p.y for p in ps)


def generalize(n):
    ps = [P(i, float(i)) for i in range(20)]
    for i in range(n):
        p = ps[i % 20]
        p.x = i
        if i == n // 2:
            p.x = 'seven'
        if i == n // 2 + 1:
            p.y = 3
    return ok(ps), [p.x for p in ps[:3]], [p.y for p in ps[:3]]


def odd_values(n):
    qs = [Q(i) for i in range(10)]
    for i in range(n):
        qs[i % 10].x = i
    qs[0].x = True
    qs[1].x = 2 ** 70
    qs[2].x = MyInt(5)
    qs[3].x = -2 ** 63
    for i in range(n):
        qs[4 + i % 6].x = i
    return ok(qs), [q.x for q in qs[:5]]


def writers(n):
    out = []
    for i in range(n):
        p = P(i, i + 0.5)
        setattr(p, 'x', i * 3)
        object.__setattr__(p, 'y', i * 1.5)
        if i % 7 == 0:
            del p.x
            p.x = i
        if i % 11 == 0:
            vars(p)
            p.x = i + 1
        if i % 13 == 0:
            p.__dict__ = {'x': i, 'y': 0.5}
        if i % 17 == 0:
            del p.__dict__
            p.x = i
            p.y = 1.0
        if i % 19 == 0:
            p.__class__ = Q
        out.append(p)
    dup = [copy.copy(p) for p in out[:50]] + [pickle.loads(pickle.dumps(p)) for p in out[50:100]]
    return ok(out), ok(dup), sum(getattr(p, 'x', 0) for p in out), sum(getattr(p, 'x', 0) for p in dup)


def floats(n):
    ps = [P(i * 0.5, i * 0.25) for i in range(20)]
    for i in range(n):
        p = ps[i % 20]
        p.x = p.x * 0.5 + i
        p.y = p.y + 1.0
    ps[3].x = 7
    return ok(ps), round(sum(p.x for p in ps), 6), sum(p.y for p in ps)


print('ints', ints(N))
print('generalize', generalize(N))
print('odd values', odd_values(N))
print('writers', writers(N // 10))
print('floats', floats(N))
