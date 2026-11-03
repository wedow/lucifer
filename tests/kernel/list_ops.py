# list: construction, mutation, slicing, arithmetic, comparison, iteration and its errors.
a = [1, 2, 3]
print(a, len(a), a[0], a[-1], a[1:], a[:-1], a[::-1], a[::2], a[5:], a[-10:2])
a.append(4)
a.extend([5, 6])
a.extend((7, 8))
a.extend(range(9, 12))
print(a)
a.insert(0, 0)
a.insert(-1, 99)
a.insert(100, 100)
a.insert(-100, -100)
print(a)
print(a.pop(), a.pop(0), a.pop(-2), a)
a.remove(10)
print(a, a.index(5), a.index(5, 2), a.index(5, 2, 6), a.count(5), a.count(1000))
a.reverse()
print(a)
b = a.copy()
b.clear()
print(a, b, a is a.copy(), a == a.copy())
a = list(range(10))
a[2:5] = [20, 30]
print(a)
a[1:1] = [7, 7, 7]
print(a)
a[::2] = [0] * len(a[::2])
print(a)
del a[::3]
print(a)
del a[1:3]
print(a)
del a[-1]
print(a)
a[:] = (1, 2, 3)
print(a)
a[len(a):] = range(3)
print(a)
a[-2:] = []
print(a)
a = list(range(10))
a[::-2] = list(range(5))
print(a)
a[8:2:-3] = [-1, -2]
print(a)
a[100:200] = [5]
print(a)
a = [1, 2, 3]
a[0:2] = a
print(a)
a[:] = a
print(a)
a += a
print(a)
a *= 2
print(len(a))
a *= 0
print(a)
a = [1, 2]
print(a + [3], a * 3, 3 * a, a * 0, a * -1, [] + [], [] * 5)
b = a
a += [3]
print(a, b is a)
a = a + [4]
print(a, b)
print([1, 2] == [1, 2], [1, 2] != [1, 2], [1, 2] < [1, 3], [1, 2] <= [1, 2], [1, 2, 3] > [1, 2], [1] >= [1, 0])
print([1, 2] == (1, 2), [] == [], [1] < [1, 0], [2] < [1, 5], [1, [2]] == [1, [2]])
print(1 in [1, 2], 3 in [1, 2], 3 not in [1, 2], [] in [[]], 1.0 in [1], True in [1])
nan = 1e308 * 10 - 1e308 * 10
n = [nan]
print(n == n, nan in n, n.count(nan), n.index(nan))
print(bool([]), bool([0]), not [], [].__len__(), list.__len__([1, 2]))
print([].__contains__(1), [1].__contains__(1), [1, 2].__getitem__(1), [1, 2].__getitem__(slice(0, 1)))
print([1, 2].__add__([3]), [1, 2].__mul__(2), [1, 2].__rmul__(2), [1, 2].__eq__([1, 2]), [1, 2].__lt__((1, 2)))
print(list(), list([1, 2]), list((1, 2)), list(range(3)), list(iter([1, 2])), list([1, 2]) is not None)


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


class Seq:
    def __getitem__(self, i):
        if i >= 3:
            raise IndexError
        return i * 10


print(list(Counter(3)), list(Seq()), [*Counter(2), *Seq()][:0] or [*Counter(2), *Seq()])
print(10 in Seq(), 20 in Seq(), 5 in Seq(), 1 in Counter(3), 5 in Counter(3))
x, y, z = Seq()
print(x, y, z)


class L(list):
    pass


l = L([3, 1, 2])
print(l, type(l).__name__, len(l), l[0], l[1:], type(l[1:]).__name__, l + [4], type(l + [4]).__name__)
l.append(4)
print(l, isinstance(l, list), l == [3, 1, 2, 4], [3, 1, 2, 4] == l)
l.x = 5
print(l.x, L.__mro__ == (L, list, object))
l *= 2
print(type(l).__name__, l)


class Loud(list):
    def __getitem__(self, i):
        print('get', i)
        return list.__getitem__(self, i)

    def __len__(self):
        print('len')
        return list.__len__(self)


q = Loud([1, 2, 3])
print(q[1], q[0:2], len(q))
for item in q:
    print(item)
print(*q)
print([*q])

# iteration while the list changes
a = [1, 2, 3]
for v in a:
    if v < 3:
        a.append(v + 10)
print(a)
a = [1, 2, 3, 4]
for v in a:
    a.remove(v)
print(a)
a = [1, 2, 3]
it = iter(a)
print(next(it), next(it))
a.append(4)
print(next(it), next(it))
a.append(5)
try:
    next(it)
except StopIteration:
    print('stopped')
print(list(it))
a = [1, 2, 3]
it = a.__iter__()
print(it.__length_hint__(), it.__next__(), it.__length_hint__())
r = a.__reversed__()
print(type(r).__name__, r.__next__(), r.__length_hint__(), list(r))
print(type(iter([])).__name__, iter([]) is not iter([]))
it = iter(a)
print(iter(it) is it, it.__iter__() is it)

# repr and recursion
r = [1]
r.append(r)
print(r, [[], [[]]], [()], [(1,)], [None, True, 1.5 if False else 2])
print(repr([1, 2]), str([1, 2]), [1, 2].__repr__())
h = [[1, 2], [3]]
print(h[0][1], h[-1][-1])
try:
    hash([])
except TypeError as e:
    print(e)
print(list.__hash__ is None, [].__hash__ is None)

errors = (
    lambda: [][0], lambda: [1, 2][2], lambda: [1][-2], lambda: [1]['a'], lambda: [1][1.0],
    lambda: [1][None], lambda: [].pop(), lambda: [1].pop(3), lambda: [1].pop('a'),
    lambda: [1].index(2), lambda: [1].remove(2), lambda: [1].index(1, 5), lambda: [].insert('a', 1),
    lambda: [1] + (2,), lambda: [1] + 1, lambda: [1] * 'a', lambda: [1] * 1.5, lambda: [1].__mul__(1.5),
    lambda: [1].extend(1), lambda: [1].extend(None), lambda: list(1), lambda: list(None),
    lambda: [1].__setitem__(5, 1), lambda: [1].__delitem__(5), lambda: [1].__setitem__(slice(0, 1), 1),
    lambda: [1, 2, 3].__setitem__(slice(None, None, 2), [1]), lambda: [1].__delitem__('a'),
    lambda: [1].__setitem__(slice(None, None, 0), [1]), lambda: [1][::0], lambda: [1][slice(1, 2, 'a')],
    lambda: [1, 2] < 1, lambda: [1] < (1,), lambda: [1] < [None], lambda: [1] < ['a'],
    lambda: list(1, 2), lambda: list(x=1),     lambda: [1][10 ** 30], lambda: [1][:10 ** 30], lambda: [1] * 10 ** 30, lambda: 5 in 5,
)
for t in errors:
    try:
        print(t())
    except Exception as e:
        print(type(e).__name__, e)
