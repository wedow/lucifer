# Warm a loop so its attribute, global and method lookups are cached, then
# change what they resolve to. Every result must follow the change.
import builtins
import sys

WARM = 300


def show(label, values):
    print(label, values)


def run(fn, *args):
    out = []
    for _ in range(WARM):
        r = fn(*args)
    out.append(r)
    return out


# A class attribute.
class C:
    v = 1


def read_v(o):
    return o.v


c = C()
before = run(read_v, c)
C.v = 2
show('class attr', before + run(read_v, c))
c.v = 'inst'
show('class attr, instance shadows', run(read_v, c))
del c.v
del C.v
try:
    read_v(c)
except AttributeError as e:
    show('class attr deleted', str(e))


# An attribute of a base, then one that shadows it.
class Base:
    v = 'base'


class Mid(Base):
    pass


class Leaf(Mid):
    pass


leaf = Leaf()
r = run(read_v, leaf)
Base.v = 'base2'
r += run(read_v, leaf)
Mid.v = 'mid'
r += run(read_v, leaf)
del Mid.v
r += run(read_v, leaf)
show('base attr', r)


# Methods, then an instance attribute that hides one.
class M:
    def f(self):
        return 'M.f'


def call_f(o):
    return o.f()


m = M()
r = run(call_f, m)
M.f = lambda self: 'M.f2'
r += run(call_f, m)
m.f = lambda: 'inst f'
r += run(call_f, m)
del m.f
r += run(call_f, m)
show('method', r)


# __bases__ assignment.
class P1:
    v = 'p1'

    def who(self):
        return 'P1'


class P2:
    v = 'p2'

    def who(self):
        return 'P2'


class K(P1):
    pass


k = K()


def who(o):
    return o.who() + o.v


r = run(who, k)
K.__bases__ = (P2,)
r += run(who, k)
show('__bases__', r)


# obj.__class__ assignment.
class X:
    def name(self):
        return 'X'


class Y:
    def name(self):
        return 'Y'


def name(o):
    return o.name()


o = X()
r = run(name, o)
o.__class__ = Y
r += run(name, o)
show('__class__', r)


# A global rebound, then removed.
g = 1


def read_g():
    return g


r = run(read_g)
g = 2
r += run(read_g)
del g
try:
    read_g()
except NameError as e:
    r.append(str(e))
g = 3
r += run(read_g)
show('global', r)


# A global that comes to hide a builtin, then goes away.
def length(x):
    return len(x)


r = run(length, 'abc')
len = lambda x: -1
r += run(length, 'abc')
del len
r += run(length, 'abc')
show('global shadows builtin', r)


# builtins.len replaced and restored.
real_len = builtins.len
r = run(length, 'abc')
builtins.len = lambda x: 42
r += run(length, 'abc')
builtins.len = real_len
r += run(length, 'abc')
show('builtins.len', r)


# A function's __code__, __defaults__ and __kwdefaults__.
def f(a=1):
    return ('f', a)


def call_f1():
    return f()


r = run(call_f1)
f.__code__ = (lambda a=1: ('lambda', a)).__code__
r += run(call_f1)
f.__defaults__ = (7,)
r += run(call_f1)
show('f.__code__', r)


def kw(*, k=1):
    return k


def call_kw():
    return kw()


r = run(call_kw)
kw.__kwdefaults__ = {'k': 9}
r += run(call_kw)
show('f.__kwdefaults__', r)


# Instance attributes: added, removed, many added (the dict resizes).
class Inst:
    pass


i = Inst()
i.a = 1


def read_a(o):
    return o.a


r = run(read_a, i)
i.a = 2
r += run(read_a, i)
for n in range(40):
    setattr(i, 'x%d' % n, n)
r += run(read_a, i)
del i.a
try:
    read_a(i)
except AttributeError as e:
    r.append(str(e))
i.a = 'back'
r += run(read_a, i)
show('instance attr', r)


# A data descriptor and __getattr__ added to the class after caching.
class D:
    pass


d = D()
d.v = 'dict'
r = run(read_v, d)
D.v = property(lambda self: 'prop')
r += run(read_v, d)
del D.v
r += run(read_v, d)
D.__getattr__ = lambda self, name: 'getattr ' + name
r += run(lambda o: o.missing, d)
show('descriptor', r)


# Attributes of a class, and a metaclass that comes to shadow one.
class Meta(type):
    pass


class T(metaclass=Meta):
    z = 'T.z'


def read_z(o):
    return o.z


r = run(read_z, T)
T.z = 'T.z2'
r += run(read_z, T)
Meta.z = property(lambda cls: 'meta prop')
r += run(read_z, T)
show('class attr via class', r)


# A module attribute.
mod = type(sys)('probe')
mod.z = 'one'
r = run(read_z, mod)
mod.z = 'two'
r += run(read_z, mod)
del mod.z
try:
    read_z(mod)
except AttributeError:
    r.append('gone')
show('module attr', r)

# A loop that mutates its own lookup target mid-flight.
class Live:
    v = 0


live = Live()
seen = []
for n in range(WARM):
    seen.append(live.v)
    if n % 100 == 99:
        Live.v = n
show('mid-loop', sorted(set(seen)))
