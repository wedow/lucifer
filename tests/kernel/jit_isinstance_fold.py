# isinstance and issubclass against classes the code names (globals,
# builtins, tuples of them) in hot loops inside functions (compiled), with
# every way the answer can change, late in the loop (at n - 50): the first
# argument's class reassigned, a class's or a grandparent's __bases__
# reassigned, a __class__ property added to a base, the global naming a
# class rebound, a builtin or isinstance shadowed by a global, a metaclass
# with __instancecheck__ or __subclasscheck__, a metaclass whose mro()
# leaves the builtin base out, an ABC with a virtual subclass registered,
# a tuple holding a non-class or nested tuples, subclasses of builtins,
# __class__ overridden as a property, a proxy whose __class__ is a builtin,
# a non-class first argument to issubclass at a site that sees many, and
# union types. Each output line must be the interpreter's.
import abc
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
LATE = N - 50


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


def mixed_hot(n):
    total = 0
    for _ in range(n // 10):
        total += one_class(mixed)
    return total


print('mixed', one_class(mixed), [one_class([x]) for x in mixed], mixed_hot(N))


# One class at the site, then another.
def switch(n):
    b = B()
    s = 0
    for i in range(n):
        x = b if i < LATE else C()
        if isinstance(x, A):
            s += 1
        if isinstance(x, (C, D)):
            s += 2
        if isinstance(x, str):
            s += 4
    return s


print('switch', switch(N))


# x.__class__ reassigned late.
class E:
    pass


def class_assigned(n):
    e = E()
    s = 0
    for i in range(n):
        if i == LATE:
            e.__class__ = B
        if isinstance(e, A):
            s += 1
        if isinstance(e, (C, B)):
            s += 2
        if isinstance(e, E):
            s += 4
    return s


print('class assigned', class_assigned(N))


# A class's __bases__ reassigned late, so the answer flips; and a
# grandparent's.
class F:
    pass


class G(F):
    pass


class H:
    pass


class Top:
    pass


class Mid(Top):
    pass


class Low(Mid):
    pass


def bases_assigned(n):
    g = G()
    low = Low()
    s = 0
    for i in range(n):
        if i == LATE:
            G.__bases__ = (H,)
            Mid.__bases__ = (H,)
        if isinstance(g, F):
            s += 1
        if isinstance(g, (H, C)):
            s += 2
        if issubclass(G, F):
            s += 4
        if issubclass(G, (C, H)):
            s += 8
        if isinstance(low, Top):
            s += 16
        if issubclass(Low, (C, H)):
            s += 32
    return s


print('bases assigned', bases_assigned(N))
G.__bases__ = (F,)
Mid.__bases__ = (Top,)


# What obj.__class__ answers changed on a base while the negative answer is
# folded: a __getattribute__ added to one base, a base with a __class__
# property put under another.
class Base:
    pass


class Kid(Base):
    pass


class Plain:
    pass


class Base2(Plain):
    pass


class Kid2(Base2):
    pass


class ClassProp:
    __class__ = property(lambda self: A)


def answer_class(self, name):
    return A if name == '__class__' else object.__getattribute__(self, name)


def class_added(n):
    k = Kid()
    k2 = Kid2()
    s = 0
    for i in range(n):
        if i == LATE:
            Base.__getattribute__ = answer_class
            Base2.__bases__ = (ClassProp,)
        if isinstance(k, A):
            s += 1
        if isinstance(k, (C, A)):
            s += 2
        if isinstance(k2, A):
            s += 4
        if isinstance(k2, (D, A)):
            s += 8
    return s


print('class added to base', class_added(N))

# The global naming a class rebound late.
Target = A


def by_global(x, n):
    global Target
    s = 0
    for i in range(n):
        if i == LATE:
            Target = C
        if isinstance(x, Target):
            s += 1
        if isinstance(x, (Target, D)):
            s += 2
    return s


print('global rebound', by_global(B(), N), by_global(C(), N))
Target = A


# A builtin shadowed by a global late.
def shadowed(x, n):
    global str
    s = 0
    for i in range(n):
        if i == LATE:
            str = MyStr
        if isinstance(x, str):
            s += 1
        if isinstance(x, (str, int)):
            s += 2
    del str
    return s


print('shadowed', shadowed('plain', N), shadowed(MyStr('m'), N))


# isinstance itself shadowed late.
def own_isinstance(x, n):
    global isinstance
    s = 0
    for i in range(n):
        if i == LATE:
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


def metaclass(n):
    s = 0
    for i in range(n):
        x = i if i & 1 else 'q'
        if isinstance(x, Odd):
            s += 1
        if isinstance(x, (C, Odd)):
            s += 2
        if issubclass(type(x), Odd):
            s += 4
    return s


print('metaclass', metaclass(N), Meta.calls)


# A metaclass whose mro() leaves the builtin base out: the subclass flag
# stays, but CPython answers from the MRO. Asked of the classes (making an
# instance diverges from CPython today: its int.__new__ is not found), at a
# site that sees many classes (the flag test) and of a constant class.
class DropMeta(type):
    def mro(cls):
        return [cls, object]


class NotInt(int, metaclass=DropMeta):
    pass


class NotStr(str, metaclass=DropMeta):
    pass


def dropped_mro(n):
    classes = [int, bool, MyInt, NotInt, str, MyStr, NotStr]
    s = 0
    for i in range(n):
        cls = classes[i % len(classes)]
        if issubclass(cls, (int, str)):
            s += 1
        if issubclass(cls, int):
            s += 2
        if issubclass(NotInt, int):
            s += 4
    return s


print('dropped mro', dropped_mro(N))


# An ABC with a virtual subclass registered late.
class Shape(abc.ABC):
    pass


class Square:
    pass


def abc_case(n):
    sq = Square()
    s = 0
    for i in range(n):
        if i == LATE:
            Shape.register(Square)
        if isinstance(sq, Shape):
            s += 1
        if isinstance(sq, (C, Shape)):
            s += 2
        if issubclass(Square, Shape):
            s += 4
    return s


print('abc', abc_case(N))


# A tuple holding a non-class, and nested tuples.
def bad_tuple(x, n):
    r = []
    for i in range(n):
        try:
            r.append(isinstance(x, (C, 3) if i >= LATE else (C, D)))
        except TypeError as err:
            r.append(str(err))
    return r[0], r[-1], len(r)


def non_class_hot(n):
    s = 0
    c = C()
    for i in range(n):
        x = c if i < LATE else D()
        try:
            if isinstance(x, (C, 5)):
                s += 1
        except TypeError:
            s += 100
        try:
            if isinstance(x, (5, C)):
                s += 2
        except TypeError:
            s += 200
    return s


def nested(n):
    b = B()
    s = 0
    for i in range(n):
        x = b if i & 1 else 'q'
        if isinstance(x, (C, (D, (A,)))):
            s += 1
        if isinstance(x, ((), (str,))):
            s += 2
    return s


print('non-class', bad_tuple(D(), N), bad_tuple(A(), N), non_class_hot(N))
try:
    isinstance(1, (int, 'x'))
except TypeError as err:
    print('non-class first match', err)
print('nested', nested(N))


# Subclasses of builtins, and builtins against user classes.
def builtin_subclasses(n):
    s = 0
    vals = [MyStr('s'), MyDict(), MyInt(2), 's', {}, 2, True, B()]
    for i in range(n):
        x = vals[i % len(vals)]
        if isinstance(x, str):
            s += 1
        if isinstance(x, dict):
            s += 2
        if isinstance(x, int):
            s += 4
        if isinstance(x, (MyStr, MyInt)):
            s += 8
        if isinstance(x, bool):
            s += 16
        if isinstance(x, (tuple, list)):
            s += 32
        if isinstance(x, object):
            s += 64
    return s


print('builtin subclasses', builtin_subclasses(N))


# __class__ overridden as a property, changing late; and a proxy whose
# __class__ is a builtin (positive only through __class__).
class Liar:
    pretend = A

    @property
    def __class__(self):
        return Liar.pretend


class IntProxy:
    @property
    def __class__(self):
        return int


def class_property(n):
    li = Liar()
    p = IntProxy()
    s = 0
    for i in range(n):
        if i == LATE:
            Liar.pretend = C
        if isinstance(li, A):
            s += 1
        if isinstance(li, (C, D)):
            s += 2
        if isinstance(li, Liar):
            s += 4
        if isinstance(p, int):
            s += 8
        if isinstance(p, (str, int)):
            s += 16
    return s


print('class property', class_property(N))


# issubclass with a non-class first argument, and against builtins.
def subclass_checks(n):
    r = []
    s = 0
    for i in range(n):
        cls = (bool, MyStr, B, int, list)[i % 5]
        if issubclass(cls, int):
            s += 1
        if issubclass(cls, (str, A)):
            s += 2
        if issubclass(cls, (list, tuple, dict)):
            s += 4
        if i == LATE:
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


def subclass_non_type(n):
    s = 0
    vals = [bool, int, str, list, dict, tuple, float, 5]
    for i in range(n):
        x = vals[i % len(vals)]
        try:
            if issubclass(x, int):
                s += 1
            if issubclass(x, (str, list)):
                s += 2
        except TypeError:
            s += 100
    return s


print('issubclass', subclass_checks(N), subclass_non_type(N))


# Union types.
def union(n):
    b = B()
    s = 0
    for i in range(n):
        x = (1, 'a', 2.0, None, b)[i % 5]
        if isinstance(x, int | str):
            s += 1
        if isinstance(x, (float, int | None)):
            s += 2
        if issubclass(type(x), A | str):
            s += 4
    return s


print('union', union(N))
