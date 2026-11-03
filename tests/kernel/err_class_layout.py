class A:
    __slots__ = ('a',)


class B:
    pass


b = B()
b.__class__ = A
