# Calls of classes from compiled code: the instance is made in line and its
# __init__ inlines when the class's __new__ is object's and its __init__ a
# Python function (or object's, called with no arguments). Every result must
# be the interpreter's: arguments and defaults, inherited and *args
# initializers, dataclasses, __slots__, finalizers and weak references, an
# initializer that raises, returns a value, leaves compiled code half done
# (a guard fails after it stored an attribute) or looks at its callers, and
# a class changed under a running loop (a new __init__, a custom __new__,
# __abstractmethods__), a class of ABCMeta called with and without
# keywords, and an initializer the tier calls instead of inlining (it takes
# keyword-only parameters, or is a generator). Classes
# the lowering leaves alone (a custom __new__, a metaclass __call__, builtin
# bases) behave as before. tools/jitcheck also runs this at two sizes and
# requires that entries into compiled code and calls it makes do not grow
# with the iterations.
import abc
import sys
import weakref
from dataclasses import dataclass

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y


class Defaults:
    def __init__(self, a, b=2, c=None):
        self.a = a
        self.b = b
        self.c = c


class Bare:
    pass


class Child(Point):
    pass


class Many:
    def __init__(self, *args):
        self.n = len(args)


@dataclass
class Rec:
    k: int
    v: float


class Slotted:
    __slots__ = ('p', 'q')

    def __init__(self, p):
        self.p = p
        self.q = p * 2


class Shape(abc.ABC):
    """A class of a metaclass (ABCMeta) whose __call__ is type's."""

    def __init__(self, sides, scale=1):
        self.sides = sides * scale


# Each initializer runs once first, as it has by the time a hot loop
# compiles: one compiled before then leaves where it never ran.
Point(0, 0), Defaults(0), Child(0, 0), Many(0), Rec(0, 0.0), Slotted(0), Bare(), Shape(0)


def hot(n):
    s = 0
    for i in range(n):
        p = Point(i, i + 1)
        d = Defaults(i)
        c = Child(1, 2)
        m = Many(i, i)
        r = Rec(i, 0.5)
        t = Slotted(i)
        Bare()
        g = Shape(i, scale=2)
        s += p.x + p.y + d.a + d.b + c.y + m.n + r.k + t.q + g.sides + Shape(1).sides
    return s


print('hot', hot(N))

# The rest runs a fixed number of times, whatever N is.
K = 300


class Grows:
    """A guard in __init__ fails once x no longer fits a machine word: the
    exit lands inside the initializer with self.x already stored."""
    def __init__(self, x):
        self.x = x
        self.y = x * x


def grows():
    s = 0
    x = 1
    for i in range(K):
        g = Grows(x)
        s += g.y % 1000003
        x = x * 3 if i % 50 == 49 else x + 1
    return s


print('grows', grows())


class Returns:
    def __init__(self, v):
        self.v = v
        if v > K - 3:
            return v


def returns():
    n = 0
    try:
        for i in range(K):
            n += Returns(i).v
    except TypeError as e:
        print('returns TypeError', e, n)


returns()


class Raises:
    def __init__(self, v):
        if v % 97 == 96:
            raise ValueError(v)
        self.v = v


def raises():
    caught = []
    for i in range(K):
        try:
            Raises(i)
        except ValueError as e:
            tb = e.__traceback__
            names = []
            while tb is not None:
                names.append(tb.tb_frame.f_code.co_name)
                tb = tb.tb_next
            caught.append((e.args[0], names))
    return caught


print('raises', raises())


class Looks:
    def __init__(self):
        f = sys._getframe(1)
        self.caller = f.f_code.co_name
        self.line = f.f_lineno - f.f_code.co_firstlineno


def looks():
    seen = set()
    for i in range(K):
        seen.add((Looks().caller, Looks().line))
    return sorted(seen)


print('looks', looks())


class Finalized:
    count = 0

    def __init__(self, v):
        self.v = v

    def __del__(self):
        Finalized.count += 1


class Referenced:
    def __init__(self, v):
        self.v = v


def finalized():
    alive = []
    for i in range(K):
        f = Finalized(i)
        r = Referenced(i)
        w = weakref.ref(r)
        if i % 100 == 0:
            alive.append((w() is r, w().v))
    return Finalized.count, alive


print('finalized', finalized())


class Order:
    def __init__(self, flip):
        if flip:
            self.b = 1
            self.a = 2
        else:
            self.a = 3
            self.b = 4


def order():
    out = []
    for i in range(K):
        o = Order(i % 7 == 6)
        if i % 7 == 6 or i == 0:
            out.append(list(vars(o)))
    return out[:3]


print('order', order())


class Changed:
    def __init__(self, v):
        self.v = v


def other_init(self, v):
    self.v = -v


def changed():
    s = 0
    for i in range(K):
        if i == K // 3:
            Changed.__init__ = other_init
        if i == 2 * K // 3:
            Changed.__new__ = lambda cls, v: Point(v, v)
        c = Changed(i)
        s += c.v if isinstance(c, Changed) else c.x * 1000
    return s


print('changed', changed())


class Abstract:
    def __init__(self, v):
        self.v = v


def abstract():
    n = 0
    for i in range(K):
        if i == K // 2:
            Abstract.__abstractmethods__ = frozenset({'f'})
        try:
            n += Abstract(i).v
        except TypeError as e:
            return n, str(e)


print('abstract', abstract())


class Meta(type):
    def __call__(cls, *args):
        return ('meta', cls.__name__, args)


class Metaed(metaclass=Meta):
    def __init__(self, v):
        self.v = v


class New:
    def __new__(cls, v):
        o = super().__new__(cls)
        o.w = v
        return o

    def __init__(self, v):
        self.v = v


class Pt(tuple):
    def __init__(self, v):
        self.v = v


class Err(Exception):
    def __init__(self, v):
        self.v = v


def others():
    s = []
    for i in range(K):
        a = Metaed(i)
        b = New(i)
        c = Pt((i,))
        e = Err(i)
        if i % 150 == 0:
            s.append((a, b.v, b.w, c, c.v, e.v, e.args))
    return s


print('others', others())


def bad_args():
    out = []
    for i in range(K):
        try:
            if i % 3 == 0:
                Bare(i)
            elif i % 3 == 1:
                Point(i)
            else:
                Point(i, i, i)
        except TypeError as e:
            if i < 3:
                out.append(str(e))
    return out


print('bad_args', bad_args())


class KwOnly:
    """An initializer the tier calls instead of inlining (keyword-only parameters)."""
    def __init__(self, a, *, b=2):
        self.a = a + b
        if a % 101 == 100:
            raise KeyError(a)


class Yields:
    def __init__(self, v):
        self.v = v
        yield v


def called():
    out = []
    for i in range(K):
        try:
            out.append(KwOnly(i).a)
        except KeyError as e:
            out.append(('KeyError', e.args))
    for i in range(3):
        try:
            Yields(i)
        except TypeError as e:
            out.append(str(e))
    return out[-8:]


print('called', called())


class Tree:
    def __init__(self, depth):
        self.kids = [Tree(depth - 1), Tree(depth - 1)] if depth > 0 else []

    def size(self):
        return 1 + sum(k.size() for k in self.kids)


def trees():
    return sum(Tree(i % 5).size() for i in range(K // 10))


print('trees', trees())
