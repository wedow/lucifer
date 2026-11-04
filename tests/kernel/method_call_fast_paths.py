"""list.append, dict.get, dict.items and isinstance answered by the call
instruction agree with the methods they skip, including subclasses that
override them and isinstance's tuples, ABCs and __class__ overrides."""

import abc
import collections

lst = []
for i in range(5):
    lst.append(i)
print(1, lst, lst.append(9), list.append(lst, 10), lst)
try:
    list.append((), 1)
except TypeError as ex:
    print(2, ex)


class L(list):
    def append(self, x):
        super().append(('sub', x))


class L2(list):
    pass


a, b = L(), L2()
a.append(1)
b.append(1)
print(3, a, b)
d = {'a': 1, 2: 'b', (1, 2): 3, None: 4, 1.5: 5}
print(4, d.get('a'), d.get(2), d.get((1, 2)), d.get(None), d.get(1.5), d.get('zz'), d.get('zz', 'dflt'), d.get([], 1) if False else 0)
try:
    d.get([])
except TypeError as ex:
    print(5, ex)
print(6, list(d.items()), type(d.items()).__name__, len(d.items()), list({}.items()))


class D(dict):
    def items(self):
        return 'sub items'

    def get(self, k, default=None):
        return 'sub get'


class D2(dict):
    pass


print(7, D(a=1).items(), D(a=1).get('a'), list(D2(a=1).items()), D2(a=1).get('a'), dict.get(D(a=1), 'a'))
od = collections.OrderedDict(a=1)
print(8, list(od.items()), od.get('a'), collections.defaultdict(int, a=1).get('b', 3))
try:
    dict.items([])
except TypeError as ex:
    print(9, ex)
it = d.items()
d['new'] = 1
print(10, len(it), ('new', 1) in it)


class Meta(abc.ABCMeta):
    pass


class Base(metaclass=Meta):
    pass


class Impl:
    pass


Base.register(Impl)


class Liar:
    @property
    def __class__(self):
        return int


class Hook:
    def __getattribute__(self, name):
        return super().__getattribute__(name)


class Sub(int):
    pass


for obj in (1, True, 'a', None, Impl(), Liar(), Hook(), Sub(2), [], int, type, Base, Ellipsis):
    print(11, type(obj).__name__, isinstance(obj, int), isinstance(obj, (str, int)), isinstance(obj, (str, (list, (Base,)))),
          isinstance(obj, (str, bytes)), isinstance(obj, ()), isinstance(obj, object), isinstance(obj, type),
          isinstance(obj, (Impl, (Liar,))), isinstance(obj, Base), isinstance(obj, bool))
for bad in (3, 'x', [int], (int, 3), ((int,), 'x'), None):
    try:
        print(12, isinstance(1, bad))
    except TypeError as ex:
        print(12, ex)
try:
    isinstance(1)
except TypeError as ex:
    print(13, ex)
nest = ()
for _ in range(40):
    nest = (nest, str)
print(14, isinstance('a', nest), isinstance(1, nest))
