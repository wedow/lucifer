# Writes of class attributes, which C makes for plain values under names no
# slot reads (luc-4yfw): readers of the class, its subclasses and its
# instances see each write, a metaclass's data descriptor or __setattr__
# still decides, dunders still update the slots, functions become methods,
# and builtin types refuse.
class Meta(type):
    @property
    def guarded(cls):
        return 'meta'

    @guarded.setter
    def guarded(cls, v):
        print('meta setter', v)


class Logged(type):
    def __setattr__(cls, name, value):
        print('setattr', name, value)
        super().__setattr__(name, value)


class A(metaclass=Meta):
    x = 0


class B(A):
    pass


class C(metaclass=Logged):
    pass


a, b = A(), B()
for i in range(2000):
    A.x += 1
    if i % 500 == 0:
        print(A.x, B.x, a.x, b.x)
B.x = 'own'
A.x = 'base'
print(A.x, B.x, a.x, b.x)
A.guarded = 5
print(A.guarded)
C.y = 1
print(C.y)
A.__len__ = lambda self: 7
print(len(a), len(b))
A.f = lambda self: 'method'
print(a.f(), b.f())
del A.x
print(hasattr(A, 'x'), B.x)
for target in (int, str, object):
    try:
        target.zz = 1
    except TypeError as e:
        print(type(e).__name__, e)
