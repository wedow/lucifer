"""Dict and set lookups settle hashing and equality of tuples, floats, bools,
bytes, None and classes without Python code; keys that need __hash__ or
__eq__ still call them, and hash() of a tuple is unchanged."""

import collections


class Loud:
    def __init__(self, n):
        self.n = n

    def __hash__(self):
        print('hash', self.n)
        return self.n

    def __eq__(self, other):
        print('eq', self.n, getattr(other, 'n', other))
        return isinstance(other, Loud) and other.n == self.n


class Unhashable(list):
    pass


P = collections.namedtuple('P', 'x y')
nan = float('nan')
keys = [(1, 2), (1, (2, 3)), ('a', b'b', None, 1.5, True), (), ((),), (int, str), (nan,), 1, 1.0, True, 2.5, b'k',
        None, int, ('x', 1), (1, 2.0), (1.0, 2), P(5, 6), (Loud(1), 2)]

d = {}
for i, k in enumerate(keys):
    d[k] = i
print(1, len(d), [d[k] for k in keys[:6]])
for k in [(1, 2), (1, 2.0), (1.0, 2), (True, 2), (1, (2, 3)), (1, (2.0, 3)), (), ((),), (int, str), (nan,), (nan, 1),
          ('a', b'b', None, 1.5, True), ('a', b'b', None, 1.5, 1), P(5, 6), (5, 6), Loud(1), Loud(2), 1, 1.0, True,
          2.5, b'k', None, int, bool, ('x', 1), ('x', 2), (1, 2, 3), (2, 1)]:
    print(2, 'loud' if isinstance(k, Loud) else repr(k), d.get(k, 'miss'), k in d)
t = (nan,)
d2 = {t: 1}
print(3, d2[t], d2.get((nan,)), d2.get((float('nan'),)))
for bad in ([], (1, []), (1, {}), ({1},), (Unhashable(),)):
    for how in (lambda: {bad: 1}, lambda: d.get(bad), lambda: bad in d, lambda: d.__getitem__(bad), lambda: hash(bad)):
        try:
            how()
            print(4, 'ok')
        except TypeError as ex:
            print(4, ex)
print(5, hash((1, 2)) == hash((1, 2)), hash(()), hash((1, None, 1.5)), hash(((1, 2), (3, (4,)))),
      hash((int,)) == hash((int,)), hash((True, 1.0)) == hash((1, 1)))
s = {(1, 2), (3, 4), 'a', 1.5, None}
print(6, (1, 2) in s, (1, 2.0) in s, (3, 5) in s, None in s, 1.5 in s, (1, 2) in frozenset(s))
lst = [(1, 2), (3, (4,)), None, 2.5, int]
print(7, (3, (4,)) in lst, (3, (4.0,)) in lst, (3, (5,)) in lst, int in lst, None in lst, lst.index((3, (4,))))
c = collections.Counter([(1, 2), (1, 2), (2, 1)])
print(8, c[(1, 2)], c[(2, 1)], c[(9, 9)])
dd = collections.defaultdict(list)
dd[(1, 2)].append(1)
dd[(1, 2)].append(2)
print(9, dict(dd))
d3 = {}
d3[(1, 2)] = 'a'
d3[(1, 2)] = 'b'
d3[(1.0, 2)] = 'c'
del d3[(1, 2)]
print(10, d3, d3.pop((5, 5), 'none'), d3.setdefault((7, 8), 9), d3)
big = {(i, i + 1, str(i)): i for i in range(300)}
print(11, big[(150, 151, '150')], (150, 151, '151') in big, len(big))
