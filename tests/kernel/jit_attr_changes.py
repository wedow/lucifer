# Attribute loads compiled hot, then changed under the compiled code without
# a new class version: a property's getter replaced in place by
# property.__init__ (a getter too big to inline, read at a site of several
# classes, which compiled code calls directly). Then changes that do give a
# new version: a class default reassigned, a property and a data descriptor
# added on the base, and an instance's __class__ reassigned.
# The getters take *args so that CPython does not specialize the property
# load: its own cache would keep the old getter.

N = 3000


# A getter of straight-line code longer than an inlined callee may be.
exec('def _g1(*args):\n    s = 1\n' + '    s = (s * 31 + 7) & 0xffff\n' * 150 + '    return 1 if s >= 0 else 0\n')


def _g2(*args):
    return 2


class Base:
    p = property(_g1)
    flag = 'class'


class A(Base):
    def __init__(self):
        self.a = 'a'


class B(Base):
    def __init__(self):
        self.b = 'b'


class C(Base):
    pass


def read_p(objs, n):
    out = []
    for i in range(n):
        out.append(objs[i % len(objs)].p)
    return out


def read_flag(objs, n):
    out = []
    for i in range(n):
        out.append(objs[i % len(objs)].flag)
    return out


def read_a(objs, n):
    out = []
    for i in range(n):
        out.append(objs[i % len(objs)].a)
    return out


class Shadow:
    """A data descriptor: it wins over the instance's own value."""

    def __get__(self, obj, owner=None):
        return 'descriptor'

    def __set__(self, obj, value):
        pass


xs = [A(), B(), A(), B()]
print(1, sorted(set(read_p(xs, N))))
Base.__dict__['p'].__init__(_g2)        # the same property object, a new getter
print(1, read_p(xs, 4))
Base.__dict__['p'].__init__(_g1)
print(1, read_p(xs, 4))

print(2, sorted(set(read_flag(xs, N))))
Base.flag = 'changed'
print(2, read_flag(xs, 4))
A.flag = 'own class'
print(2, read_flag(xs, 4))

ys = [A(), A()]
print(3, sorted(set(read_a(ys, N))))
Base.a = property(lambda self: 'property')
print(3, read_a(ys, 2))
del Base.a
print(3, read_a(ys, 2))
Base.a = Shadow()
print(3, read_a(ys, 2))
del Base.a

print(4, sorted(set(read_a(ys, N))))
ys[1].__class__ = C
try:
    print(4, read_a(ys, 2))
except AttributeError as e:
    print(4, type(e).__name__)
ys[1].__class__ = A
print(4, read_a(ys, 2))
