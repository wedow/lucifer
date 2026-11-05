"""== and != on instances of classes that keep object's equality, alternating
between classes in one hot loop, then after one class gains __eq__ and
another loses it: compiled code must follow each change."""


class A:
    pass


class B:
    pass


class C:
    def __eq__(self, other):
        return True


def count(pairs, rounds):
    eq = ne = 0
    for _ in range(rounds):
        for x, y in pairs:
            eq += x == y
            ne += x != y
    return eq, ne


a, b, c = A(), B(), C()
pairs = [(a, a), (a, b), (b, b), (b, None), (None, a), (c, a), (a, c)]
print(count(pairs, 3000))
A.__eq__ = lambda self, other: other is None
print(count(pairs, 3000))
del C.__eq__
print(count(pairs, 3000))
del A.__eq__
print(count(pairs, 3000))
