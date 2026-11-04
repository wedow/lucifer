"""== and != the eval loop settles itself (bools, tuples item by item, objects
with object's equality) agree with __eq__ dispatch, including every case
that must still call Python."""

import collections
import itertools

nan = float('nan')
values = [True, False, 0, 1, 2, 2**70, 1.0, 0.0, nan, '', 'a', b'a', None, Ellipsis, int, bool,
          object, (), (1,), (1, 2), (1, nan), (nan,), ((1, 2), 3), [1], {1}, {'a': 1}]
for a, b in itertools.product(values, values):
    print(1, repr(a), repr(b), a == b, a != b, a < b if type(a) == type(b) == int else '-')
t = (1, nan)
print(2, t == t, t == (1, nan), (t, 1) == (t, 1), ((1, 2), (3,)) == ((1, 2), (3,)))

P = collections.namedtuple('P', 'x y')
print(3, P(1, 2) == P(1, 2), P(1, 2) == (1, 2), (1, 2) == P(1, 2), P(1, 2) != P(1, 3), P(1, 2) == P(1, 2, ) )


class Always:
    def __eq__(self, other):
        return True


class Never:
    def __eq__(self, other):
        return False

    def __ne__(self, other):
        return 'ne'


class Quiet:
    pass


class Loud(tuple):
    def __eq__(self, other):
        print('loud eq')
        return True


class Raise:
    def __eq__(self, other):
        raise RuntimeError('boom')


a, n, q = Always(), Never(), Quiet()
print(4, a == 1, 1 == a, n == n, n != n, q == q, q != q, q == Quiet(), q != Quiet(), q == 1, 1 == q)
print(5, (1, a) == (1, 2), (1, n) == (1, n), (1, 2) == (1, a), (q, 1) == (q, 1), (q,) == (Quiet(),))
print(6, Loud((1, 2)) == (1, 2), (1, 2) == Loud((1, 2)), Loud((1,)) != (2,), (Loud((1,)),) == ((1,),))
try:
    print((1, Raise()) == (1, 2))
except RuntimeError as ex:
    print(7, ex)
print(8, (2, Raise()) == (1, Raise()), (1, 2, Raise()) == (1, 3))


class Sub(Quiet):
    def __eq__(self, other):
        return 'sub'


print(9, q == Sub(), Sub() == q, Sub() != q)


class M(type):
    def __eq__(cls, other):
        return True

    __hash__ = type.__hash__


class WithM(metaclass=M):
    pass


print(10, WithM == int, int == WithM, int == int, int != bool, WithM == WithM)
print(11, True == 1, True is 1, (True, 1) == (1, True), (True,) == (1.0,), 5 == True, [1] == [1.0])
if (1, 2) == (1, 2) and not (1, 2) == (2, 1):
    print(12, 'branch')
