# isinstance and issubclass against classes the code names (globals,
# builtins, tuples of them) in hot loops, with every way the answer can
# change mid-loop: the first argument's class reassigned, a class's
# __bases__ reassigned, the global naming a class rebound, a builtin
# shadowed by a global, a metaclass with __instancecheck__ or
# __subclasscheck__, an ABC with a virtual subclass registered, a tuple
# holding a non-class or nested tuples, subclasses of builtins, __class__
# overridden as a property, a non-class first argument to issubclass, and
# union types. Each output line must be the interpreter's.
import abc
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
HALF = N // 2


class A:
    pass


class B(A):
    pass


class C:
    pass


class D:
    pass


class MyStr(str):
    pass


class MyDict(dict):
    pass


class MyInt(int):
    pass


def one_class(xs):
    s = 0
    for x in xs:
        if isinstance(x, A):
            s += 1
        if isinstance(x, (C, B)):
            s += 10
        if isinstance(x, str):
            s += 100
        if isinstance(x, (int, list, dict)):
            s += 1000
    return s


mixed = [A(), B(), C(), D(), 'a', MyStr('b'), 3, MyInt(4), True, [1], {}, MyDict(), (1,), 2.5, None]
print('mixed', one_class(mixed), [one_class([x]) for x in mixed])
total = 0
for _ in range(N // 10):
    total += one_class(mixed)
print('mixed hot', total)

# One class at the site, then another.
b = B()
s = 0
for i in range(N):
    x = b if i < HALF else C()
    s += isinstance(x, A) + 2 * isinstance(x, (C, D)) + 4 * isinstance(x, str)
print('switch', s)

# x.__class__ reassigned mid-loop.
class E:
    pass


e = E()
s = 0
for i in range(N):
    if i == HALF:
        e.__class__ = B
    s += isinstance(e, A) + 2 * isinstance(e, (C, B)) + 4 * isinstance(e, E)
print('class assigned', s)

# A class's __bases__ reassigned mid-loop, so the answer flips.
class F:
    pass


class G(F):
    pass


class H:
    pass


g = G()
s = 0
for i in range(N):
    if i == HALF:
        G.__bases__ = (H,)
    s += isinstance(g, F) + 2 * isinstance(g, (H, C)) + 4 * issubclass(G, F) + 8 * issubclass(G, (C, H))
print('bases assigned', s)
G.__bases__ = (F,)

# The global naming a class rebound mid-loop.
Target = A


def by_global(x, n):
    global Target
    s = 0
    for i in range(n):
        if i == HALF:
            Target = C
        s += isinstance(x, Target) + 2 * isinstance(x, (Target, D))
    return s


print('global rebound', by_global(B(), N), by_global(C(), N))
Target = A

# A builtin shadowed by a global mid-loop.
def shadowed(x, n):
    global str
    s = 0
    for i in range(n):
        if i == HALF:
            str = MyStr
        s += isinstance(x, str) + 2 * isinstance(x, (str, int))
    del str
    return s


print('shadowed', shadowed('plain', N), shadowed(MyStr('m'), N))

# isinstance itself shadowed mid-loop.
def own_isinstance(x, n):
    global isinstance
    s = 0
    for i in range(n):
        if i == HALF:
            isinstance = lambda o, c: 7
        s += isinstance(x, int)
    del isinstance
    return s


print('isinstance shadowed', own_isinstance(3, N))

# A metaclass with __instancecheck__ and __subclasscheck__.
class Meta(type):
    calls = 0

    def __instancecheck__(cls, obj):
        Meta.calls += 1
        return type(obj) is int

    def __subclasscheck__(cls, sub):
        Meta.calls += 1
        return sub is int


class Odd(metaclass=Meta):
    pass


s = 0
for i in range(N):
    x = i if i & 1 else str(i)
    s += isinstance(x, Odd) + 2 * isinstance(x, (C, Odd)) + 4 * issubclass(type(x), Odd)
print('metaclass', s, Meta.calls)

# An ABC with a virtual subclass registered mid-loop.
class Shape(abc.ABC):
    pass


class Square:
    pass


sq = Square()
s = 0
for i in range(N):
    if i == HALF:
        Shape.register(Square)
    s += isinstance(sq, Shape) + 2 * isinstance(sq, (C, Shape)) + 4 * issubclass(Square, Shape)
print('abc', s)

# A tuple holding a non-class, and nested tuples.
def bad_tuple(x, n):
    r = []
    for i in range(n):
        try:
            r.append(isinstance(x, (C, 3) if i >= HALF else (C, D)))
        except TypeError as err:
            r.append(str(err))
    return r[0], r[-1], len(r)


print('non-class', bad_tuple(D(), N), bad_tuple(A(), N))
try:
    isinstance(1, (int, 'x'))
except TypeError as err:
    print('non-class first match', err)
s = 0
for i in range(N):
    x = b if i & 1 else 'q'
    s += isinstance(x, (C, (D, (A,)))) + 2 * isinstance(x, ((), (str,)))
print('nested', s)

# Subclasses of builtins, and builtins against user classes.
s = 0
vals = [MyStr('s'), MyDict(), MyInt(2), 's', {}, 2, True, b]
for i in range(N):
    x = vals[i % len(vals)]
    s += (isinstance(x, str) + 2 * isinstance(x, dict) + 4 * isinstance(x, int) + 8 * isinstance(x, (MyStr, MyInt))
          + 16 * isinstance(x, bool) + 32 * isinstance(x, (tuple, list)) + 64 * isinstance(x, object))
print('builtin subclasses', s)

# __class__ overridden as a property, changing mid-loop.
class Liar:
    pretend = A

    @property
    def __class__(self):
        return Liar.pretend


li = Liar()
s = 0
for i in range(N):
    if i == HALF:
        Liar.pretend = C
    s += isinstance(li, A) + 2 * isinstance(li, (C, D)) + 4 * isinstance(li, Liar)
print('class property', s)

# issubclass with a non-class first argument, and against builtins.
def subclass_checks(n):
    r = []
    s = 0
    for i in range(n):
        cls = (bool, MyStr, B, int, list)[i % 5]
        s += issubclass(cls, int) + 2 * issubclass(cls, (str, A)) + 4 * issubclass(cls, (list, tuple, dict))
        if i == HALF:
            try:
                issubclass(i, int)
            except TypeError as err:
                r.append(str(err))
            try:
                issubclass(int, (int, 5))
                r.append('no error')
            except TypeError as err:
                r.append(str(err))
            try:
                issubclass(str, (int, 5))
            except TypeError as err:
                r.append(str(err))
    return s, r


print('issubclass', subclass_checks(N))

# Union types.
s = 0
for i in range(N):
    x = (1, 'a', 2.0, None, b)[i % 5]
    s += isinstance(x, int | str) + 2 * isinstance(x, (float, int | None)) + 4 * issubclass(type(x), A | str)
print('union', s)
