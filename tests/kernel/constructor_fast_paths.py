"""Calling builtin classes and classes with their own __new__ and object's
__init__ runs __new__ in the eval loop; results, errors and subclasses match
type.__call__."""

import collections
import decimal
import enum
import fractions


def attempt(label, make):
    for _ in range(2):
        try:
            r = make()
            print(label, type(r).__name__, r if not isinstance(r, (type, object)) or isinstance(r, (int, str, float, bytes, tuple, list, dict, set, frozenset, range, bytearray, complex)) else '')
        except Exception as ex:
            print(label, type(ex).__name__, ex)


attempt(1, lambda: int('12'))
attempt(2, lambda: int(3.9))
attempt(3, lambda: int('x'))
attempt(4, lambda: int(1, 2))
attempt(5, lambda: str(5))
attempt(6, lambda: str(b'a', 'ascii'))
attempt(7, lambda: float('1.5'))
attempt(8, lambda: float('z'))
attempt(9, lambda: tuple([1, 2]))
attempt(10, lambda: tuple(1))
attempt(11, lambda: range(3))
attempt(12, lambda: range(1, 10, 3))
attempt(13, lambda: range(1.5))
attempt(14, lambda: range())
attempt(15, lambda: bytes(3))
attempt(16, lambda: bytes('a'))
attempt(17, lambda: frozenset([1, 2, 1]))
attempt(18, lambda: complex(1, 2))
attempt(19, lambda: list('ab'))
attempt(20, lambda: list(1, 2))
attempt(21, lambda: dict(a=1))
attempt(22, lambda: set([1, 1]))
attempt(23, lambda: bytearray(b'xy'))
attempt(24, lambda: object())
attempt(25, lambda: object(1))
attempt(26, lambda: bool(0))
attempt(27, lambda: bool(1, 2))
attempt(28, lambda: slice(1, 2))
attempt(29, lambda: type(3))
attempt(30, lambda: type('T', (), {}))
attempt(31, lambda: type('T', 5, {}))
attempt(32, lambda: enumerate('ab'))
attempt(33, lambda: zip('ab', 'cd'))
attempt(34, lambda: map(str, [1]))
attempt(35, lambda: ValueError('v'))
attempt(36, lambda: KeyError(1, 2))
attempt(37, lambda: decimal.Decimal('1.5') + 1)
attempt(38, lambda: fractions.Fraction(3, 6))
attempt(39, lambda: collections.OrderedDict(a=1))
attempt(40, lambda: collections.deque([1, 2], 5))
attempt(41, lambda: collections.Counter('abca'))


class I(int):
    pass


class S(str):
    def __init__(self, *a):
        self.extra = 'init'


class T(tuple):
    def __new__(cls, a, b):
        return super().__new__(cls, (a, b))


try:
    class R(range):
        pass
except TypeError as ex:
    print(49, ex)
    R = range


attempt(50, lambda: I('7') + 1)
attempt(51, lambda: (S('s'), S('s').extra))
attempt(52, lambda: T(1, 2))
attempt(53, lambda: T(1))
attempt(54, lambda: R(3))
attempt(55, lambda: type(I(2)).__name__)


class Color(enum.Enum):
    RED = 1


attempt(56, lambda: Color(1))
attempt(57, lambda: Color(9))
P = collections.namedtuple('P', 'a b', defaults=[0])
attempt(58, lambda: P(1))
attempt(59, lambda: P())
attempt(60, lambda: P(1, 2, 3))
attempt(61, lambda: P(a=1, b=2))
attempt(62, lambda: P._make([5, 6]))


class Lies:
    def __new__(cls):
        return 5

    def __init__(self):
        print('never')


attempt(63, lambda: Lies())


class Sing:
    inst = None

    def __new__(cls):
        if cls.inst is None:
            cls.inst = object.__new__(cls)
        return cls.inst


print(64, Sing() is Sing())


class WithNewKw:
    def __new__(cls, *, flag=False):
        return ('kw', flag)


attempt(65, lambda: WithNewKw(flag=True))
attempt(66, lambda: WithNewKw(1))


# list() and tuple() of an exact list or tuple, or nothing, are copied by the loop.
src = [1, [2], 3]
cp = list(src)
cp.append(4)
print(70, src, cp, cp[1] is src[1], list(), tuple(), tuple(src), list((1, 2)), list([]), tuple([]))
tt = (1, 2)
print(71, tuple(tt) is tt, list(tt) == [1, 2], tuple(src) == (1, [2], 3), type(tuple(src)).__name__)


class LS(list):
    pass


class TS(tuple):
    pass


print(72, list(LS([1])), type(list(LS([1]))).__name__, tuple(TS((1,))), type(tuple(TS((1,)))).__name__)
for bad in (lambda: list(1), lambda: tuple(None), lambda: list(1, 2), lambda: list(sequence=[1]), lambda: tuple([1], 2)):
    try:
        bad()
    except TypeError as ex:
        print(73, ex)
print(74, list(range(3)), tuple('ab'), list({1: 2}), list(x for x in 'xy'), list(iter([1, 2])))
print(75, bytearray(b'ab') + bytearray([1]), end=' ')
ba = bytearray()
for i in (0, 255, 7):
    ba.append(i)
print(ba)
for bad in (256, -1, 1.5, 'a', None, 2**70, True):
    try:
        ba.append(bad)
        print(76, bad, 'ok', ba[-1])
    except (TypeError, ValueError) as ex:
        print(76, type(ex).__name__, ex)
view = memoryview(ba)
try:
    ba.append(1)
except BufferError as ex:
    print(77, ex)
view.release()
ba.append(9)


class BA(bytearray):
    def append(self, x):
        super().append(x + 1)


b2 = BA()
b2.append(1)
print(78, b2)
Pt = collections.namedtuple('Pt', 'x y')
pt = Pt(1, 2)
for _ in range(2):
    print(79, pt.x, pt.y, type(Pt.x).__name__, getattr(pt, 'x'))
try:
    pt.x = 5
except AttributeError as ex:
    print(80, ex)


class PtSub(Pt):
    pass


print(81, PtSub(3, 4).x, PtSub(3, 4).y)
try:
    Pt.x.__get__(5, int)
except TypeError as ex:
    print(82, ex)
