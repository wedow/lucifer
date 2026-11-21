# A class's C slots follow its dunders (fixup_slot_dispatchers): defined in the
# body, inherited through the MRO, None for __hash__, set or deleted on the
# class or a base afterwards, and after __bases__ changes. Builtins reach the
# slots from C: sum (nb_add), sorted (tp_richcompare), hash, len, iter, bool.
class Num:
    def __init__(self, v):
        self.v = v

    def __add__(self, other):
        return Num(self.v + (other.v if isinstance(other, Num) else other))

    __radd__ = __add__

    def __lt__(self, other):
        return self.v < other.v

    def __repr__(self):
        return f"Num({self.v})"


class Sub(Num):
    pass


print(sum([Sub(1), Sub(2), Num(3)]), sorted([Sub(3), Num(1), Sub(2)]))


class Eq:
    def __eq__(self, other):
        return True


class NoHash:
    __hash__ = None


for cls in (Eq, NoHash):
    try:
        hash(cls())
        print(cls.__name__, "hashable")
    except TypeError as e:
        print(cls.__name__, e)


class Box:
    pass


b = Box()
print(bool(b))
Box.__len__ = lambda self: 0
print(bool(b), len(b))
Box.__iter__ = lambda self: iter("ab")
print(list(b))
del Box.__len__
print(bool(b))


class Leaf(Box):
    pass


Box.__getitem__ = lambda self, i: i * 2
print(Leaf()[4])


class A:
    def __len__(self):
        return 3


class B:
    def __len__(self):
        return 5


class C(A):
    pass


print(len(C()))
C.__bases__ = (B,)
print(len(C()))


class Meta(type):
    def __call__(cls, *args):
        return ("made", cls.__name__, args)


class Made(metaclass=Meta):
    pass


print(Made(1, 2))
