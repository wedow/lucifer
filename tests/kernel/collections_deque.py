import _collections
import sys
import types
from _collections import deque, defaultdict, _count_elements, _tuplegetter

def err(f, *a, **k):
    try:
        r = f(*a, **k)
    except BaseException as e:
        print(type(e).__name__, e)
    else:
        print("ok", r)

def terr(f, *a, **k):
    try:
        f(*a, **k)
    except BaseException as e:
        print(type(e).__name__)
    else:
        print("no error")

d = deque()
print(d, repr(deque([1, 2, 3])), deque("ab", 5), deque([1, 2, 3], maxlen=2), deque(maxlen=0))
print(deque.__module__, deque.__name__, deque.__qualname__, defaultdict.__module__, defaultdict.__qualname__)
print(type(d).__name__, d.maxlen, deque([], 3).maxlen, len(deque(range(5))))
err(deque, 1)
err(deque, [], -1)
err(deque, [], "x")
terr(deque, [], 1, 2)
terr(deque, foo=1)
print(deque([1], None).maxlen)

d = deque([1, 2, 3])
d.append(4); d.appendleft(0)
print(d, d.pop(), d.popleft(), d)
d.extend([7, 8]); d.extendleft([-1, -2])
print(d, list(reversed(d)))
d.extend(d)
print(d)
d = deque([1, 2])
d.extendleft(d)
print(d)
err(deque().pop); err(deque().popleft)
err(d.extend, 5); err(d.extendleft, 5)
d = deque([1, 2, 3, 4, 5], 3)
print(d); d.append(6); d.appendleft(0); print(d)
d.extend(range(10)); print(d); d.extendleft(range(10)); print(d)
z = deque(maxlen=0); z.append(1); z.appendleft(2); z.extend([1, 2]); print(z, len(z))
d = deque([1, 2, 3, 2, 1])
print(d.count(2), d.count(9), d.index(2), d.index(2, 2), d.index(1, 1), d.index(1, -2), d.index(3, 0, 5))
err(d.index, 9); err(d.index, 1, 1, 3); terr(d.index); err(d.index, 1, "a")
d.insert(1, 'a'); d.insert(-1, 'b'); d.insert(100, 'c'); d.insert(-100, 'd')
print(d)
err(deque([1], 1).insert, 0, 2)
err(d.insert, 'x', 1)
d.remove('a'); print(d); err(d.remove, 'zzz')
d.reverse(); print(d)
d = deque(range(6))
d.rotate(2); print(d); d.rotate(-3); print(d); d.rotate(); print(d); d.rotate(0); d.rotate(100); print(d)
err(d.rotate, 'a'); err(d.rotate, 1.5); terr(d.rotate, 1, 2)
deque().rotate(3)
d.clear(); print(d, len(d), bool(d))
d = deque(range(5))
print(d[0], d[-1], d[4], d[-5])
err(lambda: d[5]); err(lambda: d[-6]); err(lambda: d['a']); err(lambda: d[1:2]); err(lambda: deque()[0])
d[1] = 'x'; d[-1] = 'y'; print(d)
err(d.__setitem__, 9, 1); err(d.__setitem__, slice(0, 1), 1)
del d[0]; del d[-1]; print(d)
err(d.__delitem__, 9); err(d.__delitem__, slice(0, 1)); err(d.__delitem__, 'a')
d = deque('abc')
print('a' in d, 'z' in d, list(d), list(iter(d)), type(iter(d)).__name__, type(reversed(d)).__name__)
print(type(iter(d)).__module__, type(reversed(d)).__qualname__)
print(iter(d) is not None, list(reversed(d)))
it = iter(d); print(next(it), next(it), next(it)); err(next, it); err(next, it)
print(iter(d).__length_hint__(), iter(reversed(d)).__length_hint__())
it = iter(d); next(it); print(it.__length_hint__())
print(iter(it) is it)
for f in (lambda: [x for x in d if d.append(1) or True], lambda: [x for x in reversed(d) if d.pop() or True]):
    err(f)
d = deque('abc'); it = iter(d); d.append('d'); err(next, it)
d = deque('abc'); it = iter(d); d.rotate(1); err(next, it)
d = deque('abc'); it = iter(d); d[0] = 'q'; print(next(it))
class E:
    def __eq__(s, o): d.clear(); return True
d = deque([1, 2, 3]); err(d.count, E()); d = deque([1, 2, 3]); err(d.index, E()); err(d.remove, E())
print(deque([1, 2]) + deque([3]), deque([1], 2) + deque([2, 3]))
err(lambda: deque([1]) + [2])
d = deque([1, 2], 3); d += [4, 5]; print(d); d += d; print(d)
err(lambda: deque([1]).__iadd__(3))
print(deque([1, 2]) * 3, 2 * deque([1, 2]), deque([1, 2], 3) * 3, deque([1, 2]) * 0, deque([1]) * -1)
d = deque([1, 2]); d *= 2; print(d); d *= 0; print(d)
err(lambda: deque([1]) * 'a'); err(lambda: deque([1]) * 1.5)
a, b = deque([1, 2]), deque([1, 3])
print(a == a, a == deque([1, 2]), a != b, a < b, a <= b, a > b, a >= b, a == [1, 2], a == 3)
terr(lambda: a < [1]); terr(lambda: a > 3)
print(deque() < deque([1]), deque([1, 2]) < deque([1, 2, 0]), deque([1, 2], 5) == deque([1, 2], 7))
terr(hash, a); print(deque.__hash__)
c = a.copy(); print(c, c is a, c == a, deque([1], 3).copy().maxlen)
print(a.__copy__(), a.__copy__().maxlen, deque([1], 3).__copy__())
print(a.__reduce__()[:3], list(a.__reduce__()[3]), deque([1], 3).__reduce__()[:3])
class D(deque): pass
x = D([1, 2], 4)
print(type(x.copy()).__name__, type(x.__copy__()).__name__, x.__reduce__()[0].__name__, x, D.__module__, D.__qualname__)
print(x.copy(), type(x + x).__name__)
r = deque([1]); r.append(r); print(r); r = deque([1], 5); r.append(r); print(r)
class WithDict(deque): pass
w = WithDict([1]); w.attr = 2; print(w.__reduce__()[2], w.__reduce__()[1])
print(deque[int], deque[int].__origin__.__name__, type(deque[int]).__name__)
print(deque.__class_getitem__(int))
d = deque([1, 2, 3], 5)
d.__init__([9]); print(d, d.maxlen)
terr(setattr, d, 'maxlen', 3)
err(deque.__init__, d, [1], 1)
print(deque.__bases__, deque.__doc__.splitlines()[0], hasattr(deque, '__dict__'))
print(sorted(n for n in dir(deque) if n[0] != '_'))
d = deque([3, 1, 2]); print(sorted(d), max(d), sum(d), list(map(str, d)), d.__len__(), d.__sizeof__() > 0)
print(deque(deque([1, 2], 3)), deque({1: 2}), deque(x for x in range(3)))
d = deque([1, 2, 3]); d.append(d); print(d.count(d), d.index(d) if False else '')
# subclass overriding
class Q(deque):
    def __init__(self, *a, **k):
        super().__init__(*a, **k); self.tag = 't'
print(Q(range(3)).tag, Q(range(3), maxlen=2))

# defaultdict
dd = defaultdict(list)
print(dd, dd.default_factory, repr(defaultdict(int, {1: 2})), repr(defaultdict(None, a=1)))
dd['a'].append(1); dd['b']; print(dd, len(dd), 'c' in dd, dd.get('c'))
print(dd.__missing__.__name__)
err(defaultdict(None).__missing__, 'k')
err(lambda: defaultdict()['x']); err(lambda: defaultdict(None)[(1, 2)])
err(defaultdict, 1); err(defaultdict, int, 1)
print(defaultdict(), defaultdict(int, [(1, 2)]), defaultdict(int, a=3), defaultdict(int, {1: 1}, b=2))
d2 = defaultdict(lambda: 5); print(d2['x'], d2['y'])
d2 = defaultdict(int); d2.default_factory = str; print(d2['x'] == '', d2)
d2.default_factory = None; err(lambda: d2['nope'])
err(setattr, d2, 'default_factory', 1) ; print(d2.default_factory)
def bad(): raise ValueError('boom')
err(lambda: defaultdict(bad)['k'])
dd = defaultdict(int, {1: 2}); c = dd.copy()
print(c, type(c).__name__, c.default_factory, c is dd, dd.__copy__(), type(dd.__copy__()).__name__)
print(dd.__reduce__()[:4], list(dd.__reduce__()[4]))
dd = defaultdict(list, a=[1]); print(dd.__reduce__()[0].__name__, dd.__reduce__()[1], dd.__reduce__()[2:3], list(dd.__reduce__()[4]))
print(defaultdict(None).__reduce__()[1])
r = defaultdict(list); r['x'] = r; print(r)
r = defaultdict(); r.default_factory = r; print(r)
a = defaultdict(int, {1: 1}); b = {2: 2}
print(a | b, type(a | b).__name__, (a | b).default_factory, b | a, type(b | a).__name__, (b | a).default_factory)
a |= {3: 3}; print(a); a |= [(4, 4)]; print(a)
terr(lambda: a | 1); terr(lambda: 1 | a)
terr(lambda: a | [(1, 1)])
print(defaultdict(int, {1: 1}) == {1: 1}, defaultdict(int) == defaultdict(list), isinstance(a, dict))
print(sorted(n for n in dir(defaultdict) if n in ('default_factory', '__missing__', 'copy', '__copy__', '__reduce__', '__repr__', '__or__', '__ror__', '__ior__')))
class DD(defaultdict):
    def __missing__(self, k): return 'm'
print(DD(int)['z'], DD(int), DD.__module__)
class DD2(defaultdict): pass
print(DD2(int, a=1), DD2(int).copy().__class__.__name__)
terr(setattr, defaultdict(int), "x", 1)
print(defaultdict.__doc__.splitlines()[0])
print(defaultdict.__bases__, defaultdict.__init__.__name__)

# _count_elements
m = {}; _count_elements(m, "abracadabra"); print(m)
m = defaultdict(int); _count_elements(m, [1, 1, 2]); print(m)
class M(dict):
    def __getitem__(self, k): return 10
    def __setitem__(self, k, v): print('set', k, v); dict.__setitem__(self, k, v)
_count_elements(M(), [1, 2])
class M2:
    def __init__(self): self.d = {}
    def get(self, k, default=None): return self.d.get(k, default)
    def __setitem__(self, k, v): self.d[k] = v
m = M2(); _count_elements(m, 'aab'); print(m.d)
err(_count_elements, {}, 5); err(_count_elements, 5, [1]); terr(_count_elements, {}, [[]]); terr(_count_elements, {})

# _tuplegetter
class P(tuple):
    first = _tuplegetter(0, 'the first')
    second = _tuplegetter(1, 'the second')
p = P((10, 20))
print(p.first, p.second, P.first.__doc__, type(P.first).__name__, type(P.first).__module__)
print(P.first.__get__(None, P) is P.first, P.__dict__['first'].__reduce__())
err(lambda: P((1,)).second); err(lambda: P.first.__get__(5)); err(lambda: P.first.__get__(None, P).__doc__)
err(setattr, p, 'first', 1); err(delattr, p, 'first'); err(_tuplegetter, 'a', 'b'); terr(_tuplegetter, 0)
err(lambda: _tuplegetter(0, 'x').__get__([1], list))
print(_tuplegetter(1, 'd').__get__((5, 6)), _tuplegetter(0, None).__doc__)
class PS(tuple):
    a = _tuplegetter(0, 'doc a')
print(PS.a.__doc__)
PS.a.__doc__ = 'changed'; print(PS.a.__doc__)
