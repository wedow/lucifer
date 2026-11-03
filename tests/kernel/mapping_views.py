import types

d = {'a': 1, 'b': 2}
p = types.MappingProxyType(d)
print(type(p), p, repr(p))
print(p['a'], p.get('b'), p.get('z'), p.get('z', 9), 'a' in p, 'z' in p, len(p), bool(p), bool(types.MappingProxyType({})))
print(list(p), list(iter(p)), list(reversed(p)), type(iter(p)).__name__)
print(p.keys(), p.values(), p.items())
print(list(p.keys()), list(p.values()), list(p.items()))
c = p.copy()
print(c, c is d, c == d, type(c))
d['c'] = 3
print(p, len(p), list(p.items()))
print(p == d, p == {'a': 1, 'b': 2, 'c': 3}, p != d, p == p, p == 3)
print(p | {'x': 1}, {'x': 1} | p, type(p | {'x': 1}), type({'x': 1} | p))
print(p | p, p | types.MappingProxyType({'q': 0}))
for bad in (3, [1], (1, 2)):
    try:
        p | bad
    except TypeError as e:
        print(e)
    try:
        bad | p
    except TypeError as e:
        print(e)
try:
    p |= {'z': 1}
except TypeError as e:
    print(e)
print(p.__class_getitem__(int))
print(types.MappingProxyType[int, str])
for name in ('__setitem__', '__delitem__'):
    print(name, hasattr(p, name))
try:
    p['a'] = 1
except TypeError as e:
    print(e)
try:
    del p['a']
except TypeError as e:
    print(e)
try:
    p['nope']
except KeyError as e:
    print(repr(e))
try:
    types.MappingProxyType([1])
except TypeError as e:
    print(e)
try:
    types.MappingProxyType()
except TypeError as e:
    print(e)
try:
    hash(p)
except TypeError as e:
    print(e)
print(p.__reversed__ is not None)
# of a non-dict mapping
class M:
    def __init__(self):
        self.d = {1: 'x', 2: 'y'}
    def __getitem__(self, k):
        return self.d[k]
    def __iter__(self):
        return iter(self.d)
    def __len__(self):
        return len(self.d)
    def __contains__(self, k):
        return k in self.d
    def keys(self):
        return self.d.keys()
    def values(self):
        return self.d.values()
    def items(self):
        return self.d.items()
    def get(self, k, default=None):
        return self.d.get(k, default)
    def copy(self):
        return 'copied'
m = types.MappingProxyType(M())
print(list(m), len(m), m[1], m.get(2), m.get(3, 'dflt'), 3 in m, list(m.keys()), list(m.values()),
      list(m.items()), m.copy(), 1 in m)
print(str(m) == str(m.__class__.__name__) or 'mappingproxy(' in str(m))
# classes: vars(cls) is a mappingproxy
class A:
    x = 1
    def f(self):
        pass
v = vars(A)
print(type(v).__name__, sorted(k for k in v if not k.startswith('__')), 'x' in v, v['x'], len(v) > 3)
print([k for k in A.__dict__ if k in ('x', 'f')], A.__dict__.get('x'), list(A.__dict__.items())[0][0] is not None)
print(sorted(v.keys() & {'x', 'f', 'zz'}), type(v.keys()).__name__)

# dict views expose their dict as a mappingproxy
for view in (d.keys(), d.values(), d.items()):
    mp = view.mapping
    print(type(view).__name__, type(mp).__name__, mp == d, mp is not d, len(mp))
d['d'] = 4
print(len(d.keys().mapping), d.items().mapping['d'])
try:
    d.keys().mapping = 1
except AttributeError as e:
    print(e)
try:
    d.keys().mapping['k'] = 1
except TypeError as e:
    print(e)
print(type(d.keys().mapping) is types.MappingProxyType)

# dict.__class_getitem__
print(dict[str, int], dict.__class_getitem__(int), type(dict[str, int]).__name__)
print(dict[str, int].__origin__ is dict, dict[str, int].__args__)
class D(dict):
    pass
print(D[int], D[int].__origin__ is D, D.__class_getitem__.__self__ is D)
print(type(dict.__dict__['__class_getitem__']).__name__, dict.__class_getitem__.__name__)
