"""FOR_ITER steps enumerate natively over the exact tuple, list, range and
dict iterators; every other inner iterator and every subclass takes the
protocol, and the results and errors match."""

big = 2**62
print(1, list(enumerate("ab")), list(enumerate([1, 2], big)), list(enumerate((7,), -3)))
for i, x in enumerate(range(3), 10):
    print(2, i, x)
d = {1: 'a', 2: 'b'}
for i, (k, v) in enumerate(d.items(), True):
    print(3, i, k, v)
for i, k in enumerate(d):
    print(4, i, k)
for i, v in enumerate(d.values()):
    print(5, i, v)
for i, x in enumerate(x for x in "xy"):
    print(6, i, x)


class E(enumerate):
    def __next__(self):
        return ('sub', super().__next__())


print(7, list(E([1, 2])))
try:
    for i, k in enumerate(d):
        d[k + 10] = 0
except RuntimeError as ex:
    print(8, ex)
e = enumerate([1, 2, 3])
print(9, next(e))
for i, x in e:
    print(9, i, x)
print(10, list(e))
e = enumerate(iter([5, 6]), big - 1)
print(11, [i for i, _ in e])
try:
    enumerate([], 1.5)
except TypeError as ex:
    print(12, ex)

# GET_ITER makes the dict and dict-view iterators itself; subclasses keep their __iter__.
d = {1: 'a', 2: 'b'}
print(13, list(d), list(d.keys()), list(d.values()), list(d.items()))
it = iter(d.items())
print(14, type(it).__name__, next(it), length_hint := it.__length_hint__())


class D(dict):
    def __iter__(self):
        return iter('sub')


print(15, list(D(a=1)), [x for x in D(a=1)])
import collections
print(16, list(collections.OrderedDict(a=1, b=2)), list(collections.defaultdict(int, z=1).items()))
try:
    for k in d.items():
        d[3] = 4
except RuntimeError as ex:
    print(17, ex)
for k in {}:
    print(18)
for k in reversed(d):
    print(19, k)

# an iterator is its own iterator
for make in (lambda: enumerate('ab'), lambda: zip('ab', 'cd'), lambda: map(str, [1, 2]), lambda: filter(None, [0, 1]),
             lambda: reversed([1, 2]), lambda: iter([1, 2]), lambda: iter((1, 2)), lambda: iter(range(2)),
             lambda: iter({1: 2}), lambda: iter({1: 2}.values()), lambda: iter({1: 2}.items())):
    it = make()
    print(20, type(it).__name__, iter(it) is it, [x for x in it], list(it))
print(21, min(3, 1, 2), max([4, 9, 2]), min('abc'), max((1, 5), (1, 7)), min([3, 1], key=lambda v: -v), max([], default='d'),
      min({3: 1, 2: 1}), max(x for x in range(5)))
for bad in (lambda: min(), lambda: max(5), lambda: min([]), lambda: min(1, 2, default=3), lambda: max(1, key=1, c=2)):
    try:
        bad()
    except (TypeError, ValueError) as ex:
        print(22, type(ex).__name__, ex)
