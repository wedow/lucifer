# tuple: construction, indexing, slicing, arithmetic, comparison, hashing, subclasses and errors.
t = (1, 2, 3)
print(t, len(t), t[0], t[-1], t[1:], t[:-1], t[::-1], t[::2], t[5:], t[-10:2], (), (1,))
print(t + (4,), t * 2, 2 * t, t * 0, t * -1, () + (), () * 5, t + (), () + t)
print(t == (1, 2, 3), t != (1, 2, 3), t < (1, 2, 4), t <= t, t > (1, 2), t >= (1, 2, 3, 0), t == [1, 2, 3])
print(2 in t, 5 in t, 5 not in t, t.index(2), t.index(3, 1), t.index(1, 0, 2), t.count(1), t.count(9))
print(bool(()), bool((0,)), not (), tuple(), tuple([1, 2]), tuple(range(3)), tuple((1, 2)), tuple(iter([4, 5])))
print(t.__len__(), t.__getitem__(1), t.__getitem__(slice(1, 3)), t.__contains__(1), t.__add__((9,)))
print(t.__mul__(2), t.__rmul__(2), t.__eq__((1, 2, 3)), t.__lt__([1]) is NotImplemented, t.__getnewargs__())
u = (1, 2, 3)
print(tuple(t) is t, t[:] is t, t + () is t, (u * 1) is u, tuple(u) is u)
print(() is (), tuple() is (), tuple([]) is (), t[0:0] is (), t[5:] is ())
print([(1, 2), (3,)], ((), ((),)), (None, True, 2.5 if False else 3), repr((1,)), (1, 2).__repr__())
nested = ((1, 2), (3, (4, 5)))
print(nested[1][1][0], nested == ((1, 2), (3, (4, 5))))
print(t.__hash__() == (1, 2, 3).__hash__(), (1, 2).__hash__() == (2, 1).__hash__(), ().__hash__())
print((1, 2, 3).__hash__(), ((1, 2), 3).__hash__(), (0,).__hash__(), (-1,).__hash__(), (10 ** 30, 2).__hash__())
print((1, 2, 3).__iter__().__next__(), type(iter(t)).__name__, iter(t).__length_hint__())
it = iter(t)
print(next(it), it.__length_hint__(), list(it), it.__length_hint__())
d = {}
nan = 1e308 * 10 - 1e308 * 10
tn = (nan,)
print(tn == tn, tn.count(nan), nan in tn, tn.index(nan))


class T(tuple):
    pass


s = T((3, 1, 2))
print(s, type(s).__name__, len(s), s[0], s[1:], type(s[1:]).__name__, s + (4,), type(s + (4,)).__name__)
print(isinstance(s, tuple), s == (3, 1, 2), (3, 1, 2) == s, tuple(s) is s, type(tuple(s)).__name__)
print(T() , T([1]), T.__mro__ == (T, tuple, object), s.__getnewargs__())
s.x = 1
print(s.x)


class P(tuple):
    def __new__(cls, a, b):
        return tuple.__new__(cls, (a, b))

    @property
    def sum(self):
        return self[0] + self[1]


p = P(3, 4)
print(p, p.sum, p[0], len(p), type(p).__name__)
x, y = p
print(x, y)


class Seq:
    def __getitem__(self, i):
        if i >= 3:
            raise IndexError
        return i


class Counter:
    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        if self.n == 0:
            raise StopIteration
        self.n -= 1
        return self.n


print(tuple(Seq()), tuple(Counter(3)), (*Counter(2), *Seq(), 9), tuple(x for x in range(0)) if False else 1)
print(hash(()) if False else (), (1, [2]).__len__())
try:
    hash((1, []))
except TypeError as e:
    print(e)
try:
    (1, [2]).__hash__()
except TypeError as e:
    print(e)

errors = (
    lambda: ()[0], lambda: (1, 2)[2], lambda: (1,)[-2], lambda: (1,)['a'], lambda: (1,)[1.0],
    lambda: (1,)[None], lambda: (1,).index(2), lambda: (1,).index(1, 5), lambda: (1,).index('a', 'b'),
    lambda: (1,) + [2], lambda: (1,) + 1, lambda: (1,) * 'a', lambda: (1,) * 1.5, lambda: (1,) * 10 ** 30,
    lambda: tuple(1), lambda: tuple(None), lambda: tuple(1, 2), lambda: tuple(x=1),
    lambda: (1,).__getitem__(slice(None, None, 0)), lambda: (1,)[::0], lambda: (1,)[slice(1, 2, 'a')],
    lambda: (1, 2) < 1, lambda: (1,) < [1], lambda: (1,) < (None,), lambda: (1,) < ('a',),
    lambda: (1,)[10 ** 30], lambda: (1,)[:10 ** 30], lambda: 5 in 5,
    lambda: hash((1, {})) if False else (1,).__setitem__(0, 1), lambda: (1,).__delitem__(0),
    lambda: tuple.__new__(1), lambda: tuple.__new__(int), lambda: tuple.__new__(list),
)
for e in errors:
    try:
        print(e())
    except Exception as ex:
        print(type(ex).__name__, ex)
