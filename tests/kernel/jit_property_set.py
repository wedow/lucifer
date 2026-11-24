# Stores through a property in hot compiled loops, while what the store
# reaches changes: the setter replaced (setter() on the class, a new
# property on the class, one on a base), the property deleted (the plain
# attribute takes the store), fset None (AttributeError), a property
# subclass, an instance dict entry of the same name (the property wins),
# __setattr__ defined on the class later, the receiver's class assigned,
# a property on a metaclass, a setter that raises; a class whose own
# __setattr__ must run (from the start, raising, added and kept), the
# property re-initialized in place, traceback lines through a raising
# setter, setters returning fresh objects (finalizer probes on the stored
# value and the dropped result), the setter replaced during its own run,
# generator and async setters, and a site mixing property and plain
# stores. Every value, call count and error must be the interpreter's.
import sys
import warnings

warnings.simplefilter('ignore', RuntimeWarning)       # the async setter's coroutine is never awaited

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

seen = []


class Watched:
    def __setattr__(self, k, v):
        seen.append(k)
        object.__setattr__(self, k, v)

    @property
    def x(self):
        return self.__dict__.get('_x')

    @x.setter
    def x(self, v):
        self.__dict__['_x'] = v


class Frozen:
    def __setattr__(self, k, v):
        raise AttributeError('frozen ' + k)

    @property
    def x(self):
        return 0

    @x.setter
    def x(self, v):
        self.__dict__['_x'] = v


class Later:
    @property
    def x(self):
        return self.__dict__.get('_x')

    @x.setter
    def x(self, v):
        self.__dict__['_x'] = v


def own_setattr(n):
    del seen[:]
    w = Watched()
    for i in range(n):
        w.x = i
    errs = set()
    f = Frozen()
    for i in range(n):
        try:
            f.x = i
        except AttributeError as e:
            errs.add(str(e))
    o = Later()
    for i in range(n):
        o.x = i
    Later.__setattr__ = Watched.__setattr__
    for i in range(n):
        o.x = i
    return len(seen), w.x, sorted(errs), '_x' in f.__dict__, o.x


class Reinit:
    def __init__(self):
        self._r = 0

    def _g(self):
        return self._r

    def _s1(self, v):
        self._r = v

    def _s2(self, v):
        self._r = -v
    r = property(_g, _s1)


def reinit(n):
    o = Reinit()
    for i in range(n):
        o.r = i
        if i == n // 2:
            Reinit.r.__init__(Reinit._g, Reinit._s2)
    return o.r


class Tb:
    @property
    def t(self):
        return 0

    @t.setter
    def t(self, v):
        if v == 1500:
            raise KeyError(v)
        self._t = v


def traceback_lines(n):
    import traceback
    o = Tb()
    lines = None
    done = 0
    for i in range(n):
        try:
            try:
                o.t = i
            finally:
                done += 1
        except KeyError:
            lines = [f.name for f in traceback.extract_tb(sys.exc_info()[2])]
    return done, lines


probes = []


class Probe:
    def __init__(self, k):
        self.k = k

    def __del__(self):
        probes.append(self.k)


class Ret:
    @property
    def v(self):
        return self._v

    @v.setter
    def v(self, value):
        self._v = value
        if value.k % 3 == 0:
            return Probe('result')
        if value.k % 3 == 1:
            return [value]
        return None


def returns(n):
    del probes[:]
    o = Ret()
    for i in range(n):
        o.v = Probe(i)
    stored = len([p for p in probes if p != 'result'])
    results = probes.count('result')
    return stored, results, o.v.k


class Swap:
    def __init__(self):
        self._w = 0

    @property
    def w(self):
        return self._w

    @w.setter
    def w(self, value):
        if value == 1000:
            Swap.w = property(Swap.w.fget, lambda self, v: setattr(self, '_w', v * 3))
        self._w = value


def swap_during(n):
    o = Swap()
    for i in range(n):
        o.w = i
    return o.w


class GenSet:
    @property
    def g(self):
        return self._g

    @g.setter
    def g(self, value):
        self._g = value
        yield value


class AsyncSet:
    @property
    def a(self):
        return self._a

    @a.setter
    async def a(self, value):
        self._a = value


def odd_setters(n):
    o = GenSet()
    p = AsyncSet()
    for i in range(n):
        o.g = i
        p.a = i
    return hasattr(o, '_g'), hasattr(p, '_a')


class Plain:
    def __init__(self):
        self.v = 0


def mixed_shapes(n):
    objs = [P(1), Plain(), P(2), Plain()]
    for i in range(n):
        for o in objs:
            o.v = i
    return [o.v for o in objs]


print('setters', setters(N))
print('from base', from_base(N))
print('read only', read_only(N))
print('subclass and shadow', subclass_and_shadow(N))
print('later setattr and class', later_setattr_and_class(N))
print('metaclass', metaclass_property(N))
print('raising', raising(N))
print('own setattr', own_setattr(N))
print('reinit', reinit(N))
print('traceback', traceback_lines(N))
print('returns', returns(N))
print('swap during', swap_during(N))
print('odd setters', odd_setters(N))
print('mixed shapes', mixed_shapes(N))
