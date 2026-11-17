# The container C-API answers without Python where no key needs it - sorting
# a list of exact strs, set and dict items keyed by strs, ints and tuples,
# copying and merging sets, listing a dict, decoding UTF-8 - and as Python
# would otherwise: equal strs keep their order, a str subclass's __lt__, a
# key's own __hash__ and __eq__, a dict subclass's overrides take no part.
import _testcapi as c
import _testlimitedcapi as l

a, b = 'x' * 3, ''.join(['x'] * 3)
L = ['b', a, 'a', b, 'é', '', 'ab', '\U0001f600', 'aa', 'xxy']
l.list_sort(L)
i = L.index('xxx')
print(L, L[i] is a, L[i + 1] is b)


class Short(str):
    def __lt__(self, other):
        return len(self) < len(other)


M = ['ccc', Short('a'), 'bb', 'd']
l.list_sort(M)
print(M)
N = [3, 'a']
try:
    l.list_sort(N)
except TypeError as e:
    print('TypeError', e)


class Key:
    def __init__(self, like):
        self.like = like

    def __hash__(self):
        return hash(self.like)

    def __eq__(self, other):
        print('eq', other)
        return other == self.like


s = set()
print(l.set_add(s, 'k'), l.set_add(s, 1), l.set_add(s, 'k'), l.set_add(s, (2, 'k')), l.set_add(s, Key(1)))
print(sorted(map(str, s)), len(s))
print(l.set_contains(s, 'k'), l.set_contains(s, 'z'), l.set_contains(s, 1.0), l.set_contains(s, (2, 'k')),
      l.set_contains(s, Key('k')))
print(l.set_discard(s, 'k'), l.set_discard(s, 'k'), l.set_discard(s, (2, 'k')), sorted(map(str, s)))
f = l.set_new()
print(type(f), f, l.sequence_contains(f, 1), l.sequence_contains(['a', 1], 1), l.sequence_contains(('a',), 'b'))

d = {}
print(l.dict_setitem(d, 'a', 1), l.dict_setitem(d, 2, 'two'), l.dict_setitem(d, (1, 'x'), 3), d)
print(l.dict_setitem(d, Key('a'), 'A'), d)
print(c.dict_getitemref(d, 'a'), c.dict_getitemref(d, 2.0), c.dict_getitemref(d, (1, 'x')), c.dict_getitemref(d, Key(2)))
try:
    print(c.dict_getitemref(d, 'zz'))
except KeyError as e:
    print('KeyError', e)


class D(dict):
    def __getitem__(self, key):
        return 'sub'

    def __setitem__(self, key, value):
        raise AssertionError


e = D()
l.dict_setitem(e, 'q', 5)
print(c.dict_getitemref(e, 'q'), dict(e))
print(l.dict_keys(e), l.dict_values(e), l.dict_items(e), l.dict_keys(d), l.dict_items(d))

t = l.set_new(s)
u = l.set_new(frozenset([1, 'k', (3,)]))
print(type(t), t is not s, t == s, sorted(map(str, u)), sorted(l.set_new([2, 1])))
v = c.number_inplaceor(t, u)
print(v is t, sorted(map(str, t)), sorted(map(str, u)))
v = c.number_inplaceor(t, {Key('k'): 0}.keys())
print(sorted(map(str, t)))
print(c.number_inplaceor(frozenset([1]), {2}), c.number_inplaceor(5, 3))
print(l.unicode_decodeutf8(b'caf\xc3\xa9'), l.unicode_decodeutf8stateful(b'caf\xc3\xa9\xc3'))
try:
    l.unicode_decodeutf8(b'caf\xc3')
except UnicodeDecodeError as e:
    print('UnicodeDecodeError', e)
