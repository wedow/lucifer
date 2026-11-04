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
print(11, [i for i, _ in e], next(iter(enumerate([], 1.5)), 'empty'))
try:
    enumerate([], 1.5)
except TypeError as ex:
    print(12, ex)
