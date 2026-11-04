"""getattr() and unspecialized attribute loads answered without the kernel's
generic lookup: instance dict, class values, methods, slots, and every case
that must still run Python (properties, __getattr__, __getattribute__,
metaclass descriptors, missing names, non-str names)."""


class A:
    cv = 10
    __slots__ = ()


class B:
    cv = 1

    def __init__(self):
        self.x = 5
        self.cv = 2

    def m(self):
        return 'm'

    @property
    def p(self):
        return 'prop'

    @staticmethod
    def s():
        return 's'

    @classmethod
    def c(cls):
        return cls


class S:
    __slots__ = ('a', 'b')

    def __init__(self):
        self.a = 1


b = B()
for name in ('x', 'cv', 'm', 'p', 'c', '__class__', '__dict__', '__doc__', 'nope', '__module__'):
    for _ in range(2):
        try:
            v = getattr(b, name)
            print(1, name, v if name not in ('m', 'c', '__dict__') else type(v).__name__)
        except AttributeError as ex:
            print(1, name, 'AE', ex)
print(2, getattr(b, 'nope', 'dflt'), getattr(b, 'x', 'dflt'), getattr(A(), 'cv'), getattr(b, 'm')())
s = S()
print(3, getattr(s, 'a'), getattr(s, 'b', 'unset'))
b.m = lambda: 'shadow'
print(4, getattr(b, 'm')(), b.m())
del b.m
print(5, getattr(b, 'm')())


class G:
    def __getattr__(self, n):
        return 'ga ' + n


class GA(G):
    y = 3

    def __init__(self):
        self.z = 4


g = GA()
print(6, getattr(g, 'y'), getattr(g, 'z'), getattr(g, 'q'), g.q)


class GB:
    def __getattribute__(self, n):
        return 'gb ' + n


print(7, getattr(GB(), 'anything'), GB().foo)


class Meta(type):
    @property
    def mp(cls):
        return 'meta prop'

    def mm(cls):
        return 'meta method'

    mv = 'meta value'


class K(metaclass=Meta):
    kv = 'class value'
    mv = 'shadowing'


print(8, getattr(K, 'mp'), getattr(K, 'mm')(), getattr(K, 'mv'), getattr(K, 'kv'), K.mv)
print(9, getattr(K, '__new__') is getattr(object, '__new__'), getattr(B, '__new__') is object.__new__,
      getattr(B, 'm') is B.__dict__['m'], type(getattr(int, '__add__')).__name__, getattr(B, 's')())
print(10, getattr(B, '__name__'), getattr(B, '__mro__'), getattr(B, '__dict__')['cv'])
try:
    getattr(b, 1)
except TypeError as ex:
    print(11, ex)
try:
    getattr(b, 1, 2)
except TypeError as ex:
    print(11, ex)


print(13, getattr(1, 'real'), getattr('a', 'upper')(), getattr([], 'append').__name__, getattr(None, '__class__'))


class D:
    def __get__(self, o, t):
        return 'nondata'


class DD:
    def __get__(self, o, t):
        return 'data'

    def __set__(self, o, v):
        pass


class H:
    nd = D()
    d = DD()

    def __init__(self):
        self.nd = 'inst'
        self.d = 'inst'


h = H()
print(14, getattr(h, 'nd'), getattr(h, 'd'), h.nd, h.d)


class Slotted:
    __slots__ = ('v', '__dict__')


sl = Slotted()
sl.v = 1
sl.w = 2
print(15, getattr(sl, 'v'), getattr(sl, 'w'), getattr(sl, 'zz', 'none'))


def f():
    return 1


f.attr = 7
print(16, getattr(f, 'attr'), getattr(f, '__name__'), getattr(f, '__defaults__'))
import types
mod = types.ModuleType('mm')
mod.q = 1
print(17, getattr(mod, 'q'), getattr(mod, '__name__'), getattr(mod, 'nope', 0))


class Chg:
    def __init__(self):
        self.v = 1


c1 = Chg()
for i in range(3):
    print(18, getattr(c1, 'v'))
    c1.v += 1
Chg.v = 100
del c1.v
print(19, getattr(c1, 'v'))
Chg.__getattr__ = lambda self, n: 'late ' + n
print(20, getattr(c1, 'zz'))


# setattr(): the same shortcuts, and the cases that run Python.
class SA:
    def __init__(self):
        self.a = 1

    @property
    def p(self):
        return self._p

    @p.setter
    def p(self, v):
        self._p = v * 2

    @property
    def ro(self):
        return 0


class SB:
    __slots__ = ('x',)


class SC:
    def __setattr__(self, n, v):
        print('SC set', n, v)
        object.__setattr__(self, n, v)


sa = SA()
setattr(sa, 'a', 2)
setattr(sa, 'b', 3)
setattr(sa, 'p', 4)
print(21, sa.a, sa.b, sa.p, sa.__dict__)
for target, name, value in ((sa, 'ro', 1), (SB(), 'x', 1), (SB(), 'y', 1), (SC(), 'z', 1), (1, 'x', 1), ([], 'x', 1),
                            (sa, 1, 1), (None, '__class__', 1), (sa, '__class__', int), (sa, '__dict__', 5), (SA, 'cls', 1),
                            (int, 'cls', 1), (object(), 'a', 1), (f, 'attr2', 8), (types.ModuleType('m'), 'v', 2)):
    try:
        print(22, setattr(target, name, value), getattr(target, name, 'unset') if not isinstance(name, int) else '')
    except (AttributeError, TypeError) as ex:
        print(22, type(ex).__name__, ex)
print(23, SA.cls, f.attr2)
try:
    setattr(sa)
except TypeError as ex:
    print(24, ex)
import dataclasses


@dataclasses.dataclass(frozen=True)
class Fr:
    x: int


try:
    setattr(Fr(1), 'x', 2)
except dataclasses.FrozenInstanceError as ex:
    print(25, ex)
