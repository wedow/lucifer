import types

GA = types.GenericAlias
print(GA, GA.__module__, GA.__name__, GA.__qualname__)

# the builtin types and their aliases
for t in (list, dict, tuple, set, frozenset, type, enumerate, memoryview, classmethod,
          staticmethod, BaseExceptionGroup, ExceptionGroup, types.MappingProxyType,
          types.GeneratorType, types.CoroutineType, types.AsyncGeneratorType):
    a = t[int]
    print(repr(a), type(a) is GA, a.__origin__ is t, a.__args__)
print(dict[str, int], dict[str, int].__args__, tuple[()], tuple[()].__args__, tuple[int, ...])
print(list[list[int]], list[int | str], list[None], list["x"], list[...])
print(list[[int, str]], dict[str, [int, str]])
print(list[types.GenericAlias], list[types.SimpleNamespace])

# equality and hashing
print(list[int] == list[int], list[int] != list[int], list[int] == list[str], list[int] == dict[int, int])
print(list[int] == 3, list[int] != 3, hash(list[int]) == hash(list[int]), hash(list[int]) == hash(list[str]))
print({list[int]: 1}[list[int]], len({list[int], list[int], list[str]}))
print(hash(list[int]) == hash(list) ^ hash((int,)))
print(isinstance(list[int], type), callable(list[int]))

# attributes forward to the origin; some do not
a = list[int]
print(a.append is list.append, a.__name__, a.__qualname__, a.__module__, a.__doc__ == list.__doc__)
print(a.__class__, a.__unpacked__, a.__parameters__, a.__typing_unpacked_tuple_args__)
for name in ('__bases__', '__copy__', '__deepcopy__'):
    print(name, hasattr(a, name))
print(a.__mro_entries__((a,)), a.__mro_entries__(()))
print('append' in dir(a), '__origin__' in dir(a), '__parameters__' in dir(a))
try:
    a.nothing
except AttributeError as e:
    print(e)
try:
    a.x = 1
except AttributeError as e:
    print(e)
try:
    a.__origin__ = 1
except AttributeError as e:
    print(e)

# calls make instances and set __orig_class__ where they can
x = list[int]((1, 2))
print(x, x.__class__)
class Box:
    __class_getitem__ = classmethod(GA)
    def __init__(self, v=None):
        self.v = v
b = Box[int](5)
print(b.v, b.__orig_class__, type(b))
d = dict[str, int](a=1)
print(d)
print(tuple[int]((3,)), set[int]([1, 1, 2]) == {1, 2})
try:
    list[int](1, 2, 3)
except TypeError as e:
    print(e)

# isinstance and issubclass refuse
for check in (isinstance, issubclass):
    try:
        check([], list[int])
    except TypeError as e:
        print(e)

# subclassing through __mro_entries__
class L(list[int]):
    pass
print(L.__mro__[1] is list, L.__bases__, L.__orig_bases__)
print(L([1, 2]))

# construction
print(GA(list, int), GA(list, (int, str)), GA(list, []), GA(int, ()))
for args in ((), (list,), (list, int, str)):
    try:
        GA(*args)
    except TypeError as e:
        print(e)
try:
    GA(list, int, x=1)
except TypeError as e:
    print(e)
class Sub(GA):
    pass
s = Sub(list, int)
print(type(s) is Sub, s, s == list[int], list[int] == s)

# subscripting a parametrized alias
T = types.SimpleNamespace(name='T')
class TV:
    def __init__(self, name):
        self.name = name
    def __repr__(self):
        return self.name
    def __typing_subst__(self, arg):
        return arg
    __hash__ = object.__hash__
T, S = TV('T'), TV('S')
a = dict[T, list[S]]
print(a, a.__parameters__, a.__args__)
print(a[int, str], a[int, str].__parameters__, a[int, S], a[int, S].__parameters__)
print(list[T][int], list[[T]][int], list[(T, T)].__parameters__ , list[T][list[T]])
for bad in ((int,), (int, str, bytes)):
    try:
        a[bad]
    except TypeError as e:
        print(e)
try:
    list[int][str]
except TypeError as e:
    print(e)
try:
    list[T][()]
except TypeError as e:
    print(e)

# starred aliases
star = [*tuple[int, str]]
print(star, star[0].__unpacked__, star[0].__origin__, star[0].__typing_unpacked_tuple_args__)
print(list[*tuple[int, str]])
print(type(iter(tuple[int])).__name__, next(iter(tuple[int])))
it = iter(list[int])
next(it)
try:
    next(it)
except StopIteration:
    print('stop')
print(repr(star[0] == tuple[int, str]), star[0] == star[0])

# pickling and copying
print(list[int].__reduce__() == (GA, (list, (int,))))

# repr corner cases
class Meta(type):
    pass
class K:
    pass
K.__qualname__ = 'Outer.K'
print(list[K], list[Meta], list[type(None)], list[type])
class Odd:
    __module__ = 'builtins'
    __qualname__ = 'Odd'
print(list[Odd])
class NoQual:
    def __repr__(self):
        return '<noqual>'
print(list[NoQual()], list[3], list['s'], dict[str, (int, str)])

# __or__
print(list[int] | None, None | list[int], list[int] | int, int | list[int], list[int] | list[str])
try:
    list[int] | 3
except TypeError as e:
    print(e)
