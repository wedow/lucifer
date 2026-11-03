# set and frozenset: construction, algebra, iteration order, hashing, mutation and subclasses.
# Iteration order is the table's, so it is printed for ints, floats and tuples (hashes that do not
# depend on the string seed) and strings are sorted.
s = set()
print(s, len(s), bool(s), {1}, bool({0}), set([3, 1, 2]), set('ba' * 3) == {'a', 'b'}, frozenset(), frozenset([2, 1]))
s = {5, 3, 9}
s.add(3)
s.add(4)
s.discard(100)
s.discard(5)
s.remove(9)
print(s, 3 in s, 5 in s, 5 not in s, len(s), list(s))
try:
    s.remove(77)
except KeyError as exc:
    print(repr(exc))
try:
    set().pop()
except KeyError as exc:
    print(repr(exc))
print(s.pop() in (3, 4), len(s), s.copy() == s, s.copy() is s, s.clear(), s)

# Order: probing, resizing, collisions, deletes and re-adds.
o = set()
for i in range(100):
    o.add(i * 37 % 101)
print(list(o)[:20], len(o))
for i in range(0, 100, 3):
    o.discard(i)
print(list(o)[:20], len(o))
for i in range(200, 230):
    o.add(i)
print(list(o)[-10:], len(o))
print(list({8, 16, 24, 32, 40, 48, 56, 64}), list({-1, -2, -3, 0, 1}), list({2 ** 61 - 1, 2 ** 61, 2 ** 62, -2 ** 61}))
print(list({1.5, 2.5, 0.1, 1e10, -0.0, 3.0}), list({(1, 2), (2, 1), (3, 4), (0, 0)}), list({None, True, 5, 'x'} - {'x'}))
print(list({frozenset([1]), frozenset([2]), frozenset([1, 2])}))
big = set(range(0, 5000, 7))
print(len(big), list(big)[:10], list(big)[-5:], 4998 in big, 4999 in big)
d = set(range(20))
for i in range(15):
    d.discard(i)
print(list(d), d.__sizeof__())
for i in range(60):
    d.add(i + 1000)
    d.discard(i + 1000)
print(list(d), d.__sizeof__())
p = set(range(10))
print([p.pop() for i in range(10)], p)
q = {10, 20, 30, 40}
print([q.pop(), q.pop()], q)
q.add(25)
print(q, list(q))
print(set().__sizeof__(), {1}.__sizeof__(), set(range(5)).__sizeof__(), set(range(6)).__sizeof__(), set(range(100)).__sizeof__(),
      frozenset().__sizeof__(), frozenset(range(100)).__sizeof__())

# Algebra.
a, b = {1, 2, 3, 4}, {3, 4, 5, 6}
print(a | b, a & b, a - b, a ^ b, b - a, a.union(b), a.intersection(b), a.difference(b), a.symmetric_difference(b))
print(a.union(), a.union([7], (8,), {9}), a.intersection([3, 4, 7]), a.intersection(), a.difference([1], [2]), a.difference())
print(a.union(range(3)), a.intersection(range(3), {2, 9}), a.difference(range(2)), a.symmetric_difference([1, 9]), a.symmetric_difference({1: 2, 9: 0}))
print(a.isdisjoint(b), a.isdisjoint({7}), a.isdisjoint([7]), a.isdisjoint([1]), a.isdisjoint(a), set().isdisjoint(set()))
print(a.issubset(b), {1, 2} <= a, {1, 2} < a, a <= a, a < a, a >= {1}, a > {1}, a >= a, a > a, a.issuperset([1, 2]), a.issubset([1, 2, 3, 4, 5]))
print(a == {1, 2, 3, 4}, a != {1, 2, 3, 4}, a == b, a == frozenset(a), frozenset(a) == a, a == [1, 2, 3, 4], {1} == {1.0}, {True} == {1})
print(a.__or__([1]) is NotImplemented, a.__and__(5) is NotImplemented, a.__lt__([1]) is NotImplemented, a.__ror__(b), a.__rsub__(b), a.__rxor__(b), a.__rand__(b))
c = {1, 2, 3, 4}
c |= {9}
c &= {1, 2, 9, 10}
c -= {2}
c ^= {1, 5}
print(c)
c.update([7], {8}, range(2))
c.intersection_update({0, 1, 5, 7, 8, 99}, [0, 1, 5, 7])
c.difference_update([1], {7})
c.symmetric_difference_update([0, 11])
print(c)
c.update()
c.intersection_update()
c.difference_update()
c.symmetric_difference_update(c)
print(c)
e = {1, 2}
e |= e
e &= e
e ^= e
print(e)
e = {1, 2}
e -= e
print(e)
for bad in (lambda: {1}.__ior__([1]), lambda: {1} | [1], lambda: {1} & [1], lambda: {1} - [1], lambda: {1} ^ [1],
            lambda: {1}.union(5), lambda: {1}.update(5), lambda: {1}.intersection(5), lambda: {1}.difference(5),
            lambda: {1}.issubset(5), lambda: {1}.issuperset(5), lambda: {1}.isdisjoint(5), lambda: {1} < [1], lambda: {1} <= 5):
    try:
        r = bad()
        print('result', r)
    except TypeError as exc:
        print('TypeError', exc)
x = {1, 2}
x |= {3}
y = x
x |= {4}
print(y is x, y)
x = x | {5}
print(y is x, y, x)

# Types of results.
class S(set):
    pass
class F(frozenset):
    pass
s1, f1 = S([1, 2]), F([1, 2])
print(type(s1 | {3}).__name__, type({3} | s1).__name__, type(s1.copy()).__name__, type(s1.union([1])).__name__,
      type(f1 | {3}).__name__, type(f1.copy()).__name__, type(frozenset([1]) | {2}).__name__, type({2} | frozenset([1])).__name__,
      type(frozenset([1]) & {1}).__name__, type({1} & frozenset([1])).__name__, type(f1.intersection([1])).__name__)
fs = frozenset([1, 2])
print(frozenset(fs) is fs, fs.copy() is fs, F(fs) is fs, type(F(fs)).__name__, frozenset(f1) is f1, fs.union() is fs, fs.union() == fs)

# frozenset hashing, bit for bit.
print(hash(frozenset()), hash(frozenset([1])), hash(frozenset([1, 2, 3])), hash(frozenset([(1, 2), 3.5, None])), hash(frozenset(range(100))))
print(hash(frozenset([frozenset([1]), frozenset()])), hash(frozenset([-1])), hash(frozenset([2 ** 70])), hash(F([1, 2])) == hash(frozenset([1, 2])))
print(frozenset([1, 2]).__hash__() == hash(frozenset([2, 1])), hash(frozenset([1, 2])) == hash(frozenset([1, 3])))
z = set(range(10))
for i in range(10):
    z.discard(i)
z.update([1, 2])
print(hash(frozenset(z)), z == {1, 2})
d = {frozenset([1, 2]): 'a', frozenset([3]): 'b'}
print(d[frozenset([2, 1])], frozenset([3]) in d, {frozenset([1]), frozenset([1])}, {1, 2} in [{1, 2}])
for f in (lambda: hash(set()), lambda: {set()}, lambda: {[]}, lambda: set([[1]]), lambda: frozenset([{1}]), lambda: {1: 2}[{1}], lambda: {(1, []): 2}):
    try:
        f()
    except TypeError as exc:
        print(exc)

# Sets as elements: an unhashable set stands for the frozenset of its elements in lookups.
fsets = {frozenset([1, 2]), frozenset([3])}
print({1, 2} in fsets, {3} in fsets, {4} in fsets, set() in fsets)
fsets.discard({1, 2})
print(fsets)
fsets.remove({3})
print(fsets)
try:
    fsets.remove({9})
except KeyError as exc:
    print(repr(exc))
try:
    fsets.add({9})
except TypeError as exc:
    print(exc)

# Iteration and mutation.
m = {1, 2, 3}
it = iter(m)
print(next(it), it.__length_hint__(), type(it).__name__, list(it), it.__length_hint__(), list(it))
it = iter(m)
next(it)
m.add(4)
try:
    next(it)
except RuntimeError as exc:
    print(exc)
try:
    next(it)
except RuntimeError as exc:
    print('again', exc)
m = {1, 2, 3}
it = iter(m)
next(it)
m.discard(2)
m.add(5)
try:
    next(it)
except RuntimeError as exc:
    print('swap', exc)
it = iter({1, 2})
print(it.__reduce__()[1], it.__reduce__()[0] is iter, next(it), it.__reduce__()[1], list(it), it.__reduce__()[1], list(iter(frozenset([5]))))
print([x for x in {3, 1, 2}], [x * 2 for x in frozenset([1, 2, 3])], len(set(iter(range(5)))), tuple({7}), list({1: 'a', 2: 'b'}.keys() | {3}))

# Keys with their own hash and equality, and an __eq__ that changes the set.
class Key:
    def __init__(self, v, h):
        self.v = v
        self.h = h
    def __hash__(self):
        return self.h
    def __eq__(self, other):
        return isinstance(other, Key) and self.v == other.v
    def __repr__(self):
        return 'K%s' % self.v
ks = {Key(1, 4), Key(2, 4), Key(1, 4), Key(3, 4)}
print(ks, len(ks), Key(2, 4) in ks, Key(9, 4) in ks)
ks.discard(Key(2, 4))
print(ks, ks == {Key(1, 4), Key(3, 4)}, ks.issubset({Key(1, 4), Key(3, 4), Key(5, 4)}))
class Mut:
    def __init__(self, target, action):
        self.target = target
        self.action = action
    def __hash__(self): return 3
    def __eq__(self, other):
        if self.action == 'add':
            self.target.add(('added', len(self.target)))
        elif self.action == 'clear':
            self.target.clear()
        elif self.action == 'discard':
            self.target.discard(self)
        return False
for action in ('add', 'clear', 'discard', 'none'):
    t = set()
    t.add(Mut(t, action))
    t.add(Mut(t, 'none'))
    t.add(Mut(t, action))
    print(action, len(t))
class BadHash:
    def __hash__(self): raise ValueError('bad hash')
class BadEq:
    def __hash__(self): return 1
    def __eq__(self, other): raise ValueError('bad eq')
for f in (lambda: {BadHash()}, lambda: BadHash() in set(), lambda: {BadEq(), BadEq()}, lambda: set().add(BadHash()),
          lambda: {1}.discard(BadHash()), lambda: {1}.issuperset([BadHash()])):
    try:
        f()
    except ValueError as exc:
        print('ValueError', exc)

# Subclasses and the constructor.
class MySet(set):
    def __init__(self, items=(), tag='t'):
        set.__init__(self, items)
        self.tag = tag
    def __repr__(self):
        return 'MySet' + set.__repr__(self)
ms = MySet([3, 1], tag='x')
ms.add(2)
print(ms, ms.tag, len(ms), 2 in ms, isinstance(ms, set), MySet.__mro__ == (MySet, set, object), repr(S()), repr(S([1])), repr(F([1])), repr(F()))
print(set.__hash__, frozenset.__hash__ is not None, set([1]).__init__([2]), set.__new__(set), frozenset.__new__(frozenset, [1]), set.__init__(S([1])))
t = set([1, 2])
t.__init__([5])
print(t)
t.__init__()
print(t)
for bad in (lambda: set(1, 2), lambda: set(a=1), lambda: frozenset(1, 2), lambda: frozenset(a=1), lambda: S(1, 2), lambda: set(5), lambda: frozenset(5),
            lambda: set.__new__(int), lambda: S.__new__(F), lambda: set.add(frozenset(), 1)):
    try:
        bad()
    except TypeError as exc:
        print('TypeError', exc)
print(S(a for a in []) if False else S(), F([1]).__class__.__name__, type(F()).__name__)
r = {1}
r.add(frozenset([r.copy().pop()]))
print(r, repr(set()), repr(frozenset([1])), str(frozenset()), [set(), frozenset()])
w = set()
w.add(frozenset([w.copy().__len__()]))
print(w)
class Self(set):
    pass
sf = Self()
print(sf, sf == set(), Self([1]) == {1}, hash(F([1])) == hash(frozenset([1])))
print({x for x in range(5) if x % 2}, {x for x in 'aab'} == {'a', 'b'}, {(x, y) for x in range(2) for y in range(2)})
print(sorted(dict.fromkeys(set('hello')).keys()), dict.fromkeys({3, 1, 2}), list(dict.fromkeys(frozenset([4, 5]))))
