# range and slice objects, their arithmetic and their errors.
r = range(10)
print(r, range(2, 5), range(0, 10, 3), range(5, 0, -1), range(5, 5), range(0, 1, 1), range(-3), repr(range(1, 2, 3)))
print(len(r), r[0], r[-1], r[3], list(r[2:5]), r[::2], r[::-1], r[100:], r[-100:2], r[2:8:3], r[8:2:-2])
print(list(r), list(range(5, 0, -2)), list(range(-5, 5, 4)), list(range(0)), list(range(3, 3)), list(range(0, 10, 11)))
print(3 in r, 10 in r, -1 in r, 3.0 in r, True in r, 2 in range(0, 10, 2), 3 in range(0, 10, 2), 9 in range(10, 0, -1), 0 in range(10, 0, -1))
print(r.start, r.stop, r.step, range(1, 2, 3).step, range(4).start, range(4).step)
print(r.index(4), r.count(4), r.count(40), range(0, 10, 2).index(6), range(10, 0, -3).index(4), r.count(3.0), r.count(True))
print(bool(range(0)), bool(range(1)), range(5) == range(5), range(5) == range(0, 5, 1), range(0) == range(5, 5), range(0) == range(3, 0),
      range(1, 4, 2) == range(1, 5, 2), range(5) != range(6), range(5) == [0, 1, 2, 3, 4], range(1, 1) == range(2, 2, 5))
print(range(5).__hash__() == range(0, 5, 1).__hash__(), range(0).__hash__() == range(3, 3).__hash__(),
      range(1, 4, 2).__hash__() == range(1, 5, 2).__hash__(), range(5).__hash__(), range(1, 10, 3).__hash__())
big = 10 ** 30
rb = range(big, big + 10, 3)
print(rb, rb[1], rb[-1], list(rb), big + 3 in rb, big + 4 in rb, rb.index(big + 6), rb.start, rb.stop, rb.step, rb.count(big))
print(type(iter(range(3))).__name__, type(iter(rb)).__name__, type(iter(range(big))).__name__, type(range(3).__reversed__()).__name__)
print(list(rb.__reversed__()), list(range(5).__reversed__()), list(range(5, 0, -2).__reversed__()), list(range(0).__reversed__()))
print(range(big)[big - 1], range(0, big, big // 3)[1], range(-big, big)[big], range(big)[::big], range(5, big)[1:3])
it = iter(range(5))
print(next(it), it.__length_hint__(), list(it), it.__length_hint__())
it = iter(range(big, big + 3))
print(next(it), it.__length_hint__(), list(it), it.__length_hint__())
it = iter(range(10, 0, -3))
print(list(it), type(it).__name__)
print(range(2 ** 63 - 1, 2 ** 63 + 2), list(range(2 ** 63 - 1, 2 ** 63 + 2)), list(range(-2 ** 63 - 1, -2 ** 63 + 1)))
print(list(range(2 ** 63 - 3, 2 ** 63 + 3, 2)), list(range(-5, -2 ** 64, -2 ** 62)))
print(r.__len__(), r.__getitem__(2), r.__contains__(2), r.__eq__(range(10)), r.__iter__().__next__())
print(range(True, 4), range(2).start.__class__.__name__, range(1.0 if False else 1))


class Idx:
    def __init__(self, n):
        self.n = n

    def __index__(self):
        return self.n


print(range(Idx(3)), list(range(Idx(1), Idx(4))), range(Idx(2))[Idx(1)], range(5)[Idx(1):Idx(3)], 2 in range(Idx(5)))
print(list(range(3)) == [0, 1, 2])

# slice
s = slice(1, 10, 2)
print(s, s.start, s.stop, s.step, slice(5), slice(1, 2), slice(None), slice(None, None, -1), repr(slice(1, 2, 3)))
print(s.indices(5), s.indices(100), slice(None).indices(4), slice(None, None, -1).indices(4), slice(-100, 100).indices(6), slice(5, 1).indices(10))
print(slice(-3, None).indices(10), slice(None, -3).indices(10), slice(None, None, -3).indices(10), slice(2, -100, -1).indices(10), slice(100, -100, -1).indices(7))
print(slice(1, 2, 3) == slice(1, 2, 3), slice(1, 2) == slice(1, 2, None), slice(1, 2) != slice(1, 3), slice(1, 2) < slice(1, 3), slice(1, 2) >= slice(1, 2),
      slice(1, 2) == (1, 2, None), slice(None) == slice(None, None))
print(slice(1, 2, 3).__hash__() == slice(1, 2, 3).__hash__(), slice(1, 2).__hash__() == slice(1, 3).__hash__(), type(s).__name__)
print(slice(1, 2).__reduce__() == (slice, (1, 2, None)), slice(0, 4, 2).__reduce__()[1])
print(slice(big, -big, big).indices(10), slice(None, None, -big).indices(10), slice(-big, big).indices(10), slice(big).indices(3))
print(slice(Idx(1), Idx(4), Idx(2)).indices(10), slice(1, 4).indices(Idx(3)))
data = list(range(10))
print(data[slice(2, 7, 2)], data[slice(None, None, -3)], data[slice(Idx(1), Idx(3))], data[1:Idx(4):Idx(2)])
print(slice(1, 2).__eq__(slice(1, 2)), slice.__name__, slice(1, [], 3), slice(slice(1), slice(2)))
try:
    hash(slice(1, []))
except TypeError as e:
    print(e)
ell = Ellipsis
print(ell, ..., type(ell).__name__, ell is ...)

errors = (
    lambda: range(), lambda: range(1, 2, 3, 4), lambda: range(1, 2, 0), lambda: range(1.5), lambda: range('a'), lambda: range(None),
    lambda: range(1, 2.0), lambda: range(0, 5, 0.5), lambda: range(5)[5], lambda: range(5)[-6], lambda: range(0)[0], lambda: range(5)['a'],
    lambda: range(5)[1.0], lambda: range(5)[::0], lambda: range(5)[1:2:'a'], lambda: range(10).index(10), lambda: range(10).index(11),
    lambda: len(range(big)), lambda: len(range(0, 2 ** 63, 1)), lambda: range(big).__len__(), lambda: range(big)[big],
    lambda: range(3) < range(4), lambda: range(3) + range(3), lambda: range(3) * 2, lambda: range(3).__setitem__(0, 1),
    lambda: slice(), lambda: slice(1, 2, 3, 4), lambda: slice(1, 2, 0).indices(5), lambda: slice(1, 2).indices(-1), lambda: slice(1, 2).indices('a'),
    lambda: slice('a').indices(3), lambda: slice(1, 2, 'a').indices(3), lambda: slice(1, 2.0).indices(3), lambda: slice(1) < 1,
    lambda: iter(range(3)).__next__().x, lambda: [1, 2][slice(1.5, 2)], lambda: (1,)[slice(None, 'x')],
    lambda: next(iter(range(0))), lambda: next(iter(range(0, 3, -1))),
)
for t in errors:
    try:
        print(t())
    except Exception as e:
        print(type(e).__name__, e)
