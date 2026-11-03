# list.sort and sorted: stability, key, reverse, comparison counts, mutation and error behaviour.
calls = []


class Item:
    def __init__(self, v, tag=0):
        self.v = v
        self.tag = tag

    def __lt__(self, other):
        calls.append(1)
        return self.v < other.v


def count(values, **kw):
    del calls[:]
    items = [Item(v, i) for i, v in enumerate(values)]
    items.sort(**kw)
    return len(calls), [(x.v, x.tag) for x in items]


def lcg(n, mod, seed=12345):
    out = []
    for _ in range(n):
        seed = (seed * 1103515245 + 12345) % 2147483648
        out.append(seed % mod)
    return out


a = [3, 1, 2]
print(a.sort(), a, sorted([5, 2, 9, 1]), sorted((3, 1, 2)), sorted(range(5, 0, -1)), sorted([]), sorted([1]))
a = [3, 1, 2]
a.sort(reverse=True)
print(a, sorted([1, 2, 3], reverse=True), sorted([3, 1, 2], key=lambda x: -x), sorted([3, 1, 2], key=None))
print(sorted([-3, 1, -2, 4], key=lambda x: x * x))

# stability
pairs = [(1, 'a'), (0, 'b'), (1, 'c'), (0, 'd'), (1, 'e'), (0, 'f')]
print(sorted(pairs, key=lambda p: p[0]), sorted(pairs, key=lambda p: p[0], reverse=True))
print(count([1, 1, 1, 1, 1]), count([2, 1, 2, 1, 2, 1], reverse=True))
print(count([1, 0, 1, 0, 1, 0, 1, 0], key=lambda x: x.v))

# comparison counts decide the algorithm: runs, binary insertion, galloping, merges
for n in (0, 1, 2, 3, 5, 31, 32, 33, 63, 64, 65, 100, 257, 1000):
    print(n, count(lcg(n, 50))[0], count(lcg(n, 10 ** 9))[0], count(list(range(n)))[0], count(list(range(n, 0, -1)))[0],
          count(list(range(n)) + list(range(n)))[0], count(list(range(n, 0, -1)), reverse=True)[0])
print(count(lcg(300, 7))[1] == sorted(count(lcg(300, 7))[1], key=lambda p: p[0]))
runs = list(range(100, 200)) + list(range(0, 100)) + list(range(300, 400)) + list(range(200, 300))
print(count(runs)[0], count(runs, reverse=True)[0])
mixed = [x for pair in zip(range(0, 200, 2), range(1000, 1100)) for x in pair]
print(count(mixed)[0])
gallop = list(range(0, 500)) + list(range(250, 750))
print(count(gallop)[0], count(gallop[::-1])[0])
big = lcg(5000, 300)
res = sorted(big)
print(res[:12], res[-5:], res == sorted(big, reverse=True)[::-1], res == sorted(sorted(big)))
print(count(lcg(2000, 20))[0])

# key function is called once per element, in order, before any comparison
log = []


def key(x):
    log.append(x)
    return -x


print(sorted([3, 1, 2], key=key), log)
log = []
data = [2, 1]
data.sort(key=key, reverse=True)
print(data, log)

# key errors and comparison errors leave the list in a defined state
data = [3, 1, 2]


def bad_key(x):
    if x == 2:
        raise ValueError('bad key')
    return x


try:
    data.sort(key=bad_key)
except ValueError as e:
    print(e, data)


class Bad:
    def __init__(self, v):
        self.v = v

    def __lt__(self, other):
        if self.v == 2 or other.v == 2:
            raise RuntimeError('bad compare')
        return self.v < other.v


data = [Bad(3), Bad(1), Bad(2), Bad(0)]
try:
    data.sort()
except RuntimeError as e:
    print(e, [x.v for x in data].__len__(), sorted([x.v for x in data]))

# the list is empty while it sorts; changes made by key or compare are an error
data = [3, 1, 2]
seen = []


def peek(x):
    seen.append(len(data))
    return x


data.sort(key=peek)
print(data, seen)


def appender(x):
    data.append(0)
    return x


data = [3, 1, 2]
try:
    data.sort(key=appender)
except ValueError as e:
    print(e, data)
data = [3, 1, 2]


def clearer(x):
    del data[:]
    return x


try:
    data.sort(key=clearer)
except ValueError as e:
    print(e, data)


class Mutate:
    def __init__(self, v, owner):
        self.v = v
        self.owner = owner

    def __lt__(self, other):
        self.owner.append(1)
        return self.v < other.v


owner = []
data = [Mutate(2, owner), Mutate(1, owner)]
data.sort()
print([x.v for x in data], len(owner))
inner = [5, 4]
inner2 = [Mutate(2, inner), Mutate(1, inner)]
inner.sort()
print(inner)


class Self:
    lst = None

    def __init__(self, v):
        self.v = v

    def __lt__(self, other):
        Self.lst.append(0)
        return self.v < other.v


s = [Self(2), Self(1)]
Self.lst = s
try:
    s.sort()
except ValueError as e:
    print(e, len(s))

# mixed types and unorderable items
for bad in ([1, 'a'], [None, 1], [[1], 1], [3, 2, True, 0], [(1, 2), (1, 'a')], [object(), object()]):
    try:
        print(sorted(bad))
    except TypeError as e:
        print(e)

# argument handling
for call in (lambda: [1].sort(1), lambda: [2, 1].sort(key=1), lambda: [1].sort(foo=1),
             lambda: sorted(), lambda: sorted([1], 1), lambda: sorted(1), lambda: sorted([2, 1], key=1), lambda: sorted([2, 1], reverse=5),
             lambda: sorted([2, 1], reverse=0), lambda: [2, 1].sort(reverse=[]), lambda: sorted(iter([3, 1, 2]))):
    try:
        print(call())
    except TypeError as e:
        print(e)

# subclass, in-place identity, and sorting a list that is also being iterated
class SL(list):
    pass


sl = SL([3, 1, 2])
r = sl.sort()
print(r, sl, type(sorted(sl)).__name__)
x = [1, 2, 3]
y = x
x.sort(reverse=True)
print(y, x is y)
src = [5, 3, 4]
it = iter(src)
next(it)
src.sort()
print(list(it), src)
print(sorted([1, 0]), sorted(range(3, 0, -1)), sorted(tuple([2, 1])))
print(sorted([2, 1, True, False]), sorted([10 ** 20, -10 ** 20, 5, 10 ** 19, 3]), sorted([(2, 1), (1, 2), (1, 1)]), sorted([[2], [1, 5], [1]]))
print(sorted([True, False, True], reverse=True))


# the observable quirks of the homogeneous fast path: NotImplemented is retried
class Pair:
    def __init__(self, v):
        self.v = v

    def __lt__(self, other):
        calls.append(self.v)
        return NotImplemented

    def __gt__(self, other):
        calls.append(-other.v)
        return self.v > other.v


del calls[:]
print(sorted([Pair(1), Pair(2)])[0].v, calls)
del calls[:]
print(sorted([(Pair(1), 0), (Pair(2), 0)])[0][1], calls)
