"""Attribute lookups through the type attribute cache see every change to a
class or its bases: set, replace, delete, bases, MRO, metaclass, dunder."""


class A:
    x = 1

    def f(self):
        return 'A.f'


class B(A):
    pass


class C(B):
    pass


b, c = B(), C()


def probe(label):
    print(label, getattr(c, 'x', 'none'), getattr(b, 'x', 'none'), c.f() if hasattr(c, 'f') else 'nof', hasattr(c, 'y'), c == b, repr(c)[:9])


for _ in range(3):
    probe(1)
A.x = 2
probe(2)
B.x = 3
probe(3)
del B.x
probe(4)
del A.x
probe(5)
A.y = 'y'
probe(6)
A.f = lambda self: 'new f'
probe(7)
C.f = lambda self: 'C.f'
probe(8)
del C.f
probe(9)
B.f = lambda self: 'B.f'
probe(10)
A.__eq__ = lambda self, other: 'eq!'
probe(11)
del A.__eq__
probe(12)
A.__repr__ = lambda self: 'reprA-reprA'
probe(13)
del A.__repr__
probe(14)


class D:
    x = 'D'
    def f(self):
        return 'D.f'


C.__bases__ = (D,)
probe(15)
C.__bases__ = (B,)
probe(16)
setattr(A, 'x', 'again')
probe(17)


class M(type):
    pass


class E(A, metaclass=M):
    pass


e = E()
for _ in range(2):
    print(18, e.x, E.x, type(E).__name__, E.__mro__ == (E, A, object))
M.extra = 'meta'
print(19, E.extra, getattr(A, 'extra', 'no'))
del M.extra
print(20, getattr(E, 'extra', 'no'))
A.x = 'late'
print(21, E.x, e.x, B().x)


class P:
    def __getattr__(self, n):
        return 'ga ' + n


p = P()
print(22, p.zz)
del P.__getattr__
try:
    p.zz
except AttributeError as ex:
    print(23, ex)
P.__getattr__ = lambda self, n: 'ga2 ' + n
print(24, p.zz)
P.__len__ = lambda self: 7
print(25, len(p), bool(p))
del P.__len__
print(26, bool(p))
P.__call__ = lambda self: 'called'
print(27, p())
del P.__call__
try:
    p()
except TypeError as ex:
    print(28, ex)
import abc


class Base(abc.ABC):
    pass


class Impl:
    pass


print(29, isinstance(Impl(), Base))
Base.register(Impl)
print(30, isinstance(Impl(), Base), issubclass(Impl, Base))
Impl.__eq__ = lambda s, o: True
print(31, Impl() == 3, 3 == Impl())
del Impl.__eq__
print(32, Impl() == 3)
for i in range(3):
    setattr(A, 'cnt%d' % i, i)
    print(33, [getattr(c, 'cnt%d' % j, None) for j in range(3)])
type.__setattr__(A, 'x', 'ts')
print(34, c.x)
A.__dict__  # mappingproxy read
class F:
    __slots__ = ('a',)
f = F()
F.a2 = 5
print(35, f.a2)
f.a = 1
print(36, f.a)
del F.a2
print(37, getattr(f, 'a2', 'gone'))


# A replaced or deleted class attribute's finalizer, which runs as the
# class changes, sees the class as it is after the change.
class Host:
    pass


class Dying:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        print(38, self.name, getattr(Host, self.name, 'gone'), getattr(Host(), self.name, 'gone'))


for name, how in (('x', 'set'), ('x', 'del'), ('__doc__', 'set'), ('__module__', 'set')):
    setattr(Host, name, Dying(name))
    print(39, name, type(getattr(Host, name)).__name__, type(getattr(Host(), name)).__name__)
    if how == 'set':
        setattr(Host, name, 'replaced')
    else:
        delattr(Host, name)
