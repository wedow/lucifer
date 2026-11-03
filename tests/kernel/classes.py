class A:
    """A's doc"""
    x = 'A.x'

    def __init__(self, v):
        self.v = v

    def who(self):
        return 'A.who'

    def get(self):
        return self.v


a = A('value')
print(a.v, a.get(), a.who())
print(A.x, a.x)
print(A.__name__, A.__qualname__, A.__module__, A.__doc__)
print(type(a).__name__, type(A).__name__, type(type).__name__)
if type(a) is A and a.__class__ is A and A.__class__ is type:
    print('types')
a.x = 'a.x'
print(A.x, a.x)
del a.x
print(a.x)
A.y = 'A.y'
print(a.y)
del A.y


class B(A):
    x = 'B.x'

    def who(self):
        return super().who()

    def both(self):
        print('B.both', A.who(self), self.who())


b = B('bv')
print(b.who(), b.get(), b.x)
b.both()
if B.__base__ is A:
    print('__base__')
for k in B.__bases__:
    print('base', k.__name__)
for k in B.__mro__:
    print('mro', k.__name__)
if A.__doc__ is not None and B.__doc__ is None:
    print('doc not inherited')


class C(A):
    def who(self):
        return 'C.who'


class D(B, C):
    pass


for k in D.__mro__:
    print('diamond', k.__name__)
for k in D.mro():
    print('mro()', k.__name__)
print(D('dv').who())
if isinstance(b, A) and isinstance(b, B) and not isinstance(a, B):
    print('isinstance')
if issubclass(D, A) and issubclass(D, (int, C)) and not issubclass(A, D):
    print('issubclass')
if isinstance(A, type) and isinstance(a, object) and issubclass(type, object):
    print('object and type')


class Outer:
    class Inner:
        def method(self):
            pass


print(Outer.Inner.__qualname__, Outer.Inner.method.__qualname__)


class Meta(type):
    @classmethod
    def __prepare__(mcls, name, bases, **kw):
        print('Meta.__prepare__', mcls.__name__, name)
        return {}

    def __new__(mcls, name, bases, ns, **kw):
        print('Meta.__new__', name)
        return super().__new__(mcls, name, bases, ns)

    def __init__(cls, name, bases, ns, **kw):
        print('Meta.__init__', name)
        super().__init__(name, bases, ns)

    def __call__(cls, *args, **kw):
        print('Meta.__call__', cls.__name__)
        return super().__call__(*args, **kw)


class M(metaclass=Meta, flag='on'):
    def __init__(self, v):
        print('M.__init__', v)


m = M('mv')
print(type(M).__name__, type(m).__name__)


class SubM(M):
    pass


print(type(SubM).__name__)


class Base:
    def __init_subclass__(cls, tag='none', **kw):
        print('__init_subclass__', cls.__name__, tag)
        super().__init_subclass__(**kw)


class Sub1(Base, tag='t1'):
    pass


class Sub2(Sub1):
    pass


class Named:
    def __set_name__(self, owner, name):
        print('__set_name__', owner.__name__, name)


class Owner:
    first = Named()
    second = Named()


class New:
    def __new__(cls, *args):
        print('New.__new__', cls.__name__, *args)
        return super().__new__(cls)

    def __init__(self, *args):
        print('New.__init__', *args)


New('p', 'q')


class Other:
    def __new__(cls):
        return a


if Other() is a:
    print('__new__ returned another type: no __init__')


class GetAttr:
    there = 'there'

    def __getattr__(self, name):
        return name


g = GetAttr()
print(g.there, g.elsewhere)


class GetAttribute:
    def __getattribute__(self, name):
        print('__getattribute__', name)
        return object.__getattribute__(self, name)


ga = GetAttribute()
ga.field = 'f'
print(ga.field)


class SetAttr:
    def __setattr__(self, name, value):
        print('__setattr__', name, value)
        object.__setattr__(self, name, value)

    def __delattr__(self, name):
        print('__delattr__', name)
        object.__delattr__(self, name)


s = SetAttr()
s.k = 'v'
print(s.k)
del s.k


class Slots:
    __slots__ = ('a', 'b')


sl = Slots()
sl.a = 'sa'
print(sl.a, type(Slots.a).__name__)


class P:
    pass


class Q:
    pass


p = P()
p.__class__ = Q
print(type(p).__name__)


class Eq:
    def __eq__(self, other):
        return True


if Eq.__hash__ is None and P.__hash__ is object.__hash__:
    print('__eq__ without __hash__')
if object.__eq__(p, p) is True and object.__eq__(p, a) is NotImplemented:
    print('object.__eq__')
if object.__ne__(p, p) is False and object.__ne__(p, a) is NotImplemented:
    print('object.__ne__')
print(type.__repr__(A), type.__repr__(int))

T = type('T', (A,), {'x': 'T.x'})
print(T.__name__, T.__qualname__, T.__module__, T('tv').get(), T.x, T.__base__.__name__)
if type(int) is type and int.__base__ is object and object.__base__ is None:
    print('static types')
print(int.__name__, int.__module__, object.__qualname__)


def make():
    class Local:
        def __repr__(self):
            return 'Local'
    return Local


print(make().__qualname__)
