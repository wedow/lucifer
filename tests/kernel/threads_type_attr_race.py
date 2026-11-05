"""Threads replace class attributes, methods and special methods while others
look them up and call them: a lookup returns the value before or after the
replacement, never a freed one, through the type cache and through a base
class. Each replacement drops the last reference to the value it replaces."""
import threading

ROUNDS = 3000
READS = 6000


class Base:
    def inherited(self):
        return 0

    attr = 'b0'


class C(Base):
    def m(self):
        return 0

    def __len__(self):
        return 0

    def __str__(self):
        return 's0'

    def __add__(self, other):
        return 0

    prop = property(lambda self: 0)
    attr2 = 'a0'


class D:
    def __init__(self):
        self.v = 0


class E:
    def __call__(self):
        return 0


def method(i):
    return lambda self: i


def binary(i):
    return lambda self, other: i


def prop(i):
    return property(lambda self: i)


def writer():
    for i in range(1, ROUNDS):
        C.m = method(i)
        C.__len__ = method(i)
        C.__str__ = lambda self, i=i: 's%d' % i
        C.__add__ = binary(i)
        C.prop = prop(i)
        C.attr2 = 'a%d' % i
        D.__init__ = lambda self, i=i: setattr(self, 'v', i)
        E.__call__ = method(i)
        Base.inherited = method(i)
        Base.attr = 'b%d' % i


def reader(out):
    o = C()
    e = E()
    bad = 0
    for _ in range(READS):
        if not 0 <= o.m() < ROUNDS:
            bad += 1
        if not 0 <= len(o) < ROUNDS:
            bad += 1
        if not str(o)[0] == 's':
            bad += 1
        if not 0 <= o + 1 < ROUNDS:
            bad += 1
        if not 0 <= o.prop < ROUNDS:
            bad += 1
        if not o.attr2[0] == 'a' or not o.attr[0] == 'b':
            bad += 1
        if not 0 <= o.inherited() < ROUNDS:
            bad += 1
        if not 0 <= C.__len__(o) < ROUNDS:
            bad += 1
        if not 0 <= D().v < ROUNDS:
            bad += 1
        if not 0 <= e() < ROUNDS:
            bad += 1
    out.append(bad)


out = []
rs = [threading.Thread(target=reader, args=(out,)) for _ in range(3)]
w = threading.Thread(target=writer)
for t in rs + [w]:
    t.start()
for t in rs + [w]:
    t.join()
print('bad', sum(out), len(out))
print(C().m(), len(C()), str(C()), C() + 1, C().prop, C().attr2, C().attr, C().inherited(), D().v, E()())
