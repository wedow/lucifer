# dict: construction, access, mutation, order, views, iteration, merging, hooks and subclasses.
d = {}
print(d, len(d), bool(d), {1: 2}, bool({1: 2}), {'a': 1, 'b': 2})
d = {'a': 1, 'b': 2, 'c': 3}
print(d['a'], d.get('z'), d.get('z', 9), d.get('a', 9), 'a' in d, 'z' in d, 'z' not in d)
d['d'] = 4
d['a'] = 10
del d['b']
print(d, list(d), list(d.values()), list(d.items()), len(d))
print(d.pop('c'), d.pop('zz', 'dflt'), d, d.setdefault('e', 5), d.setdefault('a', 0), d)
print(d.popitem(), d.popitem(), d, d.popitem(), d, bool(d))
d.update(x=1, y=2)
d.update({'z': 3}, w=4)
d.update([('p', 5), ('q', 6)])
print(d, d.copy(), d.copy() is d, d == d.copy(), d != d.copy(), d.clear(), d)

# Keys of every kind; an int and an equal float are one key; bool is an int.
k = {1: 'int', 'a': 'str', (1, 2): 'tuple', None: 'none', 2.5: 'float', b'x': 'bytes', frozenset([1]): 'fs'}
print(k[1], k[1.0], k[True], k['a'], k[(1, 2)], k[None], k[2.5], k[b'x'], k[frozenset([1])], k.get(2))
k[1.0] = 'replaced'
k[True] = 'again'
print(k, len(k))
print({0: 'z', False: 'f', 0.0: 'g'}, {10 ** 30: 1}[10 ** 30], {-1: 1, -2: 2}[-2], {2 ** 64: 1}[2 ** 64])
big = {}
for i in range(1000):
    big[i * 7919 % 1009] = i
print(len(big), big[0], big[1008], sum(big.values()), list(big)[:5], list(big)[-3:])
for i in range(0, 1000, 2):
    big.pop(i % 1009, None)
print(len(big), list(big)[:5])
mixed = {}
for i in range(50):
    mixed[i] = i
    mixed[str(i)] = i
    mixed[(i,)] = i
    mixed[i + 0.5] = i
print(len(mixed), mixed[7], mixed['7'], mixed[(7,)], mixed[7.5], list(mixed)[:6])

# Insertion order survives deletion and reinsertion.
o = {}
o['a'] = 1
o['b'] = 2
o['c'] = 3
del o['a']
o['a'] = 4
print(o, list(reversed(o)), list(reversed(o.values())), list(reversed(o.items())))
while o:
    print(o.popitem())

# Keys with their own __hash__ and __eq__.
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

a, b, c = Key(1, 7), Key(1, 7), Key(2, 7)
e = {a: 'a'}
e[b] = 'b'
e[c] = 'c'
print(e, len(e), e[Key(1, 7)], e[Key(2, 7)], Key(3, 7) in e, Key(1, 8) in e)
del e[Key(1, 7)]
print(e, list(e))

class Always:
    def __hash__(self): return 5
    def __eq__(self, other): return True
z = {Always(): 1}
print(len(z), z[Always()], 5 in z, Always() in {5: 1})
class Never:
    def __hash__(self): return 5
    def __eq__(self, other): return False
n1, n2 = Never(), Never()
w = {n1: 1, n2: 2}
print(len(w), w[n1], w[n2], Never() in w)

# __eq__ that changes the dict being searched.
class Mut:
    def __init__(self, target, action):
        self.target = target
        self.action = action
    def __hash__(self): return 3
    def __eq__(self, other):
        if self.action == 'add':
            self.target['added%d' % len(self.target)] = 1
        elif self.action == 'del':
            self.target.pop('victim', None)
        elif self.action == 'clear':
            self.target.clear()
        return False
t = {}
m = Mut(t, 'add')
t[m] = 1
t[Mut(t, 'none')] = 2
print(len(t), sorted(k for k in t if isinstance(k, str)))
t = {'victim': 1}
m = Mut(t, 'del')
t[m] = 1
t[Mut(t, 'none')] = 2
print(len(t))
t = {}
m = Mut(t, 'clear')
t[m] = 1
t[Mut(t, 'none')] = 2
print(len(t))

# Hash and equality errors.
class BadHash:
    def __hash__(self): raise ValueError('bad hash')
class BadEq:
    def __hash__(self): return 1
    def __eq__(self, other): raise ValueError('bad eq')
for f in (lambda: {BadHash(): 1}, lambda: {}.get(BadHash()), lambda: {BadEq(): 1, BadEq(): 2},
          lambda: {1: 2}.pop(BadHash()), lambda: BadHash() in {}):
    try:
        f()
    except ValueError as exc:
        print('ValueError', exc)
try:
    d = {BadEq(): 1}
    d[BadEq()] = 2
except ValueError as exc:
    print('ValueError', exc, len(d))
for f in (lambda: {}[[]], lambda: {}.get([]), lambda: {}.setdefault([], 1), lambda: [] in {},
          lambda: {}.__delitem__([]), lambda: {([],): 1}, lambda: dict.fromkeys([[]]),
          lambda: {1: 2}.pop([]), lambda: {{}: 1}):
    try:
        f()
    except (TypeError, KeyError) as exc:
        print(type(exc).__name__, exc)

# KeyError texts.
for key in (1, 'a', (1, 2), None, 1.5, 'a\nb', (1, 'x'), frozenset(), b'q'):
    try:
        {}[key]
    except KeyError as exc:
        print(repr(exc), str(exc), exc.args)
try:
    {}.popitem()
except KeyError as exc:
    print(repr(exc))
try:
    {1: 1}.pop(2)
except KeyError as exc:
    print(repr(exc))
try:
    del {}['x']
except KeyError as exc:
    print(repr(exc))

# Views: live, set-like, comparable.
d = {'a': 1, 'b': 2}
ks, vs, its = d.keys(), d.values(), d.items()
d['c'] = 3
print(ks, vs, its, len(ks), len(vs), len(its), 'a' in ks, 'z' in ks, ('a', 1) in its, ('a', 2) in its, 3 in vs)
print(ks == {'a', 'b', 'c'}, ks == {'a'}, ks <= {'a', 'b', 'c', 'd'}, ks < {'a', 'b', 'c'}, ks >= {'a'}, ks > {'a'}, ks != {'a'})
print(its == {('a', 1), ('b', 2), ('c', 3)}, ks == d.keys(), its == d.items(), vs == d.values(), ks.isdisjoint(['x']), ks.isdisjoint(['a']))
print(sorted(ks & {'a', 'q'}), sorted(ks | {'q'}), sorted(ks - {'a'}), sorted(ks ^ {'a', 'q'}), sorted({'a', 'q'} & ks), sorted({'q'} | ks), sorted({'a', 'q'} - ks))
print(sorted(its & {('a', 1), ('a', 9)}), sorted(its | {('z', 0)}), sorted(its ^ {('a', 1)}), sorted(['a', 'x'] and ks - ['a']))
print(list(reversed(ks)), list(reversed(vs)), list(reversed(its)), type(ks).__name__, type(vs).__name__, type(its).__name__)
print(sorted(ks & ['a', 'z', 'a']), type(ks & ['a']).__name__, sorted(d.items() ^ d.items()), sorted(ks | ks), sorted(d.items() - [('a', 1)]))
try:
    hash(ks)
except TypeError as exc:
    print(exc)
print(type(hash(vs)).__name__)
try:
    ks - 5
except TypeError as exc:
    print(exc)

# Iteration and mutation during iteration.
d = {'a': 1, 'b': 2, 'c': 3}
it = iter(d)
print(next(it), it.__length_hint__(), list(it), it.__length_hint__())
for name, make in (('keys', lambda: iter(d)), ('values', lambda: iter(d.values())), ('items', lambda: iter(d.items())),
                   ('rkeys', lambda: reversed(d))):
    d = {'a': 1, 'b': 2, 'c': 3}
    it = make()
    next(it)
    d['new'] = 4
    try:
        next(it)
    except RuntimeError as exc:
        print(name, exc)
    try:
        next(it)
    except RuntimeError as exc:
        print(name, 'again', exc)
d = {'a': 1, 'b': 2, 'c': 3}
it = iter(d)
next(it)
del d['b']
d['z'] = 9
try:
    next(it)
except RuntimeError as exc:
    print('swap', exc)
d = {'a': 1, 'b': 2}
for key in d:
    d[key] = 5
print(d)
d = {'a': 1}
it = iter(d)
d.clear()
d['b'] = 2
try:
    next(it)
except RuntimeError as exc:
    print(exc)
it = iter({})
print(list(it), type(it).__name__, type(iter({}.values())).__name__, type(iter({}.items())).__name__,
      type(reversed({})).__name__, type(reversed({}.values())).__name__, type(reversed({}.items())).__name__)
d = {1: 'a', 2: 'b', 3: 'c'}
it = iter(d.items())
next(it)
print(it.__reduce__(), list(it))
for a, b in d.items():
    print(a, b)
print([k for k in d], [v for v in d.values()], list(map(str, d)), sorted(d, reverse=True), max(d), min(d), sum(d))
print(tuple(d), list(d.items()), dict(d.items()), set(d), len(set(d.values())))

# Merging: update, |, |=, fromkeys, dict().
a = {'x': 1, 'y': 2}
b = {'y': 3, 'z': 4}
print(a | b, b | a, a, b, a.__or__(b), a.__ror__(b), a.__or__([]) is NotImplemented)
a |= b
print(a)
a |= [('p', 1), ('q', 2)]
print(a)
a.update(b)
a.update()
a.update([])
a.update(zip('mn', (7, 8)))
print(a)
class Mapping:
    def keys(self): return ['k1', 'k2']
    def __getitem__(self, key): return key.upper()
print(dict(Mapping()), {}.update(Mapping()), dict([('a', 1)], b=2), dict({'a': 1}, a=2), dict(a=1) == {'a': 1})
class Keys:
    def keys(self): return iter('ab')
    def __getitem__(self, key): return ord(key)
print(dict(Keys()))
d = {}
d.update(Keys())
print(d)
print(dict.fromkeys([1, 2, 3]), dict.fromkeys('ab', 7), dict.fromkeys({'x': 1, 'y': 2}, 0), dict.fromkeys({3, 1, 2}, 'v'), dict.fromkeys(range(3)))
class D2(dict):
    pass
print(type(D2.fromkeys([1])).__name__, D2.fromkeys([1]), type(dict.fromkeys([1])).__name__)
print(dict(zip([1, 2], 'ab')), dict(), dict({}), dict([]), dict(()), dict(iter([(1, 2)])))
for bad in (lambda: dict(5), lambda: dict([1]), lambda: dict([(1, 2, 3)]), lambda: dict([(1,)]), lambda: dict(1, 2),
            lambda: {}.update(1, 2), lambda: dict(['ab', 'c']), lambda: dict([[1, 2], 'x']), lambda: {} | 5,
            lambda: {}.update(5), lambda: {**5}):
    try:
        bad()
    except (TypeError, ValueError) as exc:
        print(type(exc).__name__, exc, getattr(exc, '__notes__', None))
print(dict(['ab', 'cd']), dict([[1, 2], (3, 4)]), dict(a=1, **{'b': 2}))
x = {}
x |= {}
print(x, {} | {}, ({1: 2} | {}) == {1: 2})

# Equality and comparison.
print({1: 2} == {1: 2}, {1: 2} == {1: 3}, {1: 2} == {2: 2}, {1: 2} != {1: 2}, {} == {}, {1: 2} == [], {1: 2} == {1: 2, 3: 4})
print({1: 2}.__eq__({1: 2}), {1: 2}.__ne__({1: 3}), {}.__lt__({}), {}.__ge__([]), {1: [2]} == {1: [2]}, {1: 1.0} == {1: 1})
try:
    {} < {}
except TypeError as exc:
    print(exc)
nan = float('nan')
dn = {nan: 1}
print(nan in dn, dn[nan], {1: nan} == {1: nan}, dn == {nan: 1}, {nan: 1} == {float('nan'): 1})
print(repr({1: {2: {3: 4}}}), {1: [1, {2: (3,)}]}, {'k': 'it\'s'}, {1: 2}.__repr__(), str({'a': None}))
r = {}
r['self'] = r
print(r, [r], {1: r})
lst = [1]
rd = {'l': lst}
lst.append(rd)
print(rd)

# __missing__ and subclasses.
class Default(dict):
    def __missing__(self, key):
        self[key] = key * 2
        return self[key]
dd = Default()
print(dd[2], dd['ab'], dd, dd.get(5), 5 in dd, len(dd))
class NoMiss(dict):
    pass
try:
    NoMiss()[1]
except KeyError as exc:
    print(repr(exc))
class Counter2(dict):
    def __init__(self, *a, **k):
        dict.__init__(self, *a, **k)
        self.extra = 'x'
    def __setitem__(self, key, value):
        dict.__setitem__(self, key, value * 2)
    def __repr__(self):
        return 'C' + dict.__repr__(self)
c2 = Counter2(a=1)
c2['b'] = 2
c2.update(c=3)
print(c2, c2.extra, len(c2), dict(c2), c2 == {'a': 1, 'b': 4, 'c': 3}, c2.copy(), type(c2.copy()).__name__)
print(isinstance(c2, dict), Counter2.__mro__ == (Counter2, dict, object), type(c2 | {}).__name__, type({} | c2).__name__)
class Plain(dict):
    pass
p = Plain({1: 2})
p.attr = 'ok'
print(p, p.attr, Plain.fromkeys('ab'), p == {1: 2}, dict(p), {**p}, len(p), list(p), p.__sizeof__() > dict.__sizeof__({}) - 1)

# Protocols: slots, hashing, truthiness, sizes.
print(dict.__hash__, {}.__hash__, dict.__len__({1: 1}), dict.__contains__({1: 1}, 1), dict.__getitem__({1: 5}, 1),
      dict.__iter__, hasattr(dict, '__reversed__'), dict.__lt__)
print(sorted(n for n in ('keys', 'values', 'items', 'get', 'pop', 'popitem', 'setdefault', 'update', 'copy', 'clear', 'fromkeys',
                         '__missing__', '__getitem__', '__setitem__', '__delitem__', '__or__', '__ror__', '__ior__', '__reversed__')
             if hasattr(dict, n)))
print({}.__sizeof__(), {1: 2}.__sizeof__(), {'a': 1}.__sizeof__(), dict(a=1, b=2, c=3, d=4, e=5, f=6).__sizeof__())
big = {}
for i in range(100):
    big[i] = i
    if i in (0, 4, 5, 10, 21, 42, 85, 99):
        print(i, big.__sizeof__())
sd = {}
for i in range(30):
    sd[str(i)] = i
print(sd.__sizeof__())
for i in range(30):
    del sd[str(i)]
print(sd.__sizeof__(), len(sd), sd)
sd['again'] = 1
print(sd.__sizeof__(), sd.copy().__sizeof__(), {1: 2}.copy().__sizeof__())
try:
    dict.__new__(int)
except TypeError as exc:
    print(exc)
print(dict.__new__(dict), type(dict.__new__(Plain)).__name__, dict.__init__({}), dict.__doc__[:20])

# A dict as globals-like namespace: store, load, delete by name.
ns = {}
exec_ns = {'__builtins__': __builtins__}
ns['x'] = 1
ns[1] = 'one'
ns['y'] = ns['x'] + 1
del ns['x']
print(ns, 'x' in ns, ns['y'])
print({}.get.__name__, {}.get.__self__ == {}, dict.get.__name__, dict.fromkeys.__self__ is dict)
print({1: 2}.items().__reduce__ if hasattr({1: 2}.items(), '__reduce__') else None is None)
