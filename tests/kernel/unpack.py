# Unpacking, starred targets, containment by iteration and the iteration protocol.
a, b = 1, 2
a, b = b, a
print(a, b)
a, b, c = (1, 2, 3)
x, y = [4, 5]
(p, q), r = (1, 2), 3
[s, t] = (6, 7)
print(a, b, c, x, y, p, q, r, s, t)
a, *b = [1, 2, 3, 4]
print(a, b)
*a, b = (1, 2, 3, 4)
print(a, b)
a, *b, c = range(5)
print(a, b, c)
a, *b, c = (1, 2)
print(a, b, c)
a, b, *c = 1, 2
print(a, b, c)
*a, = (1, 2, 3)
print(a)
a, (b, *c), *d = 1, (2, 3, 4), 5, 6
print(a, b, c, d)
for i, (j, k) in [(1, (2, 3)), (4, (5, 6))]:
    print(i, j, k)
for first, *rest in [(1, 2, 3), (4,), [5, 6]]:
    print(first, rest)


class Counter:
    def __init__(self, n):
        self.n = n
        self.taken = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.n == 0:
            raise StopIteration
        self.n -= 1
        self.taken += 1
        return self.n


c = Counter(5)
a, b = c.__iter__().__next__(), c.__next__()
print(a, b, c.taken)
c = Counter(3)
x, y, z = c
print(x, y, z, c.taken)
c = Counter(6)
a, *b, d = c
print(a, b, d, c.taken)
c = Counter(4)
try:
    a, b = c
except ValueError as e:
    print(e, c.taken)
c = Counter(1)
try:
    a, b = c
except ValueError as e:
    print(e, c.taken)
c = Counter(0)
try:
    a, *b, d = c
except ValueError as e:
    print(e)
c = Counter(1)
try:
    a, *b, d = c
except ValueError as e:
    print(e)


class Seq:
    def __getitem__(self, i):
        if i >= 4:
            raise IndexError
        return i


a, b, c, d = Seq()
print(a, b, c, d)
a, *b = Seq()
print(a, b)
print(list(Seq()), tuple(Seq()), [*Seq()], (*Seq(),), 3 in Seq(), 4 in Seq(), 4 not in Seq())


class Contains:
    def __contains__(self, x):
        return x == 'yes'

    def __iter__(self):
        raise RuntimeError('iterated')


print('yes' in Contains(), 'no' in Contains())


class IterOnly:
    def __init__(self):
        self.log = []

    def __iter__(self):
        return iter([1, 2, 3])


print(2 in IterOnly(), 5 in IterOnly(), 5 not in IterOnly())


class EqLog:
    def __eq__(self, other):
        print('eq', other)
        return other == 2

    __hash__ = None


print(2 in [1, 2, 3], EqLog() in [1, 2, 3], 2 in (EqLog(),), 5 in (EqLog(),))

errors = (
    lambda: [None for a, b in [1]], lambda: exec_unpack(1),
    lambda: exec_unpack(None), lambda: exec_unpack(object()), lambda: exec_unpack((1,)), lambda: exec_unpack((1, 2, 3)),
    lambda: exec_unpack([]), lambda: exec_unpack([1, 2, 3, 4]), lambda: exec_unpack(iter([1])), lambda: exec_unpack(iter([1, 2, 3])),
    lambda: exec_unpack(range(5)), lambda: exec_unpack(Counter(1)),
    lambda: exec_star(1), lambda: exec_star(()), lambda: exec_star((1,)), lambda: exec_star(Counter(1)), lambda: exec_star(None),
    lambda: exec_star2((1,)), lambda: exec_star2([1, 2]), lambda: exec_star2(iter([])),
    lambda: iter(1), lambda: iter(None), lambda: iter(object()), lambda: next(1), lambda: next(iter([])), lambda: next(Counter(0)),
    lambda: [*1], lambda: [*None], lambda: (*1,), lambda: print(*1), lambda: len(1), lambda: len(None), lambda: len(object()),
    lambda: 1 in Counter, lambda: 1 in object(), lambda: [] in 1, lambda: 'a' in 5,
    lambda: next(iter([]), 'dflt'), lambda: next(Counter(0), 7), lambda: iter(Counter(1)).__next__(),
    lambda: iter(Seq()).__next__(), lambda: list(iter(Seq())), lambda: type(iter(Seq())).__name__,
)


def exec_unpack(v):
    a, b = v


def exec_star(v):
    a, *b, c = v


def exec_star2(v):
    a, b, *c = v


for t in errors:
    try:
        print(t())
    except Exception as e:
        print(type(e).__name__, e)

# a for loop over the protocols
for i in Seq():
    print(i)
for i in Counter(2):
    print(i)
for i in iter(range(2)):
    print(i)
total = 0
for i in range(1000):
    total += i
print(total)
for i in []:
    print('never')
else:
    print('else')
for i in (1, 2):
    pass
else:
    print('else', i)
n = 0
while True:
    n += 1
    for j in range(5):
        if j == 3:
            break
    else:
        print('no break')
    if n == 2:
        break
print(n, j)

# len
print(len([1, 2]), len((1, 2, 3)), len(range(5)), len(()))


class Sized:
    def __len__(self):
        return 7


class BadLen:
    def __len__(self):
        return -1


class FloatLen:
    def __len__(self):
        return 1.5


class BigLen:
    def __len__(self):
        return 10 ** 30


print(len(Sized()), bool(Sized()))
for k in (BadLen, FloatLen, BigLen):
    try:
        len(k())
    except Exception as e:
        print(type(e).__name__, e)

# subscript protocol on classes
class Sub:
    def __getitem__(self, k):
        return ('get', k)

    def __setitem__(self, k, v):
        print('set', k, v)

    def __delitem__(self, k):
        print('del', k)


s = Sub()
print(s[1], s[1:2], s[1:], s[:2], s[::3], s[1:2:3], s[:], s[1, 2], s[...], s[None])
s[1] = 2
s[1:2] = 3
s[::2] = 4
s[1, 2] = 5
del s[1]
del s[1:2]
del s[::2]
for obj in (1, None, object(), (1).__class__):
    for op in (lambda: obj[0], lambda: obj.__getitem__):
        try:
            op()
        except Exception as e:
            print(type(e).__name__, e)


def setit(o):
    o[0] = 1


def delit(o):
    del o[0]


for bad in (1, None, (1,), range(1)):
    for fn in (setit, delit):
        try:
            fn(bad)
        except Exception as e:
            print(type(e).__name__, e)
