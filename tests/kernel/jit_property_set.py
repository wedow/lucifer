# Stores through a property in hot compiled loops, while what the store
# reaches changes: the setter replaced (setter() on the class, a new
# property on the class, one on a base), the property deleted (the plain
# attribute takes the store), fset None (AttributeError), a property
# subclass, an instance dict entry of the same name (the property wins),
# __setattr__ defined on the class later, the receiver's class assigned,
# a property on a metaclass, and a setter that raises. Every value, call
# count and error must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
calls = []


class P:
    def __init__(self, v):
        self._v = v

    @property
    def v(self):
        return self._v

    @v.setter
    def v(self, value):
        self._v = value


def bump(ps, n):
    for _ in range(n):
        for o in ps:
            o.v = o.v + 1
    return [o._v for o in ps[:3]]


def setters(n):
    ps = [P(i) for i in range(10)]
    out = [bump(ps, n)]

    def twice(self, value):
        calls.append(1)
        self._v = 2 * value
    P.v = P.v.setter(twice)
    out.append(bump(ps, 3))
    P.v = property(lambda self: self._v, lambda self, value: setattr(self, '_v', value - 1))
    out.append(bump(ps, n))
    del P.v
    for o in ps:
        o.v = o._v                      # the plain attribute now
    out.append(bump(ps, 2))
    out.append([o.v for o in ps[:3]])
    return out, len(calls)


class Base:
    def __init__(self):
        self._w = 0

    @property
    def w(self):
        return self._w

    @w.setter
    def w(self, value):
        self._w = value


class Sub(Base):
    pass


def from_base(n):
    s = [Sub() for _ in range(5)]
    for i in range(n):
        for o in s:
            o.w = i
    Base.w = property(Base.w.fget, lambda self, value: setattr(self, '_w', -value))
    for i in range(n):
        for o in s:
            o.w = i
    return [o.w for o in s]


class RO:
    @property
    def r(self):
        return 1


def read_only(n):
    errs = set()
    o = RO()
    for i in range(n):
        try:
            o.r = i
        except AttributeError as e:
            errs.add(str(e))
    return sorted(errs)


class MyProp(property):
    pass


class Q:
    def __init__(self):
        self._q = 0

    def _get(self):
        return self._q

    def _set(self, value):
        self._q = value + 100
    q = MyProp(_get, _set)


def subclass_and_shadow(n):
    qs = [Q() for _ in range(5)]
    for o in qs:
        o.__dict__['q'] = 'shadow'
    for i in range(n):
        for o in qs:
            o.q = i
    return [(o.q, o.__dict__['q']) for o in qs[:2]]


class S:
    def __init__(self):
        self._s = 0

    @property
    def s(self):
        return self._s

    @s.setter
    def s(self, value):
        self._s = value


class S2:
    def __init__(self):
        self._s = 0

    @property
    def s(self):
        return self._s

    @s.setter
    def s(self, value):
        self._s = value * 10


def later_setattr_and_class(n):
    ss = [S() for _ in range(5)]
    for i in range(n):
        for o in ss:
            o.s = i
    seen = []
    S.__setattr__ = lambda self, k, v: (seen.append(k), object.__setattr__(self, k, v))
    for i in range(3):
        for o in ss:
            o.s = i
    del S.__setattr__
    ss[0].__class__ = S2
    for i in range(n):
        for o in ss:
            o.s = i
    return [o._s for o in ss], len(seen)


class Meta(type):
    @property
    def m(cls):
        return cls._m

    @m.setter
    def m(cls, value):
        cls._m = value + 1


class WithMeta(metaclass=Meta):
    _m = 0


def metaclass_property(n):
    for i in range(n):
        WithMeta.m = i
    return WithMeta.m


class Raises:
    @property
    def z(self):
        return 0

    @z.setter
    def z(self, value):
        if value % 100 == 0:
            raise ValueError(value)
        self._z = value


def raising(n):
    o = Raises()
    caught = 0
    for i in range(n):
        try:
            o.z = i
        except ValueError:
            caught += 1
    return caught, o._z


print('setters', setters(N))
print('from base', from_base(N))
print('read only', read_only(N))
print('subclass and shadow', subclass_and_shadow(N))
print('later setattr and class', later_setattr_and_class(N))
print('metaclass', metaclass_property(N))
print('raising', raising(N))
