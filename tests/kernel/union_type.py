import types

U = types.UnionType
print(U, U.__module__, U.__name__, U.__qualname__)
u = int | str
print(u, type(u) is U, u.__args__, u.__parameters__, u.__origin__ is U, u.__name__, u.__qualname__)
print(repr(int | None), repr(None | int))
print(int | int, (int | str) | float, int | (str | float), (int | str) | (str | bytes))
print(int | list[int], list[int] | int, list[int] | None, None | list[int])
print(int | str | None | bytes)

# equality and hashing
print(int | str == str | int, int | str == int | str | int, int | str != str | int)
print(int | str == (int, str), (int | str) == 3)
print(hash(int | str) == hash(str | int), hash(int | str) == hash(frozenset({int, str})))
print({int | str: 1}[str | int])
print(len({int | str, str | int, int | None}))

# unhashable members
a, b = list[int], list[int]
print(hash(list[int] | None) == hash(list[int] | None))
class U1:
    __hash__ = None
    def __eq__(self, other):
        return isinstance(other, U1)
    def __repr__(self):
        return 'U1()'
u1 = types.UnionType.__class_getitem__
try:
    print(u1(int))
except TypeError as e:
    print(e)

# attributes
print(u.__class__, 'int' in dir(u) or 'no int', u.__module__)
try:
    u.x = 1
except AttributeError as e:
    print(e)
try:
    u.__args__ = ()
except AttributeError as e:
    print(e)
print(isinstance(1, int | str), isinstance(1.5, int | str), isinstance(None, int | None))
print(issubclass(bool, int | str), issubclass(float, int | str))
try:
    isinstance(1, list[int] | str)
except TypeError as e:
    print(e)

# errors
for bad in ("x", 3, [int], (int, str)):
    for expr in (lambda: int | bad, lambda: bad | int):
        try:
            expr()
        except TypeError as e:
            print(e)
try:
    class C(int | str):
        pass
except TypeError as e:
    print(e)
try:
    types.UnionType()
except TypeError as e:
    print(e)

# __class_getitem__ on the type
print(U.__class_getitem__((int, str)), U.__class_getitem__(int), U.__class_getitem__(int | str))
try:
    U.__class_getitem__(())
except TypeError as e:
    print(e)

# type.__or__ and None
print(type.__or__(int, str))
print(hasattr(type(None), '__or__'), type.__ror__(int, None))
print(int.__class__.__or__(int, None), type.__ror__(int, str))
print(type.__or__(int, 3))

# subscription and parameters
class TV:
    def __init__(self, name):
        self.name = name
    def __repr__(self):
        return self.name
    def __typing_subst__(self, arg):
        return arg
    def __or__(self, other):
        return U.__class_getitem__((self, other))
    __hash__ = object.__hash__
T = TV('T')
w = list[T] | int
print(w, w.__parameters__, w[str], w[str].__parameters__, w[int])
v = U.__class_getitem__((T, int))
print(v, v.__parameters__, v[str], v[int])
try:
    (int | str)[int]
except TypeError as e:
    print(e)
try:
    w[int, str]
except TypeError as e:
    print(e)

# typing.Union is the same object
print(U.__mro__, U.__bases__)
print(int | str | list[T])

# nested and ellipsis repr
class Local:
    pass
print(Local | None, types.SimpleNamespace | None)
print(int | types.UnionType, int | types.GenericAlias)
