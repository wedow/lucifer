# tuple hash: xxHash values over every element kind, the hash cached in the
# tuple (3.14), and the errors of an element's __hash__.

class Counted:
    calls = 0

    def __hash__(self):
        Counted.calls += 1
        return Counted.calls


t = (1, Counted())
print(hash(t) == hash(t), t.__hash__() == hash(t), Counted.calls)
d = {t: 1}
print(d[t], t in {t}, Counted.calls)


class Sub(tuple):
    pass


s = Sub((Counted(),))
print(hash(s) == hash(s), Counted.calls)


class OwnHash(tuple):
    def __hash__(self):
        return 7


print(hash(OwnHash((1, 2))), hash(Sub((1, 2))) == hash((1, 2)))


class Wide:
    def __init__(self, h):
        self.h = h

    def __hash__(self):
        return self.h


for h in (0, 1, -1, -2, 2 ** 61 - 1, 2 ** 61, 2 ** 63 - 1, -2 ** 63, 2 ** 64, 2 ** 70, -2 ** 70, True):
    print(h, hash(Wide(h)), hash((Wide(h),)), hash((Wide(h), 0)))

values = [0, 1, -1, -2, 2 ** 30, 2 ** 62, -2 ** 62, 10 ** 30, -10 ** 30, True, False, None, 0.0, -0.0,
          1.5, -2.25, 1e300, float('inf'), float('-inf'), 1j, 2 - 3j, (), (1,), ((1, 2), (3,)),
          frozenset(), frozenset({1, 2, 3}), range(3), range(0, 10, 2), slice(1, 2), slice(None)]
for v in values:
    print(repr(v), hash((v,)), hash((v, v)), hash((0, v, None)))
for v in (Ellipsis, NotImplemented, int, tuple, object()):
    print(hash((v,)) == hash((v,)), hash((v, 1)) == hash((v, 1)), hash((v,)) == hash((1,)))
print(hash(tuple(range(100))), hash(tuple(range(-50, 50, 3))), hash(tuple(values[:24])))
print(hash(()), hash(((),)), hash((((),),)), hash((0,) * 7))
deep = ()
for i in range(200):
    deep = (deep, i)
print(hash(deep))


class Boom:
    calls = 0

    def __hash__(self):
        Boom.calls += 1
        raise ValueError('boom %d' % Boom.calls)


b = (1, Boom())
for _ in range(2):
    try:
        hash(b)
    except ValueError as e:
        print('ValueError', e)
print(Boom.calls)


class NotInt:
    def __hash__(self):
        return 'x'


class Unhashable:
    __hash__ = None


for bad in ((1, []), (1, {}), ((1, [2]),), (NotInt(),), (Unhashable(),), (1, (2, set()))):
    try:
        hash(bad)
    except TypeError as e:
        print('TypeError', e)
    try:
        {bad: 1}
    except TypeError as e:
        print('TypeError', e)


class IntSub(int):
    def __hash__(self):
        return 5


class StrSub(str):
    def __hash__(self):
        return 6


print(hash((IntSub(1), StrSub('a'))), hash(IntSub(1)), hash(StrSub('a')))
print(hash(('',)), hash((b'',)), hash(('', b'')) == hash(('', b'')))
print(hash(('abc', 1)) == hash(('abc', 1)), hash((b'xyz',)) == hash((b'xyz',)))


# A deeply nested tuple raises RecursionError instead of overflowing the C
# stack; a moderately deep one hashes as CPython's does.
class T(tuple):
    pass


for depth, make in ((1000, lambda t: (t,)), (100, lambda t: T((t,)))):
    t = ()
    for _ in range(depth):
        t = make(t)
    print(type(hash(t)).__name__)
    for _ in range(500_000):
        t = make(t)
    try:
        hash(t)
        print('hashed')
    except RecursionError as e:
        # A subclass's nesting may hit the recursion limit first.
        print('RecursionError', (type(make(())) is T or 'while hashing a tuple' in str(e)))
    del t
